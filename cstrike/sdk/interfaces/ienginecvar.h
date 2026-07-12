#pragma once

// used: cutllinkedlist
#include "../datatypes/utllinkedlist.h"
// used: fnv1a hashing
#include "../../utilities/fnv1a.h"

// used: sdk datatypes
#include "../datatypes/color.h"
#include "../datatypes/vector.h"
#include "../datatypes/qangle.h"

#pragma region convar_enumerations
// command to convars and concommands
enum EConVarFlag : int
{
	// convar systems
	FCVAR_NONE = 0,
	FCVAR_UNREGISTERED =			(1 << 0),	// if this is set, don't add to linked list, etc
	FCVAR_DEVELOPMENTONLY =			(1 << 1),	// hidden in released products. flag is removed automatically if allow_development_cvars is defined
	FCVAR_GAMEDLL =					(1 << 2),	// defined by the game dll
	FCVAR_CLIENTDLL =				(1 << 3),	// defined by the client dll
	FCVAR_HIDDEN =					(1 << 4),	// hidden. doesn't appear in find or autocomplete. like developmentonly, but can't be compiled out

	// convar only
	FCVAR_PROTECTED =				(1 << 5),	// it's a server cvar, but we don't send the data since it's a password, etc. sends 1 if it's not bland/zero, 0 otherwise as value
	FCVAR_SPONLY =					(1 << 6),	// this cvar cannot be changed by clients connected to a multiplayer server
	FCVAR_ARCHIVE =					(1 << 7),	// set to cause it to be saved to vars.rc
	FCVAR_NOTIFY =					(1 << 8),	// notifies players when changed
	FCVAR_USERINFO =				(1 << 9),	// changes the client's info string
	FCVAR_CHEAT =					(1 << 14),	// only useable in singleplayer/debug/multiplayer & sv_cheats
	FCVAR_PRINTABLEONLY =			(1 << 10),	// this cvar's string cannot contain unprintable characters (e.g., used for player name etc)
	FCVAR_UNLOGGED =				(1 << 11),	// if this is a fcvar_server, don't log changes to the log file / console if we are creating a log
	FCVAR_NEVER_AS_STRING =			(1 << 12),	// never try to print that cvar

	// it's a convar that's shared between the client and the server.
	// at signon, the values of all such convars are sent from the server to the client (skipped for local client, ofc)
	// if a change is requested it must come from the console (i.e., no remote client changes)
	// if a value is changed while a server is active, it's replicated to all connected clients
	FCVAR_REPLICATED =				(1 << 13),	// server setting enforced on clients, replicated
	// @todo: (1 << 14) used by the game, probably used as modification detection
	FCVAR_DEMO =					(1 << 16),	// record this cvar when starting a demo file
	FCVAR_DONTRECORD =				(1 << 17),	// don't record these command in demofiles
	FCVAR_RELOAD_MATERIALS =		(1 << 20),	// if this cvar changes, it forces a material reload
	FCVAR_RELOAD_TEXTURES =			(1 << 21),	// if this cvar changes, if forces a texture reload
	FCVAR_NOT_CONNECTED =			(1 << 22),	// cvar cannot be changed by a client that is connected to a server
	FCVAR_MATERIAL_SYSTEM_THREAD =	(1 << 23),	// indicates this cvar is read from the material system thread
	FCVAR_ARCHIVE_XBOX =			(1 << 24),	// cvar written to config.cfg on the xbox
	FCVAR_ACCESSIBLE_FROM_THREADS = (1 << 25),	// used as a debugging tool necessary to check material system thread convars
	FCVAR_SERVER_CAN_EXECUTE =		(1 << 28),	// the server is allowed to execute this command on clients via clientcommand/net_stringcmd/cbaseclientstate::processstringcmd
	FCVAR_SERVER_CANNOT_QUERY =		(1 << 29),	// if this is set, then the server is not allowed to query this cvar's value (via iserverpluginhelpers::startquerycvarvalue)
	FCVAR_CLIENTCMD_CAN_EXECUTE =	(1 << 30),	// ivengineclient::clientcmd is allowed to execute this command
	FCVAR_MATERIAL_THREAD_MASK =	(FCVAR_RELOAD_MATERIALS | FCVAR_RELOAD_TEXTURES | FCVAR_MATERIAL_SYSTEM_THREAD)
};

enum EConVarType : short
{
	EConVarType_Invalid = -1,
	EConVarType_Bool,
	EConVarType_Int16,
	EConVarType_UInt16,
	EConVarType_Int32,
	EConVarType_UInt32,
	EConVarType_Int64,
	EConVarType_UInt64,
	EConVarType_Float32,
	EConVarType_Float64,
	EConVarType_String,
	EConVarType_Color,
	EConVarType_Vector2,
	EConVarType_Vector3,
	EConVarType_Vector4,
	EConVarType_Qangle,
	EConVarType_MAX
};

#pragma endregion

union CVValue_t
{
	bool i1;
	short i16;
	uint16_t u16;
	int i32;
	uint32_t u32;
	int64_t i64;
	uint64_t u64;
	float fl;
	double db;
	const char* sz;
	Color_t clr;
	Vector2D_t vec2;
	Vector_t vec3;
	Vector4D_t vec4;
	QAngle_t ang;
};

class CConVar
{
public:
	const char* szName; // 0x0000
	CConVar* m_pNext; // 0x0008
	MEM_PAD(0x10); // 0x0010
	const char* szDescription; // 0x0020
	uint32_t nType; // 0x28
	uint32_t nRegistered; // 0x2C
	uint32_t nFlags; // 0x30
	MEM_PAD(0x8); // 0x34
#ifdef __linux__
	MEM_PAD(0x10); // native Linux stores the value at 0x50
#endif
	// @note: read-only, mofify with caution
	CVValue_t value; // 0x40
};

class IEngineCVar
{
public:
	MEM_PAD(0x40);
	CUtlLinkedList<CConVar*> listConvars;

	CConVar* Find(FNV1A_t uHashedName)
	{
#ifdef __linux__
		const auto base = reinterpret_cast<std::uintptr_t>(this);
		const auto objects = *reinterpret_cast<const std::uintptr_t*>(base + 0x48);
		const auto count = *reinterpret_cast<const std::uint64_t*>(base + 0xA0);
		if (objects < 0x10000 || count > 100000)
			return nullptr;
		for (std::uint64_t i = 0; i < count; ++i)
		{
			const auto object = *reinterpret_cast<CConVar* const*>(objects + i * 0x10);
			if (object == nullptr || object->szName == nullptr)
				continue;
			if (FNV1A::Hash(object->szName) == uHashedName)
				return object;
		}
		return nullptr;
#else
		for (int i = I::Cvar->listConvars.Head(); i != I::Cvar->listConvars.InvalidIndex(); i = I::Cvar->listConvars.Next(i))
		{
			CConVar* pConVar = I::Cvar->listConvars.Element(i);
			if (pConVar == nullptr)
				continue;

			if (FNV1A::Hash(pConVar->szName) == uHashedName)
				return pConVar;
		}

		CS_ASSERT(false); // invalid convar name
		return nullptr;
#endif
	}

#ifdef __linux__
	std::uint64_t GetNativeCount() const
	{
		const auto count = *reinterpret_cast<const std::uint64_t*>(reinterpret_cast<std::uintptr_t>(this) + 0xA0);
		return count <= 100000 ? count : 0;
	}

	CConVar* GetNativeAt(std::uint64_t index) const
	{
		const auto objects = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(this) + 0x48);
		if (objects < 0x10000 || index >= GetNativeCount())
			return nullptr;
		return *reinterpret_cast<CConVar* const*>(objects + index * 0x10);
	}
#endif

	void UnlockHiddenCVars()
	{
		for (int i = I::Cvar->listConvars.Head(); i != I::Cvar->listConvars.InvalidIndex(); i = I::Cvar->listConvars.Next(i))
		{
			CConVar* pConVar = I::Cvar->listConvars.Element(i);
			if (pConVar == nullptr)
				continue;

			if (pConVar->nFlags & FCVAR_HIDDEN)
				pConVar->nFlags &= ~FCVAR_HIDDEN;

			if (pConVar->nFlags & FCVAR_DEVELOPMENTONLY)
				pConVar->nFlags &= ~FCVAR_DEVELOPMENTONLY;
		}
	}
};
