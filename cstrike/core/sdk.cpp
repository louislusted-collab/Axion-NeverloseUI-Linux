#include "sdk.h"

// used: getmodulebasehandle
#include "../utilities/memory.h"

bool SDK::Setup()
{
	bool bSuccess = true;

	const void* hTier0Lib = MEM::GetModuleBaseHandle(TIER0_DLL);
	if (hTier0Lib == nullptr)
		return false;

#ifdef _WIN32
	#ifdef _WIN32
	fnConColorMsg = reinterpret_cast<decltype(fnConColorMsg)>(MEM::GetExportAddress(hTier0Lib, CS_XOR("?ConColorMsg@@YAXAEBVColor@@PEBDZZ")));
#else
	fnConColorMsg = reinterpret_cast<decltype(fnConColorMsg)>(MEM::GetExportAddress(hTier0Lib, CS_XOR("_Z11ConColorMsgRK5ColorPKcz")));
#endif
#else
	fnConColorMsg = reinterpret_cast<decltype(fnConColorMsg)>(MEM::GetExportAddress(hTier0Lib, CS_XOR("_Z11ConColorMsgRK5ColorPKcz")));
#endif
	bSuccess &= fnConColorMsg != nullptr;

	return bSuccess;
}
