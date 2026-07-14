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
#include "../cstrike/sdk/interfaces/iglobalvars.h"
#include "../cstrike/sdk/interfaces/itrace.h"
#include "../cstrike/utilities/draw.h"
#include "../cstrike/utilities/inputsystem.h"
#include "../cstrike/utilities/memory.h"
#include "../dependencies/json.hpp"

#include <cstring>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include <SDL3/SDL.h>

namespace
{
ViewMatrix_t* native_view_matrix = nullptr;
CCSPlayerController** native_local_controller = nullptr;
QAngle_t* native_view_angles = nullptr;
bool attempted_matrix_lookup = false;
std::uint64_t frame_counter = 0;
bool native_thirdperson_active = false;
bool native_aim_command_queued = false;
std::uint16_t active_skin_weapon_definition = 0;
bool native_skin_regenerator_ready = false;
bool native_skin_refresh_requested = false;

enum class NativeSkinRuntimeStatus
{
    WaitingForPlayer,
    MissingSchema,
    WaitingForWeapon,
    Disabled,
    ProfileDisabled,
    PaintKitRequired,
    MissingRegenerator,
    Ready,
    Refreshed,
};
NativeSkinRuntimeStatus native_skin_runtime_status = NativeSkinRuntimeStatus::WaitingForPlayer;

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
    std::vector<NativeEconAttribute> attributes{};
};

struct NativeSkinSettings
{
    std::int32_t paint = 0;
    std::int32_t seed = 1;
    float wear = 0.01f;
    std::int32_t statTrak = -1;
    bool legacyMesh = false;

    bool operator==(const NativeSkinSettings&) const = default;
};

struct OriginalWeaponSkin
{
    std::int32_t idHigh = 0;
    std::int32_t idLow = 0;
    std::uint32_t account = 0;
    std::uint32_t quality = 0;
    bool initialized = false;
    bool disallowSoc = false;
    bool storeItem = false;
    bool restoreMaterial = false;
    bool attributesInitialized = false;
    bool attachmentDirty = false;
    std::int32_t paint = 0;
    std::int32_t seed = 0;
    float wear = 0.f;
    std::int32_t statTrak = -1;
    std::uint64_t meshMask = 1;
};

struct AppliedWeaponSkin
{
    C_CSWeaponBase* weapon = nullptr;
    std::uint16_t definition = 0;
    NativeSkinSettings settings{};
    OriginalWeaponSkin original{};
    bool applied = false;
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

void LegitLog(const char* message, ...)
{
    static FILE* file = [] {
        FILE* opened = std::fopen("/tmp/cs2_legit_debug.log", "a");
        if (opened != nullptr)
            std::setvbuf(opened, nullptr, _IOLBF, 0);
        return opened;
    }();
    if (file == nullptr)
        return;
    va_list arguments;
    va_start(arguments, message);
    std::vfprintf(file, message, arguments);
    va_end(arguments);
    std::fputc('\n', file);
}

void RefreshNativeInputObject()
{
    static CCSGOInput** inputStorage = nullptr;
    static bool searched = false;
    if (!searched)
    {
        searched = true;
        std::uint8_t* match = MEM::FindPattern(CLIENT_DLL,
            "F3 41 0F 7E 06 F3 0F 7E 4D B0 48 8D 05 ? ? ? ? 0F 58 C1");
        if (match != nullptr)
            inputStorage = reinterpret_cast<CCSGOInput**>(
                MEM::GetAbsoluteAddress(match + 10, 3, 0) + 0x10);
    }
    if (inputStorage != nullptr && *inputStorage != nullptr)
        I::Input = *inputStorage;
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
        // The dumper export and the legacy auxiliary input object are not a
        // verified live command-angle destination in the current native build.
        native_view_angles = nullptr;
        return native_view_matrix != nullptr && native_local_controller != nullptr;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void ResolveViewMatrix()
{
    RefreshNativeInputObject();
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

void DrawOutlinedText(ImDrawList* draw, ImFont* font, const ImVec2& position,
                      const char* text, ImU32 color, ImU32 outline)
{
    if (draw == nullptr || font == nullptr || text == nullptr || text[0] == '\0')
        return;
    draw->AddText(font, font->FontSize, position + ImVec2(1.f, 1.f), outline, text);
    draw->AddText(font, font->FontSize, position, color, text);
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
    const float thickness = std::clamp(C_GET(float, Vars.esp_skeleton_thickness), 1.f, 5.f);
    for (const auto& segment : segments)
    {
        Vector_t start{}, end{};
        ImVec2 startScreen{}, endScreen{};
        if (!GetLiveBonePosition(scene, segment[0], start) ||
            !GetLiveBonePosition(scene, segment[1], end) ||
            !D::WorldToScreen(start, startScreen) || !D::WorldToScreen(end, endScreen))
            continue;
        draw->AddLine(startScreen, endScreen, outline, thickness + 2.f);
        draw->AddLine(startScreen, endScreen, color, thickness);
    }
}

void DrawCornerBox(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                   ImU32 color, float thickness)
{
    const float segmentX = (max.x - min.x) * 0.28f;
    const float segmentY = (max.y - min.y) * 0.22f;
    draw->AddLine(min, ImVec2(min.x + segmentX, min.y), color, thickness);
    draw->AddLine(min, ImVec2(min.x, min.y + segmentY), color, thickness);
    draw->AddLine(ImVec2(max.x - segmentX, min.y), ImVec2(max.x, min.y), color, thickness);
    draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + segmentY), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y - segmentY), ImVec2(min.x, max.y), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + segmentX, max.y), color, thickness);
    draw->AddLine(ImVec2(max.x, max.y - segmentY), max, color, thickness);
    draw->AddLine(ImVec2(max.x - segmentX, max.y), max, color, thickness);
}

void DrawOffscreenArrow(ImDrawList* draw, const Vector_t& localOrigin,
                        const Vector_t& targetOrigin, float localYaw, ImU32 color)
{
    if (draw == nullptr)
        return;
    constexpr float degrees = 180.f / 3.14159265358979323846f;
    constexpr float radians = 3.14159265358979323846f / 180.f;
    const Vector_t delta = targetOrigin - localOrigin;
    if (std::hypot(delta.x, delta.y) < 1.f)
        return;
    const float targetYaw = std::atan2(delta.y, delta.x) * degrees;
    const float relative = std::remainder(targetYaw - localYaw, 360.f) * radians;
    const ImVec2 direction(std::sin(relative), -std::cos(relative));
    const ImVec2 tangent(-direction.y, direction.x);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 center = display * 0.5f;
    const float radiusX = std::max(40.f, center.x - 28.f);
    const float radiusY = std::max(40.f, center.y - 28.f);
    const float denominator = std::sqrt(
        (direction.x * direction.x) / (radiusX * radiusX) +
        (direction.y * direction.y) / (radiusY * radiusY));
    if (!std::isfinite(denominator) || denominator <= 0.f)
        return;
    const float edgeDistance = 1.f / denominator;
    const ImVec2 tip = center + direction * edgeDistance;
    const ImVec2 base = center + direction * (edgeDistance - 18.f);
    const ImVec2 left = base + tangent * 8.f;
    const ImVec2 right = base - tangent * 8.f;
    draw->AddTriangleFilled(tip, left, right, color);
    draw->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 230), 2.f);
}

bool IsNativeButtonDown(int key)
{
    float x = 0.f, y = 0.f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
    const bool tracked = key > 0 && IPT::IsKeyDown(static_cast<std::uint32_t>(key));
    switch (key)
    {
    case VK_LBUTTON: return tracked || (buttons & SDL_BUTTON_LMASK) != 0;
    case VK_RBUTTON: return tracked || (buttons & SDL_BUTTON_RMASK) != 0;
    case VK_MBUTTON: return tracked || (buttons & SDL_BUTTON_MMASK) != 0;
    case VK_XBUTTON1: return tracked || (buttons & SDL_BUTTON_X1MASK) != 0;
    case VK_XBUTTON2: return tracked || (buttons & SDL_BUTTON_X2MASK) != 0;
    default: return tracked;
    }
}

template <typename T>
bool SafeNativeRead(const void* address, T& value)
{
    if (address == nullptr)
        return false;
    iovec local{&value, sizeof(value)};
    iovec remote{const_cast<void*>(address), sizeof(value)};
    const long read = ::syscall(SYS_process_vm_readv, ::getpid(), &local, 1UL, &remote, 1UL, 0UL);
    return read == static_cast<long>(sizeof(value));
}

bool IsSaneSchemaOffset(std::uint32_t offset, std::uint32_t maximum)
{
    return offset != 0 && offset <= maximum;
}

struct WeaponDisplayOffsets
{
    std::uint32_t weaponServices = 0;
    std::uint32_t activeWeapon = 0;
    std::uint32_t subclassId = 0;
    std::uint32_t clip1 = 0;
    std::uint32_t name = 0;
    std::uint32_t maximumClip1 = 0;
    bool valid = false;
};

const WeaponDisplayOffsets& GetWeaponDisplayOffsets()
{
    static const WeaponDisplayOffsets offsets = [] {
        WeaponDisplayOffsets result{};
        result.weaponServices = SCHEMA::GetOffset("C_BasePlayerPawn->m_pWeaponServices");
        result.activeWeapon = SCHEMA::GetOffset("CPlayer_WeaponServices->m_hActiveWeapon");
        result.subclassId = SCHEMA::GetOffset("C_BaseEntity->m_nSubclassID");
        result.clip1 = SCHEMA::GetOffset("C_BasePlayerWeapon->m_iClip1");
        result.name = SCHEMA::GetOffset("CCSWeaponBaseVData->m_szName");
        result.maximumClip1 = SCHEMA::GetOffset("CBasePlayerWeaponVData->m_iMaxClip1");
        result.valid = IsSaneSchemaOffset(result.weaponServices, 0x10000) &&
            IsSaneSchemaOffset(result.activeWeapon, 0x1000) &&
            IsSaneSchemaOffset(result.subclassId, 0x10000 - sizeof(void*)) &&
            IsSaneSchemaOffset(result.clip1, 0x10000) &&
            IsSaneSchemaOffset(result.name, 0x10000) &&
            IsSaneSchemaOffset(result.maximumClip1, 0x10000);
        EspLog("[WEAPON] schema services=0x%x active=0x%x subclass=0x%x clip=0x%x name=0x%x max_clip=0x%x valid=%d",
            result.weaponServices, result.activeWeapon, result.subclassId, result.clip1,
            result.name, result.maximumClip1, result.valid ? 1 : 0);
        return result;
    }();
    return offsets;
}

void LogWeaponReadFailure(const char* stage, const void* pawn, const void* services,
                          const void* weapon, const void* data)
{
    static std::uint64_t failures = 0;
    ++failures;
    if (failures <= 12 || failures % 600 == 0)
        EspLog("[WEAPON] read failed stage=%s pawn=%p services=%p weapon=%p data=%p count=%llu",
            stage, pawn, services, weapon, data, static_cast<unsigned long long>(failures));
}

bool SafeReadWeaponName(const char* rawName, std::string& name)
{
    if (rawName == nullptr)
        return false;
    std::array<char, 96> buffer{};
    iovec local{buffer.data(), buffer.size()};
    iovec remote{const_cast<char*>(rawName), buffer.size()};
    const long read = ::syscall(SYS_process_vm_readv, ::getpid(), &local, 1UL, &remote, 1UL, 0UL);
    if (read <= 0)
        return false;
    const auto end = std::find(buffer.begin(), buffer.begin() + read, '\0');
    if (end == buffer.begin() || end == buffer.begin() + read)
        return false;
    for (auto iterator = buffer.begin(); iterator != end; ++iterator)
    {
        const unsigned char character = static_cast<unsigned char>(*iterator);
        if (!(std::isalnum(character) || character == '_' || character == '-'))
            return false;
    }
    name.assign(buffer.begin(), end);
    constexpr std::string_view prefix = "weapon_";
    if (name.starts_with(prefix))
        name.erase(0, prefix.size());
    return !name.empty();
}

bool SafeGetEntity(CGameEntitySystem* entities, std::uint32_t handle, C_CSWeaponBase*& weapon)
{
    weapon = nullptr;
    if (entities == nullptr || handle == INVALID_EHANDLE_INDEX)
        return false;
    const int entry = static_cast<int>(handle & ENT_ENTRY_MASK);
    if (entry < 0 || entry >= MAX_TOTAL_ENTITIES)
        return false;
    std::uintptr_t bucket = 0;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entities) + 0x10 + 0x8 * (entry >> 9), bucket) ||
        bucket == 0)
        return false;
    if (!SafeNativeRead(reinterpret_cast<const void*>(bucket + 0x70 * (entry & 0x1FF)), weapon) ||
        weapon == nullptr)
        return false;
    void* vtable = nullptr;
    if (!SafeNativeRead(weapon, vtable) || vtable == nullptr)
    {
        weapon = nullptr;
        return false;
    }
    return true;
}

bool GetWeaponDisplay(CGameEntitySystem* entities, C_CSPlayerPawn* pawn, std::string& name,
                      int& ammo, int& maximumAmmo, C_CSWeaponBase** activeWeapon = nullptr)
{
    name.clear();
    ammo = 0;
    maximumAmmo = 0;
    if (activeWeapon != nullptr)
        *activeWeapon = nullptr;

    const WeaponDisplayOffsets& offsets = GetWeaponDisplayOffsets();
    if (entities == nullptr || pawn == nullptr || !offsets.valid)
    {
        LogWeaponReadFailure("prerequisite", pawn, nullptr, nullptr, nullptr);
        return false;
    }

    CPlayer_WeaponServices* services = nullptr;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(pawn) + offsets.weaponServices, services) ||
        services == nullptr)
    {
        LogWeaponReadFailure("services", pawn, services, nullptr, nullptr);
        return false;
    }
    std::uint32_t handle = INVALID_EHANDLE_INDEX;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(services) + offsets.activeWeapon, handle))
    {
        LogWeaponReadFailure("active-handle", pawn, services, nullptr, nullptr);
        return false;
    }
    C_CSWeaponBase* weapon = nullptr;
    if (!SafeGetEntity(entities, handle, weapon))
    {
        LogWeaponReadFailure("entity", pawn, services, weapon, nullptr);
        return false;
    }
    if (activeWeapon != nullptr)
        *activeWeapon = weapon;

    void* data = nullptr;
    const auto* weaponBytes = reinterpret_cast<const std::uint8_t*>(weapon);
    const bool readData = SafeNativeRead(weaponBytes + offsets.subclassId + sizeof(void*), data) && data != nullptr;
    int rawAmmo = 0;
    const bool readAmmo = SafeNativeRead(weaponBytes + offsets.clip1, rawAmmo) &&
        rawAmmo >= -1 && rawAmmo <= 1000;
    int rawMaximumAmmo = 0;
    const bool readMaximumAmmo = readData &&
        SafeNativeRead(reinterpret_cast<const std::uint8_t*>(data) + offsets.maximumClip1, rawMaximumAmmo) &&
        rawMaximumAmmo > 0 && rawMaximumAmmo <= 1000;
    const char* rawName = nullptr;
    const bool readName = readData &&
        SafeNativeRead(reinterpret_cast<const std::uint8_t*>(data) + offsets.name, rawName) &&
        SafeReadWeaponName(rawName, name);

    if (readAmmo)
        ammo = std::max(0, rawAmmo);
    if (readMaximumAmmo)
        maximumAmmo = rawMaximumAmmo;
    if (!readData || (!readName && !readMaximumAmmo))
        LogWeaponReadFailure(!readData ? "vdata" : "vdata-fields", pawn, services, weapon, data);
    return readName || (readAmmo && readMaximumAmmo);
}

bool GetWeaponName(C_CSWeaponBase* weapon, std::string& name, int* maximumAmmo = nullptr)
{
    name.clear();
    if (maximumAmmo != nullptr)
        *maximumAmmo = 0;
    if (weapon == nullptr)
        return false;
    const WeaponDisplayOffsets& offsets = GetWeaponDisplayOffsets();
    if (!offsets.valid)
        return false;
    void* data = nullptr;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(weapon) + offsets.subclassId + sizeof(void*), data) ||
        data == nullptr)
        return false;
    const char* rawName = nullptr;
    const bool hasName = SafeNativeRead(reinterpret_cast<const std::uint8_t*>(data) + offsets.name, rawName) &&
        SafeReadWeaponName(rawName, name);
    if (maximumAmmo != nullptr)
    {
        int rawMaximumAmmo = 0;
        if (SafeNativeRead(reinterpret_cast<const std::uint8_t*>(data) + offsets.maximumClip1, rawMaximumAmmo) &&
            rawMaximumAmmo > 0 && rawMaximumAmmo <= 1000)
            *maximumAmmo = rawMaximumAmmo;
    }
    return hasName;
}

bool PlayerHasC4(CGameEntitySystem* entities, C_CSPlayerPawn* pawn)
{
    static const std::uint32_t myWeapons = SCHEMA::GetOffset("CPlayer_WeaponServices->m_hMyWeapons");
    const WeaponDisplayOffsets& offsets = GetWeaponDisplayOffsets();
    if (entities == nullptr || pawn == nullptr || myWeapons == 0 || !offsets.valid)
        return false;
    CPlayer_WeaponServices* services = nullptr;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(pawn) + offsets.weaponServices, services) ||
        services == nullptr)
        return false;
    C_NetworkUtlVectorBase<CBaseHandle> collection{};
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(services) + myWeapons, collection) ||
        collection.pElements == nullptr || collection.nSize > 64)
        return false;
    for (std::uint32_t index = 0; index < collection.nSize; ++index)
    {
        std::uint32_t handle = INVALID_EHANDLE_INDEX;
        if (!SafeNativeRead(collection.pElements + index, handle))
            continue;
        C_CSWeaponBase* weapon = nullptr;
        if (!SafeGetEntity(entities, handle, weapon))
            continue;
        std::string name;
        if (GetWeaponName(weapon, name) && name == "c4")
            return true;
    }
    return false;
}

char GetWeaponIcon(std::string_view name)
{
    struct IconEntry { std::string_view name; char glyph; };
    static constexpr IconEntry icons[] = {
        {"deagle", 'A'}, {"elite", 'B'}, {"fiveseven", 'C'}, {"glock", 'D'},
        {"hkp2000", 'E'}, {"p250", 'F'}, {"usp_silencer", 'G'}, {"tec9", 'H'},
        {"cz75a", 'I'}, {"revolver", 'J'}, {"mac10", 'K'}, {"ump45", 'L'},
        {"bizon", 'M'}, {"mp7", 'N'}, {"mp9", 'O'}, {"mp5sd", 'O'}, {"p90", 'P'},
        {"galilar", 'Q'}, {"famas", 'R'}, {"m4a1", 'S'}, {"m4a1_silencer", 'T'},
        {"aug", 'U'}, {"sg556", 'V'}, {"ak47", 'W'}, {"g3sg1", 'X'},
        {"scar20", 'Y'}, {"awp", 'Z'}, {"ssg08", 'a'}, {"xm1014", 'b'},
        {"sawedoff", 'c'}, {"mag7", 'd'}, {"nova", 'e'}, {"negev", 'f'},
        {"m249", 'g'}, {"taser", 'h'}, {"flashbang", 'i'}, {"hegrenade", 'j'},
        {"smokegrenade", 'k'}, {"molotov", 'l'}, {"decoy", 'm'},
        {"incgrenade", 'n'}, {"c4", 'o'},
    };
    if (name == "knife" || name.starts_with("knife_") || name == "bayonet")
        return ']';
    for (const IconEntry& icon : icons)
        if (icon.name == name)
            return icon.glyph;
    return '\0';
}

int GetRageWeaponGroup(std::string_view name)
{
    if (name == "deagle" || name == "revolver")
        return 2; // Heavy pistols
    if (name == "glock" || name == "hkp2000" || name == "usp_silencer" ||
        name == "p250" || name == "tec9" || name == "fiveseven" ||
        name == "cz75a" || name == "elite")
        return 1; // Pistols
    if (name == "ak47" || name == "m4a1" || name == "m4a1_silencer" ||
        name == "galilar" || name == "famas" || name == "aug" || name == "sg556")
        return 3; // Assault rifles
    if (name == "g3sg1" || name == "scar20")
        return 4; // Auto snipers
    if (name == "ssg08")
        return 5;
    if (name == "awp")
        return 6;
    return 0;
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

void ApplyNativeMovement(C_CSPlayerPawn* localPawn)
{
    if (localPawn == nullptr || localPawn->GetHealth() <= 0 || MENU::bMainWindowOpened)
    {
        SetNativeBhopInput(false, false, false);
        SetNativeStrafeInput(false, 0.f, 0.f);
        return;
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool spaceHeld = keys != nullptr && keys[SDL_SCANCODE_SPACE];
    const bool onGround = (localPawn->GetFlags() & FL_ONGROUND) != 0;
    const bool bhopEnabled = C_GET(bool, Vars.bAutoBHop);
    static bool previousSpace = false;
    static bool previousGround = true;
    static bool allowedForLanding = true;
    if (bhopEnabled && spaceHeld && (!previousSpace || (onGround && !previousGround)))
    {
        const int chance = std::clamp(C_GET(int, Vars.nAutoBHopChance), 1, 100);
        const std::uint64_t mixed = SDL_GetTicks() * 1103515245ULL + frame_counter * 12345ULL;
        allowedForLanding = static_cast<int>(mixed % 100ULL) < chance;
    }
    if (!spaceHeld)
        allowedForLanding = true;
    // Keep the CreateMove handler active even when this landing failed the
    // configured chance so the physical held-space bit is explicitly cleared.
    SetNativeBhopInput(bhopEnabled, spaceHeld, onGround && allowedForLanding);
    previousSpace = spaceHeld;
    previousGround = onGround;

    if (!C_GET(bool, Vars.bAutostrafe) || onGround)
    {
        SetNativeStrafeInput(false, 0.f, 0.f);
        return;
    }

    const Vector_t velocity = localPawn->GetAbsVelocity();
    const float speed = std::hypot(velocity.x, velocity.y);
    if (!std::isfinite(speed) || speed < 10.f)
    {
        SetNativeStrafeInput(false, 0.f, 0.f);
        return;
    }

    float viewYaw = native_view_angles != nullptr ? native_view_angles->y : 0.f;
    static const std::uint32_t viewAngleOffset = SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    if (viewAngleOffset != 0)
    {
        const QAngle_t view = *reinterpret_cast<const QAngle_t*>(
            reinterpret_cast<const std::uint8_t*>(localPawn) + viewAngleOffset);
        if (view.IsValid())
            viewYaw = view.y;
    }
    constexpr float degrees = 180.f / 3.14159265358979323846f;
    const float velocityYaw = std::atan2(velocity.y, velocity.x) * degrees;
    const float delta = std::remainder(viewYaw - velocityYaw, 360.f);
    const float correction = std::atan2(15.f, speed) * degrees * 1.8f;
    static bool alternate = false;
    alternate = !alternate;
    float direction = alternate ? -1.f : 1.f;
    if (std::fabs(delta) > 170.f || delta > correction)
        direction = -1.f;
    else if (delta < -correction)
        direction = 1.f;
    const float strength = std::clamp(C_GET(float, Vars.autostrafe_smooth) / 100.f, 0.01f, 1.f);
    SetNativeStrafeInput(true, 0.f, direction * strength);
}

void ApplySmokeRemoval(CGameEntitySystem* entities, C_CSPlayerPawn* localPawn)
{
    static const std::uint32_t overlayAlpha =
        SCHEMA::GetOffset("C_CSPlayerPawnBase->m_flLastSmokeOverlayAlpha");
    static const std::uint32_t smokeAge =
        SCHEMA::GetOffset("C_CSPlayerPawnBase->m_flLastSmokeAge");
    static CConVar* smokeVolume = CONVAR::Find("cl_smoke_volumeprop");
    static CConVar* smokeFullResolution = CONVAR::Find("r_csgo_smoke_fullres_pass");
    static CConVar* smokeEnhance = CONVAR::Find("r_csgo_smoke_fullres_enhance");
    static CConVar* smokeShadow = CONVAR::Find("r_csgo_smoke_shadow");
    static CConVar* drawParticles = CONVAR::Find("r_drawparticles");
    static bool previousEnabled = false;
    static bool originalVolume = true;
    static bool originalFullResolution = true;
    static bool originalEnhance = true;
    static bool originalShadow = true;
    static bool originalDrawParticles = true;
    static bool capturedVolume = false;
    static bool capturedFullResolution = false;
    static bool capturedEnhance = false;
    static bool capturedShadow = false;
    static bool capturedDrawParticles = false;

    const bool enabled = C_GET(bool, Vars.bRemoveSmoke);
    if (enabled && !previousEnabled)
    {
        capturedVolume = CONVAR::ReadBool(smokeVolume, originalVolume);
        capturedFullResolution = CONVAR::ReadBool(smokeFullResolution, originalFullResolution);
        capturedEnhance = CONVAR::ReadBool(smokeEnhance, originalEnhance);
        capturedShadow = CONVAR::ReadBool(smokeShadow, originalShadow);
        capturedDrawParticles = CONVAR::ReadBool(drawParticles, originalDrawParticles);
    }
    else if (!enabled && previousEnabled)
    {
        if (capturedVolume) CONVAR::WriteBool(smokeVolume, originalVolume);
        if (capturedFullResolution) CONVAR::WriteBool(smokeFullResolution, originalFullResolution);
        if (capturedEnhance) CONVAR::WriteBool(smokeEnhance, originalEnhance);
        if (capturedShadow) CONVAR::WriteBool(smokeShadow, originalShadow);
        if (capturedDrawParticles) CONVAR::WriteBool(drawParticles, originalDrawParticles);
    }
    previousEnabled = enabled;
    if (!enabled || localPawn == nullptr)
        return;

    // Do not iterate raw entity slots or mutate projectile internals here.
    // Those handles can disappear on the particle thread exactly when smoke
    // blooms, which was the source of the reported crash. These are stable
    // client render controls plus the local pawn's schema-resolved overlay.
    bool volumeReadback = true;
    bool fullResolutionReadback = true;
    bool enhanceReadback = true;
    bool shadowReadback = true;
    bool particlesReadback = true;
    const bool wroteVolume = CONVAR::WriteBool(smokeVolume, false, &volumeReadback);
    const bool wroteFullResolution = CONVAR::WriteBool(smokeFullResolution, false, &fullResolutionReadback);
    const bool wroteEnhance = CONVAR::WriteBool(smokeEnhance, false, &enhanceReadback);
    const bool wroteShadow = CONVAR::WriteBool(smokeShadow, false, &shadowReadback);
    const bool wroteParticles = CONVAR::WriteBool(drawParticles, false, &particlesReadback);

    auto* pawnBytes = reinterpret_cast<std::uint8_t*>(localPawn);
    if (overlayAlpha != 0)
        *reinterpret_cast<float*>(pawnBytes + overlayAlpha) = 0.f;
    if (smokeAge != 0)
        *reinterpret_cast<float*>(pawnBytes + smokeAge) = 100000.f;

    const std::uint64_t now = SDL_GetTicks();
    static std::uint64_t nextLog = 0;
    if (now >= nextLog)
    {
        nextLog = now + 2000;
        EspLog("[removals] smoke active=1 overlay=0x%x age=0x%x write/readback volume=%d/%d fullres=%d/%d enhance=%d/%d shadow=%d/%d particles=%d/%d",
            overlayAlpha, smokeAge, wroteVolume, !volumeReadback,
            wroteFullResolution, !fullResolutionReadback, wroteEnhance, !enhanceReadback,
            wroteShadow, !shadowReadback, wroteParticles, !particlesReadback);
    }
}

void ApplyCameraAndRemovals(C_CSPlayerPawn* localPawn)
{
    static CConVar* cameraFov = CONVAR::Find("fov_cs_debug");
    static CConVar* viewmodelFov = CONVAR::Find("viewmodel_fov");
    static CConVar* sniperStencil = CONVAR::Find("r_csgo_stencil_sniper_zoom");
    static CConVar* dofOverride = CONVAR::Find("r_dof_override");
    static CConVar* dofMaxBlur = CONVAR::Find("r_dof2_maxblursize");
    static CConVar* dofFarBlur = CONVAR::Find("r_dof_override_far_blurry");
    static CConVar* dofNearBlur = CONVAR::Find("r_dof_override_near_blurry");

    static bool previousCameraFov = false;
    static bool previousViewmodelFov = false;
    static bool previousScope = false;
    static bool previousBlur = false;
    static float originalCameraFov = 0.f;
    static float originalViewmodelFov = 60.f;
    static bool originalScope = true;
    static bool originalDofOverride = false;
    static float originalDofMaxBlur = 0.f;
    static float originalDofFarBlur = 0.f;
    static float originalDofNearBlur = 0.f;
    static bool capturedCameraFov = false;
    static bool capturedViewmodelFov = false;
    static bool capturedScope = false;
    static bool capturedDofOverride = false;
    static bool capturedDofMaxBlur = false;
    static bool capturedDofFarBlur = false;
    static bool capturedDofNearBlur = false;

    bool cameraFovWrite = true;
    bool viewmodelFovWrite = true;
    bool scopeWrite = true;
    bool blurWrite = true;
    float cameraFovReadback = originalCameraFov;
    float viewmodelFovReadback = originalViewmodelFov;
    bool scopeReadback = originalScope;
    bool dofOverrideReadback = originalDofOverride;
    float dofMaxBlurReadback = originalDofMaxBlur;
    float dofFarBlurReadback = originalDofFarBlur;
    float dofNearBlurReadback = originalDofNearBlur;

    const bool cameraFovEnabled = C_GET(bool, Vars.bFOV);
    if (cameraFovEnabled && !previousCameraFov)
        capturedCameraFov = CONVAR::ReadFloat(cameraFov, originalCameraFov);
    if (!cameraFovEnabled && previousCameraFov && capturedCameraFov)
        CONVAR::WriteFloat(cameraFov, originalCameraFov);
    if (cameraFovEnabled)
        cameraFovWrite = CONVAR::WriteFloat(cameraFov,
            std::clamp(C_GET(float, Vars.fFOVAmount), 30.f, 150.f), &cameraFovReadback);
    previousCameraFov = cameraFovEnabled;

    const bool viewmodelFovEnabled = C_GET(bool, Vars.bSetViewModelFOV);
    if (viewmodelFovEnabled && !previousViewmodelFov)
        capturedViewmodelFov = CONVAR::ReadFloat(viewmodelFov, originalViewmodelFov);
    if (!viewmodelFovEnabled && previousViewmodelFov && capturedViewmodelFov)
        CONVAR::WriteFloat(viewmodelFov, originalViewmodelFov);
    if (viewmodelFovEnabled)
        viewmodelFovWrite = CONVAR::WriteFloat(viewmodelFov,
            std::clamp(C_GET(float, Vars.flSetViewModelFOV), 40.f, 150.f), &viewmodelFovReadback);
    previousViewmodelFov = viewmodelFovEnabled;

    const bool removeScope = C_GET(bool, Vars.bRemoveScopeOverlay);
    if (removeScope && !previousScope)
        capturedScope = CONVAR::ReadBool(sniperStencil, originalScope);
    if (!removeScope && previousScope && capturedScope)
        CONVAR::WriteBool(sniperStencil, originalScope);
    if (removeScope)
        scopeWrite = CONVAR::WriteBool(sniperStencil, false, &scopeReadback);
    previousScope = removeScope;

    const bool removeBlur = C_GET(bool, Vars.bRemoveMotionBlur);
    if (removeBlur && !previousBlur)
    {
        capturedDofOverride = CONVAR::ReadBool(dofOverride, originalDofOverride);
        capturedDofMaxBlur = CONVAR::ReadFloat(dofMaxBlur, originalDofMaxBlur);
        capturedDofFarBlur = CONVAR::ReadFloat(dofFarBlur, originalDofFarBlur);
        capturedDofNearBlur = CONVAR::ReadFloat(dofNearBlur, originalDofNearBlur);
    }
    if (!removeBlur && previousBlur)
    {
        if (capturedDofOverride) CONVAR::WriteBool(dofOverride, originalDofOverride);
        if (capturedDofMaxBlur) CONVAR::WriteFloat(dofMaxBlur, originalDofMaxBlur);
        if (capturedDofFarBlur) CONVAR::WriteFloat(dofFarBlur, originalDofFarBlur);
        if (capturedDofNearBlur) CONVAR::WriteFloat(dofNearBlur, originalDofNearBlur);
    }
    if (removeBlur)
    {
        blurWrite = CONVAR::WriteBool(dofOverride, true, &dofOverrideReadback);
        blurWrite &= CONVAR::WriteFloat(dofMaxBlur, 0.f, &dofMaxBlurReadback);
        blurWrite &= CONVAR::WriteFloat(dofFarBlur, 0.f, &dofFarBlurReadback);
        blurWrite &= CONVAR::WriteFloat(dofNearBlur, 0.f, &dofNearBlurReadback);
    }
    previousBlur = removeBlur;

    static C_CSPlayerPawn* flashPawn = nullptr;
    static float originalFlashAlpha = 255.f;
    static bool previousFlash = false;
    const bool removeFlash = C_GET(bool, Vars.bRemoveFlash);
    if (localPawn != flashPawn)
    {
        flashPawn = localPawn;
        previousFlash = false;
    }
    if (localPawn != nullptr)
    {
        if (removeFlash && !previousFlash)
            originalFlashAlpha = localPawn->GetFlashMaxAlpha();
        if (!removeFlash && previousFlash)
            localPawn->GetFlashMaxAlpha() = originalFlashAlpha;
        if (removeFlash)
            localPawn->GetFlashMaxAlpha() = std::clamp(C_GET(float, Vars.flFlashOpacity), 0.f, 255.f);

        if (C_GET(bool, Vars.bRemoveAimPunch))
        {
            static const std::uint32_t viewPunchOffset =
                SCHEMA::GetOffset("CPlayer_CameraServices->m_vecCsViewPunchAngle");
            CPlayer_CameraServices* cameraServices = localPawn->GetCameraServices();
            if (cameraServices != nullptr && viewPunchOffset != 0)
                *reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(cameraServices) + viewPunchOffset) = {};
        }
    }
    previousFlash = removeFlash;

    static std::uint64_t nextLog = 0;
    if (SDL_GetTicks() >= nextLog &&
        (cameraFovEnabled || viewmodelFovEnabled || removeScope || removeBlur || removeFlash ||
         C_GET(bool, Vars.bRemoveAimPunch)))
    {
        nextLog = SDL_GetTicks() + 2000;
        EspLog("[camera/removals] fov=%d write=%d readback=%.1f viewmodel=%d write=%d readback=%.1f scope=%d write=%d readback=%d blur=%d write=%d readback=%d/%.1f/%.1f/%.1f flash=%d alpha=%.0f punch=%d",
            cameraFovEnabled, cameraFovWrite, cameraFovReadback,
            viewmodelFovEnabled, viewmodelFovWrite, viewmodelFovReadback,
            removeScope, scopeWrite, scopeReadback, removeBlur, blurWrite,
            dofOverrideReadback, dofMaxBlurReadback, dofFarBlurReadback, dofNearBlurReadback,
            removeFlash, C_GET(float, Vars.flFlashOpacity), C_GET(bool, Vars.bRemoveAimPunch));
    }
}

void ApplyThirdPerson(C_CSPlayerPawn* localPawn)
{
    const bool featureEnabled = C_GET(bool, Vars.bThirdperson);
    static bool wasPressed = false;
    static bool wasEnabled = false;
    static bool capturedClThirdPerson = false;
    static bool capturedIdealDistance = false;
    static bool capturedCollision = false;
    static bool capturedSnap = false;
    static bool capturedShoulder = false;
    static bool capturedShoulderAimDistance = false;
    static bool capturedShoulderDistance = false;
    static bool capturedShoulderHeight = false;
    static bool capturedShoulderOffset = false;
    static bool originalClThirdPerson = false;
    static float originalIdealDistance = 0.f;
    static std::int32_t originalCollision = 0;
    static bool originalSnap = false;
    static bool originalShoulder = false;
    static float originalShoulderAimDistance = 0.f;
    static float originalShoulderDistance = 0.f;
    static float originalShoulderHeight = 0.f;
    static float originalShoulderOffset = 0.f;

    if (featureEnabled && !wasEnabled)
    {
        capturedClThirdPerson = CONVAR::ReadBool(CONVAR::cl_thirdperson, originalClThirdPerson);
        capturedIdealDistance = CONVAR::ReadFloat(CONVAR::cam_idealdist, originalIdealDistance);
        capturedCollision = CONVAR::ReadInt32(CONVAR::cam_collision, originalCollision);
        capturedSnap = CONVAR::ReadBool(CONVAR::cam_snapto, originalSnap);
        capturedShoulder = CONVAR::ReadBool(CONVAR::c_thirdpersonshoulder, originalShoulder);
        capturedShoulderAimDistance = CONVAR::ReadFloat(CONVAR::c_thirdpersonshoulderaimdist, originalShoulderAimDistance);
        capturedShoulderDistance = CONVAR::ReadFloat(CONVAR::c_thirdpersonshoulderdist, originalShoulderDistance);
        capturedShoulderHeight = CONVAR::ReadFloat(CONVAR::c_thirdpersonshoulderheight, originalShoulderHeight);
        capturedShoulderOffset = CONVAR::ReadFloat(CONVAR::c_thirdpersonshoulderoffset, originalShoulderOffset);
    }
    else if (!featureEnabled && wasEnabled)
    {
        if (capturedClThirdPerson) CONVAR::WriteBool(CONVAR::cl_thirdperson, originalClThirdPerson);
        if (capturedIdealDistance) CONVAR::WriteFloat(CONVAR::cam_idealdist, originalIdealDistance);
        if (capturedCollision) CONVAR::WriteInt32(CONVAR::cam_collision, originalCollision);
        if (capturedSnap) CONVAR::WriteBool(CONVAR::cam_snapto, originalSnap);
        if (capturedShoulder) CONVAR::WriteBool(CONVAR::c_thirdpersonshoulder, originalShoulder);
        if (capturedShoulderAimDistance) CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderaimdist, originalShoulderAimDistance);
        if (capturedShoulderDistance) CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderdist, originalShoulderDistance);
        if (capturedShoulderHeight) CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderheight, originalShoulderHeight);
        if (capturedShoulderOffset) CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderoffset, originalShoulderOffset);
    }
    if (!featureEnabled)
    {
        native_thirdperson_active = false;
        wasPressed = false;
    }
    else
    {
        const bool pressed = IsNativeButtonDown(C_GET(int, Vars.thirdperson_ui_key));
        if (!wasEnabled)
        {
            // Enabling the checkbox should visibly enable the feature. The
            // bind remains available to toggle it afterward.
            native_thirdperson_active = true;
            wasPressed = pressed;
        }
        else if (pressed && !wasPressed && !MENU::bMainWindowOpened)
            native_thirdperson_active = !native_thirdperson_active;
        wasPressed = pressed;
    }
    wasEnabled = featureEnabled;

    const bool enabled = featureEnabled && native_thirdperson_active;
    const float distance = std::clamp(C_GET(float, Vars.flThirdperson), 50.f, 300.f);
    SetNativeThirdPersonInput(enabled);

    bool clThirdPersonReadback = false;
    float distanceReadback = 0.f;
    std::int32_t collisionReadback = 0;
    bool snapReadback = false;
    bool shoulderReadback = false;
    bool cvarWrites = true;
    if (featureEnabled)
    {
        // cl_thirdperson is absent in some current native builds, so the
        // shoulder and input paths remain independently validated.
        if (CONVAR::cl_thirdperson != nullptr)
            cvarWrites &= CONVAR::WriteBool(CONVAR::cl_thirdperson, enabled, &clThirdPersonReadback);
        cvarWrites &= CONVAR::WriteFloat(CONVAR::cam_idealdist, distance, &distanceReadback);
        cvarWrites &= CONVAR::WriteInt32(CONVAR::cam_collision,
            C_GET(bool, Vars.thirdperson_collision) ? 1 : 0, &collisionReadback);
        cvarWrites &= CONVAR::WriteBool(CONVAR::cam_snapto, true, &snapReadback);
        cvarWrites &= CONVAR::WriteBool(CONVAR::c_thirdpersonshoulder, enabled, &shoulderReadback);
        if (enabled)
        {
            cvarWrites &= CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderaimdist, 0.f);
            cvarWrites &= CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderdist, 0.f);
            cvarWrites &= CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderheight, 0.f);
            cvarWrites &= CONVAR::WriteFloat(CONVAR::c_thirdpersonshoulderoffset, 0.f);
        }
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
        EspLog("[thirdperson] requested=1 writes=%d input=%p schema=0x%x cl=%p/%d ideal=%.0f collision=%d snap=%d shoulder=%d create_move_calls=%llu",
               cvarWrites, I::Input, thirdPersonOffset, CONVAR::cl_thirdperson,
               clThirdPersonReadback, distanceReadback, collisionReadback,
               snapReadback, shoulderReadback, GetNativeCreateMoveCalls());
    }
}

void ApplyLegitAim(CGameEntitySystem* entities, CCSPlayerController* localController, C_CSPlayerPawn* localPawn)
{
    struct AimCandidate
    {
        std::uintptr_t pawn = 0;
        int bone = -2;
        QAngle_t delta{};
        float fov = 9999.f;
        float score = 9999999.f;
        bool valid = false;
    };

    static std::uintptr_t lockedPawn = 0;
    static int lockedBone = -2;
    static bool previousKey = false;
    static bool toggled = false;
    static bool previousToggleMode = false;
    static int previousKeyCode = 0;
    static std::uint64_t sequence = 0;
    static std::uint64_t lockStartedAt = 0;
    static std::uint64_t recoveryUntil = 0;
    static QAngle_t previousDelta{};
    static std::uintptr_t previousDeltaPawn = 0;
    static int previousDeltaBone = -2;
    const auto stopAim = [&](bool releaseTarget) {
        ClearNativeAimDelta();
        SetNativeCombatInput(false, false, false, false);
        if (releaseTarget)
        {
            lockedPawn = 0;
            lockedBone = -2;
            lockStartedAt = 0;
            recoveryUntil = 0;
            previousDelta = {};
            previousDeltaPawn = 0;
            previousDeltaBone = -2;
        }
    };

    if (!C_GET(bool, Vars.legit_ui_enable) || !C_GET(bool, Vars.legit_ui_aim))
    {
        stopAim(true);
        toggled = false;
        previousKey = true;
        previousToggleMode = C_GET(bool, Vars.legit_ui_toggle);
        previousKeyCode = C_GET(int, Vars.legit_ui_key);
        return;
    }

    static std::uint64_t nextDiagnostic = 0;
    const std::uint64_t now = SDL_GetTicks();
    const bool diagnose = now >= nextDiagnostic;
    if (diagnose)
        nextDiagnostic = now + 250;

    const bool toggleMode = C_GET(bool, Vars.legit_ui_toggle);
    const int keyCode = C_GET(int, Vars.legit_ui_key);
    const bool keyDown = IsNativeButtonDown(keyCode);
    const std::uint64_t currentSequence = ++sequence;
    if (diagnose)
        LegitLog("[%llu] state time=%llu enabled=1 aim=1 key=%d down=%d toggle_mode=%d toggle_active=%d menu=%d thirdperson_requested=%d pawn=%p health=%d create_move=%llu applied=%llu",
            static_cast<unsigned long long>(currentSequence), static_cast<unsigned long long>(now),
            keyCode, keyDown, toggleMode, toggled, MENU::bMainWindowOpened,
            native_thirdperson_active, localPawn, localPawn != nullptr ? localPawn->GetHealth() : 0,
            GetNativeCreateMoveCalls(), GetNativeAimAngleApplications());

    // Third person is a camera presentation choice and must never suppress
    // aim command generation. The old check made legitbot permanently inert
    // whenever the (previously broken) camera toggle was enabled.
    if (MENU::bMainWindowOpened || localPawn == nullptr || localPawn->GetHealth() <= 0)
    {
        stopAim(true);
        // Never resume a toggled lock merely because the menu closed or the
        // player respawned. Require a fresh release/press edge.
        toggled = false;
        previousKey = true;
        previousToggleMode = toggleMode;
        previousKeyCode = keyCode;
        if (diagnose)
            LegitLog("[%llu] BLOCKED menu=%d pawn=%p health=%d",
                static_cast<unsigned long long>(currentSequence), MENU::bMainWindowOpened, localPawn,
                localPawn != nullptr ? localPawn->GetHealth() : 0);
        return;
    }

    if (toggleMode != previousToggleMode || keyCode != previousKeyCode)
    {
        toggled = false;
        previousKey = false;
    }
    if (toggleMode && keyDown && !previousKey)
        toggled = !toggled;
    previousKey = keyDown;
    previousToggleMode = toggleMode;
    previousKeyCode = keyCode;
    if (!(toggleMode ? toggled : keyDown))
    {
        stopAim(true);
        if (diagnose)
            LegitLog("[%llu] WAITING key=%d down=%d toggle_mode=%d toggle_active=%d",
                static_cast<unsigned long long>(currentSequence),
                C_GET(int, Vars.legit_ui_key), keyDown, toggleMode, toggled);
        return;
    }

    CGameSceneNode* localScene = localPawn != nullptr ? localPawn->GetGameSceneNode() : nullptr;
    if (localScene == nullptr)
    {
        stopAim(true);
        LegitLog("[%llu] ABORT local scene node is null", static_cast<unsigned long long>(currentSequence));
        return;
    }
    const Vector_t localEye = localScene->GetAbsOrigin() + localPawn->m_vecViewOffset();

    std::string activeWeaponName;
    int activeAmmo = 0;
    int activeMaximumAmmo = 0;
    GetWeaponDisplay(entities, localPawn, activeWeaponName, activeAmmo, activeMaximumAmmo);
    const int profile = C_GET(bool, Vars.legit_ui_per_weapon)
        ? GetRageWeaponGroup(activeWeaponName) : 0;
    const float configuredFov = C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(float, 7, Vars.legit_profile_fov, profile)
        : C_GET(float, Vars.legit_ui_fov_size);
    const float configuredSmoothness = C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(float, 7, Vars.legit_profile_smoothness, profile)
        : C_GET(float, Vars.legit_ui_smoothness);
    const int targetSelection = std::clamp(C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(int, 7, Vars.legit_profile_target_selection, profile)
        : C_GET(int, Vars.legit_ui_target_selection), 0, 2);
    const int hitboxMode = std::clamp(C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(int, 7, Vars.legit_profile_hitbox_mode, profile)
        : C_GET(int, Vars.legit_ui_hitbox_mode), 0, 1);
    const bool visibilityCheck = C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(bool, 7, Vars.legit_profile_visibility_check, profile)
        : C_GET(bool, Vars.legit_ui_visibility_check);
    const bool smokeCheck = C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(bool, 7, Vars.legit_profile_smoke_check, profile)
        : C_GET(bool, Vars.legit_ui_smoke_check);
    const bool flashCheck = C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(bool, 7, Vars.legit_profile_flash_check, profile)
        : C_GET(bool, Vars.legit_ui_flash_check);
    const float reactionMs = std::clamp(C_GET(bool, Vars.legit_ui_per_weapon)
        ? C_GET_ARRAY(float, 7, Vars.legit_profile_reaction_ms, profile)
        : C_GET(float, Vars.legit_ui_reaction_ms), 0.f, 500.f);

    if (flashCheck && localPawn->GetFlashDuration() > 0.05f)
    {
        stopAim(true);
        if (diagnose) LegitLog("[%llu] BLOCKED flash duration=%.3f",
            static_cast<unsigned long long>(currentSequence), localPawn->GetFlashDuration());
        return;
    }
    if (smokeCheck)
    {
        static const std::uint32_t smokeOverlayOffset =
            SCHEMA::GetOffset("C_CSPlayerPawnBase->m_flLastSmokeOverlayAlpha");
        float smokeOverlay = 0.f;
        if (smokeOverlayOffset != 0 && SafeNativeRead(
                reinterpret_cast<const std::uint8_t*>(localPawn) + smokeOverlayOffset, smokeOverlay) &&
            std::isfinite(smokeOverlay) && smokeOverlay > 0.05f)
        {
            stopAim(true);
            if (diagnose) LegitLog("[%llu] BLOCKED smoke overlay=%.3f",
                static_cast<unsigned long long>(currentSequence), smokeOverlay);
            return;
        }
    }
    if (visibilityCheck && !TRACE::NativeReady())
    {
        stopAim(true);
        if (diagnose) LegitLog("[%llu] BLOCKED visibility requested but native trace is unavailable",
            static_cast<unsigned long long>(currentSequence));
        return;
    }
    static const std::uint32_t pawnViewAngleOffset = SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    static const std::uint32_t pawnEyeAngleOffset = SCHEMA::GetOffset("C_CSPlayerPawn->m_angEyeAngles");
    if (pawnViewAngleOffset == 0 && pawnEyeAngleOffset == 0 && native_view_angles == nullptr)
    {
        stopAim(true);
        LegitLog("[%llu] ABORT no angle destination view_offset=0x%x eye_offset=0x%x global=%p",
            static_cast<unsigned long long>(currentSequence), pawnViewAngleOffset,
            pawnEyeAngleOffset, native_view_angles);
        return;
    }

    // Pawn view angles are the best read source; the corrected CCSGOInput
    // mirror is written during CreateMove so the command builder consumes the
    // same result rather than immediately overwriting this pawn field.
    QAngle_t current{};
    const char* angleSource = "none";
    const auto usableAngle = [](const QAngle_t& angle) {
        return angle.IsValid() && std::fabs(angle.x) <= 180.f &&
            std::fabs(angle.y) <= 360.f && std::fabs(angle.z) <= 180.f;
    };
    if (pawnViewAngleOffset != 0)
    {
        current = *reinterpret_cast<QAngle_t*>(
            reinterpret_cast<std::uint8_t*>(localPawn) + pawnViewAngleOffset);
        angleSource = "pawn.v_angle";
    }
    if (!usableAngle(current) && pawnEyeAngleOffset != 0)
    {
        current = *reinterpret_cast<QAngle_t*>(
            reinterpret_cast<std::uint8_t*>(localPawn) + pawnEyeAngleOffset);
        angleSource = "pawn.eye";
    }
    if (!usableAngle(current) && native_view_angles != nullptr)
    {
        current = *native_view_angles;
        angleSource = "global";
    }
    if (!usableAngle(current))
    {
        stopAim(true);
        LegitLog("[%llu] ABORT invalid current angles %.3f %.3f %.3f source=%s",
            static_cast<unsigned long long>(currentSequence), current.x, current.y, current.z, angleSource);
        return;
    }

    if (diagnose)
        LegitLog("[%llu] angles source=%s current=(%.3f,%.3f,%.3f) offsets view=0x%x eye=0x%x eye_pos=(%.2f,%.2f,%.2f)",
            static_cast<unsigned long long>(currentSequence), angleSource,
            current.x, current.y, current.z, pawnViewAngleOffset, pawnEyeAngleOffset,
            localEye.x, localEye.y, localEye.z);

    const float acquisitionFov = std::clamp(configuredFov, 0.1f, 60.f);
    // Once acquired, allow a very small margin outside the selection cone.
    // This prevents two nearby players/bones from alternating every frame,
    // while still releasing promptly if the user deliberately moves away.
    const float lockBreakFov = std::min(60.f, acquisitionFov + 1.5f);
    AimCandidate bestCandidate{};
    AimCandidate lockedCandidate{};
    int controllersScanned = 0;
    int enemiesScanned = 0;
    int bonesTested = 0;
    int validCandidates = 0;
    constexpr float degrees = 180.f / 3.14159265358979323846f;

    QAngle_t recoilCorrection{};
    if (C_GET(bool, Vars.legit_ui_recoil))
    {
        static const std::uint32_t cameraPunchOffset =
            SCHEMA::GetOffset("CPlayer_CameraServices->m_vecCsViewPunchAngle");
        CPlayer_CameraServices* camera = localPawn->GetCameraServices();
        if (camera != nullptr && cameraPunchOffset != 0)
        {
            recoilCorrection = *reinterpret_cast<QAngle_t*>(
                reinterpret_cast<std::uint8_t*>(camera) + cameraPunchOffset);
            recoilCorrection.x *= 2.f;
            recoilCorrection.y *= 2.f;
        }
    }

    const auto considerTarget = [&](C_CSPlayerPawn* pawn, int bone, int bonePriority, Vector_t target) {
        ++bonesTested;
        if (C_GET(bool, Vars.legit_ui_prediction))
            target += pawn->GetAbsVelocity() *
                (std::clamp(C_GET(float, Vars.legit_ui_prediction_ms), 0.f, 250.f) / 1000.f);

        if (visibilityCheck)
        {
            TRACE::NativeResult trace{};
            if (!TRACE::NativeLine(localEye, target, localPawn, trace) ||
                (trace.hit && trace.entity != pawn))
            {
                if (diagnose)
                    LegitLog("[%llu] candidate rejected visibility pawn=%p bone=%d hit=%d entity=%p fraction=%.3f",
                        static_cast<unsigned long long>(currentSequence), pawn, bone,
                        trace.hit, trace.entity, trace.fraction);
                return;
            }
        }

        const Vector_t difference = target - localEye;
        const float horizontal = std::hypot(difference.x, difference.y);
        if (horizontal < 0.001f)
            return;

        QAngle_t wanted(-std::atan2(difference.z, horizontal) * degrees,
                         std::atan2(difference.y, difference.x) * degrees, 0.f);
        wanted.x -= recoilCorrection.x;
        wanted.y -= recoilCorrection.y;
        const QAngle_t delta(std::remainder(wanted.x - current.x, 360.f),
                             std::remainder(wanted.y - current.y, 360.f), 0.f);
        const float fov = std::hypot(delta.x, delta.y);
        if (!std::isfinite(fov))
            return;
        const float distance = difference.Length();
        float targetScore = fov;
        if (targetSelection == 1)
            targetScore = distance;
        else if (targetSelection == 2)
            targetScore = static_cast<float>(pawn->GetHealth());
        // Priority mode makes the enabled hitbox order decisive; nearest mode
        // lets angular proximity choose the point on the selected target.
        const float score = hitboxMode == 0
            ? static_cast<float>(bonePriority) * 100000.f + targetScore
            : targetScore + fov * 0.001f;
        ++validCandidates;

        if (diagnose)
            LegitLog("[%llu] candidate pawn=%p bone=%d target=(%.1f,%.1f,%.1f) wanted=(%.3f,%.3f) delta=(%.3f,%.3f) fov=%.3f acquire=%d locked=%d",
                static_cast<unsigned long long>(currentSequence), pawn, bone,
                target.x, target.y, target.z, wanted.x, wanted.y, delta.x, delta.y,
                fov, fov <= acquisitionFov,
                reinterpret_cast<std::uintptr_t>(pawn) == lockedPawn && bone == lockedBone);

        const std::uintptr_t pawnAddress = reinterpret_cast<std::uintptr_t>(pawn);
        if (pawnAddress == lockedPawn && bone == lockedBone && fov <= lockBreakFov)
        {
            lockedCandidate = {pawnAddress, bone, delta, fov, score, true};
        }
        if (fov <= acquisitionFov && score < bestCandidate.score)
        {
            bestCandidate = {pawnAddress, bone, delta, fov, score, true};
        }
    };

    for (int index = 1; index <= 128; ++index)
    {
        CCSPlayerController* controller = entities->Get<CCSPlayerController>(index);
        if (controller == nullptr || controller == localController)
            continue;
        ++controllersScanned;
        C_CSPlayerPawn* pawn = entities->Get<C_CSPlayerPawn>(controller->GetPawnHandle());
        if (pawn == nullptr || pawn == localPawn || pawn->GetHealth() <= 0 || pawn->GetTeam() == localPawn->GetTeam())
            continue;
        ++enemiesScanned;
        CGameSceneNode* scene = pawn->GetGameSceneNode();
        if (scene == nullptr || scene->IsDormant())
            continue;

        const int candidates[] = {
            C_GET(bool, Vars.legit_ui_bone_head) ? 5 : -1,
            C_GET(bool, Vars.legit_ui_bone_torso) ? 3 : -1,
            C_GET(bool, Vars.legit_ui_bone_arms) ? 9 : -1,
            C_GET(bool, Vars.legit_ui_bone_arms) ? 14 : -1,
            C_GET(bool, Vars.legit_ui_bone_legs) ? 23 : -1,
            C_GET(bool, Vars.legit_ui_bone_legs) ? 26 : -1,
        };
        bool foundBone = false;
        for (int candidateIndex = 0; candidateIndex < static_cast<int>(std::size(candidates)); ++candidateIndex)
        {
            const int bone = candidates[candidateIndex];
            Vector_t target{};
            if (bone >= 0 && GetLiveBonePosition(scene, bone, target))
            {
                foundBone = true;
                considerTarget(pawn, bone, candidateIndex, target);
            }
        }
        if (!foundBone)
            considerTarget(pawn, -1, 99, scene->GetAbsOrigin() + pawn->m_vecViewOffset());
    }

    AimCandidate chosen = lockedCandidate.valid ? lockedCandidate : bestCandidate;
    if (!chosen.valid)
    {
        stopAim(true);
        if (diagnose)
            LegitLog("[%llu] NO_TARGET controllers=%d enemies=%d bones=%d valid=%d fov_limit=%.1f current=(%.2f,%.2f) source=%s",
                static_cast<unsigned long long>(currentSequence), controllersScanned,
                enemiesScanned, bonesTested, validCandidates,
                C_GET(float, Vars.legit_ui_fov_size), current.x, current.y, angleSource);
        return;
    }
    constexpr float angularDeadZone = 0.04f;
    const bool newLock = chosen.pawn != lockedPawn || chosen.bone != lockedBone;
    if (newLock)
    {
        lockStartedAt = now;
        recoveryUntil = 0;
    }
    else if (previousDeltaPawn == chosen.pawn && previousDeltaBone == chosen.bone)
    {
        // Detect a real sign change after crossing a bone. Recovery only
        // corrects naturally occurring overshoot; it never creates one.
        const bool crossedPitch = previousDelta.x * chosen.delta.x < 0.f &&
            std::fabs(previousDelta.x) > angularDeadZone * 2.f;
        const bool crossedYaw = previousDelta.y * chosen.delta.y < 0.f &&
            std::fabs(previousDelta.y) > angularDeadZone * 2.f;
        if (crossedPitch || crossedYaw)
        {
            const float recoveryMs = std::clamp(
                C_GET(float, Vars.legit_ui_recovery_ms), 5.f, 250.f);
            recoveryUntil = now + static_cast<std::uint64_t>(recoveryMs * 2.f);
        }
    }
    lockedPawn = chosen.pawn;
    lockedBone = chosen.bone;
    previousDelta = chosen.delta;
    previousDeltaPawn = chosen.pawn;
    previousDeltaBone = chosen.bone;

    if (reactionMs > 0.f && now - lockStartedAt < static_cast<std::uint64_t>(reactionMs))
    {
        ClearNativeAimDelta();
        if (diagnose) LegitLog("[%llu] REACTION_WAIT target=%p elapsed=%llu required=%.0f",
            static_cast<unsigned long long>(currentSequence),
            reinterpret_cast<void*>(chosen.pawn),
            static_cast<unsigned long long>(now - lockStartedAt), reactionMs);
        return;
    }

    AddNativeCombatInput(C_GET(bool, Vars.legit_ui_auto_shoot) && chosen.fov < 2.f,
        false, false, false);

    // A sub-count dead zone prevents the bot from constantly correcting bone
    // animation/noise after it is already centered.
    if (chosen.fov <= angularDeadZone)
    {
        ClearNativeAimDelta();
        LegitLog("[%llu] CENTERED target=%p bone=%d fov=%.4f deadzone=%.4f no movement needed",
            static_cast<unsigned long long>(currentSequence),
            reinterpret_cast<void*>(chosen.pawn), chosen.bone, chosen.fov, angularDeadZone);
        return;
    }

    // Exponential response is stable across frame rates. Clamp hitch time and
    // per-sample angular motion so a stalled frame can never produce a snap.
    const float smoothnessSeconds = std::max(0.005f,
        configuredSmoothness / 1000.f);
    const float frameSeconds = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 1.f / 30.f);
    const float baseResponse = 1.f - std::exp(-frameSeconds / smoothnessSeconds);

    float accelerationFactor = 1.f;
    const float accelerationMs = std::clamp(
        C_GET(float, Vars.legit_ui_acceleration_ms), 0.f, 500.f);
    if (accelerationMs > 0.f && lockStartedAt != 0)
    {
        const float elapsed = static_cast<float>(now - lockStartedAt);
        const float linear = std::clamp(elapsed / accelerationMs, 0.f, 1.f);
        const float eased = linear * linear * (3.f - 2.f * linear);
        accelerationFactor = 0.20f + 0.80f * eased;
    }

    float decelerationFactor = 1.f;
    const float decelerationZone = std::clamp(
        C_GET(float, Vars.legit_ui_deceleration_degrees), 0.f, 10.f);
    if (decelerationZone > angularDeadZone && chosen.fov < decelerationZone)
    {
        const float normalized = std::clamp(
            (chosen.fov - angularDeadZone) / (decelerationZone - angularDeadZone), 0.f, 1.f);
        decelerationFactor = 0.15f + 0.85f * normalized;
    }

    float response = std::clamp(
        baseResponse * accelerationFactor * decelerationFactor, 0.0005f, 1.f);
    const bool recovering = now < recoveryUntil;
    if (recovering)
    {
        const float recoverySeconds = std::max(0.005f,
            C_GET(float, Vars.legit_ui_recovery_ms) / 1000.f);
        response = std::max(response,
            1.f - std::exp(-frameSeconds / recoverySeconds));
    }
    constexpr float maxDegreesPerSample = 3.f;
    const float stepPitch = std::clamp(chosen.delta.x * response,
        -maxDegreesPerSample, maxDegreesPerSample);
    const float stepYaw = std::clamp(chosen.delta.y * response,
        -maxDegreesPerSample, maxDegreesPerSample);
    QAngle_t result(current.x + stepPitch, current.y + stepYaw, 0.f);
    result.Clamp();

    // Apply to the schema-resolved pawn view angles immediately and queue the
    // same destination for CreateMove. This is the native internal path used
    // to build game commands; it never moves or captures the desktop cursor.
    QAngle_t* angleDestination = pawnViewAngleOffset != 0
        ? reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(localPawn) + pawnViewAngleOffset)
        : native_view_angles;
    const bool inputHookInstalled = IsNativeInputHookInstalled();
    bool presentWrite = false;
    bool readbackVerified = false;
    QAngle_t readback{};
    // No render-thread fallback writes: without the verified command hook the
    // feature fails closed instead of mutating a pawn field at an arbitrary
    // point in the frame.
    // Discard any legacy SDL correction before publishing this frame's single
    // authoritative CreateMove angle update.
    ClearNativeAimDelta();
    const bool createMoveQueued = QueueNativeAimAngles(
        angleDestination, result.x, result.y, result.z);
    native_aim_command_queued = createMoveQueued;

    LegitLog("[%llu] MOVE_ATTEMPT target=%p bone=%d scan controllers=%d enemies=%d bones=%d valid=%d fov=%.3f delta=(%.3f,%.3f) smooth_ms=%.1f frame_s=%.4f response=%.4f accel=%.3f decel=%.3f recovery=%d step=(%.3f,%.3f) result=(%.3f,%.3f) destination=%p hook_installed=%d present_fallback=%d readback=(%.3f,%.3f) verified=%d command_queued=%d create_move_calls=%llu applied=%llu source=%s",
        static_cast<unsigned long long>(currentSequence),
        reinterpret_cast<void*>(chosen.pawn), chosen.bone,
        controllersScanned, enemiesScanned, bonesTested, validCandidates,
        chosen.fov, chosen.delta.x, chosen.delta.y,
        configuredSmoothness, frameSeconds, response,
        accelerationFactor, decelerationFactor, recovering,
        stepPitch, stepYaw, result.x, result.y, angleDestination,
        inputHookInstalled, presentWrite, readback.x, readback.y, readbackVerified,
        createMoveQueued,
        GetNativeCreateMoveCalls(), GetNativeAimAngleApplications(), angleSource);
}

void ApplyNativeTriggerAndRecoil(CGameEntitySystem* entities,
    CCSPlayerController* localController, C_CSPlayerPawn* localPawn)
{
    static C_CSPlayerPawn* triggerTarget = nullptr;
    static int triggerBone = -1;
    static std::uint64_t triggerAcquiredAt = 0;
    static QAngle_t previousPunch{};

    if (entities == nullptr || localController == nullptr || localPawn == nullptr ||
        localPawn->GetHealth() <= 0 || MENU::bMainWindowOpened)
    {
        triggerTarget = nullptr;
        triggerBone = -1;
        triggerAcquiredAt = 0;
        previousPunch = {};
        return;
    }

    static const std::uint32_t viewAngleOffset =
        SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    QAngle_t current{};
    if (viewAngleOffset != 0)
        SafeNativeRead(reinterpret_cast<const std::uint8_t*>(localPawn) + viewAngleOffset, current);
    if (!current.IsValid() && native_view_angles != nullptr)
        SafeNativeRead(native_view_angles, current);
    if (!current.IsValid())
        return;

    const std::uint64_t now = SDL_GetTicks();
    if (C_GET(bool, Vars.trigger_ui_enable) &&
        IsNativeButtonDown(C_GET(int, Vars.trigger_ui_key)))
    {
        const bool traceBlocked = C_GET(bool, Vars.trigger_ui_visibility_check) &&
            !TRACE::NativeReady();
        bool smokeBlocked = false;
        if (C_GET(bool, Vars.trigger_ui_smoke_check))
        {
            static const std::uint32_t smokeOverlayOffset =
                SCHEMA::GetOffset("C_CSPlayerPawnBase->m_flLastSmokeOverlayAlpha");
            float overlay = 0.f;
            smokeBlocked = smokeOverlayOffset != 0 && SafeNativeRead(
                reinterpret_cast<const std::uint8_t*>(localPawn) + smokeOverlayOffset, overlay) &&
                std::isfinite(overlay) && overlay > 0.05f;
        }

        std::string weaponName;
        int ammo = 0, maximumAmmo = 0;
        GetWeaponDisplay(entities, localPawn, weaponName, ammo, maximumAmmo);
        const int weaponGroup = GetRageWeaponGroup(weaponName);
        bool scopeBlocked = false;
        if (C_GET(bool, Vars.trigger_ui_scoped_only) && weaponGroup >= 4)
        {
            static const std::uint32_t scopedOffset =
                SCHEMA::GetOffset("C_CSPlayerPawnBase->m_bIsScoped");
            std::uint8_t scoped = 0;
            scopeBlocked = scopedOffset == 0 || !SafeNativeRead(
                reinterpret_cast<const std::uint8_t*>(localPawn) + scopedOffset, scoped) || scoped != 1;
        }

        CGameSceneNode* localScene = localPawn->GetGameSceneNode();
        C_CSPlayerPawn* bestPawn = nullptr;
        int bestBone = -1;
        float bestFov = 9999.f;
        if (!traceBlocked && !smokeBlocked && !scopeBlocked && localScene != nullptr)
        {
            const Vector_t eye = localScene->GetAbsOrigin() + localPawn->m_vecViewOffset();
            const unsigned int mask = C_GET(unsigned int, Vars.trigger_ui_hitboxes);
            const struct { int bone; unsigned int bit; } bones[] = {
                {5, 1U << 0}, {3, 1U << 1}, {6, 1U << 1},
                {9, 1U << 2}, {14, 1U << 2}, {23, 1U << 3}, {26, 1U << 3}
            };
            constexpr float degrees = 180.f / 3.14159265358979323846f;
            for (int index = 1; index <= 128; ++index)
            {
                CCSPlayerController* controller = entities->Get<CCSPlayerController>(index);
                if (controller == nullptr || controller == localController)
                    continue;
                C_CSPlayerPawn* pawn = entities->Get<C_CSPlayerPawn>(controller->GetPawnHandle());
                if (pawn == nullptr || pawn->GetHealth() <= 0 || pawn->GetTeam() == localPawn->GetTeam())
                    continue;
                CGameSceneNode* scene = pawn->GetGameSceneNode();
                if (scene == nullptr || scene->IsDormant())
                    continue;
                for (const auto& candidate : bones)
                {
                    if ((mask & candidate.bit) == 0)
                        continue;
                    Vector_t point{};
                    if (!GetLiveBonePosition(scene, candidate.bone, point))
                        continue;
                    if (C_GET(bool, Vars.trigger_ui_visibility_check))
                    {
                        TRACE::NativeResult trace{};
                        if (!TRACE::NativeLine(eye, point, localPawn, trace) ||
                            (trace.hit && trace.entity != pawn))
                            continue;
                    }
                    const Vector_t deltaPosition = point - eye;
                    const float horizontal = std::hypot(deltaPosition.x, deltaPosition.y);
                    if (horizontal < 0.001f)
                        continue;
                    const float pitch = -std::atan2(deltaPosition.z, horizontal) * degrees;
                    const float yaw = std::atan2(deltaPosition.y, deltaPosition.x) * degrees;
                    const float fov = std::hypot(std::remainder(pitch - current.x, 360.f),
                        std::remainder(yaw - current.y, 360.f));
                    const float angularRadius = candidate.bone == 5 ? 0.38f : 0.55f;
                    if (std::isfinite(fov) && fov <= angularRadius && fov < bestFov)
                    {
                        bestPawn = pawn;
                        bestBone = candidate.bone;
                        bestFov = fov;
                    }
                }
            }
        }

        if (bestPawn != triggerTarget || bestBone != triggerBone)
        {
            triggerTarget = bestPawn;
            triggerBone = bestBone;
            triggerAcquiredAt = now;
        }
        const float delay = std::clamp(C_GET(float, Vars.trigger_ui_delay_ms), 0.f, 500.f);
        const bool fire = bestPawn != nullptr && now - triggerAcquiredAt >=
            static_cast<std::uint64_t>(delay);
        if (fire)
            AddNativeCombatInput(true, false, false, false);

        if (C_GET(bool, Vars.trigger_ui_diagnostics))
        {
            static std::uint64_t nextLog = 0;
            if (now >= nextLog)
            {
                nextLog = now + 250;
                LegitLog("[trigger] target=%p bone=%d fov=%.3f elapsed=%llu delay=%.0f fire=%d blocked=trace:%d smoke:%d scope:%d hook=%d",
                    bestPawn, bestBone, bestFov,
                    static_cast<unsigned long long>(bestPawn ? now - triggerAcquiredAt : 0),
                    delay, fire, traceBlocked, smokeBlocked, scopeBlocked,
                    IsNativeInputHookInstalled());
            }
        }
    }
    else
    {
        triggerTarget = nullptr;
        triggerBone = -1;
        triggerAcquiredAt = 0;
    }

    if (!C_GET(bool, Vars.recoil_ui_enable) || localPawn->GetShotsFired() <= 1 ||
        native_aim_command_queued || !IsNativeInputHookInstalled())
    {
        previousPunch = {};
        return;
    }

    static const std::uint32_t punchOffset =
        SCHEMA::GetOffset("CPlayer_CameraServices->m_vecCsViewPunchAngle");
    CPlayer_CameraServices* camera = localPawn->GetCameraServices();
    QAngle_t punch{};
    if (camera == nullptr || punchOffset == 0 || !SafeNativeRead(
            reinterpret_cast<const std::uint8_t*>(camera) + punchOffset, punch) ||
        !punch.IsValid())
        return;

    const QAngle_t punchDelta(punch.x - previousPunch.x, punch.y - previousPunch.y, 0.f);
    previousPunch = punch;
    const float smoothingSeconds = std::max(0.001f,
        C_GET(float, Vars.recoil_ui_smoothing_ms) / 1000.f);
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 1.f / 30.f);
    const float response = std::clamp(1.f - std::exp(-dt / smoothingSeconds), 0.f, 1.f);
    QAngle_t result(current.x - punchDelta.x * 2.f * response,
        current.y - punchDelta.y * 2.f * response, 0.f);
    result.Clamp();
    native_aim_command_queued = QueueNativeAimAngles(
        localPawn, result.x, result.y, result.z);
}

void ApplyNativeAntiAim(C_CSPlayerPawn* localPawn)
{
    if (!C_GET(bool, Vars.bAntiAim) || native_aim_command_queued ||
        MENU::bMainWindowOpened || localPawn == nullptr || localPawn->GetHealth() <= 0 ||
        IsNativeCombatAttackRequested() || IsNativeButtonDown(VK_LBUTTON))
        return;

    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (C_GET(bool, Vars.antiaim_disable_use) && keys != nullptr && keys[SDL_SCANCODE_E])
        return;

    const int pitchType = std::clamp(C_GET(int, Vars.iPitchType), 0, 4);
    const int yawType = std::clamp(C_GET(int, Vars.iBaseYawType), 0, 4);
    if (pitchType == 0 && yawType == 0)
        return;

    static const std::uint32_t pawnViewAngleOffset =
        SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    QAngle_t* destination = pawnViewAngleOffset != 0
        ? reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(localPawn) + pawnViewAngleOffset)
        : native_view_angles;
    QAngle_t current{};
    if (destination == nullptr || !SafeNativeRead(destination, current) || !current.IsValid())
        return;

    QAngle_t result = current;
    switch (pitchType)
    {
    case 1: result.x = 89.f; break;
    case 2: result.x = -89.f; break;
    case 3: result.x = 0.f; break;
    case 4: result.x = std::clamp(C_GET(float, Vars.antiaim_custom_pitch), -89.f, 89.f); break;
    default: break;
    }
    if (yawType == 1)
        result.y = std::remainder(result.y + 180.f, 360.f);
    else if (yawType == 3)
        result.y = std::remainder(result.y + 90.f, 360.f);
    else if (yawType == 4)
        result.y = std::remainder(result.y +
            std::clamp(C_GET(float, Vars.antiaim_custom_yaw), -180.f, 180.f), 360.f);
    // Forward yaw intentionally preserves the current yaw.

    const Vector_t velocity = localPawn->GetAbsVelocity();
    const float speed = std::hypot(velocity.x, velocity.y);
    const bool onGround = (localPawn->GetFlags() & FL_ONGROUND) != 0;
    const bool crouching = (localPawn->GetFlags() & FL_DUCKING) != 0;
    int profile = 0; // standing
    if (!onGround)
        profile = 2;
    else if (crouching)
        profile = 3;
    else if (speed > 5.f && speed < 110.f)
        profile = 4;
    else if (speed >= 5.f)
        profile = 1;
    if (C_GET_ARRAY(bool, 5, Vars.antiaim_profile_enable, profile))
        result.y = std::remainder(result.y +
            C_GET_ARRAY(float, 5, Vars.antiaim_profile_yaw, profile), 360.f);

    const bool manualBack = IsNativeButtonDown(C_GET(int, Vars.antiaim_manual_back_key));
    const bool manualForward = IsNativeButtonDown(C_GET(int, Vars.antiaim_manual_forward_key));
    if (manualBack)
        result.y = std::remainder(current.y + 180.f, 360.f);
    else if (manualForward)
        result.y = current.y;

    float jitterAmount = std::clamp(C_GET(float, Vars.antiaim_jitter_amount), 0.f, 180.f);
    if (C_GET_ARRAY(bool, 5, Vars.antiaim_profile_enable, profile))
        jitterAmount = std::clamp(C_GET_ARRAY(float, 5, Vars.antiaim_profile_jitter, profile), 0.f, 180.f);
    const int jitterMode = std::clamp(C_GET(int, Vars.antiaim_jitter_mode), 0, 2);
    if (!manualBack && !manualForward && jitterMode == 1)
        result.y = std::remainder(result.y + ((frame_counter & 1U) ? jitterAmount : -jitterAmount), 360.f);
    else if (!manualBack && !manualForward && jitterMode == 2)
    {
        const std::uint32_t mixed = static_cast<std::uint32_t>(
            frame_counter * 1664525ULL + 1013904223ULL);
        const float unit = static_cast<float>(mixed & 0xFFFFU) / 32767.5f - 1.f;
        result.y = std::remainder(result.y + unit * jitterAmount, 360.f);
    }
    if (!manualBack && !manualForward && C_GET(bool, Vars.antiaim_spin))
    {
        const float seconds = static_cast<float>(SDL_GetTicks()) / 1000.f;
        result.y = std::remainder(result.y + seconds *
            std::clamp(C_GET(float, Vars.antiaim_spin_speed), 1.f, 720.f), 360.f);
    }
    result.z = 0.f;
    result.Clamp();

    const bool queued = QueueNativeAimAngles(
        destination, result.x, result.y, result.z);
    native_aim_command_queued = queued;

    static std::uint64_t nextLog = 0;
    const std::uint64_t now = SDL_GetTicks();
    if (now >= nextLog)
    {
        nextLog = now + 2000;
        LegitLog("[antiaim] pitch_type=%d yaw_type=%d profile=%d speed=%.1f jitter=%d/%.1f manual=%d/%d spin=%d result=(%.1f,%.1f) destination=%p hook=%d queued=%d",
            pitchType, yawType, profile, speed, jitterMode, jitterAmount,
            manualBack, manualForward, C_GET(bool, Vars.antiaim_spin), result.x, result.y, destination,
            IsNativeInputHookInstalled(), queued);
    }
}

void ApplyNativeRage(CGameEntitySystem* entities, CCSPlayerController* localController,
                     C_CSPlayerPawn* localPawn)
{
    if (!C_GET(bool, Vars.rage_enable) || MENU::bMainWindowOpened ||
        entities == nullptr || localController == nullptr || localPawn == nullptr ||
        localPawn->GetHealth() <= 0)
    {
        ClearNativeAimDelta();
        return;
    }

    CGameSceneNode* localScene = localPawn->GetGameSceneNode();
    if (localScene == nullptr)
        return;
    const Vector_t localEye = localScene->GetAbsOrigin() + localPawn->m_vecViewOffset();
    static const std::uint32_t pawnViewAngleOffset =
        SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    QAngle_t* destination = pawnViewAngleOffset != 0
        ? reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uint8_t*>(localPawn) + pawnViewAngleOffset)
        : native_view_angles;
    QAngle_t current{};
    if (destination == nullptr || !SafeNativeRead(destination, current) || !current.IsValid())
        return;

    std::string activeWeapon;
    int ammo = 0;
    int maximumAmmo = 0;
    GetWeaponDisplay(entities, localPawn, activeWeapon, ammo, maximumAmmo);
    const int weaponGroup = GetRageWeaponGroup(activeWeapon);
    const bool hitscan = C_GET(bool, Vars.rage_hitscan);
    const int selection = std::clamp(
        C_GET_ARRAY(int, 7, Vars.rage_target_select, weaponGroup), 0, 2);
    const bool ballisticsRequested = selection == 1 ||
        C_GET_ARRAY(int, 7, Vars.rage_minimum_damage, weaponGroup) > 0 ||
        C_GET_ARRAY(bool, 7, Vars.rage_hitchance, weaponGroup) ||
        C_GET_ARRAY(bool, 7, Vars.rage_penetration, weaponGroup) ||
        C_GET_ARRAY(bool, 7, Vars.rage_lethal_body, weaponGroup) ||
        C_GET_ARRAY(bool, 7, Vars.rage_prefer_high_damage, weaponGroup) ||
        C_GET_ARRAY(bool, 7, Vars.rage_delay_accurate, weaponGroup);
    if (ballisticsRequested && C_GET(bool, Vars.rage_shot_logging))
    {
        static std::uint64_t nextBallisticsLog = 0;
        if (SDL_GetTicks() >= nextBallisticsLog)
        {
            nextBallisticsLog = SDL_GetTicks() + 500;
            LegitLog("[rage] HOLD reason=native_ballistics_unavailable trace_ready=%d selection=%d mindamage=%d hitchance=%d penetration=%d",
                TRACE::NativeReady(), selection,
                C_GET_ARRAY(int, 7, Vars.rage_minimum_damage, weaponGroup),
                C_GET_ARRAY(bool, 7, Vars.rage_hitchance, weaponGroup),
                C_GET_ARRAY(bool, 7, Vars.rage_penetration, weaponGroup));
        }
    }

    // Damage ranking needs a real damage/penetration simulation. Keep target
    // acquisition and bounded aim assistance available for an old config that
    // requests it, but rank by angular distance and hold every combat input.
    // This makes the unsupported part fail closed without making Rage appear
    // completely dead after loading such a profile.
    const int selectionForScoring = selection == 1 ? 2 : selection;

    const bool forceBody = IsNativeButtonDown(C_GET(int, Vars.rage_force_body_key));
    const bool forceHead = IsNativeButtonDown(C_GET(int, Vars.rage_force_head_key));
    const bool preferExposed = C_GET_ARRAY(bool, 7, Vars.rage_prefer_exposed, weaponGroup);
    const bool requireVisible = C_GET_ARRAY(bool, 7, Vars.rage_delay_visible, weaponGroup);
    if ((preferExposed || requireVisible) && !TRACE::NativeReady())
    {
        ClearNativeAimDelta();
        return;
    }
    const bool multipoint = C_GET_ARRAY(bool, 7, Vars.rage_multipoint, weaponGroup);
    const float multipointScale = std::clamp(
        C_GET_ARRAY(float, 7, Vars.rage_multipoint_scale, weaponGroup), 10.f, 100.f) / 100.f;

    struct Candidate
    {
        C_CSPlayerPawn* pawn = nullptr;
        int bone = -1;
        QAngle_t delta{};
        Vector_t point{};
        float fov = 9999.f;
        float score = 9999999.f;
        int health = 0;
    } best;
    constexpr float degrees = 180.f / 3.14159265358979323846f;
    int pawnsScanned = 0;
    int pointsScanned = 0;

    for (int index = 1; index <= 128; ++index)
    {
        CCSPlayerController* controller = entities->Get<CCSPlayerController>(index);
        if (controller == nullptr || controller == localController)
            continue;
        C_CSPlayerPawn* pawn = entities->Get<C_CSPlayerPawn>(controller->GetPawnHandle());
        if (pawn == nullptr || pawn == localPawn || pawn->GetHealth() <= 0 ||
            pawn->GetTeam() == localPawn->GetTeam())
            continue;
        CGameSceneNode* scene = pawn->GetGameSceneNode();
        if (scene == nullptr || scene->IsDormant())
            continue;
        ++pawnsScanned;

        const bool anyConfigured =
            C_GET_ARRAY(bool, 7, Vars.hitbox_head, weaponGroup) ||
            C_GET_ARRAY(bool, 7, Vars.hitbox_neck, weaponGroup) ||
            C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, weaponGroup) ||
            C_GET_ARRAY(bool, 7, Vars.hitbox_chest, weaponGroup) ||
            C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, weaponGroup) ||
            C_GET_ARRAY(bool, 7, Vars.hitbox_legs, weaponGroup) ||
            C_GET_ARRAY(bool, 7, Vars.hitbox_feet, weaponGroup);
        const int bones[] = {
            (!forceBody && (forceHead || !anyConfigured || C_GET_ARRAY(bool, 7, Vars.hitbox_head, weaponGroup))) ? 5 : -1,
            (!forceBody && !forceHead && C_GET_ARRAY(bool, 7, Vars.hitbox_neck, weaponGroup)) ? 4 : -1,
            (!forceHead && (forceBody || C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, weaponGroup))) ? 3 : -1,
            (!forceHead && (forceBody || C_GET_ARRAY(bool, 7, Vars.hitbox_chest, weaponGroup))) ? 6 : -1,
            (!forceHead && (forceBody || C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, weaponGroup))) ? 2 : -1,
            (!forceHead && !forceBody && C_GET_ARRAY(bool, 7, Vars.hitbox_legs, weaponGroup)) ? 23 : -1,
            (!forceHead && !forceBody && C_GET_ARRAY(bool, 7, Vars.hitbox_legs, weaponGroup)) ? 26 : -1,
            (!forceHead && !forceBody && C_GET_ARRAY(bool, 7, Vars.hitbox_feet, weaponGroup)) ? 27 : -1,
            (!forceHead && !forceBody && C_GET_ARRAY(bool, 7, Vars.hitbox_feet, weaponGroup)) ? 30 : -1,
        };

        for (const int bone : bones)
        {
            Vector_t target{};
            if (bone < 0 || !GetLiveBonePosition(scene, bone, target))
                continue;
            std::array<Vector_t, 5> points{target, target, target, target, target};
            int pointCount = 1;
            if (multipoint)
            {
                const Vector_t toTarget = target - localEye;
                const float length2d = std::hypot(toTarget.x, toTarget.y);
                if (length2d > 0.001f)
                {
                    const float approximateRadius = (bone == 5 ? 3.5f :
                        (bone == 2 || bone == 3 || bone == 6 ? 5.5f : 3.f)) * multipointScale;
                    const Vector_t right(-toTarget.y / length2d, toTarget.x / length2d, 0.f);
                    points[1] = target + right * approximateRadius;
                    points[2] = target - right * approximateRadius;
                    points[3] = target + Vector_t(0.f, 0.f, approximateRadius * 0.6f);
                    points[4] = target - Vector_t(0.f, 0.f, approximateRadius * 0.6f);
                    pointCount = 5;
                }
            }
            for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
            {
                ++pointsScanned;
                bool exposed = false;
                if (preferExposed || requireVisible)
                {
                    TRACE::NativeResult trace{};
                    if (!TRACE::NativeLine(localEye, points[pointIndex], localPawn, trace))
                        continue;
                    exposed = !trace.hit || trace.entity == pawn;
                    if (requireVisible && !exposed)
                        continue;
                }
                const Vector_t difference = points[pointIndex] - localEye;
                const float horizontal = std::hypot(difference.x, difference.y);
                if (horizontal < 0.001f)
                    continue;
                QAngle_t wanted(-std::atan2(difference.z, horizontal) * degrees,
                                 std::atan2(difference.y, difference.x) * degrees, 0.f);
                const QAngle_t delta(std::remainder(wanted.x - current.x, 360.f),
                                     std::remainder(wanted.y - current.y, 360.f), 0.f);
                const float fov = std::hypot(delta.x, delta.y);
                float score = selectionForScoring == 0 ? difference.Length() : fov;
                if (C_GET_ARRAY(bool, 7, Vars.rage_prefer_low_health, weaponGroup))
                    score += static_cast<float>(pawn->GetHealth()) * 4.f;
                if (preferExposed && !exposed)
                    score += 100000.f;
                if (std::isfinite(score) && score < best.score)
                    best = { pawn, bone, delta, points[pointIndex], fov, score, pawn->GetHealth() };
            }
            if (!hitscan)
                break;
        }
    }

    if (best.pawn == nullptr)
    {
        ClearNativeAimDelta();
        return;
    }

    // Rage is deliberately faster than Legitbot, but still bounded per frame
    // so a stale bone or one long frame cannot spin the camera uncontrollably.
    constexpr float maxDegreesPerFrame = 15.f;
    const float stepPitch = std::clamp(best.delta.x, -maxDegreesPerFrame, maxDegreesPerFrame);
    const float stepYaw = std::clamp(best.delta.y, -maxDegreesPerFrame, maxDegreesPerFrame);
    QAngle_t result(current.x + stepPitch, current.y + stepYaw, 0.f);
    result.Clamp();

    bool scoped = false;
    static const std::uint32_t scopedOffset =
        SCHEMA::GetOffset("C_CSPlayerPawnBase->m_bIsScoped");
    std::uint8_t scopedRaw = 0;
    scoped = scopedOffset != 0 && SafeNativeRead(
        reinterpret_cast<const std::uint8_t*>(localPawn) + scopedOffset, scopedRaw) && scopedRaw == 1;
    const bool requestScope = !ballisticsRequested && weaponGroup >= 4 &&
        C_GET_ARRAY(bool, 7, Vars.rage_auto_scope, weaponGroup) && !scoped;
    const bool centered = best.fov <= 0.35f;
    const bool requestAttack = !ballisticsRequested && centered &&
        C_GET(bool, Vars.rage_auto_shoot) && !requestScope;
    const bool requestCrouch = !ballisticsRequested &&
        C_GET_ARRAY(bool, 7, Vars.rage_auto_crouch, weaponGroup);
    const bool requestStop = !ballisticsRequested &&
        C_GET_ARRAY(bool, 7, Vars.rage_auto_stop, weaponGroup);
    AddNativeCombatInput(requestAttack, requestCrouch, requestScope, requestStop);

    ClearNativeAimDelta();
    const bool queued = QueueNativeAimAngles(destination, result.x, result.y, 0.f);
    native_aim_command_queued = queued;

    if (C_GET(bool, Vars.rage_decision_overlay))
    {
        ImVec2 screen{};
        if (D::WorldToScreen(best.point, screen))
        {
            char decision[160]{};
            std::snprintf(decision, sizeof(decision),
                "RAGE %s | HP %d | FOV %.2f | DMG/HC n/a%s",
                ballisticsRequested ? "HOLD BALLISTICS" :
                    (requestAttack ? "FIRE" : (requestScope ? "SCOPE" : "AIM")),
                best.health, best.fov, TRACE::NativeReady() ? "" : " (trace offline)");
            DrawOutlinedText(ImGui::GetBackgroundDrawList(), screen + ImVec2(8.f, -18.f),
                decision, IM_COL32(255, 190, 80, 255), IM_COL32(0, 0, 0, 230));
        }
    }

    static std::uint64_t nextLog = 0;
    const std::uint64_t now = SDL_GetTicks();
    if (C_GET(bool, Vars.rage_shot_logging) && now >= nextLog)
    {
        nextLog = now + 250;
        LegitLog("[rage] decision=%s weapon=%s group=%d hitscan=%d multipoint=%d selection=%d pawns=%d points=%d target=%p bone=%d hp=%d fov=%.2f score=%.2f step=(%.2f,%.2f) requests=attack:%d crouch:%d scope:%d stop:%d hook=%d queued=%d",
            ballisticsRequested ? "HOLD_BALLISTICS" :
                (requestAttack ? "FIRE" : (requestScope ? "SCOPE" : "AIM")),
            activeWeapon.c_str(), weaponGroup, hitscan, multipoint, selection, pawnsScanned,
            pointsScanned, best.pawn, best.bone, best.health, best.fov, best.score,
            stepPitch, stepYaw, requestAttack,
            requestCrouch, requestScope, requestStop,
            IsNativeInputHookInstalled(), queued);
    }
}

void ApplyNativeSkin(CGameEntitySystem* entities, CCSPlayerController* localController, C_CSPlayerPawn* localPawn)
{
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
    static const std::uint32_t modelState = SCHEMA::GetOffset("CSkeletonInstance->m_modelState");
    static const std::uint32_t meshGroupMask = SCHEMA::GetOffset("CModelState->m_MeshGroupMask");
    if (entities == nullptr || localController == nullptr || localPawn == nullptr ||
        attributeManager == 0 || itemOffset == 0 || idHigh == 0 || paintKit == 0 || definitionIndex == 0)
    {
        active_skin_weapon_definition = 0;
        native_skin_runtime_status = entities == nullptr || localController == nullptr || localPawn == nullptr
            ? NativeSkinRuntimeStatus::WaitingForPlayer
            : NativeSkinRuntimeStatus::MissingSchema;
        return;
    }

    CPlayer_WeaponServices* services = localPawn->GetWeaponServices();
    if (services == nullptr)
    {
        active_skin_weapon_definition = 0;
        native_skin_runtime_status = NativeSkinRuntimeStatus::WaitingForPlayer;
        return;
    }

    C_CSWeaponBase* activeWeapon = entities->Get<C_CSWeaponBase>(services->m_hActiveWeapon());
    const auto getItem = [&](C_CSWeaponBase* weapon) -> std::uint8_t* {
        return weapon == nullptr ? nullptr : reinterpret_cast<std::uint8_t*>(weapon) + attributeManager + itemOffset;
    };
    const auto getDefinition = [&](C_CSWeaponBase* weapon) -> std::uint16_t {
        std::uint8_t* item = getItem(weapon);
        return item == nullptr ? 0 : *reinterpret_cast<const std::uint16_t*>(item + definitionIndex);
    };
    active_skin_weapon_definition = getDefinition(activeWeapon);
    if (active_skin_weapon_definition == 0)
        native_skin_runtime_status = NativeSkinRuntimeStatus::WaitingForWeapon;

    static bool legacyMigrated = false;
    if (!legacyMigrated && active_skin_weapon_definition != 0 &&
        active_skin_weapon_definition < LinuxNativeEsp::SkinWeaponDefinitionCount &&
        C_GET(int, Vars.skin_ui_paint_kit) > 0)
    {
        const std::size_t definition = active_skin_weapon_definition;
        C_GET_ARRAY(bool, 1024, Vars.skin_weapon_enable, definition) = true;
        C_GET_ARRAY(int, 1024, Vars.skin_weapon_paint_kit, definition) = C_GET(int, Vars.skin_ui_paint_kit);
        C_GET_ARRAY(int, 1024, Vars.skin_weapon_seed, definition) = C_GET(int, Vars.skin_ui_seed);
        C_GET_ARRAY(float, 1024, Vars.skin_weapon_wear, definition) = C_GET(float, Vars.skin_ui_wear);
        C_GET_ARRAY(int, 1024, Vars.skin_weapon_stattrak, definition) = C_GET(int, Vars.skin_ui_stattrak);
        legacyMigrated = true;
        EspLog("[skin] migrated legacy profile to definition=%u", active_skin_weapon_definition);
    }

    const std::uint32_t ownerAccount = steamId == 0 ? 0U : static_cast<std::uint32_t>(
        *reinterpret_cast<const std::uint64_t*>(reinterpret_cast<std::uint8_t*>(localController) + steamId));
    static bool loggedOffsets = false;
    if (!loggedOffsets)
    {
        EspLog("[skin] offsets weapons=0x%x attr=0x%x item=0x%x list=0x%x/0x%x paint=0x%x wear=0x%x account=0x%x",
            myWeapons, attributeManager, itemOffset, attributeList, attributes, paintKit, wear, accountId);
        loggedOffsets = true;
    }

    using RegenerateWeaponSkinsFn = void(*)(bool);
    static RegenerateWeaponSkinsFn regenerateWeaponSkins = nullptr;
    static bool attemptedRegenerateLookup = false;
    if (!attemptedRegenerateLookup)
    {
        attemptedRegenerateLookup = true;
        regenerateWeaponSkins = reinterpret_cast<RegenerateWeaponSkinsFn>(MEM::FindPattern(
            CLIENT_DLL,
            "55 48 89 E5 41 55 44 0F B6 EF 41 54 53 48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 7D"));
        native_skin_regenerator_ready = regenerateWeaponSkins != nullptr;
        EspLog("[skin] native regenerate callback=%p", regenerateWeaponSkins);
    }

    static std::array<AppliedWeaponSkin, 64> appliedStates{};

    std::array<C_CSWeaponBase*, 64> ownedWeapons{};
    std::size_t ownedWeaponCount = 0;
    if (myWeapons != 0)
    {
        auto* collection = reinterpret_cast<C_NetworkUtlVectorBase<CBaseHandle>*>(
            reinterpret_cast<std::uint8_t*>(services) + myWeapons);
        if (collection->pElements != nullptr)
        {
            const std::uint32_t count = std::min(collection->nSize, 64U);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                C_CSWeaponBase* weapon = entities->Get<C_CSWeaponBase>(collection->pElements[index]);
                if (weapon != nullptr &&
                    std::find(ownedWeapons.begin(), ownedWeapons.begin() + ownedWeaponCount, weapon) ==
                        ownedWeapons.begin() + ownedWeaponCount)
                    ownedWeapons[ownedWeaponCount++] = weapon;
            }
        }
    }
    if (ownedWeaponCount == 0 && activeWeapon != nullptr)
        ownedWeapons[ownedWeaponCount++] = activeWeapon;

    static C_CSWeaponBase* previousActiveWeapon = nullptr;
    const bool activeWeaponChanged = activeWeapon != previousActiveWeapon;
    previousActiveWeapon = activeWeapon;

    // Release state from dropped/destroyed weapons before allocating state for
    // replacements. This avoids a one-frame failure when a full inventory is
    // recreated during a team switch or respawn.
    for (AppliedWeaponSkin& state : appliedStates)
    {
        if (state.weapon == nullptr)
            continue;
        const bool stillOwned = std::any_of(ownedWeapons.begin(), ownedWeapons.begin() + ownedWeaponCount,
            [&](C_CSWeaponBase* weapon) {
                return weapon == state.weapon && getDefinition(weapon) == state.definition;
            });
        if (!stillOwned)
            state = {};
    }

    std::vector<TemporarySkinAttributes> temporaryAttributes;
    temporaryAttributes.reserve(ownedWeaponCount);
    std::vector<AppliedWeaponSkin*> appliedThisPass;
    std::vector<AppliedWeaponSkin*> restoredThisPass;
    bool shouldRegenerate = false;

    const auto getMeshMask = [&](CGameSceneNode* scene) -> std::uint64_t {
        if (scene == nullptr || modelState == 0 || meshGroupMask == 0)
            return 1;
        return *reinterpret_cast<const std::uint64_t*>(
            reinterpret_cast<const std::uint8_t*>(scene) + modelState + meshGroupMask);
    };
    const auto setMeshMask = [&](CGameSceneNode* scene, std::uint64_t mask) {
        if (scene == nullptr || modelState == 0 || meshGroupMask == 0)
            return;
        auto* state = reinterpret_cast<std::uint8_t*>(scene) + modelState;
        *reinterpret_cast<std::uint64_t*>(state + meshGroupMask) = mask;
    };

    const auto prepareAttributes = [&](std::uint8_t* item, const NativeSkinSettings& settings) {
        if (attributeList == 0 || attributes == 0)
            return false;
        auto* list = reinterpret_cast<C_NetworkUtlVectorBase<NativeEconAttribute>*>(
            item + attributeList + attributes);
        if (list->nSize > 64 || (list->nSize != 0 && list->pElements == nullptr))
        {
            EspLog("[skin] rejected invalid attribute list item=%p count=%u elements=%p",
                item, list->nSize, list->pElements);
            return false;
        }

        temporaryAttributes.emplace_back();
        TemporarySkinAttributes& temporary = temporaryAttributes.back();
        temporary.list = list;
        temporary.originalSize = list->nSize;
        temporary.originalElements = list->pElements;
        temporary.attributes.reserve(static_cast<std::size_t>(list->nSize) + 3);
        if (list->nSize != 0)
            temporary.attributes.assign(list->pElements, list->pElements + list->nSize);

        const std::array<std::pair<std::uint16_t, float>, 3> values{{
            {6, static_cast<float>(settings.paint)},
            {7, static_cast<float>(settings.seed)},
            {8, settings.wear},
        }};
        for (const auto& [definition, value] : values)
        {
            auto iterator = std::find_if(temporary.attributes.begin(), temporary.attributes.end(),
                [definition](const NativeEconAttribute& attribute) { return attribute.definition == definition; });
            if (iterator == temporary.attributes.end())
            {
                temporary.attributes.emplace_back();
                iterator = std::prev(temporary.attributes.end());
                iterator->owner = item;
                iterator->definition = definition;
            }
            iterator->value = value;
            iterator->initialValue = value;
        }
        list->pElements = temporary.attributes.data();
        list->nSize = static_cast<std::uint32_t>(temporary.attributes.size());
        return true;
    };

    const auto findState = [&](C_CSWeaponBase* weapon, std::uint16_t definition) -> AppliedWeaponSkin* {
        for (AppliedWeaponSkin& state : appliedStates)
            if (state.weapon == weapon && state.definition == definition)
                return &state;
        return nullptr;
    };
    const auto allocateState = [&]() -> AppliedWeaponSkin* {
        for (AppliedWeaponSkin& state : appliedStates)
            if (state.weapon == nullptr)
                return &state;
        return nullptr;
    };
    const auto restoreState = [&](AppliedWeaponSkin& state, std::uint8_t* base, std::uint8_t* item) {
        *reinterpret_cast<std::int32_t*>(item + idHigh) = state.original.idHigh;
        if (idLow != 0) *reinterpret_cast<std::int32_t*>(item + idLow) = state.original.idLow;
        if (accountId != 0) *reinterpret_cast<std::uint32_t*>(item + accountId) = state.original.account;
        if (entityQuality != 0) *reinterpret_cast<std::uint32_t*>(item + entityQuality) = state.original.quality;
        if (initialized != 0) *reinterpret_cast<bool*>(item + initialized) = state.original.initialized;
        if (disallowSoc != 0) *reinterpret_cast<bool*>(item + disallowSoc) = state.original.disallowSoc;
        if (storeItem != 0) *reinterpret_cast<bool*>(item + storeItem) = state.original.storeItem;
        if (restoreMaterial != 0) *reinterpret_cast<bool*>(item + restoreMaterial) = state.original.restoreMaterial;
        if (attributesInitialized != 0) *reinterpret_cast<bool*>(base + attributesInitialized) = state.original.attributesInitialized;
        if (attachmentDirty != 0) *reinterpret_cast<bool*>(base + attachmentDirty) = true;
        *reinterpret_cast<std::int32_t*>(base + paintKit) = state.original.paint;
        if (seed != 0) *reinterpret_cast<std::int32_t*>(base + seed) = state.original.seed;
        if (wear != 0) *reinterpret_cast<float*>(base + wear) = state.original.wear;
        if (statTrak != 0) *reinterpret_cast<std::int32_t*>(base + statTrak) = state.original.statTrak;
        setMeshMask(state.weapon->GetGameSceneNode(), state.original.meshMask);
    };

    for (std::size_t weaponIndex = 0; weaponIndex < ownedWeaponCount; ++weaponIndex)
    {
        C_CSWeaponBase* weapon = ownedWeapons[weaponIndex];
        auto* base = reinterpret_cast<std::uint8_t*>(weapon);
        std::uint8_t* item = getItem(weapon);
        const std::uint16_t definition = getDefinition(weapon);
        if (item == nullptr || definition == 0 || definition >= LinuxNativeEsp::SkinWeaponDefinitionCount)
            continue;

        AppliedWeaponSkin* state = findState(weapon, definition);
        const bool profileSelected = C_GET_ARRAY(bool, 1024, Vars.skin_weapon_enable, definition);
        const int configuredPaint = C_GET_ARRAY(int, 1024, Vars.skin_weapon_paint_kit, definition);
        const bool profileEnabled = C_GET(bool, Vars.skin_ui_enable) && profileSelected && configuredPaint > 0;
        if (!profileEnabled)
        {
            if (state != nullptr && state->applied)
            {
                restoreState(*state, base, item);
                restoredThisPass.push_back(state);
                shouldRegenerate = true;
                EspLog("[skin] restoring weapon=%p definition=%u", weapon, definition);
            }
            continue;
        }
        // Do not mutate a live econ item if the only known refresh path for
        // this exact client build is unavailable. Repeated fallback writes
        // without regeneration previously left half-applied state every frame.
        if (regenerateWeaponSkins == nullptr)
            continue;

        NativeSkinSettings settings{
            configuredPaint,
            std::clamp(C_GET_ARRAY(int, 1024, Vars.skin_weapon_seed, definition), 0, 1000),
            std::clamp(C_GET_ARRAY(float, 1024, Vars.skin_weapon_wear, definition), 0.000001f, 1.f),
            C_GET_ARRAY(int, 1024, Vars.skin_weapon_stattrak, definition),
            C_GET_ARRAY(bool, 1024, Vars.skin_weapon_legacy_mesh, definition),
        };
        if (state == nullptr)
        {
            state = allocateState();
            if (state == nullptr)
            {
                EspLog("[skin] no free state slot for weapon=%p definition=%u", weapon, definition);
                continue;
            }
            *state = {};
            state->weapon = weapon;
            state->definition = definition;
            state->original.idHigh = *reinterpret_cast<const std::int32_t*>(item + idHigh);
            if (idLow != 0) state->original.idLow = *reinterpret_cast<const std::int32_t*>(item + idLow);
            if (accountId != 0) state->original.account = *reinterpret_cast<const std::uint32_t*>(item + accountId);
            if (entityQuality != 0) state->original.quality = *reinterpret_cast<const std::uint32_t*>(item + entityQuality);
            if (initialized != 0) state->original.initialized = *reinterpret_cast<const bool*>(item + initialized);
            if (disallowSoc != 0) state->original.disallowSoc = *reinterpret_cast<const bool*>(item + disallowSoc);
            if (storeItem != 0) state->original.storeItem = *reinterpret_cast<const bool*>(item + storeItem);
            if (restoreMaterial != 0) state->original.restoreMaterial = *reinterpret_cast<const bool*>(item + restoreMaterial);
            if (attributesInitialized != 0) state->original.attributesInitialized = *reinterpret_cast<const bool*>(base + attributesInitialized);
            if (attachmentDirty != 0) state->original.attachmentDirty = *reinterpret_cast<const bool*>(base + attachmentDirty);
            state->original.paint = *reinterpret_cast<const std::int32_t*>(base + paintKit);
            if (seed != 0) state->original.seed = *reinterpret_cast<const std::int32_t*>(base + seed);
            if (wear != 0) state->original.wear = *reinterpret_cast<const float*>(base + wear);
            if (statTrak != 0) state->original.statTrak = *reinterpret_cast<const std::int32_t*>(base + statTrak);
            state->original.meshMask = getMeshMask(weapon->GetGameSceneNode());
        }

        const std::uint64_t desiredMeshMask = settings.legacyMesh ? 2U : 1U;
        const bool identityDrifted = *reinterpret_cast<const std::int32_t*>(item + idHigh) != -1 ||
            (idLow != 0 && *reinterpret_cast<const std::int32_t*>(item + idLow) != -1);
        const bool meshDrifted = getMeshMask(weapon->GetGameSceneNode()) != desiredMeshMask;
        const bool changed = !state->applied || !(state->settings == settings) || identityDrifted ||
            meshDrifted || native_skin_refresh_requested || (activeWeaponChanged && weapon == activeWeapon);
        setMeshMask(weapon->GetGameSceneNode(), desiredMeshMask);
        if (!changed)
            continue;

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
            if (definition >= 500 || definition == 42 || definition == 59)
                *reinterpret_cast<std::uint32_t*>(item + entityQuality) = 4;
        }

        *reinterpret_cast<std::int32_t*>(base + paintKit) = settings.paint;
        if (seed != 0)
            *reinterpret_cast<std::int32_t*>(base + seed) = settings.seed;
        if (wear != 0)
            *reinterpret_cast<float*>(base + wear) = settings.wear;
        if (statTrak != 0)
            *reinterpret_cast<std::int32_t*>(base + statTrak) = settings.statTrak;
        prepareAttributes(item, settings);
        state->settings = settings;
        appliedThisPass.push_back(state);
        shouldRegenerate = true;
        EspLog("[skin] queued weapon=%p definition=%u paint=%d seed=%d wear=%.6f stattrak=%d mesh=%u attrs=%zu",
            weapon, definition, settings.paint, settings.seed, settings.wear, settings.statTrak,
            settings.legacyMesh ? 2U : 1U, temporaryAttributes.size());
    }

    CCSPlayer_ViewModelServices* viewModelServices = localPawn->GetViewModelServices();
    C_CSGOViewModel* viewModel = viewModelServices != nullptr
        ? entities->Get<C_CSGOViewModel>(viewModelServices->m_hViewModel())
        : nullptr;
    static C_CSGOViewModel* trackedViewModel = nullptr;
    static std::uint64_t originalViewModelMask = 1;
    static bool viewModelOverridden = false;
    if (viewModel != trackedViewModel)
    {
        trackedViewModel = viewModel;
        viewModelOverridden = false;
    }
    if (activeWeaponChanged && viewModel != nullptr && viewModelOverridden)
    {
        setMeshMask(viewModel->GetGameSceneNode(), originalViewModelMask);
        viewModelOverridden = false;
    }
    const std::uint16_t activeDefinition = getDefinition(activeWeapon);
    const bool activeProfileSelected = activeDefinition != 0 &&
        activeDefinition < LinuxNativeEsp::SkinWeaponDefinitionCount &&
        C_GET_ARRAY(bool, 1024, Vars.skin_weapon_enable, activeDefinition);
    const bool activeProfileEnabled = regenerateWeaponSkins != nullptr && activeProfileSelected &&
        C_GET(bool, Vars.skin_ui_enable) &&
        C_GET_ARRAY(int, 1024, Vars.skin_weapon_paint_kit, activeDefinition) > 0;
    if (viewModel != nullptr && activeProfileEnabled)
    {
        if (!viewModelOverridden)
        {
            originalViewModelMask = getMeshMask(viewModel->GetGameSceneNode());
            viewModelOverridden = true;
        }
        setMeshMask(viewModel->GetGameSceneNode(),
            C_GET_ARRAY(bool, 1024, Vars.skin_weapon_legacy_mesh, activeDefinition) ? 2U : 1U);
    }
    else if (viewModel != nullptr && viewModelOverridden)
    {
        setMeshMask(viewModel->GetGameSceneNode(), originalViewModelMask);
        viewModelOverridden = false;
    }

    const bool regenerated = shouldRegenerate && regenerateWeaponSkins != nullptr;
    if (regenerated)
    {
        regenerateWeaponSkins(false);
        EspLog("[skin] regenerated applied=%zu restored=%zu temporary_lists=%zu",
            appliedThisPass.size(), restoredThisPass.size(), temporaryAttributes.size());
    }

    // The engine consumes these lists synchronously. Restore its original
    // vectors immediately so no entity retains a pointer into our stack.
    for (TemporarySkinAttributes& temporary : temporaryAttributes)
    {
        temporary.list->pElements = temporary.originalElements;
        temporary.list->nSize = temporary.originalSize;
    }

    if (regenerated)
    {
        for (AppliedWeaponSkin* state : appliedThisPass)
        {
            // Keep fallback fields on the owned weapon for its whole lifetime.
            // Clearing the paint kit immediately after material regeneration
            // made the override disappear on later model/material refreshes.
            state->applied = true;
        }
        for (AppliedWeaponSkin* state : restoredThisPass)
        {
            if (attachmentDirty != 0)
            {
                auto* base = reinterpret_cast<std::uint8_t*>(state->weapon);
                *reinterpret_cast<bool*>(base + attachmentDirty) = state->original.attachmentDirty;
            }
            *state = {};
        }
        native_skin_refresh_requested = false;
    }

    if (activeDefinition == 0)
        native_skin_runtime_status = NativeSkinRuntimeStatus::WaitingForWeapon;
    else if (!C_GET(bool, Vars.skin_ui_enable))
        native_skin_runtime_status = NativeSkinRuntimeStatus::Disabled;
    else if (!activeProfileSelected)
        native_skin_runtime_status = NativeSkinRuntimeStatus::ProfileDisabled;
    else if (C_GET_ARRAY(int, 1024, Vars.skin_weapon_paint_kit, activeDefinition) <= 0)
        native_skin_runtime_status = NativeSkinRuntimeStatus::PaintKitRequired;
    else if (!native_skin_regenerator_ready)
        native_skin_runtime_status = NativeSkinRuntimeStatus::MissingRegenerator;
    else if (regenerated)
        native_skin_runtime_status = NativeSkinRuntimeStatus::Refreshed;
    else
        native_skin_runtime_status = NativeSkinRuntimeStatus::Ready;
}

void DrawNativeGrenadeTrajectory(CGameEntitySystem* entities, C_CSPlayerPawn* localPawn,
    ImDrawList* draw)
{
    if (!C_GET(bool, Vars.grenade_trajectory) || entities == nullptr ||
        localPawn == nullptr || draw == nullptr)
        return;

    std::string weaponName;
    int ammo = 0, maximumAmmo = 0;
    C_CSWeaponBase* weapon = nullptr;
    if (!GetWeaponDisplay(entities, localPawn, weaponName, ammo, maximumAmmo, &weapon) ||
        weapon == nullptr)
        return;
    const bool isGrenade = weaponName == "flashbang" || weaponName == "hegrenade" ||
        weaponName == "smokegrenade" || weaponName == "molotov" ||
        weaponName == "incgrenade" || weaponName == "decoy";
    if (!isGrenade)
        return;
    if (!TRACE::NativeReady())
    {
        static bool loggedUnavailable = false;
        if (!loggedUnavailable)
        {
            EspLog("[GRENADE] trajectory held because native trace self-test has not passed");
            loggedUnavailable = true;
        }
        return;
    }

    CGameSceneNode* scene = localPawn->GetGameSceneNode();
    if (scene == nullptr)
        return;
    QAngle_t view{};
    static const std::uint32_t viewAngleOffset =
        SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    if (viewAngleOffset != 0)
        SafeNativeRead(reinterpret_cast<const std::uint8_t*>(localPawn) + viewAngleOffset, view);
    if (!view.IsValid() && native_view_angles != nullptr)
        SafeNativeRead(native_view_angles, view);
    if (!view.IsValid())
        return;

    static const std::uint32_t throwStrengthOffset =
        SCHEMA::GetOffset("C_BaseCSGrenade->m_flThrowStrength");
    static const std::uint32_t throwVelocityOffset =
        SCHEMA::GetOffset("CCSWeaponBaseVData->m_flThrowVelocity");
    const WeaponDisplayOffsets& displayOffsets = GetWeaponDisplayOffsets();
    if (throwStrengthOffset == 0 || throwVelocityOffset == 0 || !displayOffsets.valid)
        return;
    float strength = 1.f;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(weapon) + throwStrengthOffset, strength) ||
        !std::isfinite(strength) || strength < 0.f || strength > 1.01f)
        return;
    strength = std::clamp(strength, 0.f, 1.f);
    void* weaponData = nullptr;
    float throwVelocity = 0.f;
    if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(weapon) +
            displayOffsets.subclassId + sizeof(void*), weaponData) || weaponData == nullptr ||
        !SafeNativeRead(reinterpret_cast<const std::uint8_t*>(weaponData) +
            throwVelocityOffset, throwVelocity) || !std::isfinite(throwVelocity) ||
        throwVelocity < 100.f || throwVelocity > 2000.f)
        return;

    float gravity = 800.f;
    static CConVar* gravityConvar = nullptr;
    if (gravityConvar == nullptr)
        gravityConvar = CONVAR::Find("sv_gravity");
    float gravityReadback = 0.f;
    if (gravityConvar != nullptr && CONVAR::ReadFloat(gravityConvar, gravityReadback) &&
        std::isfinite(gravityReadback) && gravityReadback >= 100.f && gravityReadback <= 2000.f)
        gravity = gravityReadback;

    constexpr float radians = 3.14159265358979323846f / 180.f;
    float adjustedPitch = std::remainder(view.x, 360.f);
    adjustedPitch -= (90.f - std::fabs(adjustedPitch)) * (10.f / 90.f);
    const float pitch = adjustedPitch * radians;
    const float yaw = view.y * radians;
    const Vector_t forward(std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw), -std::sin(pitch));
    Vector_t position = scene->GetAbsOrigin() + localPawn->m_vecViewOffset() + forward * 16.f;
    Vector_t velocity = forward * (throwVelocity * 0.9f * (0.3f + 0.7f * strength)) +
        localPawn->GetAbsVelocity() * 1.25f;
    if (!position.IsValid() || !velocity.IsValid())
        return;

    const float interval = I::GlobalVars != nullptr &&
        I::GlobalVars->flIntervalPerTick > 0.001f && I::GlobalVars->flIntervalPerTick < 0.1f
        ? I::GlobalVars->flIntervalPerTick : (1.f / 64.f);
    float fuse = 3.f;
    if (weaponName == "flashbang" || weaponName == "hegrenade")
        fuse = 1.5f;
    else if (weaponName == "molotov" || weaponName == "incgrenade")
        fuse = 2.f;
    const int maximumSteps = std::clamp(static_cast<int>(std::ceil(fuse / interval)), 1, 384);
    const Vector_t mins(-2.f, -2.f, -2.f);
    const Vector_t maxs(2.f, 2.f, 2.f);
    static thread_local std::vector<Vector_t> points;
    static thread_local std::vector<Vector_t> bounces;
    points.clear();
    bounces.clear();
    if (points.capacity() < static_cast<std::size_t>(maximumSteps) + 1)
        points.reserve(static_cast<std::size_t>(maximumSteps) + 1);
    if (bounces.capacity() < 32)
        bounces.reserve(32);
    points.push_back(position);
    for (int step = 0; step < maximumSteps; ++step)
    {
        Vector_t next = position + velocity * interval;
        next.z -= gravity * 0.4f * interval * interval * 0.5f;
        velocity.z -= gravity * 0.4f * interval;
        TRACE::NativeResult trace{};
        if (!TRACE::NativeHull(position, next, mins, maxs, localPawn, trace))
            return;
        if (trace.hit)
        {
            position = trace.end;
            points.push_back(position);
            bounces.push_back(position);
            const float intoSurface = velocity.Dot(trace.normal);
            velocity -= trace.normal * (1.f + 0.45f) * intoSurface;
            velocity *= 0.45f;
            position += trace.normal * 0.5f;
            if (velocity.Length() < 20.f || trace.allSolid)
                break;
        }
        else
        {
            position = next;
            points.push_back(position);
        }
    }
    if (points.size() < 2)
        return;

    const ImU32 color = C_GET(ColorPickerVar_t, Vars.world_esp_color).colValue.GetU32();
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        ImVec2 from{}, to{};
        if (D::WorldToScreen(points[index - 1], from) && D::WorldToScreen(points[index], to))
        {
            draw->AddLine(from, to, IM_COL32(0, 0, 0, 220), 3.f);
            draw->AddLine(from, to, color, 1.5f);
        }
    }
    if (C_GET(bool, Vars.grenade_bounce_markers))
    {
        for (const Vector_t& bounce : bounces)
        {
            ImVec2 screen{};
            if (D::WorldToScreen(bounce, screen))
                draw->AddCircleFilled(screen, 4.f, color, 12);
        }
    }
    if (C_GET(bool, Vars.grenade_landing_marker))
    {
        ImVec2 landing{};
        if (D::WorldToScreen(points.back(), landing))
        {
            draw->AddCircle(landing, 8.f, IM_COL32(0, 0, 0, 230), 24, 3.f);
            draw->AddCircle(landing, 8.f, color, 24, 1.5f);
        }
    }
}

void DrawNativeWorldEntities(CGameEntitySystem* entities, C_CSPlayerPawn* localPawn,
    ImDrawList* draw)
{
    const bool drawWeapons = C_GET(bool, Vars.dropped_weapon_esp);
    const bool drawSmoke = C_GET(bool, Vars.smoke_duration_timer);
    const bool drawMolotov = C_GET(bool, Vars.molotov_expiration_timer);
    const bool drawBomb = C_GET(bool, Vars.planted_bomb_timer);
    const bool trackBombChams = C_GET(bool, Vars.chams_bomb);
    static C_BaseEntity* cachedBombTarget = nullptr;
    static std::uint64_t nextBombOnlyScan = 0;
    if ((!drawWeapons && !drawSmoke && !drawMolotov && !drawBomb && !trackBombChams) ||
        entities == nullptr || localPawn == nullptr || draw == nullptr) {
        cachedBombTarget = nullptr;
        NativeChams::UpdateBombTarget(nullptr);
        return;
    }
    const bool bombOnlyScan = trackBombChams && !drawWeapons && !drawSmoke &&
        !drawMolotov && !drawBomb;
    const std::uint64_t nowTicks = SDL_GetTicks();
    if (bombOnlyScan && nowTicks < nextBombOnlyScan) {
        NativeChams::UpdateBombTarget(cachedBombTarget);
        return;
    }
    // Bomb-only chams does not need thousands of entity/name reads at the
    // render rate. A 10 Hz refresh reacts within 100 ms while keeping the
    // present hook light; timer/ESP modes still refresh every frame.
    if (bombOnlyScan)
        nextBombOnlyScan = nowTicks + 100;
    if (!trackBombChams)
        cachedBombTarget = nullptr;
    NativeChams::UpdateBombTarget(nullptr);

    static const std::uint32_t identityOffset =
        SCHEMA::GetOffset("CEntityInstance->m_pEntity");
    static const std::uint32_t designerNameOffset =
        SCHEMA::GetOffset("CEntityIdentity->m_designerName");
    static const std::uint32_t ownerOffset =
        SCHEMA::GetOffset("C_BaseEntity->m_hOwnerEntity");
    static const std::uint32_t sceneNodeOffset =
        SCHEMA::GetOffset("C_BaseEntity->m_pGameSceneNode");
    static const std::uint32_t sceneOriginOffset =
        SCHEMA::GetOffset("CGameSceneNode->m_vecAbsOrigin");
    static const std::uint32_t smokeBeginOffset =
        SCHEMA::GetOffset("C_SmokeGrenadeProjectile->m_nSmokeEffectTickBegin");
    static const std::uint32_t infernoBeginOffset =
        SCHEMA::GetOffset("C_Inferno->m_nFireEffectTickBegin");
    static const std::uint32_t infernoLifetimeOffset =
        SCHEMA::GetOffset("C_Inferno->m_nFireLifetime");
    static const std::uint32_t bombTickingOffset =
        SCHEMA::GetOffset("C_PlantedC4->m_bBombTicking");
    static const std::uint32_t bombDefusedOffset =
        SCHEMA::GetOffset("C_PlantedC4->m_bBombDefused");
    static const std::uint32_t bombBlowOffset =
        SCHEMA::GetOffset("C_PlantedC4->m_flC4Blow");
    static const std::uint32_t bombBeingDefusedOffset =
        SCHEMA::GetOffset("C_PlantedC4->m_bBeingDefused");
    static const std::uint32_t bombDefuseOffset =
        SCHEMA::GetOffset("C_PlantedC4->m_flDefuseCountDown");

    if (identityOffset == 0 || designerNameOffset == 0 ||
        sceneNodeOffset == 0 || sceneOriginOffset == 0) {
        cachedBombTarget = nullptr;
        return;
    }

    const float currentTime = I::GlobalVars != nullptr &&
        std::isfinite(I::GlobalVars->flCurtime) ? I::GlobalVars->flCurtime : 0.f;
    const float tickInterval = I::GlobalVars != nullptr &&
        I::GlobalVars->flIntervalPerTick > 0.001f && I::GlobalVars->flIntervalPerTick < 0.1f
        ? I::GlobalVars->flIntervalPerTick : (1.f / 64.f);
    const ImU32 color = C_GET(ColorPickerVar_t, Vars.world_esp_color).colValue.GetU32();
    C_BaseEntity* trackedBomb = nullptr;

    // Native entity indices are bucketed in groups of 512. World gameplay
    // entities stay in the low buckets; a bounded scan avoids relying on the
    // patch-sensitive highest-index field.
    for (int index = 1; index < 4096; ++index)
    {
        C_BaseEntity* entity = entities->Get<C_BaseEntity>(index);
        if (entity == nullptr || entity == localPawn)
            continue;
        CEntityIdentity* identity = nullptr;
        const char* rawDesignerName = nullptr;
        char designerName[96]{};
        if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + identityOffset, identity) ||
            identity == nullptr || !SafeNativeRead(
                reinterpret_cast<const std::uint8_t*>(identity) + designerNameOffset, rawDesignerName) ||
            rawDesignerName == nullptr)
            continue;
        for (std::size_t character = 0; character + 1 < sizeof(designerName); ++character)
        {
            if (!SafeNativeRead(rawDesignerName + character, designerName[character]))
            {
                designerName[0] = '\0';
                break;
            }
            if (designerName[character] == '\0')
                break;
            if (!std::isprint(static_cast<unsigned char>(designerName[character])))
            {
                designerName[0] = '\0';
                break;
            }
        }
        if (designerName[0] == '\0')
            continue;

        const std::string_view name(designerName);
        if (trackBombChams) {
            if (name.find("planted_c4") != std::string_view::npos)
                trackedBomb = entity;
            else if (trackedBomb == nullptr && name == "weapon_c4")
                trackedBomb = entity;
        }

        CGameSceneNode* scene = nullptr;
        if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + sceneNodeOffset, scene) ||
            scene == nullptr)
            continue;
        Vector_t origin{};
        if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(scene) + sceneOriginOffset, origin) ||
            !origin.IsValid())
            continue;
        ImVec2 screen{};
        if (!D::WorldToScreen(origin, screen))
            continue;

        if (drawWeapons && ownerOffset != 0 && name.starts_with("weapon_"))
        {
            CBaseHandle owner{};
            if (!SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + ownerOffset, owner) ||
                !owner.IsValid())
            {
                std::string weaponName;
                int maximumAmmo = 0;
                if (!GetWeaponName(reinterpret_cast<C_CSWeaponBase*>(entity), weaponName, &maximumAmmo) ||
                    weaponName.empty())
                    weaponName.assign(name.substr(7));
                DrawOutlinedText(draw, screen, weaponName.c_str(), color, IM_COL32(0, 0, 0, 230));
            }
        }
        else if (drawSmoke && name.find("smokegrenade_projectile") != std::string_view::npos &&
            smokeBeginOffset != 0)
        {
            int beginTick = 0;
            if (SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + smokeBeginOffset, beginTick) &&
                beginTick > 0)
            {
                const float remaining = std::clamp(20.f -
                    (currentTime - static_cast<float>(beginTick) * tickInterval), 0.f, 20.f);
                if (remaining > 0.f)
                {
                    char text[48]{};
                    std::snprintf(text, sizeof(text), "SMOKE %.1fs", remaining);
                    DrawOutlinedText(draw, screen, text, color, IM_COL32(0, 0, 0, 230));
                }
            }
        }
        else if (drawMolotov && (name == "inferno" || name.find("inferno") != std::string_view::npos) &&
            infernoBeginOffset != 0)
        {
            int beginTick = 0;
            int lifetimeTicks = 0;
            if (SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + infernoBeginOffset, beginTick) &&
                SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + infernoLifetimeOffset, lifetimeTicks) &&
                beginTick > 0)
            {
                float lifetime = static_cast<float>(lifetimeTicks) * tickInterval;
                if (!std::isfinite(lifetime) || lifetime < 0.5f || lifetime > 30.f)
                    lifetime = 7.f;
                const float remaining = std::clamp(lifetime -
                    (currentTime - static_cast<float>(beginTick) * tickInterval), 0.f, lifetime);
                if (remaining > 0.f)
                {
                    char text[48]{};
                    std::snprintf(text, sizeof(text), "FIRE %.1fs", remaining);
                    DrawOutlinedText(draw, screen, text, color, IM_COL32(0, 0, 0, 230));
                }
            }
        }
        else if (drawBomb && name.find("planted_c4") != std::string_view::npos &&
            bombTickingOffset != 0 && bombBlowOffset != 0)
        {
            std::uint8_t ticking = 0, defused = 0, beingDefused = 0;
            float blowTime = 0.f, defuseTime = 0.f;
            const bool valid = SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + bombTickingOffset, ticking) &&
                SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + bombBlowOffset, blowTime) &&
                (bombDefusedOffset == 0 || SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + bombDefusedOffset, defused));
            if (valid && ticking == 1 && defused == 0 && std::isfinite(blowTime))
            {
                const float remaining = std::max(0.f, blowTime - currentTime);
                bool hasDefuse = bombBeingDefusedOffset != 0 && bombDefuseOffset != 0 &&
                    SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + bombBeingDefusedOffset, beingDefused) &&
                    SafeNativeRead(reinterpret_cast<const std::uint8_t*>(entity) + bombDefuseOffset, defuseTime) &&
                    beingDefused == 1 && std::isfinite(defuseTime);
                char text[96]{};
                if (hasDefuse)
                    std::snprintf(text, sizeof(text), "C4 %.1fs | DEFUSE %.1fs", remaining,
                        std::max(0.f, defuseTime - currentTime));
                else
                    std::snprintf(text, sizeof(text), "C4 %.1fs", remaining);
                DrawOutlinedText(draw, screen, text, color, IM_COL32(0, 0, 0, 230));
            }
        }
    }
    cachedBombTarget = trackedBomb;
    NativeChams::UpdateBombTarget(cachedBombTarget);
}
}

std::uint16_t LinuxNativeEsp::GetActiveSkinWeaponDefinition()
{
    return active_skin_weapon_definition;
}

const char* LinuxNativeEsp::GetActiveSkinWeaponName()
{
    switch (active_skin_weapon_definition)
    {
    case 1: return "Desert Eagle";
    case 2: return "Dual Berettas";
    case 3: return "Five-SeveN";
    case 4: return "Glock-18";
    case 7: return "AK-47";
    case 8: return "AUG";
    case 9: return "AWP";
    case 10: return "FAMAS";
    case 11: return "G3SG1";
    case 13: return "Galil AR";
    case 14: return "M249";
    case 16: return "M4A4";
    case 17: return "MAC-10";
    case 19: return "P90";
    case 23: return "MP5-SD";
    case 24: return "UMP-45";
    case 25: return "XM1014";
    case 26: return "PP-Bizon";
    case 27: return "MAG-7";
    case 28: return "Negev";
    case 29: return "Sawed-Off";
    case 30: return "Tec-9";
    case 31: return "Zeus x27";
    case 32: return "P2000";
    case 33: return "MP7";
    case 34: return "MP9";
    case 35: return "Nova";
    case 36: return "P250";
    case 38: return "SCAR-20";
    case 39: return "SG 553";
    case 40: return "SSG 08";
    case 42: return "CT knife";
    case 43: return "Flashbang";
    case 44: return "HE grenade";
    case 45: return "Smoke grenade";
    case 46: return "Molotov";
    case 47: return "Decoy grenade";
    case 48: return "Incendiary grenade";
    case 49: return "C4";
    case 59: return "T knife";
    case 60: return "M4A1-S";
    case 61: return "USP-S";
    case 63: return "CZ75-Auto";
    case 64: return "R8 Revolver";
    default: return "Weapon";
    }
}

const char* LinuxNativeEsp::GetSkinRuntimeStatus()
{
    switch (native_skin_runtime_status)
    {
    case NativeSkinRuntimeStatus::WaitingForPlayer: return "Waiting for a live player pawn.";
    case NativeSkinRuntimeStatus::MissingSchema: return "Blocked: required live schema fields are missing.";
    case NativeSkinRuntimeStatus::WaitingForWeapon: return "Waiting for an active weapon.";
    case NativeSkinRuntimeStatus::Disabled: return "Skin changer is disabled.";
    case NativeSkinRuntimeStatus::ProfileDisabled: return "Ready; this weapon has no enabled override.";
    case NativeSkinRuntimeStatus::PaintKitRequired: return "Set a paint kit ID greater than zero.";
    case NativeSkinRuntimeStatus::MissingRegenerator: return "Blocked: this CS2 build's native refresh callback was not found.";
    case NativeSkinRuntimeStatus::Ready: return "Ready; override is active and monitored for resets.";
    case NativeSkinRuntimeStatus::Refreshed: return "Native weapon materials refreshed.";
    }
    return "Unknown skin changer state.";
}

bool LinuxNativeEsp::IsSkinRegeneratorReady()
{
    return native_skin_regenerator_ready;
}

void LinuxNativeEsp::RequestSkinRefresh()
{
    native_skin_refresh_requested = true;
}

void LinuxNativeEsp::Render()
{
    // Every present is a new entity generation. Clear last frame's raw target
    // identities before any early return (menu/loading/disconnect) can leave
    // chams matching recycled entity addresses.
    NativeChams::UpdateTargets(nullptr, 0, nullptr);
    NativeChams::UpdateBombTarget(nullptr);
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

    static std::uint64_t nextInputHookRetry = 0;
    if (!IsNativeInputHookInstalled() && SDL_GetTicks() >= nextInputHookRetry)
    {
        nextInputHookRetry = SDL_GetTicks() + 2000;
        RefreshNativeInputObject();
        InstallNativeInputHook();
    }

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
    ApplySmokeRemoval(entities, local_pawn);
    ApplyCameraAndRemovals(local_pawn);
    ApplyNativeMovement(local_pawn);
    DrawLegitFov();
    native_aim_command_queued = false;
    SetNativeCombatInput(false, false, false, false);
    if (C_GET(bool, Vars.rage_enable))
        ApplyNativeRage(entities, local_controller, local_pawn);
    else
        ApplyLegitAim(entities, local_controller, local_pawn);
    ApplyNativeTriggerAndRecoil(entities, local_controller, local_pawn);
    ApplyNativeAntiAim(local_pawn);
    ApplyNativeSkin(entities, local_controller, local_pawn);
    NativeChams::UpdateColors(
        C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue,
        C_GET(ColorPickerVar_t, Vars.colVisualChamsIgnoreZ).colValue);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (draw == nullptr)
        return;

    DrawNativeGrenadeTrajectory(entities, local_pawn, draw);
    DrawNativeWorldEntities(entities, local_pawn, draw);

    CGameSceneNode* localScene = local_pawn->GetGameSceneNode();
    const Vector_t localOrigin = localScene != nullptr ? localScene->GetAbsOrigin() : Vector_t{};
    float localYaw = native_view_angles != nullptr ? native_view_angles->y : 0.f;
    static const std::uint32_t localViewAngleOffset = SCHEMA::GetOffset("C_BasePlayerPawn->v_angle");
    if (localViewAngleOffset != 0)
    {
        QAngle_t pawnView{};
        if (SafeNativeRead(reinterpret_cast<const std::uint8_t*>(local_pawn) + localViewAngleOffset,
                pawnView) && pawnView.IsValid() && std::fabs(pawnView.y) <= 360.f)
            localYaw = pawnView.y;
    }

    int found_entities = 0;
    int found_pawns = 0;
    int alive_pawns = 0;
    int enemy_pawns = 0;
    int projected_pawns = 0;
    int drawn = 0;
    int armorFlagsDrawn = 0;
    int kitFlagsDrawn = 0;
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
        Vector_t head{};
        if (!GetLiveBonePosition(scene, 6, head))
            head = Vector_t(feet.x, feet.y, feet.z + 72.f);
        else
            head.z += 8.f;
        ImVec2 feet_screen{}, head_screen{};
        const bool projected = D::WorldToScreen(feet, feet_screen) && D::WorldToScreen(head, head_screen);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const bool onScreen = projected && feet_screen.x >= 0.f && feet_screen.x <= display.x &&
            feet_screen.y >= 0.f && feet_screen.y <= display.y && head_screen.x >= 0.f &&
            head_screen.x <= display.x && head_screen.y >= 0.f && head_screen.y <= display.y;
        if (!onScreen)
        {
            if (enabled && C_GET(bool, Vars.esp_offscreen_arrows))
                DrawOffscreenArrow(draw, localOrigin, feet, localYaw,
                    C_GET(ColorPickerVar_t, Vars.esp_detail_color).colValue.GetU32());
            continue;
        }
        ++projected_pawns;

        const float height = feet_screen.y - head_screen.y;
        if (height < 4.f || height > ImGui::GetIO().DisplaySize.y * 1.5f)
            continue;
        const float width = height * 0.45f;
        const ImVec2 min(head_screen.x - width * 0.5f, head_screen.y);
        const ImVec2 max(head_screen.x + width * 0.5f, feet_screen.y);

        if (!enabled)
            continue;

		if (C_GET(bool, Vars.esp_box_fill))
		{
			if (C_GET(bool, Vars.esp_box_fill_gradient))
			{
				const ImU32 top = C_GET(ColorPickerVar_t, Vars.esp_box_fill_top_color).colValue.GetU32();
				const ImU32 bottom = C_GET(ColorPickerVar_t, Vars.esp_box_fill_bottom_color).colValue.GetU32();
				draw->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
			}
			else
				draw->AddRectFilled(min, max,
					C_GET(ColorPickerVar_t, Vars.esp_box_fill_color).colValue.GetU32(),
					C_GET(FrameOverlayVar_t, Vars.overlayBox).flRounding);
		}

        if (const auto& box = C_GET(FrameOverlayVar_t, Vars.overlayBox); box.bEnable)
        {
            if (C_GET(int, Vars.esp_box_style) == VISUAL_OVERLAY_BOX_CORNERS)
            {
                DrawCornerBox(draw, min - ImVec2(1.f, 1.f), max + ImVec2(1.f, 1.f),
                    box.colOutline.GetU32(), box.flThickness + 2.f);
                DrawCornerBox(draw, min, max, box.colPrimary.GetU32(), box.flThickness);
            }
            else
            {
                draw->AddRect(min - ImVec2(1.f, 1.f), max + ImVec2(1.f, 1.f), box.colOutline.GetU32(),
                              box.flRounding, 0, box.flThickness + 2.f);
                draw->AddRect(min, max, box.colPrimary.GetU32(), box.flRounding, 0, box.flThickness);
            }
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
            if (healthConfig.bShowValue && health_value < 100)
            {
                char value[8];
                std::snprintf(value, sizeof(value), "%d", health_value);
                const ImVec2 size = ImGui::CalcTextSize(value);
                DrawOutlinedText(draw,
                    ImVec2(min.x - barWidth - size.x - 5.f, max.y - height * health - size.y * 0.5f),
                    value, IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 230));
            }
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

        float bottomOffset = 3.f;
        if (C_GET(bool, Vars.esp_armor_bar) && pawn->GetArmorValue() > 0)
        {
            const float factor = std::clamp(static_cast<float>(pawn->GetArmorValue()) / 100.f, 0.f, 1.f);
            draw->AddRectFilled(ImVec2(min.x, max.y + bottomOffset),
                ImVec2(max.x, max.y + bottomOffset + 4.f), IM_COL32(0, 0, 0, 220));
            draw->AddRectFilled(ImVec2(min.x + 1.f, max.y + bottomOffset + 1.f),
                ImVec2(min.x + 1.f + (width - 2.f) * factor, max.y + bottomOffset + 3.f),
                C_GET(ColorPickerVar_t, Vars.esp_armor_color).colValue.GetU32());
            bottomOffset += 6.f;
        }

        const unsigned int flags = C_GET(unsigned int, Vars.pEspFlags);
        std::string weaponName;
        int ammo = 0, maximumAmmo = 0;
        C_CSWeaponBase* activeWeapon = nullptr;
        const bool needWeapon = C_GET(TextOverlayVar_t, Vars.Weaponesp).bEnable ||
            C_GET(BarOverlayVar_t, Vars.AmmoBar).bEnable ||
            (flags & (FLAGS_PLANTING | FLAGS_RELOADING | FLAGS_BOMB_CARRIER)) != 0;
        const bool hasWeapon = needWeapon &&
            GetWeaponDisplay(entities, pawn, weaponName, ammo, maximumAmmo, &activeWeapon);
        if (hasWeapon)
        {
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
                const char glyph = GetWeaponIcon(weaponName);
                if (weaponConfig.bIcon && glyph != '\0' && FONT::pEspIcons != nullptr)
                {
                    const char iconText[2] = { glyph, '\0' };
                    const ImVec2 textSize = FONT::pEspIcons->CalcTextSizeA(
                        FONT::pEspIcons->FontSize, FLT_MAX, 0.f, iconText);
                    DrawOutlinedText(draw, FONT::pEspIcons,
                        ImVec2((min.x + max.x - textSize.x) * 0.5f, max.y + bottomOffset),
                        iconText, weaponConfig.colPrimary.GetU32(), weaponConfig.colOutline.GetU32());
                }
                else
                {
                    const ImVec2 textSize = ImGui::CalcTextSize(weaponName.c_str());
                    DrawOutlinedText(draw,
                        ImVec2((min.x + max.x - textSize.x) * 0.5f, max.y + bottomOffset),
                        weaponName.c_str(), weaponConfig.colPrimary.GetU32(), weaponConfig.colOutline.GetU32());
                }
                bottomOffset += ImGui::GetTextLineHeight();
            }
        }

        if (C_GET(bool, Vars.esp_distance))
        {
            char distance[32];
            std::snprintf(distance, sizeof(distance), "%.0f m", feet.DistTo(localOrigin) / 39.37f);
            const ImVec2 textSize = ImGui::CalcTextSize(distance);
            DrawOutlinedText(draw, ImVec2((min.x + max.x - textSize.x) * 0.5f, max.y + bottomOffset),
                distance, C_GET(ColorPickerVar_t, Vars.esp_detail_color).colValue.GetU32(),
                IM_COL32(0, 0, 0, 230));
        }

        if (C_GET(bool, Vars.bSkeleton))
            DrawSkeleton(draw, scene);

        const ImU32 detailColor = C_GET(ColorPickerVar_t, Vars.esp_detail_color).colValue.GetU32();
        if (C_GET(bool, Vars.esp_head_circle))
        {
            Vector_t liveHead{};
            ImVec2 liveHeadScreen{};
            if (GetLiveBonePosition(scene, 5, liveHead) && D::WorldToScreen(liveHead, liveHeadScreen))
            {
                const float radius = std::clamp(width * 0.105f, 3.f, 12.f);
                draw->AddCircle(liveHeadScreen, radius + 1.f, IM_COL32(0, 0, 0, 230), 24, 3.f);
                draw->AddCircle(liveHeadScreen, radius, detailColor, 24, 1.f);
            }
        }
        if (C_GET(bool, Vars.esp_view_direction))
        {
            static const std::uint32_t eyeAnglesOffset = SCHEMA::GetOffset("C_CSPlayerPawn->m_angEyeAngles");
            Vector_t eye{};
            if (eyeAnglesOffset != 0 && GetLiveBonePosition(scene, 5, eye))
            {
                const QAngle_t angles = *reinterpret_cast<const QAngle_t*>(
                    reinterpret_cast<const std::uint8_t*>(pawn) + eyeAnglesOffset);
                constexpr float radians = 3.14159265358979323846f / 180.f;
                const float pitch = angles.x * radians;
                const float yaw = angles.y * radians;
                const float cosinePitch = std::cos(pitch);
                const Vector_t end = eye + Vector_t(cosinePitch * std::cos(yaw),
                    cosinePitch * std::sin(yaw), -std::sin(pitch)) * 65.f;
                ImVec2 eyeScreen{}, endScreen{};
                if (D::WorldToScreen(eye, eyeScreen) && D::WorldToScreen(end, endScreen))
                {
                    draw->AddLine(eyeScreen, endScreen, IM_COL32(0, 0, 0, 230), 3.f);
                    draw->AddLine(eyeScreen, endScreen, detailColor, 1.f);
                }
            }
        }
        if (C_GET(bool, Vars.esp_snaplines))
        {
            const ImVec2 start(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y - 1.f);
            draw->AddLine(start, feet_screen, IM_COL32(0, 0, 0, 230), 3.f);
            draw->AddLine(start, feet_screen, detailColor, 1.f);
        }

        float flagOffset = 0.f;
        const auto drawFlag = [&](const char* text, ImU32 color) {
            DrawOutlinedText(draw, ImVec2(max.x + 4.f, min.y + flagOffset), text,
                color, IM_COL32(0, 0, 0, 230));
            flagOffset += ImGui::GetTextLineHeight();
        };
        if ((flags & FLAGS_ARMOR) != 0 && pawn->GetArmorValue() > 0)
        {
            const char* armor = controller->m_bPawnHasHelmet() ? "HK" : "K";
            const auto& config = C_GET(TextOverlayVar_t, Vars.HKFlag);
            drawFlag(armor, config.colPrimary.GetU32());
            ++armorFlagsDrawn;
        }
        if ((flags & FLAGS_DEFUSER) != 0 && controller->m_bPawnHasDefuser())
        {
            const auto& config = C_GET(TextOverlayVar_t, Vars.KitFlag);
            drawFlag("KIT", config.colPrimary.GetU32());
            ++kitFlagsDrawn;
        }
        static const std::uint32_t scopedOffset =
            SCHEMA::GetOffset("C_CSPlayerPawnBase->m_bIsScoped");
        static const std::uint32_t defusingOffset =
            SCHEMA::GetOffset("C_CSPlayerPawnBase->m_bIsDefusing");
        std::uint8_t scopedRaw = 0;
        std::uint8_t defusingRaw = 0;
        const bool scopedResolved = scopedOffset != 0 && SafeNativeRead(
            reinterpret_cast<const std::uint8_t*>(pawn) + scopedOffset, scopedRaw) && scopedRaw <= 1;
        const bool defusingResolved = defusingOffset != 0 && SafeNativeRead(
            reinterpret_cast<const std::uint8_t*>(pawn) + defusingOffset, defusingRaw) && defusingRaw <= 1;
        static bool loggedScopedFailure = false;
        static bool loggedDefusingFailure = false;
        if ((flags & FLAGS_SCOPED) != 0 && !scopedResolved && !loggedScopedFailure)
        {
            EspLog("[ESP flags] scoped unavailable offset=0x%x raw=%u", scopedOffset, scopedRaw);
            loggedScopedFailure = true;
        }
        if ((flags & FLAGS_DEFUSING) != 0 && !defusingResolved && !loggedDefusingFailure)
        {
            EspLog("[ESP flags] defusing unavailable offset=0x%x raw=%u", defusingOffset, defusingRaw);
            loggedDefusingFailure = true;
        }
        if ((flags & FLAGS_SCOPED) != 0 && scopedResolved && scopedRaw == 1)
            drawFlag("SCOPED", IM_COL32(100, 170, 255, 255));
        if ((flags & FLAGS_FLASHED) != 0 && pawn->GetFlashDuration() > 0.05f)
            drawFlag("FLASHED", IM_COL32(255, 220, 80, 255));
        if ((flags & FLAGS_DEFUSING) != 0 && defusingResolved && defusingRaw == 1)
            drawFlag("DEFUSING", IM_COL32(70, 150, 255, 255));

        static const std::uint32_t reloadingOffset = SCHEMA::GetOffset("C_CSWeaponBase->m_bInReload");
        static const std::uint32_t startedArmingOffset = SCHEMA::GetOffset("C_C4->m_bStartedArming");
        static const std::uint32_t plantingViaUseOffset = SCHEMA::GetOffset("C_C4->m_bIsPlantingViaUse");
        bool startedArming = false;
        bool plantingViaUse = false;
        if (activeWeapon != nullptr && weaponName == "c4")
        {
            if (startedArmingOffset != 0)
                SafeNativeRead(reinterpret_cast<const std::uint8_t*>(activeWeapon) + startedArmingOffset,
                    startedArming);
            if (plantingViaUseOffset != 0)
                SafeNativeRead(reinterpret_cast<const std::uint8_t*>(activeWeapon) + plantingViaUseOffset,
                    plantingViaUse);
        }
        const bool planting = activeWeapon != nullptr && weaponName == "c4" &&
            (startedArming || plantingViaUse);
        if ((flags & FLAGS_PLANTING) != 0 && planting)
            drawFlag("PLANTING", IM_COL32(255, 145, 60, 255));
        bool reloading = false;
        if (activeWeapon != nullptr && reloadingOffset != 0)
            SafeNativeRead(reinterpret_cast<const std::uint8_t*>(activeWeapon) + reloadingOffset, reloading);
        if ((flags & FLAGS_RELOADING) != 0 && reloading)
            drawFlag("RELOADING", IM_COL32(235, 235, 235, 255));
        if ((flags & FLAGS_BOMB_CARRIER) != 0 && PlayerHasC4(entities, pawn))
            drawFlag("C4", IM_COL32(255, 85, 85, 255));
        if ((flags & FLAGS_PING) != 0)
        {
            char ping[24];
            std::snprintf(ping, sizeof(ping), "%u ms", controller->GetPing());
            drawFlag(ping, IM_COL32(190, 190, 190, 255));
        }
        ++drawn;
    }
    NativeChams::UpdateTargets(chamsTargets.data(), chamsTargetCount, local_pawn);

    if ((++frame_counter % 300) == 0)
        EspLog("[ESP] local=%p entities=%d pawns=%d alive=%d enemies=%d projected=%d drawn=%d flagmask=0x%x armorflags=%d kitflags=%d",
               local_controller, found_entities, found_pawns, alive_pawns, enemy_pawns,
               projected_pawns, drawn, C_GET(unsigned int, Vars.pEspFlags),
               armorFlagsDrawn, kitFlagsDrawn);
}

#endif
