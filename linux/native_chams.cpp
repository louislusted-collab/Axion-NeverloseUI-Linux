#ifdef __linux__

#include "native_chams.h"

#include "../cstrike/core/interfaces.h"
#include "../cstrike/core/variables.h"
#include "../cstrike/sdk/entity.h"
#include "../cstrike/sdk/datatypes/K3V.h"
#include "../cstrike/sdk/interfaces/cgameentitysystem.h"
#include "../cstrike/sdk/interfaces/imaterialsystem.h"
#include "../cstrike/utilities/memory.h"

#include <funchook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <string_view>
#include <vector>

namespace NativeChams
{
namespace
{
using DrawArrayFn = void(*)(void*, void*, void*, int, void*, void*, void*, void*);

constexpr std::size_t kMeshStride = 0x70;
constexpr std::size_t kSceneAnimatableOffset = 0x18;
constexpr std::size_t kMaterialOffset = 0x20;
constexpr std::size_t kColorOffset = 0x40;

std::array<std::atomic<C_CSPlayerPawn*>, 128> targets{};
std::atomic<C_CSPlayerPawn*> localTarget{};
std::atomic<C_BaseEntity*> bombTarget{};
funchook_t* hook = nullptr;
DrawArrayFn original = nullptr;
struct MaterialPair
{
    material2_t* visible = nullptr;
    material2_t* hidden = nullptr;
};

enum class MaterialStyle : std::size_t
{
    Flat,
    Metallic,
    Glow,
    Glass,
    Wireframe,
    Count
};

std::array<MaterialPair, static_cast<std::size_t>(MaterialStyle::Count)> materials{};
std::array<material2_t*, static_cast<std::size_t>(MaterialStyle::Count)> localMaterials{};
bool materialLoadAttempted = false;
bool colorsInitialized = false;
Color_t activeVisibleColor{255, 255, 255, 255};
Color_t activeHiddenColor{255, 255, 255, 255};
Color_t pendingVisibleColor{255, 255, 255, 255};
Color_t pendingHiddenColor{255, 255, 255, 255};
float activeGlowIntensity = -1.f;
float pendingGlowIntensity = -1.f;
std::chrono::steady_clock::time_point pendingColorSince{};
unsigned int materialGeneration = 0;

void Log(const char* message, ...)
{
    static FILE* file = std::fopen("/tmp/cs2_chams_debug.log", "a");
    if (file == nullptr)
        return;
    va_list args;
    va_start(args, message);
    std::vfprintf(file, message, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}

material2_t* CreateMaterial(const char* name, MaterialStyle style, bool ignoreDepth,
                            const Color_t& color, float glowIntensity)
{
    if (I::MaterialSystem2 == nullptr)
        return nullptr;

    using LoadKv3Fn = bool(*)(CKeyValues3*, void*, const char*, const KV3IVD_t*, const char*, unsigned int);
    static auto loadKv3 = reinterpret_cast<LoadKv3Fn>(dlsym(
        RTLD_DEFAULT, "_Z7LoadKV3P10KeyValues3P10CUtlStringPKcRK7KV3ID_tS4_j"));
    if (loadKv3 == nullptr)
        return nullptr;

    static constexpr char flatVisibleVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    F_PAINT_VERTEX_COLORS = 1
    g_vColorTint = [1, 1, 1, 1]
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_mask_tga_fde710a5.vtex"
})";
    static constexpr char flatHiddenVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    F_PAINT_VERTEX_COLORS = 1
    F_IGNOREZ = 1
    F_DISABLE_Z_WRITE = 1
    F_DISABLE_Z_BUFFERING = 1
    g_vColorTint = [1, 1, 1, 1]
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_mask_tga_b3f4ec4c.vtex"
})";
    static constexpr char metallicVisibleVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_complex.vfx"
    F_PAINT_VERTEX_COLORS = 1
    F_METALNESS_TEXTURE = 0
    F_ROUGHNESS_TEXTURE = 0
    g_vColorTint = [1, 1, 1, 1]
    g_flMetalness = 1.0
    g_flRoughness = 0.18
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_b3f4ec4c.vtex"
    g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
})";
    static constexpr char metallicHiddenVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_complex.vfx"
    F_PAINT_VERTEX_COLORS = 1
    F_METALNESS_TEXTURE = 0
    F_ROUGHNESS_TEXTURE = 0
    F_IGNOREZ = 1
    F_DISABLE_Z_WRITE = 1
    F_DISABLE_Z_BUFFERING = 1
    g_vColorTint = [1, 1, 1, 1]
    g_flMetalness = 1.0
    g_flRoughness = 0.18
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_b3f4ec4c.vtex"
    g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
})";
    static constexpr char glowVisibleVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_complex.vfx"
    F_SELF_ILLUM = 1
    F_PAINT_VERTEX_COLORS = 1
    F_TRANSLUCENT = 1
    g_vColorTint = [1, 1, 1, 1]
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_b3f4ec4c.vtex"
    g_tSelfIllumMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_flSelfIllumScale = 1.0
    g_flSelfIllumBrightness = 1.0
    g_vSelfIllumTint = [1, 1, 1, 1]
})";
    static constexpr char glowHiddenVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_complex.vfx"
    F_SELF_ILLUM = 1
    F_PAINT_VERTEX_COLORS = 1
    F_TRANSLUCENT = 1
    F_IGNOREZ = 1
    F_DISABLE_Z_WRITE = 1
    F_DISABLE_Z_BUFFERING = 1
    g_vColorTint = [1, 1, 1, 1]
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_b3f4ec4c.vtex"
    g_tSelfIllumMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_flSelfIllumScale = 1.0
    g_flSelfIllumBrightness = 1.0
    g_vSelfIllumTint = [1, 1, 1, 1]
})";
    static constexpr char glassVisibleVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    F_PAINT_VERTEX_COLORS = 1
    F_TRANSLUCENT = 1
    F_BLEND_MODE = 1
    F_DISABLE_Z_WRITE = 1
    g_vColorTint = [1, 1, 1, 1]
    g_flOpacityScale = 0.35
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_b3f4ec4c.vtex"
})";
    static constexpr char glassHiddenVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    F_PAINT_VERTEX_COLORS = 1
    F_TRANSLUCENT = 1
    F_BLEND_MODE = 1
    F_IGNOREZ = 1
    F_DISABLE_Z_WRITE = 1
    F_DISABLE_Z_BUFFERING = 1
    g_vColorTint = [1, 1, 1, 1]
    g_flOpacityScale = 0.35
    g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_b3f4ec4c.vtex"
})";
    static constexpr char wireframeVisibleVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "tools_wireframe.vfx"
    F_UNLIT = 1
    F_WIREFRAME = 1
    F_PAINT_VERTEX_COLORS = 1
    g_vColorTint = [1, 1, 1, 1]
    g_LineThickness = 1.0
    g_DepthBiasAmount = 0.005
})";
    static constexpr char wireframeHiddenVmat[] = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "tools_wireframe.vfx"
    F_UNLIT = 1
    F_WIREFRAME = 1
    F_PAINT_VERTEX_COLORS = 1
    F_IGNOREZ = 1
    F_DISABLE_Z_WRITE = 1
    F_DISABLE_Z_BUFFERING = 1
    g_vColorTint = [1, 1, 1, 1]
    g_LineThickness = 1.0
    g_DepthBiasAmount = 0.005
})";

    const char* vmatTemplate = nullptr;
    if (style == MaterialStyle::Metallic)
        vmatTemplate = ignoreDepth ? metallicHiddenVmat : metallicVisibleVmat;
    else if (style == MaterialStyle::Glow)
        vmatTemplate = ignoreDepth ? glowHiddenVmat : glowVisibleVmat;
    else if (style == MaterialStyle::Glass)
        vmatTemplate = ignoreDepth ? glassHiddenVmat : glassVisibleVmat;
    else if (style == MaterialStyle::Wireframe)
        vmatTemplate = ignoreDepth ? wireframeHiddenVmat : wireframeVisibleVmat;
    else
        vmatTemplate = ignoreDepth ? flatHiddenVmat : flatVisibleVmat;

    std::string vmat(vmatTemplate);
    constexpr char tintPlaceholder[] = "g_vColorTint = [1, 1, 1, 1]";
    const std::size_t tintPosition = vmat.find(tintPlaceholder);
    if (tintPosition != std::string::npos)
    {
        char tint[160];
        std::snprintf(tint, sizeof(tint), "g_vColorTint = [%.6f, %.6f, %.6f, %.6f]",
            static_cast<float>(color.r) / 255.f, static_cast<float>(color.g) / 255.f,
            static_cast<float>(color.b) / 255.f, static_cast<float>(color.a) / 255.f);
        vmat.replace(tintPosition, sizeof(tintPlaceholder) - 1, tint);
    }
    if (style == MaterialStyle::Glow)
    {
        const float shaderIntensity = 0.02f +
            std::clamp(glowIntensity, 0.f, 100.f) * 0.0038f;
        const auto replaceScalar = [&](std::string_view key) {
            const std::size_t position = vmat.find(key);
            if (position == std::string::npos)
                return;
            const std::size_t valueStart = position + key.size();
            const std::size_t valueEnd = vmat.find('\n', valueStart);
            char value[32] = {};
            std::snprintf(value, sizeof(value), "%.4f", shaderIntensity);
            vmat.replace(valueStart, valueEnd - valueStart, value);
        };
        replaceScalar("g_flSelfIllumScale = ");
        replaceScalar("g_flSelfIllumBrightness = ");
    }

    alignas(16) std::array<std::byte, 0x100 + sizeof(CKeyValues3)> storage{};
    auto* kv3 = reinterpret_cast<CKeyValues3*>(storage.data() + 0x100);
    const KV3IVD_t id{name, 0x469806E97412167CULL, 0xE73790B53EE6F2AFULL};
    if (!loadKv3(kv3, nullptr, vmat.c_str(), &id, nullptr, 0))
        return nullptr;

    // The native VMaterialSystem2_001 ABI returns a strong resource binding
    // from vfunc 28. Its first pointer is the IMaterial2 instance.
    auto** binding = MEM::CallVFunc<material2_t**, 28>(
        I::MaterialSystem2, name, kv3, 0U, 1U);
    return binding != nullptr ? *binding : nullptr;
}

bool RebuildMaterials(const Color_t& visibleColor, const Color_t& hiddenColor,
                      float glowIntensity)
{
    const unsigned int generation = ++materialGeneration;
    std::array<MaterialPair, static_cast<std::size_t>(MaterialStyle::Count)> replacement{};
    std::array<material2_t*, static_cast<std::size_t>(MaterialStyle::Count)> localReplacement{};
    static constexpr const char* styleNames[]{"flat", "metallic", "glow", "glass", "wireframe"};
    for (std::size_t index = 0; index < replacement.size(); ++index)
    {
        char visibleName[112], hiddenName[112];
        std::snprintf(visibleName, sizeof(visibleName),
            "materials/axion/player_%s_visible_%u.vmat", styleNames[index], generation);
        std::snprintf(hiddenName, sizeof(hiddenName),
            "materials/axion/player_%s_hidden_%u.vmat", styleNames[index], generation);
        const MaterialStyle style = static_cast<MaterialStyle>(index);
        replacement[index] = {
            CreateMaterial(visibleName, style, false, visibleColor, glowIntensity),
            CreateMaterial(hiddenName, style, true, hiddenColor, glowIntensity)};
        if (index == static_cast<std::size_t>(MaterialStyle::Flat) &&
            (replacement[index].visible == nullptr || replacement[index].hidden == nullptr))
            return false;
        if (replacement[index].visible == nullptr || replacement[index].hidden == nullptr)
        {
            Log("material style=%s failed; falling back to flat", styleNames[index]);
            replacement[index] = replacement[static_cast<std::size_t>(MaterialStyle::Flat)];
        }
        char localName[112];
        std::snprintf(localName, sizeof(localName),
            "materials/axion/local_%s_%u.vmat", styleNames[index], generation);
        localReplacement[index] = CreateMaterial(localName, style, false,
            Color_t(255, 255, 255, 255), glowIntensity);
        if (index == static_cast<std::size_t>(MaterialStyle::Flat) &&
            localReplacement[index] == nullptr)
            return false;
        if (localReplacement[index] == nullptr)
            localReplacement[index] = localReplacement[static_cast<std::size_t>(MaterialStyle::Flat)];
    }

    materials = replacement;
    localMaterials = localReplacement;
    activeVisibleColor = visibleColor;
    activeHiddenColor = hiddenColor;
    colorsInitialized = true;
    activeGlowIntensity = glowIntensity;
    Log("materials generation=%u flat=%p/%p metallic=%p/%p glow=%p/%p glass=%p/%p wire=%p/%p colors=%u,%u,%u/%u,%u,%u",
        generation, materials[0].visible, materials[0].hidden,
        materials[1].visible, materials[1].hidden,
        materials[2].visible, materials[2].hidden,
        materials[3].visible, materials[3].hidden,
        materials[4].visible, materials[4].hidden,
        visibleColor.r, visibleColor.g, visibleColor.b,
        hiddenColor.r, hiddenColor.g, hiddenColor.b);
    return true;
}

void LoadMaterials()
{
    if (materialLoadAttempted)
        return;
    materialLoadAttempted = true;
    const Color_t visible = C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue;
    const Color_t hidden = C_GET(ColorPickerVar_t, Vars.colVisualChamsIgnoreZ).colValue;
    const float glowIntensity = std::clamp(C_GET(float, Vars.chams_glow_intensity), 0.f, 100.f);
    RebuildMaterials(visible, hidden, glowIntensity);
    pendingVisibleColor = visible;
    pendingHiddenColor = hidden;
    pendingGlowIntensity = glowIntensity;
}

bool IsTrackedTarget(const C_BaseEntity* entity)
{
    if (entity == nullptr)
        return false;
    for (const auto& target : targets)
        if (target.load(std::memory_order_relaxed) == entity)
            return true;
    return false;
}

enum class TargetKind
{
    None,
    Enemy,
    Local,
    Bomb
};

TargetKind ResolvesToTarget(C_BaseEntity* entity)
{
    auto* entitySystem = I::GameResourceService->pGameEntitySystem;
    for (int depth = 0; entity != nullptr && depth < 4; ++depth)
    {
        if (IsTrackedTarget(entity))
            return TargetKind::Enemy;
        if (localTarget.load(std::memory_order_relaxed) == entity)
            return TargetKind::Local;
        if (bombTarget.load(std::memory_order_relaxed) == entity)
            return TargetKind::Bomb;
        const CBaseHandle ownerHandle = entity->GetOwnerHandle();
        C_BaseEntity* owner = entitySystem->Get<C_BaseEntity>(ownerHandle);
        if (owner == entity)
            break;
        entity = owner;
    }
    return TargetKind::None;
}

enum class LocalMaterialKind
{
    None,
    Arms,
    Sleeves,
    Knife,
    Weapon,
    Grenade,
    Bomb,
};

LocalMaterialKind ClassifyLocalMaterial(material2_t* material)
{
    if (material == nullptr)
        return LocalMaterialKind::None;
    const char* rawName = material->get_name();
    if (rawName == nullptr)
        return LocalMaterialKind::None;
    const std::string_view name(rawName);
    const auto contains = [&](std::string_view token) { return name.find(token) != std::string_view::npos; };

    const bool sleeve = contains("sleeve");
    const bool arms = contains("arms") || contains("glove") || contains("hands");
    const bool bomb = contains("c4") || contains("ied") || contains("bomb");
    const bool grenade = contains("grenade") || contains("flashbang") || contains("molotov") ||
        contains("incendiary") || contains("smoke") || contains("decoy") || contains("eq_");
    const bool knife = contains("knife") || contains("bayonet") || contains("kukri");
    const bool viewWeapon = contains("weapons/v_models") || contains("weapons\\v_models") ||
        contains("v_models/");

    if (sleeve)
        return LocalMaterialKind::Sleeves;
    if (arms)
        return LocalMaterialKind::Arms;
    if (bomb)
        return LocalMaterialKind::Bomb;
    if (grenade)
        return LocalMaterialKind::Grenade;
    if (knife)
        return LocalMaterialKind::Knife;
    return viewWeapon ? LocalMaterialKind::Weapon : LocalMaterialKind::None;
}

bool IsLocalMaterialEnabled(LocalMaterialKind kind)
{
    switch (kind)
    {
    case LocalMaterialKind::Arms: return C_GET(bool, Vars.chams_arms);
    case LocalMaterialKind::Sleeves: return C_GET(bool, Vars.chams_sleeves);
    case LocalMaterialKind::Knife: return C_GET(bool, Vars.chams_knife);
    case LocalMaterialKind::Weapon: return C_GET(bool, Vars.chams_held_weapon);
    case LocalMaterialKind::Grenade: return C_GET(bool, Vars.chams_grenades);
    case LocalMaterialKind::Bomb: return C_GET(bool, Vars.chams_bomb);
    default: return false;
    }
}

Color_t GetLocalMaterialColor(LocalMaterialKind kind)
{
    if (kind == LocalMaterialKind::Arms)
        return C_GET(ColorPickerVar_t, Vars.chams_arms_color).colValue;
    if (kind == LocalMaterialKind::Sleeves)
        return C_GET(ColorPickerVar_t, Vars.chams_sleeves_color).colValue;
    if (kind == LocalMaterialKind::Knife)
        return C_GET(ColorPickerVar_t, Vars.chams_knife_color).colValue;
    return C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue;
}

enum class SceneMaterialKind
{
    None,
    World,
    Sky,
};

SceneMaterialKind ClassifySceneMaterial(material2_t* material)
{
    if (material == nullptr)
        return SceneMaterialKind::None;
    const char* rawName = material->get_name();
    if (rawName == nullptr || *rawName == '\0')
        return SceneMaterialKind::None;
    const std::string_view name(rawName);
    const auto contains = [&](std::string_view token) {
        return name.find(token) != std::string_view::npos;
    };
    if (contains("skybox") || contains("/sky/") || contains("materials/sky") ||
        contains("sky_") || contains("skybox_"))
        return SceneMaterialKind::Sky;
    if (contains("characters") || contains("weapons") || contains("player") ||
        contains("panorama") || contains("particle") || contains("effects") ||
        contains("decals") || contains("hud"))
        return SceneMaterialKind::None;
    if (contains("materials/world") || contains("materials/maps") || contains("maps/") ||
        contains("materials/models/props") || contains("materials/props") ||
        contains("materials/nature") || contains("materials/ground"))
        return SceneMaterialKind::World;
    return SceneMaterialKind::None;
}

TargetKind GetTargetKind(void* mesh)
{
    if (mesh == nullptr || I::GameResourceService == nullptr ||
        I::GameResourceService->pGameEntitySystem == nullptr)
        return TargetKind::None;
    auto* sceneAnimatable = *reinterpret_cast<std::uint8_t**>(
        reinterpret_cast<std::uint8_t*>(mesh) + kSceneAnimatableOffset);
    if (sceneAnimatable == nullptr)
        return TargetKind::None;
    // Player LODs and attachment meshes do not all use the same owner slot.
    // Only accept handles that resolve back to a tracked live enemy, so this
    // broader layout probe cannot turn unrelated world meshes into targets.
    for (std::size_t ownerOffset = 0x98U; ownerOffset <= 0xE0U; ownerOffset += 8U)
    {
        const auto handle = *reinterpret_cast<const CBaseHandle*>(sceneAnimatable + ownerOffset);
        C_BaseEntity* owner = I::GameResourceService->pGameEntitySystem->Get<C_BaseEntity>(handle);
        const TargetKind kind = ResolvesToTarget(owner);
        if (kind != TargetKind::None)
            return kind;
    }
    return TargetKind::None;
}

void SetMeshColor(void* mesh, const Color_t& color)
{
    std::memcpy(reinterpret_cast<std::uint8_t*>(mesh) + kColorOffset, &color, sizeof(color));
}

void DrawArray(void* descriptor, void* renderContext, void* meshArray, int count,
               void* sceneView, void* sceneLayer, void* drawContext, void* frameStats)
{
    const bool localChams = C_GET(bool, Vars.chams_arms) || C_GET(bool, Vars.chams_sleeves) ||
        C_GET(bool, Vars.chams_held_weapon) || C_GET(bool, Vars.chams_knife) ||
        C_GET(bool, Vars.chams_grenades) ||
        C_GET(bool, Vars.chams_bomb);
    const bool sceneModulation = C_GET(bool, Vars.world_color_enable) ||
        C_GET(bool, Vars.sky_color_enable);
    if (original == nullptr || meshArray == nullptr || count <= 0 ||
        (!C_GET(bool, Vars.bVisualChams) && !localChams && !sceneModulation))
    {
        if (original != nullptr)
            original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);
        return;
    }

    struct Backup
    {
        void* mesh;
        material2_t* material;
        Color_t color;
        TargetKind targetKind = TargetKind::None;
        LocalMaterialKind localKind = LocalMaterialKind::None;
    };
    // Far-away/alternate-LOD batches regularly contain more than 128 mesh
    // records. Reuse thread-local storage so scene batches do not allocate two
    // vectors on every render call, while still avoiding cross-thread races.
    const int safeCount = std::min(count, 4096);
    static thread_local std::vector<Backup> changed;
    static thread_local std::vector<Backup> modulated;
    changed.clear();
    modulated.clear();
    if (changed.capacity() < static_cast<std::size_t>(safeCount))
        changed.reserve(static_cast<std::size_t>(safeCount));
    if (sceneModulation && modulated.capacity() < static_cast<std::size_t>(safeCount))
        modulated.reserve(static_cast<std::size_t>(safeCount));
    for (int index = 0; index < safeCount; ++index)
    {
        auto* mesh = reinterpret_cast<std::uint8_t*>(meshArray) + index * kMeshStride;
        material2_t* originalMaterial = *reinterpret_cast<material2_t**>(mesh + kMaterialOffset);
        if (sceneModulation)
        {
            const SceneMaterialKind sceneKind = ClassifySceneMaterial(originalMaterial);
            const bool tintWorld = sceneKind == SceneMaterialKind::World &&
                C_GET(bool, Vars.world_color_enable);
            const bool tintSky = sceneKind == SceneMaterialKind::Sky &&
                C_GET(bool, Vars.sky_color_enable);
            if (tintWorld || tintSky)
            {
                auto& backup = modulated.emplace_back();
                backup.mesh = mesh;
                backup.material = originalMaterial;
                std::memcpy(&backup.color, mesh + kColorOffset, sizeof(backup.color));
                SetMeshColor(mesh, tintSky
                    ? C_GET(ColorPickerVar_t, Vars.sky_color).colValue
                    : C_GET(ColorPickerVar_t, Vars.world_color).colValue);
            }
        }
        const TargetKind kind = GetTargetKind(mesh);
        if (kind == TargetKind::None)
            continue;
        if (kind == TargetKind::Enemy && !C_GET(bool, Vars.bVisualChams))
            continue;
        const LocalMaterialKind localKind = kind == TargetKind::Local
            ? ClassifyLocalMaterial(originalMaterial)
            : (kind == TargetKind::Bomb ? LocalMaterialKind::Bomb : LocalMaterialKind::None);
        if ((kind == TargetKind::Local || kind == TargetKind::Bomb) &&
            !IsLocalMaterialEnabled(localKind))
            continue;
        auto& backup = changed.emplace_back();
        backup.mesh = mesh;
        backup.material = originalMaterial;
        std::memcpy(&backup.color, mesh + kColorOffset, sizeof(backup.color));
        backup.targetKind = kind;
        backup.localKind = localKind;
    }

    if (changed.empty())
    {
        original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);
        for (const Backup& backup : modulated)
            SetMeshColor(backup.mesh, backup.color);
        return;
    }

    const int selectedStyle = std::clamp(C_GET(int, Vars.nVisualChamMaterial), 0,
        static_cast<int>(MaterialStyle::Count) - 1);
    const MaterialPair& selectedMaterials = materials[static_cast<std::size_t>(selectedStyle)];
    material2_t* selectedLocalMaterial = localMaterials[static_cast<std::size_t>(selectedStyle)];
    const int changedCount = static_cast<int>(changed.size());

    static std::uint64_t matchedMeshes = 0;
    static std::uint64_t matchedBatches = 0;
    static auto nextStats = std::chrono::steady_clock::now();
    matchedMeshes += changed.size();
    ++matchedBatches;
    const auto statsNow = std::chrono::steady_clock::now();
    if (statsNow >= nextStats)
    {
        Log("coverage batches=%llu meshes=%llu current_batch=%d/%d style=%d through_walls=%d",
            static_cast<unsigned long long>(matchedBatches),
            static_cast<unsigned long long>(matchedMeshes), changedCount, count,
            selectedStyle, C_GET(bool, Vars.bVisualChamsIgnoreZ));
        nextStats = statsNow + std::chrono::seconds(5);
    }

    if (C_GET(bool, Vars.bVisualChamsIgnoreZ) && selectedMaterials.hidden != nullptr)
    {
        for (int index = 0; index < changedCount; ++index)
        {
            if (changed[index].targetKind != TargetKind::Enemy)
                continue;
            auto* mesh = static_cast<std::uint8_t*>(changed[index].mesh);
            *reinterpret_cast<material2_t**>(mesh + kMaterialOffset) = selectedMaterials.hidden;
            SetMeshColor(mesh, Color_t(255, 255, 255, 255));
        }
        // DrawArray's descriptor describes the entire submitted mesh batch.
        // Replaying individual records produces only a thin partial shell, so
        // the no-depth pass must preserve the original array and count.
        original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);
    }

    for (int index = 0; index < changedCount; ++index)
    {
        auto* mesh = static_cast<std::uint8_t*>(changed[index].mesh);
        const bool localMaterial = changed[index].targetKind == TargetKind::Local ||
            changed[index].targetKind == TargetKind::Bomb;
        material2_t* visibleMaterial = localMaterial
            ? selectedLocalMaterial
            : selectedMaterials.visible;
        if (visibleMaterial != nullptr)
            *reinterpret_cast<material2_t**>(mesh + kMaterialOffset) = visibleMaterial;
        SetMeshColor(mesh, localMaterial
            ? GetLocalMaterialColor(changed[index].localKind)
            : Color_t(255, 255, 255, 255));
    }
    original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);

    for (int index = 0; index < changedCount; ++index)
    {
        auto* mesh = static_cast<std::uint8_t*>(changed[index].mesh);
        *reinterpret_cast<material2_t**>(mesh + kMaterialOffset) = changed[index].material;
        SetMeshColor(mesh, changed[index].color);
    }
    for (const Backup& backup : modulated)
        SetMeshColor(backup.mesh, backup.color);
}
}

bool Install()
{
    if (hook != nullptr)
        return true;

    // Create the native KV3 materials before intercepting scene draws. This
    // keeps resource creation out of the render callback and avoids recursively
    // entering DrawArray while MaterialSystem2 is loading the resource.
    LoadMaterials();
    void* drawArray = MEM::FindPattern(SCENESYSTEM_DLL,
        "55 48 89 E5 41 57 41 56 41 55 41 54 4C 63 E1 53 48 81 EC D8 00 00 00 48 89 BD 28 FF FF FF 4C 8B 6D 10 45 85 E4");
    if (drawArray == nullptr)
    {
        Log("DrawArray signature not found");
        return false;
    }
    original = reinterpret_cast<DrawArrayFn>(drawArray);
    hook = funchook_create();
    if (hook == nullptr ||
        funchook_prepare(hook, reinterpret_cast<void**>(&original), reinterpret_cast<void*>(&DrawArray)) != 0 ||
        funchook_install(hook, 0) != 0)
    {
        Log("DrawArray hook install failed target=%p", drawArray);
        if (hook != nullptr)
            funchook_destroy(hook);
        hook = nullptr;
        original = nullptr;
        return false;
    }
    Log("DrawArray hook installed target=%p", drawArray);
    return true;
}

void Destroy()
{
    if (hook == nullptr)
        return;
    funchook_uninstall(hook, 0);
    funchook_destroy(hook);
    hook = nullptr;
    original = nullptr;
    materials = {};
    localMaterials = {};
    materialLoadAttempted = false;
    colorsInitialized = false;
    materialGeneration = 0;
    activeGlowIntensity = -1.f;
    pendingGlowIntensity = -1.f;
    UpdateTargets(nullptr, 0, nullptr);
    UpdateBombTarget(nullptr);
}

void UpdateTargets(C_CSPlayerPawn* const* newTargets, std::size_t count, C_CSPlayerPawn* newLocalTarget)
{
    const std::size_t safeCount = std::min(count, targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index)
        targets[index].store(index < safeCount ? newTargets[index] : nullptr, std::memory_order_relaxed);
    localTarget.store(newLocalTarget, std::memory_order_relaxed);
}

void UpdateBombTarget(C_BaseEntity* newBombTarget)
{
    bombTarget.store(newBombTarget, std::memory_order_relaxed);
}

void UpdateColors(const Color_t& visible, const Color_t& hidden)
{
    if (!materialLoadAttempted || !colorsInitialized)
        return;
    const float glowIntensity = std::clamp(C_GET(float, Vars.chams_glow_intensity), 0.f, 100.f);
    if (visible == activeVisibleColor && hidden == activeHiddenColor &&
        std::fabs(glowIntensity - activeGlowIntensity) < 0.01f)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (visible != pendingVisibleColor || hidden != pendingHiddenColor ||
        std::fabs(glowIntensity - pendingGlowIntensity) >= 0.01f)
    {
        pendingVisibleColor = visible;
        pendingHiddenColor = hidden;
        pendingGlowIntensity = glowIntensity;
        pendingColorSince = now;
        return;
    }
    // Avoid compiling four materials for every intermediate mouse movement in
    // the color picker. Rebuild once the selected color has settled briefly.
    if (now - pendingColorSince < std::chrono::milliseconds(150))
        return;
    if (!RebuildMaterials(pendingVisibleColor, pendingHiddenColor, pendingGlowIntensity))
    {
        pendingColorSince = now;
        Log("material color rebuild failed; retaining previous generation");
    }
}
}

#endif
