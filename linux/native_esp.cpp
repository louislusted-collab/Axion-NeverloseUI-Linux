#ifdef __linux__

#include "native_esp.h"

#include "../dependencies/imgui/imgui.h"
#include "../cstrike/core/interfaces.h"
#include "../cstrike/core/sdk.h"
#include "../cstrike/core/variables.h"
#include "../cstrike/sdk/entity.h"
#include "../cstrike/sdk/interfaces/cgameentitysystem.h"
#include "../cstrike/sdk/interfaces/iengineclient.h"
#include "../cstrike/utilities/draw.h"
#include "../cstrike/utilities/memory.h"
#include "../dependencies/json.hpp"

#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>

namespace
{
ViewMatrix_t* native_view_matrix = nullptr;
CCSPlayerController** native_local_controller = nullptr;
bool attempted_matrix_lookup = false;
std::uint64_t frame_counter = 0;

void EspLog(const char* message, const void* first = nullptr, const void* second = nullptr)
{
    static FILE* file = std::fopen("/tmp/cs2_esp_debug.log", "a");
    if (file == nullptr)
        return;
    std::fprintf(file, message, first, second);
    std::fputc('\n', file);
    std::fflush(file);
}

bool LoadResolvedOffsets()
{
    Dl_info library_info{};
    if (dladdr(reinterpret_cast<void*>(&LinuxNativeEsp::Render), &library_info) == 0 || library_info.dli_fname == nullptr)
        return false;

    const std::filesystem::path manifest =
        std::filesystem::path(library_info.dli_fname).parent_path() / ".axion-cache" / "linux-offsets.resolved.json";
    std::ifstream input(manifest);
    if (!input)
        return false;

    try
    {
        const nlohmann::json data = nlohmann::json::parse(input);
        const auto& offsets = data.at("offsets");
        const auto client_base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBaseHandle(CLIENT_DLL));
        if (client_base == 0)
            return false;
        native_view_matrix = reinterpret_cast<ViewMatrix_t*>(
            client_base + offsets.at("dwViewMatrixNative").at("value").get<std::uintptr_t>());
        native_local_controller = reinterpret_cast<CCSPlayerController**>(
            client_base + offsets.at("dwLocalPlayerControllerNative").at("value").get<std::uintptr_t>());
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void ResolveViewMatrix()
{
    if (attempted_matrix_lookup)
        return;
    attempted_matrix_lookup = true;

    if (LoadResolvedOffsets())
    {
        EspLog("[ESP] loaded updater offsets: view_matrix=%p local_controller_ptr=%p", native_view_matrix, native_local_controller);
        return;
    }

    // Bundled fallback for first run when the updater cache is unavailable.
    // lea r8, [rip+rel32] starts seven bytes into this native-only pattern.
    std::uint8_t* match = MEM::FindPattern(CLIENT_DLL, "C6 83 ? ? 00 00 01 4C 8D 05");
    if (match != nullptr)
        native_view_matrix = reinterpret_cast<ViewMatrix_t*>(MEM::GetAbsoluteAddress(match + 7, 3, 0));

    std::uint8_t* local_match = MEM::FindPattern(CLIENT_DLL, "48 83 3D ? ? ? ? 00 0F 95 C0 C3");
    if (local_match != nullptr)
        native_local_controller = reinterpret_cast<CCSPlayerController**>(MEM::GetAbsoluteAddress(local_match, 3, 1));
    EspLog("[ESP] view_matrix=%p local_controller_ptr=%p", native_view_matrix, native_local_controller);
}
}

void LinuxNativeEsp::Render()
{
    if (!C_GET(bool, Vars.bVisualOverlay))
        return;

    if (I::Engine == nullptr || I::GameResourceService == nullptr || !I::Engine->IsConnected())
        return;

    CGameEntitySystem* entities = I::GameResourceService->pGameEntitySystem;
    if (entities == nullptr)
        return;

    ResolveViewMatrix();
    if (native_view_matrix == nullptr)
        return;
    SDK::ViewMatrix = *native_view_matrix;

    CCSPlayerController* local_controller = native_local_controller != nullptr ? *native_local_controller : nullptr;
    if (local_controller == nullptr)
        return;
    C_CSPlayerPawn* local_pawn = entities->Get<C_CSPlayerPawn>(local_controller->GetPawnHandle());
    if (local_pawn == nullptr)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (draw == nullptr)
        return;

    int drawn = 0;
    for (int index = 1; index <= 64; ++index)
    {
        CCSPlayerController* controller = entities->Get<CCSPlayerController>(index);
        if (controller == nullptr || controller == local_controller)
            continue;

        SchemaClassInfoData_t* class_info = nullptr;
        controller->GetSchemaClassInfo(&class_info);
        if (class_info == nullptr || class_info->szName == nullptr ||
            std::strcmp(class_info->szName, "CCSPlayerController") != 0)
            continue;

        C_CSPlayerPawn* pawn = entities->Get<C_CSPlayerPawn>(controller->GetPawnHandle());
        if (pawn == nullptr || pawn->GetHealth() <= 0 || pawn->GetHealth() > 200 ||
            pawn->GetTeam() == local_pawn->GetTeam())
            continue;

        CGameSceneNode* scene = pawn->GetGameSceneNode();
        if (scene == nullptr || scene->IsDormant())
            continue;

        const Vector_t feet = scene->GetAbsOrigin();
        const Vector_t head(feet.x, feet.y, feet.z + 72.f);
        ImVec2 feet_screen{}, head_screen{};
        if (!D::WorldToScreen(feet, feet_screen) || !D::WorldToScreen(head, head_screen))
            continue;

        const float height = feet_screen.y - head_screen.y;
        if (height < 4.f || height > ImGui::GetIO().DisplaySize.y * 1.5f)
            continue;
        const float width = height * 0.45f;
        const ImVec2 min(head_screen.x - width * 0.5f, head_screen.y);
        const ImVec2 max(head_screen.x + width * 0.5f, feet_screen.y);

        draw->AddRect(min - ImVec2(1.f, 1.f), max + ImVec2(1.f, 1.f), IM_COL32(0, 0, 0, 220), 0.f, 0, 3.f);
        draw->AddRect(min, max, IM_COL32(40, 170, 255, 255), 0.f, 0, 1.f);

        const float health = static_cast<float>(pawn->GetHealth()) / 100.f;
        draw->AddRectFilled(ImVec2(min.x - 6.f, min.y), ImVec2(min.x - 3.f, max.y), IM_COL32(0, 0, 0, 220));
        draw->AddRectFilled(ImVec2(min.x - 5.f, max.y - height * health), ImVec2(min.x - 4.f, max.y),
                            IM_COL32(static_cast<int>(255.f * (1.f - health)), static_cast<int>(255.f * health), 60, 255));
        ++drawn;
    }

    if ((++frame_counter % 300) == 0)
        EspLog("[ESP] active local=%p drawn=%p", local_controller, reinterpret_cast<void*>(static_cast<std::uintptr_t>(drawn)));
}

#endif
