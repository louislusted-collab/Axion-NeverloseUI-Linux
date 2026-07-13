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
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

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
    Count
};

std::array<MaterialPair, static_cast<std::size_t>(MaterialStyle::Count)> materials{};
bool materialLoadAttempted = false;

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

material2_t* CreateMaterial(const char* name, MaterialStyle style, bool ignoreDepth)
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

    const char* vmat = nullptr;
    if (style == MaterialStyle::Metallic)
        vmat = ignoreDepth ? metallicHiddenVmat : metallicVisibleVmat;
    else
        vmat = ignoreDepth ? flatHiddenVmat : flatVisibleVmat;

    alignas(16) std::array<std::byte, 0x100 + sizeof(CKeyValues3)> storage{};
    auto* kv3 = reinterpret_cast<CKeyValues3*>(storage.data() + 0x100);
    const KV3IVD_t id{name, 0x469806E97412167CULL, 0xE73790B53EE6F2AFULL};
    if (!loadKv3(kv3, nullptr, vmat, &id, nullptr, 0))
        return nullptr;

    // The native VMaterialSystem2_001 ABI returns a strong resource binding
    // from vfunc 28. Its first pointer is the IMaterial2 instance.
    auto** binding = MEM::CallVFunc<material2_t**, 28>(
        I::MaterialSystem2, name, kv3, 0U, 1U);
    return binding != nullptr ? *binding : nullptr;
}

void LoadMaterials()
{
    if (materialLoadAttempted)
        return;
    materialLoadAttempted = true;
    materials[static_cast<std::size_t>(MaterialStyle::Flat)] = {
        CreateMaterial("materials/axion/player_flat_visible.vmat", MaterialStyle::Flat, false),
        CreateMaterial("materials/axion/player_flat_hidden.vmat", MaterialStyle::Flat, true)};
    materials[static_cast<std::size_t>(MaterialStyle::Metallic)] = {
        CreateMaterial("materials/axion/player_metallic_visible.vmat", MaterialStyle::Metallic, false),
        CreateMaterial("materials/axion/player_metallic_hidden.vmat", MaterialStyle::Metallic, true)};
    Log("materials flat=%p/%p metallic=%p/%p",
        materials[0].visible, materials[0].hidden, materials[1].visible, materials[1].hidden);
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

bool ResolvesToTarget(C_BaseEntity* entity)
{
    auto* entitySystem = I::GameResourceService->pGameEntitySystem;
    for (int depth = 0; entity != nullptr && depth < 3; ++depth)
    {
        if (IsTrackedTarget(entity))
            return true;
        const CBaseHandle ownerHandle = entity->GetOwnerHandle();
        C_BaseEntity* owner = entitySystem->Get<C_BaseEntity>(ownerHandle);
        if (owner == entity)
            break;
        entity = owner;
    }
    return false;
}

bool IsTarget(void* mesh)
{
    if (mesh == nullptr || I::GameResourceService == nullptr ||
        I::GameResourceService->pGameEntitySystem == nullptr)
        return false;
    auto* sceneAnimatable = *reinterpret_cast<std::uint8_t**>(
        reinterpret_cast<std::uint8_t*>(mesh) + kSceneAnimatableOffset);
    if (sceneAnimatable == nullptr)
        return false;
    // SceneAnimatableObject has moved between 0xB0 and 0xC0 across CS2
    // builds. Resolve only handles that map back to one of our known live
    // enemy pawns, which makes the check safe across both layouts.
    for (const std::size_t ownerOffset : {0xB0U, 0xB8U, 0xC0U})
    {
        const auto handle = *reinterpret_cast<const CBaseHandle*>(sceneAnimatable + ownerOffset);
        C_BaseEntity* owner = I::GameResourceService->pGameEntitySystem->Get<C_BaseEntity>(handle);
        if (ResolvesToTarget(owner))
            return true;
    }
    return false;
}

void SetMeshColor(void* mesh, const Color_t& color)
{
    std::memcpy(reinterpret_cast<std::uint8_t*>(mesh) + kColorOffset, &color, sizeof(color));
}

void DrawArray(void* descriptor, void* renderContext, void* meshArray, int count,
               void* sceneView, void* sceneLayer, void* drawContext, void* frameStats)
{
    if (original == nullptr || meshArray == nullptr || count <= 0 ||
        !C_GET(bool, Vars.bVisualChams))
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
    };
    std::array<Backup, 128> changed{};
    int changedCount = 0;
    const int safeCount = std::min(count, static_cast<int>(changed.size()));
    for (int index = 0; index < safeCount; ++index)
    {
        auto* mesh = reinterpret_cast<std::uint8_t*>(meshArray) + index * kMeshStride;
        if (!IsTarget(mesh))
            continue;
        auto& backup = changed[changedCount++];
        backup.mesh = mesh;
        backup.material = *reinterpret_cast<material2_t**>(mesh + kMaterialOffset);
        std::memcpy(&backup.color, mesh + kColorOffset, sizeof(backup.color));
    }

    if (changedCount == 0)
    {
        original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);
        return;
    }

    const int selectedStyle = std::clamp(C_GET(int, Vars.nVisualChamMaterial), 0,
        static_cast<int>(MaterialStyle::Count) - 1);
    const MaterialPair& selectedMaterials = materials[static_cast<std::size_t>(selectedStyle)];

    if (C_GET(bool, Vars.bVisualChamsIgnoreZ) && selectedMaterials.hidden != nullptr)
    {
        const Color_t hiddenColor = C_GET(ColorPickerVar_t, Vars.colVisualChamsIgnoreZ).colValue;
        for (int index = 0; index < changedCount; ++index)
        {
            auto* mesh = static_cast<std::uint8_t*>(changed[index].mesh);
            *reinterpret_cast<material2_t**>(mesh + kMaterialOffset) = selectedMaterials.hidden;
            SetMeshColor(mesh, hiddenColor);
        }
        // DrawArray's descriptor describes the entire submitted mesh batch.
        // Replaying individual records produces only a thin partial shell, so
        // the no-depth pass must preserve the original array and count.
        original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);
    }

    const Color_t visibleColor = C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue;
    for (int index = 0; index < changedCount; ++index)
    {
        auto* mesh = static_cast<std::uint8_t*>(changed[index].mesh);
        if (selectedMaterials.visible != nullptr)
            *reinterpret_cast<material2_t**>(mesh + kMaterialOffset) = selectedMaterials.visible;
        SetMeshColor(mesh, visibleColor);
    }
    original(descriptor, renderContext, meshArray, count, sceneView, sceneLayer, drawContext, frameStats);

    for (int index = 0; index < changedCount; ++index)
    {
        auto* mesh = static_cast<std::uint8_t*>(changed[index].mesh);
        *reinterpret_cast<material2_t**>(mesh + kMaterialOffset) = changed[index].material;
        SetMeshColor(mesh, changed[index].color);
    }
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
    materialLoadAttempted = false;
    UpdateTargets(nullptr, 0);
}

void UpdateTargets(C_CSPlayerPawn* const* newTargets, std::size_t count)
{
    const std::size_t safeCount = std::min(count, targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index)
        targets[index].store(index < safeCount ? newTargets[index] : nullptr, std::memory_order_relaxed);
}
}

#endif
