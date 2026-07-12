#pragma once

// used: mem_pad
#include "../../utilities/memory.h"

class CGameEntitySystem;

class IGameResourceService
{
public:
#ifdef __linux__
	MEM_PAD(0x50);
#else
	MEM_PAD(0x58);
#endif
	CGameEntitySystem* pGameEntitySystem;
};
