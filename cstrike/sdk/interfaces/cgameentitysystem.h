#pragma once

// used: schema field
#include "../../utilities/memory.h"

#include "../entity_handle.h"

#define MAX_ENTITIES_IN_LIST 512
#define MAX_ENTITY_LISTS 64 // 0x3F
#define MAX_TOTAL_ENTITIES MAX_ENTITIES_IN_LIST* MAX_ENTITY_LISTS

class C_BaseEntity;

class CGameEntitySystem
{
public:
	/// GetClientEntity
	template <typename T = C_BaseEntity>
	T* Get(int nIndex)
	{
		return reinterpret_cast<T*>(this->GetEntityByIndex(nIndex));
	}

	/// GetClientEntityFromHandle
	template <typename T = C_BaseEntity>
	T* Get(const CBaseHandle hHandle)
	{
		if (!hHandle.IsValid())
			return nullptr;

		return reinterpret_cast<T*>(this->GetEntityByIndex(hHandle.GetEntryIndex()));
	}

	int GetHighestEntityIndex()
	{
#ifdef __linux__
		// Native ESP only needs controller slots; avoid a patch-sensitive static offset.
		return 128;
#else
		return *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + 0x1510);
#endif
	}

private:
	void* GetEntityByIndex(int nIndex)
	{
#ifdef __linux__
		if (nIndex < 0 || nIndex >= MAX_TOTAL_ENTITIES)
			return nullptr;
		const auto base = reinterpret_cast<std::uintptr_t>(this);
		const auto bucket = *reinterpret_cast<std::uintptr_t*>(base + 0x10 + 0x8 * (nIndex >> 9));
		if (bucket == 0)
			return nullptr;
		return *reinterpret_cast<void**>(bucket + 0x70 * (nIndex & 0x1FF));
#else
		//@ida: #STR: "(missing),", "(missing)", "Ent %3d: %s class %s name %s\n" | or find "cl_showents" cvar -> look for callback
		//	do { pEntity = GetBaseEntityByIndex(g_pGameEntitySystem, nCurrentIndex); ... }
		using fnGetBaseEntity = void*(CS_THISCALL*)(void*, int);
		static auto GetBaseEntity = reinterpret_cast<fnGetBaseEntity>(MEM::FindPattern(CLIENT_DLL, CS_XOR("81 FA ? ? ? ? 77 ? 8B C2 C1 F8 ? 83 F8 ? 77 ? 48 98 48 8B 4C C1 ? 48 85 C9 74 ? 8B C2 25 ? ? ? ? 48 6B C0 ? 48 03 C8 74 ? 8B 41 ? 25 ? ? ? ? 3B C2 75 ? 48 8B 01")));
		return GetBaseEntity(this, nIndex);
#endif
	}
};

enum CSWeaponID {
	GLOCK = 1,
	USP_S = 2,
	P2000 = 3,
	DUAL_BERETTAS = 4,
	P250 = 5,
	TEC9 = 6,
	FIVE_SEVEN = 7,
	DESERT_EAGLE = 8,

	MAC10 = 17,
	MP9 = 18,
	MP7 = 19,
	UMP45 = 24,
	P90 = 26,

	GALIL_AR = 13,
	FAMAS = 14,
	AK47 = 10,
	M4A4 = 16,
	M4A1_S = 20,
	AUG = 23,
	SG553 = 27,

	AWP = 9,
	G3SG1 = 11,
	SCAR20 = 38,

	M249 = 28,
	NEGEV = 35
};
