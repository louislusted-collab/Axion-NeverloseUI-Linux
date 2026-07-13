#ifdef __linux__

#include "native_esp.h"

#include "../dependencies/imgui/imgui.h"
#include "../cstrike/core/interfaces.h"
#include "../cstrike/core/convars.h"
#include "../cstrike/core/menu.h"
#include "../cstrike/core/sdk.h"
#include "../cstrike/core/variables.h"
#include "../cstrike/sdk/entity.h"
#include "../cstrike/sdk/interfaces/ccsgoinput.h"
#include "../cstrike/sdk/interfaces/cgameentitysystem.h"
#include "../cstrike/sdk/interfaces/ienginecvar.h"
#include "../cstrike/sdk/interfaces/iengineclient.h"
#include "../cstrike/utilities/draw.h"
#include "../cstrike/utilities/memory.h"
#include "../dependencies/json.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>

#include <SDL3/SDL.h>

namespace
{
ViewMatrix_t* native_view_matrix = nullptr;
CCSPlayerController** native_local_controller = nullptr;
QAngle_t* native_view_angles = nullptr;
bool attempted_matrix_lookup = false;
std::uint64_t frame_counter = 0;

void EspLog(const char* message, ...)
{
    static FILE* file = std::fopen("/tmp/cs2_esp_debug.log", "a");
    if (file == nullptr)
        return;
    va_list arguments;
    va_start(arguments, message);
    std::vfprintf(file, message, arguments);
    va_end(arguments);
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
        const auto resolve = [&](const char* primary, const char* fallback = nullptr) -> std::uintptr_t {
            auto iterator = offsets.find(primary);
            if (iterator == offsets.end() && fallback != nullptr)
                iterator = offsets.find(fallback);
            return iterator == offsets.end() ? 0 : client_base + iterator->at("value").get<std::uintptr_t>();
        };
        native_view_matrix = reinterpret_cast<ViewMatrix_t*>(resolve("dwViewMatrixNative", "dwViewMatrix"));
        native_local_controller = reinterpret_cast<CCSPlayerController**>(
            resolve("dwLocalPlayerControllerNative", "dwLocalPlayerController"));
        native_view_angles = reinterpret_cast<QAngle_t*>(resolve("dwViewAnglesNative", "dwViewAngles"));
        return native_view_matrix != nullptr && native_local_controller != nullptr;
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

    const bool loadedOffsets = LoadResolvedOffsets();
    if (loadedOffsets && native_view_angles != nullptr)
    {
        EspLog("[ESP] loaded updater offsets: view_matrix=%p local_controller_ptr=%p view_angles=%p",
               native_view_matrix, native_local_controller, native_view_angles);
        return;
    }

    // Bundled fallback for first run when the updater cache is unavailable.
    // lea r8, [rip+rel32] starts seven bytes into this native-only pattern.
    if (native_view_matrix == nullptr)
    {
        std::uint8_t* match = MEM::FindPattern(CLIENT_DLL, "C6 83 ? ? 00 00 01 4C 8D 05");
        if (match != nullptr)
            native_view_matrix = reinterpret_cast<ViewMatrix_t*>(MEM::GetAbsoluteAddress(match + 7, 3, 0));
    }

    if (native_local_controller == nullptr)
    {
        std::uint8_t* local_match = MEM::FindPattern(CLIENT_DLL, "48 83 3D ? ? ? ? 00 0F 95 C0 C3");
        if (local_match != nullptr)
            native_local_controller = reinterpret_cast<CCSPlayerController**>(MEM::GetAbsoluteAddress(local_match, 3, 1));
    }

    if (native_view_angles == nullptr)
    {
        std::uint8_t* input_match = MEM::FindPattern(CLIENT_DLL, "F3 41 0F 7E 06 F3 0F 7E 4D B0 48 8D 05 ? ? ? ? 0F 58 C1");
        if (input_match != nullptr)
        {
            auto* input_base = MEM::GetAbsoluteAddress(input_match + 10, 3, 0);
            native_view_angles = reinterpret_cast<QAngle_t*>(input_base + 0x558);
        }
    }
    EspLog("[ESP] view_matrix=%p local_controller_ptr=%p view_angles=%p",
           native_view_matrix, native_local_controller, native_view_angles);
}

void DrawOutlinedText(ImDrawList* draw, const ImVec2& position, const char* text, ImU32 color, ImU32 outline)
{
    if (draw == nullptr || text == nullptr || text[0] == '\0')
        return;
    draw->AddText(position + ImVec2(1.f, 1.f), outline, text);
    draw->AddText(position, color, text);
}

void DrawSkeleton(ImDrawList* draw, const ImVec2& minimum, const ImVec2& maximum)
{
    const float width = maximum.x - minimum.x;
    const float height = maximum.y - minimum.y;
    const ImVec2 head((minimum.x + maximum.x) * 0.5f, minimum.y + height * 0.08f);
    const ImVec2 neck(head.x, minimum.y + height * 0.19f);
    const ImVec2 chest(head.x, minimum.y + height * 0.36f);
    const ImVec2 pelvis(head.x, minimum.y + height * 0.56f);
    const ImVec2 leftShoulder(head.x - width * 0.30f, neck.y + height * 0.03f);
    const ImVec2 rightShoulder(head.x + width * 0.30f, neck.y + height * 0.03f);
    const ImVec2 leftElbow(head.x - width * 0.42f, minimum.y + height * 0.39f);
    const ImVec2 rightElbow(head.x + width * 0.42f, minimum.y + height * 0.39f);
    const ImVec2 leftHand(head.x - width * 0.36f, minimum.y + height * 0.57f);
    const ImVec2 rightHand(head.x + width * 0.36f, minimum.y + height * 0.57f);
    const ImVec2 leftKnee(head.x - width * 0.19f, minimum.y + height * 0.77f);
    const ImVec2 rightKnee(head.x + width * 0.19f, minimum.y + height * 0.77f);
    const ImVec2 leftFoot(head.x - width * 0.23f, maximum.y);
    const ImVec2 rightFoot(head.x + width * 0.23f, maximum.y);
    const ImVec2 segments[][2] = {
        {head, neck}, {neck, chest}, {chest, pelvis},
        {neck, leftShoulder}, {leftShoulder, leftElbow}, {leftElbow, leftHand},
        {neck, rightShoulder}, {rightShoulder, rightElbow}, {rightElbow, rightHand},
        {pelvis, leftKnee}, {leftKnee, leftFoot}, {pelvis, rightKnee}, {rightKnee, rightFoot},
    };
    const ImU32 outline = C_GET(ColorPickerVar_t, Vars.colSkeletonOutline).colValue.GetU32();
    const ImU32 color = C_GET(ColorPickerVar_t, Vars.colSkeleton).colValue.GetU32();
    for (const auto& segment : segments)
    {
        draw->AddLine(segment[0], segment[1], outline, 3.f);
        draw->AddLine(segment[0], segment[1], color, 1.f);
    }
}

void SetNativeGlow(C_CSPlayerPawn* pawn, bool enabled)
{
    static const std::uint32_t glowOffset = SCHEMA::GetOffset("C_BaseModelEntity->m_Glow");
    static const std::uint32_t glowingOffset = SCHEMA::GetOffset("CGlowProperty->m_bGlowing");
    static const std::uint32_t eligibleOffset = SCHEMA::GetOffset("CGlowProperty->m_bEligibleForScreenHighlight");
    static const std::uint32_t colorOffset = SCHEMA::GetOffset("CGlowProperty->m_glowColorOverride");
    if (pawn == nullptr || glowOffset == 0 || glowingOffset == 0)
        return;
    auto* glow = reinterpret_cast<std::uint8_t*>(pawn) + glowOffset;
    *reinterpret_cast<bool*>(glow + glowingOffset) = enabled;
    if (eligibleOffset != 0)
        *reinterpret_cast<bool*>(glow + eligibleOffset) = enabled;
    if (enabled && colorOffset != 0)
    {
        const Color_t color = C_GET(bool, Vars.bVisualChamsIgnoreZ)
            ? C_GET(ColorPickerVar_t, Vars.colVisualChamsIgnoreZ).colValue
            : C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue;
        *reinterpret_cast<Color_t*>(glow + colorOffset) = color;
    }
}

bool GetWeaponDisplay(CGameEntitySystem* entities, C_CSPlayerPawn* pawn, std::string& name, int& ammo, int& maximumAmmo)
{
    CPlayer_WeaponServices* services = pawn != nullptr ? pawn->GetWeaponServices() : nullptr;
    if (services == nullptr)
        return false;
    C_CSWeaponBase* weapon = entities->Get<C_CSWeaponBase>(services->m_hActiveWeapon());
    if (weapon == nullptr)
        return false;
    ammo = std::max(0, weapon->clip1());
    auto* data = weapon->GetVData();
    if (data == nullptr)
        return false;
    static const std::uint32_t nameOffset = SCHEMA::GetOffset("CCSWeaponBaseVData->m_szName");
    static const std::uint32_t clipOffset = SCHEMA::GetOffset("CBasePlayerWeaponVData->m_iMaxClip1");
    if (nameOffset != 0)
    {
        const char* rawName = *reinterpret_cast<const char* const*>(reinterpret_cast<std::uint8_t*>(data) + nameOffset);
        if (rawName != nullptr)
        {
            name = rawName;
            constexpr const char prefix[] = "weapon_";
            if (name.starts_with(prefix))
                name.erase(0, sizeof(prefix) - 1);
        }
    }
    maximumAmmo = clipOffset == 0 ? 0 : *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(data) + clipOffset);
    return !name.empty() || maximumAmmo > 0;
}

void DrawLegitFov()
{
    if (!C_GET(bool, Vars.legit_ui_enable) || !C_GET(bool, Vars.legit_ui_draw_fov))
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const float degrees = std::clamp(C_GET(float, Vars.legit_ui_fov_size), 5.f, 60.f);
    constexpr float radians = 3.14159265358979323846f / 180.f;
    const float radius = std::tan(degrees * 0.5f * radians) * (size.x * 0.5f);
    draw->AddCircle(size * 0.5f, radius, IM_COL32(255, 255, 255, 210), 96, 1.f);
}

void ApplyThirdPerson(C_CSPlayerPawn* localPawn)
{
    const bool enabled = C_GET(bool, Vars.bThirdperson);
    const float distance = std::clamp(C_GET(float, Vars.flThirdperson), 0.f, 150.f);
    if (I::Input != nullptr)
    {
        auto* input = reinterpret_cast<std::uint8_t*>(I::Input);
        *reinterpret_cast<bool*>(input + 0x229) = enabled;
        if (enabled)
            reinterpret_cast<QAngle_t*>(input + 0x230)->z = distance;
    }

    if (CONVAR::cam_idealdist != nullptr)
        CONVAR::cam_idealdist->GetValue<float>() = distance;
    if (CONVAR::cam_collision != nullptr)
        CONVAR::cam_collision->GetValue<bool>() = true;
    if (CONVAR::cam_snapto != nullptr)
        CONVAR::cam_snapto->GetValue<bool>() = true;
    if (CONVAR::c_thirdpersonshoulder != nullptr)
        CONVAR::c_thirdpersonshoulder->GetValue<bool>() = enabled;
    if (enabled)
    {
        if (CONVAR::c_thirdpersonshoulderaimdist != nullptr)
            CONVAR::c_thirdpersonshoulderaimdist->GetValue<float>() = 0.f;
        if (CONVAR::c_thirdpersonshoulderdist != nullptr)
            CONVAR::c_thirdpersonshoulderdist->GetValue<float>() = 0.f;
        if (CONVAR::c_thirdpersonshoulderheight != nullptr)
            CONVAR::c_thirdpersonshoulderheight->GetValue<float>() = 0.f;
        if (CONVAR::c_thirdpersonshoulderoffset != nullptr)
            CONVAR::c_thirdpersonshoulderoffset->GetValue<float>() = 0.f;
    }

    static const std::uint32_t thirdPersonOffset = [] {
        for (const char* field : {
                 "C_BasePlayerPawn->m_bIsThirdPersonPerspective",
                 "C_CSPlayerPawn->m_bIsThirdPersonPerspective",
                 "C_CSPlayerPawnBase->m_bIsThirdPersonPerspective"})
        {
            const std::uint32_t offset = SCHEMA::GetOffset(field);
            if (offset != 0)
                return offset;
        }
        return 0U;
    }();
    if (localPawn != nullptr && thirdPersonOffset != 0)
        *reinterpret_cast<bool*>(reinterpret_cast<std::uint8_t*>(localPawn) + thirdPersonOffset) = enabled;
}

void ApplyLegitAim(CGameEntitySystem* entities, CCSPlayerController* localController, C_CSPlayerPawn* localPawn)
{
    if (!C_GET(bool, Vars.legit_ui_enable) || !C_GET(bool, Vars.legit_ui_aim) ||
        MENU::bMainWindowOpened || native_view_angles == nullptr)
        return;
    float mouseX = 0.f, mouseY = 0.f;
    if ((SDL_GetMouseState(&mouseX, &mouseY) & SDL_BUTTON_LMASK) == 0)
        return;
    CGameSceneNode* localScene = localPawn != nullptr ? localPawn->GetGameSceneNode() : nullptr;
    if (localScene == nullptr)
        return;
    const Vector_t localEye = localScene->GetAbsOrigin() + Vector_t(0.f, 0.f, 64.f);
    const QAngle_t current = *native_view_angles;
    if (!current.IsValid())
        return;

    float bestFov = std::clamp(C_GET(float, Vars.legit_ui_fov_size), 5.f, 60.f);
    QAngle_t bestDelta{};
    bool found = false;
    constexpr float degrees = 180.f / 3.14159265358979323846f;
    for (int index = 1; index <= 128; ++index)
    {
        CCSPlayerController* controller = entities->Get<CCSPlayerController>(index);
        if (controller == nullptr || controller == localController)
            continue;
        C_CSPlayerPawn* pawn = entities->Get<C_CSPlayerPawn>(controller->GetPawnHandle());
        if (pawn == nullptr || pawn == localPawn || pawn->GetHealth() <= 0 || pawn->GetTeam() == localPawn->GetTeam())
            continue;
        CGameSceneNode* scene = pawn->GetGameSceneNode();
        if (scene == nullptr || scene->IsDormant())
            continue;
        const Vector_t target = scene->GetAbsOrigin() + Vector_t(0.f, 0.f, 64.f);
        const Vector_t difference = target - localEye;
        const float horizontal = std::hypot(difference.x, difference.y);
        if (horizontal < 0.001f)
            continue;
        const QAngle_t wanted(-std::atan2(difference.z, horizontal) * degrees,
                              std::atan2(difference.y, difference.x) * degrees, 0.f);
        const QAngle_t delta(std::remainder(wanted.x - current.x, 360.f),
                             std::remainder(wanted.y - current.y, 360.f), 0.f);
        const float fov = std::hypot(delta.x, delta.y);
        if (fov < bestFov)
        {
            bestFov = fov;
            bestDelta = delta;
            found = true;
        }
    }
    if (!found)
        return;
    const float smoothness = std::max(1.f, C_GET(float, Vars.legit_ui_smoothness));
    QAngle_t result(current.x + bestDelta.x / smoothness, current.y + bestDelta.y / smoothness, 0.f);
    result.Clamp();
    *native_view_angles = result;
}
}

void LinuxNativeEsp::Render()
{
    static bool logged_entry = false;
    const bool enabled = C_GET(bool, Vars.bVisualOverlay);
    if (!logged_entry)
    {
        EspLog("[ESP] render path reached enabled=%p resource_service=%p",
               reinterpret_cast<void*>(static_cast<std::uintptr_t>(enabled)), I::GameResourceService);
        logged_entry = true;
    }
    // Native engine vtable indices differ from Windows and are not required
    // here; valid resource/local-player pointers are a stronger state check.
    if (I::GameResourceService == nullptr)
        return;

    ResolveViewMatrix();
    if (native_view_matrix == nullptr || native_local_controller == nullptr)
        return;

    CGameEntitySystem* entities = I::GameResourceService->pGameEntitySystem;
    if (entities == nullptr)
        return;

    SDK::ViewMatrix = *native_view_matrix;

    CCSPlayerController* local_controller = native_local_controller != nullptr ? *native_local_controller : nullptr;
    if (local_controller == nullptr)
    {
        static bool logged_local = false;
        if (!logged_local)
        {
            EspLog("[ESP] local controller is null; pointer=%p", native_local_controller);
            logged_local = true;
        }
        return;
    }
    C_CSPlayerPawn* local_pawn = entities->Get<C_CSPlayerPawn>(local_controller->GetPawnHandle());
    if (local_pawn == nullptr)
        return;

    ApplyThirdPerson(local_pawn);
    DrawLegitFov();
    ApplyLegitAim(entities, local_controller, local_pawn);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (draw == nullptr)
        return;

    int found_entities = 0;
    int found_pawns = 0;
    int alive_pawns = 0;
    int enemy_pawns = 0;
    int projected_pawns = 0;
    int drawn = 0;
    for (int index = 1; index <= 128; ++index)
    {
        CCSPlayerController* controller = entities->Get<CCSPlayerController>(index);
        if (controller == nullptr || controller == local_controller)
            continue;

        ++found_entities;

        C_CSPlayerPawn* pawn = entities->Get<C_CSPlayerPawn>(controller->GetPawnHandle());
        if (pawn == nullptr || pawn == local_pawn)
            continue;
        ++found_pawns;

        const int health_value = pawn->GetHealth();
        if (health_value <= 0 || health_value > 200)
            continue;
        ++alive_pawns;

        const bool isEnemy = pawn->GetTeam() != local_pawn->GetTeam();
        const bool glowEnabled = enabled && isEnemy &&
            (C_GET(bool, Vars.bVisualChams) || C_GET(bool, Vars.bVisualChamsIgnoreZ));
        SetNativeGlow(pawn, glowEnabled);
        if (!isEnemy)
            continue;
        ++enemy_pawns;

        CGameSceneNode* scene = pawn->GetGameSceneNode();
        if (scene == nullptr || scene->IsDormant())
            continue;

        const Vector_t feet = scene->GetAbsOrigin();
        const Vector_t head(feet.x, feet.y, feet.z + 72.f);
        ImVec2 feet_screen{}, head_screen{};
        if (!D::WorldToScreen(feet, feet_screen) || !D::WorldToScreen(head, head_screen))
            continue;
        ++projected_pawns;

        const float height = feet_screen.y - head_screen.y;
        if (height < 4.f || height > ImGui::GetIO().DisplaySize.y * 1.5f)
            continue;
        const float width = height * 0.45f;
        const ImVec2 min(head_screen.x - width * 0.5f, head_screen.y);
        const ImVec2 max(head_screen.x + width * 0.5f, feet_screen.y);

        if (!enabled)
            continue;

        if (const auto& box = C_GET(FrameOverlayVar_t, Vars.overlayBox); box.bEnable)
        {
            draw->AddRect(min - ImVec2(1.f, 1.f), max + ImVec2(1.f, 1.f), box.colOutline.GetU32(),
                          box.flRounding, 0, box.flThickness + 2.f);
            draw->AddRect(min, max, box.colPrimary.GetU32(), box.flRounding, 0, box.flThickness);
        }

        if (const auto& healthConfig = C_GET(BarOverlayVar_t, Vars.overlayHealthBar); healthConfig.bEnable)
        {
            const float health = std::clamp(static_cast<float>(health_value) / 100.f, 0.f, 1.f);
            const float barWidth = std::max(2.f, healthConfig.flThickness + 1.f);
            const ImU32 healthColor = healthConfig.bUseFactorColor
                ? Color_t::FromHSB((health * 120.f) / 360.f, 1.f, 1.f).GetU32()
                : healthConfig.colPrimary.GetU32();
            draw->AddRectFilled(ImVec2(min.x - barWidth - 3.f, min.y), ImVec2(min.x - 2.f, max.y), IM_COL32(0, 0, 0, 220));
            draw->AddRectFilled(ImVec2(min.x - barWidth - 2.f, max.y - height * health),
                                ImVec2(min.x - 3.f, max.y - 1.f), healthColor);
        }

        if (const auto& nameConfig = C_GET(TextOverlayVar_t, Vars.overlayName); nameConfig.bEnable)
        {
            const char* playerName = controller->GetPlayerName();
            if (playerName != nullptr)
            {
                const ImVec2 textSize = ImGui::CalcTextSize(playerName);
                DrawOutlinedText(draw, ImVec2((min.x + max.x - textSize.x) * 0.5f, min.y - textSize.y - 2.f),
                                 playerName, nameConfig.colPrimary.GetU32(), nameConfig.colOutline.GetU32());
            }
        }

        std::string weaponName;
        int ammo = 0, maximumAmmo = 0;
        const bool needWeapon = C_GET(TextOverlayVar_t, Vars.Weaponesp).bEnable || C_GET(BarOverlayVar_t, Vars.AmmoBar).bEnable;
        if (needWeapon && GetWeaponDisplay(entities, pawn, weaponName, ammo, maximumAmmo))
        {
            float bottomOffset = 3.f;
            if (const auto& ammoConfig = C_GET(BarOverlayVar_t, Vars.AmmoBar); ammoConfig.bEnable && maximumAmmo > 0)
            {
                const float factor = std::clamp(static_cast<float>(ammo) / static_cast<float>(maximumAmmo), 0.f, 1.f);
                draw->AddRectFilled(ImVec2(min.x, max.y + bottomOffset), ImVec2(max.x, max.y + bottomOffset + 4.f), IM_COL32(0, 0, 0, 220));
                draw->AddRectFilled(ImVec2(min.x + 1.f, max.y + bottomOffset + 1.f),
                                    ImVec2(min.x + 1.f + (width - 2.f) * factor, max.y + bottomOffset + 3.f),
                                    ammoConfig.colPrimary.GetU32());
                bottomOffset += 6.f;
            }
            if (const auto& weaponConfig = C_GET(TextOverlayVar_t, Vars.Weaponesp); weaponConfig.bEnable && !weaponName.empty())
            {
                const ImVec2 textSize = ImGui::CalcTextSize(weaponName.c_str());
                DrawOutlinedText(draw, ImVec2((min.x + max.x - textSize.x) * 0.5f, max.y + bottomOffset),
                                 weaponName.c_str(), weaponConfig.colPrimary.GetU32(), weaponConfig.colOutline.GetU32());
            }
        }

        if (C_GET(bool, Vars.bSkeleton))
            DrawSkeleton(draw, min, max);

        float flagOffset = 0.f;
        const unsigned int flags = C_GET(unsigned int, Vars.pEspFlags);
        if ((flags & FLAGS_ARMOR) != 0 && pawn->GetArmorValue() > 0)
        {
            const char* armor = controller->m_bPawnHasHelmet() ? "HK" : "K";
            const auto& config = C_GET(TextOverlayVar_t, Vars.HKFlag);
            DrawOutlinedText(draw, ImVec2(max.x + 4.f, min.y + flagOffset), armor,
                             config.colPrimary.GetU32(), config.colOutline.GetU32());
            flagOffset += ImGui::GetTextLineHeight();
        }
        if ((flags & FLAGS_DEFUSER) != 0 && controller->m_bPawnHasDefuser())
        {
            const auto& config = C_GET(TextOverlayVar_t, Vars.KitFlag);
            DrawOutlinedText(draw, ImVec2(max.x + 4.f, min.y + flagOffset), "KIT",
                             config.colPrimary.GetU32(), config.colOutline.GetU32());
        }
        ++drawn;
    }

    if ((++frame_counter % 300) == 0)
        EspLog("[ESP] local=%p entities=%d pawns=%d alive=%d enemies=%d projected=%d drawn=%d",
               local_controller, found_entities, found_pawns, alive_pawns, enemy_pawns, projected_pawns, drawn);
}

#endif
