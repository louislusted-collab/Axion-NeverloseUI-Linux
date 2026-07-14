#pragma once

#ifdef __linux__

#include <cstddef>

class C_CSPlayerPawn;
class C_BaseEntity;
struct Color_t;

namespace NativeChams
{
bool Install();
void Destroy();
void UpdateTargets(C_CSPlayerPawn* const* targets, std::size_t count, C_CSPlayerPawn* localTarget = nullptr);
void UpdateBombTarget(C_BaseEntity* bombTarget);
void UpdateColors(const Color_t& visible, const Color_t& hidden);
bool IsInstalled();
bool HasBombTarget();
unsigned long long GetEnemyMeshMatches();
unsigned long long GetKnifeMeshMatches();
unsigned long long GetBombMeshMatches();
}

#endif
