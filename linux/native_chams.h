#pragma once

#ifdef __linux__

#include <cstddef>

class C_CSPlayerPawn;

namespace NativeChams
{
bool Install();
void Destroy();
void UpdateTargets(C_CSPlayerPawn* const* targets, std::size_t count);
}

#endif
