#ifdef __linux__

#include "native_esp.h"
#include "native_chams.h"
#include "vulkan_hook.h"

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
#include "../cstrike/utilities/inputsystem.h"
#include "../cstrike/utilities/memory.h"
#include "../dependencies/json.hpp"

#include <cstring>
#include <algorithm>
#include <array>
#include <cstddef>
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
bool native_thirdperson_active = false;

struct NativeEconAttribute
{
    void* vtable = nullptr;
    void* owner = nullptr;
    std::array<std::byte, 0x20> padding{};
    std::uint16_t definition = 0;
    std::uint16_t definitionPadding = 0;
    float value = 0.f;
    float initialValue = 0.f;
    std::int32_t refundableCurrency = 0;
    bool setBonus = false;
    std::array<std::byte, 7> tailPadding{};
};
static_assert(sizeof(NativeEconAttribute) == 0x48);

struct TemporarySkinAttributes
{
    C_NetworkUtlVectorBase<NativeEconAttribute>* list = nullptr;
    std::uint32_t originalSize = 0;
    NativeEconAttribute* originalElements = nullptr;
    std::array<NativeEconAttribute, 3> attributes{};
};

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

bone_data* GetLiveBoneData(CGameSceneNode* scene)
{
    static const std::uint32_t modelStateOffset = SCHEMA::GetOffset("CSkeletonInstance->m_modelState");
    static const std::uint32_t boneCacheOffset =
        SCHEMA::GetOffset("CBodyComponentSkeletonInstance->m_skeletonInstance");
    if (scene == nullptr || modelStateOffset == 0 || boneCacheOffset == 0)
        return nullptr;
    return *reinterpret_cast<bone_data**>(reinterpret_cast<std::uint8_t*>(scene) + modelStateOffset + boneCacheOffset);
}

bool GetLiveBonePosition(CGameSceneNode* scene, int index, Vector_t& output)
{
    bone_data* bones = GetLiveBoneData(scene);
    if (bones == nullptr || index < 0 || index > 128)
        return false;
    output = bones[index].pos;
    const Vector_t origin = scene->GetAbsOrigin();
    return output.IsValid() && output.DistToSqr(origin) < 90000.f;
}

void DrawSkeleton(ImDrawList* draw, CGameSceneNode* scene)
{
    // CS2's live humanoid bone cache. These are parent/child joints, not
    // points synthesized from the ESP bounding box.
    static constexpr int segments[][2] = {
        {6, 5}, {5, 4}, {4, 3}, {3, 2}, {2, 1}, {1, 0},
        {5, 8}, {8, 9}, {9, 10},
        {5, 13}, {13, 14}, {14, 15},
        {0, 22}, {22, 23}, {23, 24},
        {0, 25}, {25, 26}, {26, 27},
    };
    const ImU32 outline = C_GET(ColorPickerVar_t, Vars.colSkeletonOutline).colValue.GetU32();
    const ImU32 color = C_GET(ColorPickerVar_t, Vars.colSkeleton).colValue.GetU32();
    for (const auto& segment : segments)
    {
        Vector_t start{}, end{};
        ImVec2 startScreen{}, endScreen{};
        if (!GetLiveBonePosition(scene, segment[0], start) ||
            !GetLiveBonePosition(scene, segment[1], end) ||
            !D::WorldToScreen(start, startScreen) || !D::WorldToScreen(end, endScreen))
            continue;
        draw->AddLine(startScreen, endScreen, outline, 3.f);
        draw->AddLine(startScreen, endScreen, color, 1.f);
    }
}

bool IsNativeButtonDown(int key)
{
    float x = 0.f, y = 0.f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
    switch (key)
    {
    case VK_LBUTTON: return (buttons & SDL_BUTTON_LMASK) != 0;
    case VK_RBUTTON: return (buttons & SDL_BUTTON_RMASK) != 0;
    case VK_MBUTTON: return (buttons & SDL_BUTTON_MMASK) != 0;
    case VK_XBUTTON1: return (buttons & SDL_BUTTON_X1MASK) != 0;
    case VK_XBUTTON2: return (buttons & SDL_BUTTON_X2MASK) != 0;
    default: return key > 0 && IPT::IsKeyDown(static_cast<std::uint32_t>(key));
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
    const bool featureEnabled = C_GET(bool, Vars.bThirdperson);
    static bool wasPressed = false;
    static bool wasEnabled = false;
    if (!featureEnabled)
    {
        native_thirdperson_active = false;
        wasPressed = false;
    }
    else
    {
        const bool pressed = IsNativeButtonDown(C_GET(int, Vars.thirdperson_ui_key));
        if (!wasEnabled)
            native_thirdperson_active = false;
        if (pressed && !wasPressed && !MENU::bMainWindowOpened)
            native_thirdperson_active = !native_thirdperson_active;
        wasPressed = pressed;
    }
    wasEnabled = featureEnabled;

    const bool enabled = featureEnabled && native_thirdperson_active;
    const float distance = std::clamp(C_GET(float, Vars.flThirdperson), 50.f, 300.f);
    if (I::Input != nullptr)
    {
        auto* input = reinterpret_cast<std::uint8_t*>(I::Input);
        *reinterpret_cast<bool*>(input + 0x229) = enabled;
    }

    if (CONVAR::cl_thirdperson != nullptr)
        CONVAR::cl_thirdperson->GetValue<bool>() = enabled;
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

    static std::uint64_t lastConfirmation = 0;
    if (enabled && SDL_GetTicks() - lastConfirmation > 2000)
    {
        lastConfirmation = SDL_GetTicks();
        EspLog("[thirdperson] writing ON input=%p schema=0x%x cvar=%p distance=%.0f",
               I::Input, thirdPersonOffset, CONVAR::cl_thirdperson, distance);
    }
}

void ApplyLegitAim(CGameEntitySystem* entities, CCSPlayerController* localController, C_CSPlayerPawn* localPawn)
{
    if (!C_GET(bool, Vars.legit_ui_enable) || !C_GET(bool, Vars.legit_ui_aim))
        return;

    static std::uint64_t nextDiagnostic = 0;
    const std::uint64_t now = SDL_GetTicks();
    const bool diagnose = now >= nextDiagnostic;
    if (diagnose)
        nextDiagnostic = now + 1000;
    if (MENU::bMainWindowOpened || native_thirdperson_active || localPawn == nullptr)
    {
        if (diagnose)
            EspLog("[legit] blocked menu=%d thirdperson=%d pawn=%p",
                MENU::bMainWindowOpened, native_thirdperson_active, localPawn);
        return;
    }

    static bool previousKey = false;
    static bool toggled = false;
    static bool previousToggleMode = false;
    const bool toggleMode = C_GET(bool, Vars.legit_ui_toggle);
    const bool keyDown = IsNativeButtonDown(C_GET(int, Vars.legit_ui_key));
    if (toggleMode != previousToggleMode)
        toggled = false;
    if (toggleMode && keyDown && !previousKey)
        toggled = !toggled;
    previousKey = keyDown;
    previousToggleMode = toggleMode;
    if (!(toggleMode ? toggled : keyDown))
    {
        if (diagnose)
            EspLog("[legit] waiting key=%d down=%d toggle=%d active=%d",
                C_GET(int, Vars.legit_ui_key), keyDown, toggleMode, toggled);
        return;
    }

    CGameSceneNode* localScene = localPawn != nullptr ? localPawn->GetGameSceneNode() : nullptr;
    if (localScene == nullptr)
        return;
    const Vector_t localEye = localScene->GetAbsOrigin() + localPawn->m_vecViewOffset();
    static const std::uint32_t pawnViewAngleOffset = SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    if (pawnViewAngleOffset == 0 && native_view_angles == nullptr)
        return;
    QAngle_t current = native_view_angles != nullptr
        ? *native_view_angles
        : *reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(localPawn) + pawnViewAngleOffset);
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

        Vector_t target{};
        const int candidates[] = {
            C_GET(bool, Vars.legit_ui_bone_head) ? 5 : -1,
            C_GET(bool, Vars.legit_ui_bone_torso) ? 3 : -1,
            C_GET(bool, Vars.legit_ui_bone_arms) ? 9 : -1,
            C_GET(bool, Vars.legit_ui_bone_arms) ? 14 : -1,
            C_GET(bool, Vars.legit_ui_bone_legs) ? 23 : -1,
            C_GET(bool, Vars.legit_ui_bone_legs) ? 26 : -1,
        };
        bool foundBone = false;
        for (const int bone : candidates)
        {
            if (bone >= 0 && GetLiveBonePosition(scene, bone, target))
            {
                foundBone = true;
                break;
            }
        }
        if (!foundBone)
            target = scene->GetAbsOrigin() + pawn->m_vecViewOffset();
        if (C_GET(bool, Vars.legit_ui_prediction))
            target += pawn->GetAbsVelocity() *
                (std::clamp(C_GET(float, Vars.legit_ui_prediction_ms), 0.f, 250.f) / 1000.f);

        const Vector_t difference = target - localEye;
        const float horizontal = std::hypot(difference.x, difference.y);
        if (horizontal < 0.001f)
            continue;

        QAngle_t wanted(-std::atan2(difference.z, horizontal) * degrees,
                        std::atan2(difference.y, difference.x) * degrees, 0.f);
        if (C_GET(bool, Vars.legit_ui_recoil))
        {
            static const std::uint32_t cameraPunchOffset =
                SCHEMA::GetOffset("CPlayer_CameraServices->m_vecCsViewPunchAngle");
            CPlayer_CameraServices* camera = localPawn->GetCameraServices();
            if (camera != nullptr && cameraPunchOffset != 0)
            {
                const auto punch = *reinterpret_cast<QAngle_t*>(
                    reinterpret_cast<std::uint8_t*>(camera) + cameraPunchOffset);
                wanted.x -= punch.x * 2.f;
                wanted.y -= punch.y * 2.f;
            }
        }
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
    {
        if (diagnose)
            EspLog("[legit] key active but no target in %.1f degree fov current=%.1f,%.1f",
                C_GET(float, Vars.legit_ui_fov_size), current.x, current.y);
        return;
    }

    if (C_GET(bool, Vars.legit_ui_auto_shoot) && bestFov < 2.f && I::Input != nullptr)
        *reinterpret_cast<std::uint64_t*>(reinterpret_cast<std::uint8_t*>(I::Input) + 0x240) |= 1ULL;

    const float smoothnessMs = std::max(1.f, C_GET(float, Vars.legit_ui_smoothness));
    const float frameMs = std::max(0.1f, ImGui::GetIO().DeltaTime * 1000.f);
    const float step = std::clamp(frameMs / smoothnessMs, 0.f, 1.f);
    QAngle_t result(current.x + bestDelta.x * step, current.y + bestDelta.y * step, 0.f);
    result.Clamp();

    // Copy the old native internal's proven input path: feed the angular
    // adjustment into SDL_GetRelativeMouseState so CreateMove consumes it as
    // ordinary mouse input. Direct angle writes remain synchronized below as
    // a visual/same-frame fallback.
    const float sensitivity = CONVAR::sensitivity != nullptr
        ? std::max(0.01f, CONVAR::sensitivity->GetValue<float>()) : 2.f;
    const float yawScale = CONVAR::m_yaw != nullptr
        ? std::max(0.0001f, std::fabs(CONVAR::m_yaw->GetValue<float>())) : 0.022f;
    const float pitchScale = CONVAR::m_pitch != nullptr
        ? std::max(0.0001f, std::fabs(CONVAR::m_pitch->GetValue<float>())) : 0.022f;
    QueueNativeAimDelta(-(bestDelta.y * step) / (sensitivity * yawScale),
                         (bestDelta.x * step) / (sensitivity * pitchScale));

    if (pawnViewAngleOffset != 0)
        *reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(localPawn) + pawnViewAngleOffset) = result;
    if (native_view_angles != nullptr)
        *native_view_angles = result;
    else if (I::Input != nullptr)
        *reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(I::Input) + 0x548) = result;

    if (diagnose)
        EspLog("[legit] applied fov=%.2f step=%.3f current=%.1f,%.1f result=%.1f,%.1f view=%p input=%p",
            bestFov, step, current.x, current.y, result.x, result.y, native_view_angles, I::Input);
}

void ApplyNativeSkin(CGameEntitySystem* entities, CCSPlayerController* localController, C_CSPlayerPawn* localPawn)
{
    if (!C_GET(bool, Vars.skin_ui_enable) || entities == nullptr ||
        localController == nullptr || localPawn == nullptr)
        return;
    CPlayer_WeaponServices* services = localPawn->GetWeaponServices();
    if (services == nullptr)
        return;

    static const std::uint32_t myWeapons = SCHEMA::GetOffset("CPlayer_WeaponServices->m_hMyWeapons");
    static const std::uint32_t attributeManager = SCHEMA::GetOffset("C_EconEntity->m_AttributeManager");
    static const std::uint32_t attributesInitialized = SCHEMA::GetOffset("C_EconEntity->m_bAttributesInitialized");
    static const std::uint32_t attachmentDirty = SCHEMA::GetOffset("C_EconEntity->m_bAttachmentDirty");
    static const std::uint32_t itemOffset = SCHEMA::GetOffset("C_AttributeContainer->m_Item");
    static const std::uint32_t attributeList = SCHEMA::GetOffset("C_EconItemView->m_AttributeList");
    static const std::uint32_t attributes = SCHEMA::GetOffset("CAttributeList->m_Attributes");
    static const std::uint32_t idHigh = SCHEMA::GetOffset("C_EconItemView->m_iItemIDHigh");
    static const std::uint32_t idLow = SCHEMA::GetOffset("C_EconItemView->m_iItemIDLow");
    static const std::uint32_t accountId = SCHEMA::GetOffset("C_EconItemView->m_iAccountID");
    static const std::uint32_t definitionIndex = SCHEMA::GetOffset("C_EconItemView->m_iItemDefinitionIndex");
    static const std::uint32_t entityQuality = SCHEMA::GetOffset("C_EconItemView->m_iEntityQuality");
    static const std::uint32_t initialized = SCHEMA::GetOffset("C_EconItemView->m_bInitialized");
    static const std::uint32_t disallowSoc = SCHEMA::GetOffset("C_EconItemView->m_bDisallowSOC");
    static const std::uint32_t storeItem = SCHEMA::GetOffset("C_EconItemView->m_bIsStoreItem");
    static const std::uint32_t restoreMaterial =
        SCHEMA::GetOffset("C_EconItemView->m_bRestoreCustomMaterialAfterPrecache");
    static const std::uint32_t paintKit = SCHEMA::GetOffset("C_EconEntity->m_nFallbackPaintKit");
    static const std::uint32_t seed = SCHEMA::GetOffset("C_EconEntity->m_nFallbackSeed");
    static const std::uint32_t wear = SCHEMA::GetOffset("C_EconEntity->m_flFallbackWear");
    static const std::uint32_t statTrak = SCHEMA::GetOffset("C_EconEntity->m_nFallbackStatTrak");
    static const std::uint32_t steamId = SCHEMA::GetOffset("CBasePlayerController->m_steamID");
    if (attributeManager == 0 || itemOffset == 0 || idHigh == 0 || paintKit == 0)
        return;

    static const std::uint32_t modelState = SCHEMA::GetOffset("CSkeletonInstance->m_modelState");
    static const std::uint32_t meshGroupMask = SCHEMA::GetOffset("CModelState->m_MeshGroupMask");
    const int desiredPaint = std::max(0, C_GET(int, Vars.skin_ui_paint_kit));
    const int desiredSeed = std::clamp(C_GET(int, Vars.skin_ui_seed), 0, 1000);
    const float desiredWear = std::clamp(C_GET(float, Vars.skin_ui_wear), 0.000001f, 1.f);
    const int desiredStatTrak = C_GET(int, Vars.skin_ui_stattrak);
    const std::uint32_t ownerAccount = steamId == 0 ? 0U : static_cast<std::uint32_t>(
        *reinterpret_cast<const std::uint64_t*>(reinterpret_cast<std::uint8_t*>(localController) + steamId));
    static bool loggedOffsets = false;
    if (!loggedOffsets)
    {
        EspLog("[skin] offsets weapons=0x%x attr=0x%x item=0x%x list=0x%x/0x%x paint=0x%x wear=0x%x account=0x%x",
            myWeapons, attributeManager, itemOffset, attributeList, attributes, paintKit, wear, accountId);
        loggedOffsets = true;
    }

    std::array<TemporarySkinAttributes, 64> temporaryAttributes{};
    std::size_t temporaryAttributeCount = 0;
    bool shouldRegenerate = false;

    const auto setMeshMask = [&](CGameSceneNode* scene, std::uint64_t mask) {
        if (scene == nullptr || modelState == 0 || meshGroupMask == 0)
            return;
        auto* state = reinterpret_cast<std::uint8_t*>(scene) + modelState;
        *reinterpret_cast<std::uint64_t*>(state + meshGroupMask) = mask;
    };

    const auto prepareAttributes = [&](std::uint8_t* item) {
        if (attributeList == 0 || attributes == 0 || temporaryAttributeCount >= temporaryAttributes.size())
            return false;
        auto* list = reinterpret_cast<C_NetworkUtlVectorBase<NativeEconAttribute>*>(
            item + attributeList + attributes);
        if (list->nSize != 0 || list->pElements != nullptr)
            return false;

        TemporarySkinAttributes& temporary = temporaryAttributes[temporaryAttributeCount++];
        temporary.list = list;
        temporary.originalSize = list->nSize;
        temporary.originalElements = list->pElements;
        const std::array<std::pair<std::uint16_t, float>, 3> values{{
            {6, static_cast<float>(desiredPaint)},
            {7, static_cast<float>(desiredSeed)},
            {8, desiredWear},
        }};
        for (std::size_t index = 0; index < temporary.attributes.size(); ++index)
        {
            temporary.attributes[index].owner = item;
            temporary.attributes[index].definition = values[index].first;
            temporary.attributes[index].value = values[index].second;
            temporary.attributes[index].initialValue = values[index].second;
        }
        list->pElements = temporary.attributes.data();
        list->nSize = static_cast<std::uint32_t>(temporary.attributes.size());
        return true;
    };

    const auto applyWeapon = [&](C_CSWeaponBase* weapon) {
        if (weapon == nullptr)
            return;
        auto* base = reinterpret_cast<std::uint8_t*>(weapon);
        auto* item = base + attributeManager + itemOffset;
        const int previousPaint = *reinterpret_cast<const std::int32_t*>(base + paintKit);
        const bool changed = previousPaint != desiredPaint ||
            (seed != 0 && *reinterpret_cast<const std::int32_t*>(base + seed) != desiredSeed) ||
            (wear != 0 && std::fabs(*reinterpret_cast<const float*>(base + wear) - desiredWear) > 0.000001f);
        *reinterpret_cast<std::int32_t*>(item + idHigh) = -1;
        if (idLow != 0)
            *reinterpret_cast<std::int32_t*>(item + idLow) = -1;
        if (accountId != 0)
            *reinterpret_cast<std::uint32_t*>(item + accountId) = ownerAccount;
        if (initialized != 0)
            *reinterpret_cast<bool*>(item + initialized) = true;
        if (disallowSoc != 0)
            *reinterpret_cast<bool*>(item + disallowSoc) = false;
        if (storeItem != 0)
            *reinterpret_cast<bool*>(item + storeItem) = false;
        if (restoreMaterial != 0)
            *reinterpret_cast<bool*>(item + restoreMaterial) = true;
        if (attributesInitialized != 0)
            *reinterpret_cast<bool*>(base + attributesInitialized) = true;
        if (attachmentDirty != 0)
            *reinterpret_cast<bool*>(base + attachmentDirty) = true;
        if (entityQuality != 0 && definitionIndex != 0)
        {
            const std::uint16_t definition = *reinterpret_cast<const std::uint16_t*>(item + definitionIndex);
            if (definition >= 500 || definition == 42 || definition == 59)
                *reinterpret_cast<std::uint32_t*>(item + entityQuality) = 4;
        }

        *reinterpret_cast<std::int32_t*>(base + paintKit) = desiredPaint;
        if (seed != 0)
            *reinterpret_cast<std::int32_t*>(base + seed) = desiredSeed;
        if (wear != 0)
            *reinterpret_cast<float*>(base + wear) = desiredWear;
        if (statTrak != 0)
            *reinterpret_cast<std::int32_t*>(base + statTrak) = desiredStatTrak;

        setMeshMask(weapon->GetGameSceneNode(), 2);
        if (changed)
        {
            prepareAttributes(item);
            shouldRegenerate = true;
            EspLog("[skin] applied weapon=%p paint=%d previous=%d seed=%d wear=%.6f account=%u",
                weapon, desiredPaint, previousPaint, desiredSeed, desiredWear, ownerAccount);
        }
    };

    bool appliedCollection = false;
    if (myWeapons != 0)
    {
        auto* collection = reinterpret_cast<C_NetworkUtlVectorBase<CBaseHandle>*>(
            reinterpret_cast<std::uint8_t*>(services) + myWeapons);
        const std::uint32_t count = std::min(collection->nSize, 64U);
        if (collection->pElements != nullptr && count > 0)
        {
            for (std::uint32_t index = 0; index < count; ++index)
                applyWeapon(entities->Get<C_CSWeaponBase>(collection->pElements[index]));
            appliedCollection = true;
        }
    }
    if (!appliedCollection)
        applyWeapon(entities->Get<C_CSWeaponBase>(services->m_hActiveWeapon()));

    CCSPlayer_ViewModelServices* viewModelServices = localPawn->GetViewModelServices();
    C_CSGOViewModel* viewModel = viewModelServices != nullptr
        ? entities->Get<C_CSGOViewModel>(viewModelServices->m_hViewModel())
        : nullptr;
    if (viewModel != nullptr)
        setMeshMask(viewModel->GetGameSceneNode(), 2);

    using RegenerateWeaponSkinsFn = void(*)(bool);
    static RegenerateWeaponSkinsFn regenerateWeaponSkins = nullptr;
    static bool attemptedRegenerateLookup = false;
    if (!attemptedRegenerateLookup)
    {
        attemptedRegenerateLookup = true;
        regenerateWeaponSkins = reinterpret_cast<RegenerateWeaponSkinsFn>(MEM::FindPattern(
            CLIENT_DLL,
            "55 48 89 E5 41 55 44 0F B6 EF 41 54 53 48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 7D"));
        EspLog("[skin] native regenerate callback=%p", regenerateWeaponSkins);
    }
    if (shouldRegenerate && regenerateWeaponSkins != nullptr)
    {
        regenerateWeaponSkins(false);
        EspLog("[skin] regenerated %zu temporary attribute lists", temporaryAttributeCount);
    }

    // The engine consumes these lists synchronously. Restore its original
    // vectors immediately so no entity retains a pointer into our stack.
    for (std::size_t index = 0; index < temporaryAttributeCount; ++index)
    {
        TemporarySkinAttributes& temporary = temporaryAttributes[index];
        temporary.list->pElements = temporary.originalElements;
        temporary.list->nSize = temporary.originalSize;
    }
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
    ApplyNativeSkin(entities, local_controller, local_pawn);
    NativeChams::UpdateColors(
        C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue,
        C_GET(ColorPickerVar_t, Vars.colVisualChamsIgnoreZ).colValue);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (draw == nullptr)
        return;

    int found_entities = 0;
    int found_pawns = 0;
    int alive_pawns = 0;
    int enemy_pawns = 0;
    int projected_pawns = 0;
    int drawn = 0;
    std::array<C_CSPlayerPawn*, 128> chamsTargets{};
    std::size_t chamsTargetCount = 0;
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
        if (!isEnemy)
            continue;
        if (chamsTargetCount < chamsTargets.size())
            chamsTargets[chamsTargetCount++] = pawn;
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
            DrawSkeleton(draw, scene);

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
    NativeChams::UpdateTargets(chamsTargets.data(), chamsTargetCount);

    if ((++frame_counter % 300) == 0)
        EspLog("[ESP] local=%p entities=%d pawns=%d alive=%d enemies=%d projected=%d drawn=%d",
               local_controller, found_entities, found_pawns, alive_pawns, enemy_pawns, projected_pawns, drawn);
}

#endif
