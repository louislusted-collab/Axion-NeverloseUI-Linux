#pragma once

#include <cstddef>
#include <cstdint>

namespace LinuxNativeEsp
{
inline constexpr std::size_t SkinWeaponDefinitionCount = 1024;

void Render();
std::uint16_t GetActiveSkinWeaponDefinition();
const char* GetActiveSkinWeaponName();
const char* GetSkinRuntimeStatus();
bool IsSkinRegeneratorReady();
void RequestSkinRefresh();
}
