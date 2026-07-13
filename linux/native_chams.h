#pragma once

#ifdef __linux__

#include <cstddef>

class C_CSPlayerPawn;
struct Color_t;

namespace NativeChams
{
bool Install();
void Destroy();
void UpdateTargets(C_CSPlayerPawn* const* targets, std::size_t count, C_CSPlayerPawn* localTarget = nullptr);
void UpdateColors(const Color_t& visible, const Color_t& hidden);
}

#endif
