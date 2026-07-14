// used: [win] shgetknownfolderpath
#ifdef _WIN32
#include <shlobj_core.h>
#endif

#include <cstdio>
#include <cstdarg>
#include "core.h"

// used: features setup
#include "cheat_features.h"
// used: string copy
#include "utilities/crt.h"
// used: mem
#include "utilities/memory.h"
// used: l_print
#include "utilities/log.h"
// used: inputsystem setup/restore
#include "utilities/inputsystem.h"
// used: draw destroy
#include "utilities/draw.h"

// used: interfaces setup/destroy
#include "core/interfaces.h"
// used: sdk setup
#include "core/sdk.h"
// used: config setup & variables
#include "core/variables.h"
// used: hooks setup/destroy
#include "core/hooks.h"
// used: schema setup/dump
#include "core/schema.h"
// used: convar setup
#include "core/convars.h"
// used: menu
#include "core/menu.h"
#ifdef _WIN32
#include <d3d11.h>
#else
#include "../linux/vulkan_hook.h"
#endif
bool CORE::GetWorkingPath(wchar_t* wszDestination)
{
#ifdef _WIN32
	bool bSuccess = false;
	PWSTR wszPathToDocuments = nullptr;

	if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &wszPathToDocuments)))
	{
		CRT::StringCat(CRT::StringCopy(wszDestination, wszPathToDocuments), CS_XOR(L"\\.cs2\\"));
		bSuccess = true;
		if (!CreateDirectoryW(wszDestination, nullptr))
		{
			if (::GetLastError() != ERROR_ALREADY_EXISTS)
			{
				L_PRINT(LOG_ERROR) << CS_XOR("failed to create default working directory");
				bSuccess = false;
			}
		}
	}
	::CoTaskMemFree(wszPathToDocuments);
	return bSuccess;
#else
	const char* home = getenv("HOME");
	if (!home) return false;
	char nbuf[MAX_PATH];
	snprintf(nbuf, sizeof(nbuf), "%s/.cs2/", home);
	mkdir(nbuf, 0755);
	size_t i = 0;
	while (nbuf[i] && i < MAX_PATH - 1) { wszDestination[i] = (wchar_t)(unsigned char)nbuf[i]; i++; }
	wszDestination[i] = L'\0';
	return true;
#endif
}

static FILE* g_setup_debug_file = nullptr;

static void SetupDebugLog(const char* fmt, ...)
{
    if (!g_setup_debug_file)
        g_setup_debug_file = fopen("/tmp/cs2_init_debug.log", "a");
    if (!g_setup_debug_file)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(g_setup_debug_file, fmt, args);
    va_end(args);
    fflush(g_setup_debug_file);
}

static bool Setup(HMODULE hModule)
{
    SetupDebugLog("Setup() entered hModule=%p\n", hModule);
#ifdef CS_LOG_CONSOLE
	if (!L::AttachConsole(CS_XOR(L"cs2 developer-mode")))
	{
		SetupDebugLog("L::AttachConsole failed\n");
		CS_ASSERT(false); // failed to attach console
		return false;
	}
	SetupDebugLog("L::AttachConsole succeeded\n");
#endif
#ifdef CS_LOG_FILE
	if (!L::OpenFile(CS_XOR(L"cs2.log")))
	{
		SetupDebugLog("L::OpenFile failed\n");
		CS_ASSERT(false); // failed to open file
		return false;
	}
	SetupDebugLog("L::OpenFile succeeded\n");
#endif
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("logging system initialization completed");

	// setup game's exported functions
	SetupDebugLog("MEM::Setup start\n");
	SetupDebugLog("ABOUT TO CALL MEM::Setup\n");
	if (!MEM::Setup())
	{
		SetupDebugLog("MEM::Setup failed\n");
#ifdef _WIN32
		CS_ASSERT(false); // failed to setup memory system
#endif
		return false;
	}
	SetupDebugLog("MEM::Setup succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("memory system initialization completed");

	SetupDebugLog("MATH::Setup start\n");
	SetupDebugLog("ABOUT TO CALL MATH::Setup\n");
	if (!MATH::Setup())
	{
		SetupDebugLog("MATH::Setup failed\n");
		CS_ASSERT(false); // failed to setup math system
		return false;
	}
	SetupDebugLog("MATH::Setup succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("math system initialization completed");
	// grab game's interfaces
	SetupDebugLog("I::Setup start\n");
	SetupDebugLog("ABOUT TO CALL I::Setup\n");
	if (!I::Setup())
	{
		SetupDebugLog("I::Setup failed\n");
#ifdef __linux__
		L_PRINT(LOG_WARNING) << CS_XOR("native optional game interfaces were not fully captured; menu-only mode will continue");
#else
		CS_ASSERT(false); // failed to setup interfaces
		return false;
#endif
	}
	SetupDebugLog("I::Setup succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("interfaces initialization completed");

	SetupDebugLog("SDK::Setup start\n");
	SetupDebugLog("ABOUT TO CALL SDK::Setup\n");
	if (!SDK::Setup())
	{
		SetupDebugLog("SDK::Setup failed\n");
#ifdef __linux__
		L_PRINT(LOG_WARNING) << CS_XOR("native optional SDK exports were not captured; menu-only mode will continue");
#else
		CS_ASSERT(false); // failed to setup sdk
		return false;
#endif
	}
	SetupDebugLog("SDK::Setup succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("sdk initialization completed");

	// setup input system and replace game's window messages processor with our
	SetupDebugLog("IPT::Setup start\n");
	SetupDebugLog("ABOUT TO CALL IPT::Setup\n");
	if (!IPT::Setup())
	{
		SetupDebugLog("IPT::Setup failed\n");
		CS_ASSERT(false); // failed to setup input system
		return false;
	}
	SetupDebugLog("IPT::Setup succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("input system initialization completed");

	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("renderer backend initialization completed");

	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("menu style Iinitialization completed");

	// initialize feature-related stuff
	SetupDebugLog("F::Setup start\n");
	SetupDebugLog("ABOUT TO CALL F::Setup\n");
	if (!F::Setup())
	{
		SetupDebugLog("F::Setup failed\n");
		CS_ASSERT(false); // failed to setup features
		return false;
	}
	SetupDebugLog("F::Setup succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("features initialization completed");

	SetupDebugLog("SCHEMA::Setup start\n");
	if (!SCHEMA::Setup(CS_XOR(L"schema.txt"), CLIENT_DLL))
	{
		SetupDebugLog("SCHEMA::Setup failed\n");
		CS_ASSERT(false); // failed to setup schema system
		return false;
	}
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("schema system initialization completed");

	SetupDebugLog("CONVAR::Dump start\n");
	if (!CONVAR::Dump(CS_XOR(L"convars.txt")))
	{
		SetupDebugLog("CONVAR::Dump failed\n");
		CS_ASSERT(false); // failed to setup convars system
		return false;
	}
	SetupDebugLog("CONVAR::Dump succeeded\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("convars dumped completed, output: \"convars.txt\"");

	SetupDebugLog("CONVAR::Setup start\n");
	if (!CONVAR::Setup())
	{
		SetupDebugLog("CONVAR::Setup failed\n");
		CS_ASSERT(false); // failed to setup convars system
		return false;
	}
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("convars system initialization completed");

	// setup hooks
	SetupDebugLog("H::Setup start\n");
	if (!H::Setup())
	{
		SetupDebugLog("H::Setup failed\n");
		CS_ASSERT(false); // failed to setup hooks
		return false;
	}
	SetupDebugLog("H::Setup succeeded\n");

#ifdef __linux__
	if (!InstallNativeInputHook())
		SetupDebugLog("native CCSGOInput::CreateMove hook unavailable; command-mutating features are held\n");
	else
		SetupDebugLog("native CCSGOInput::CreateMove hook installed\n");
#endif

	L_PRINT(LOG_NONE) << CS_XOR("hooks initialization completed");


	L_PRINT(LOG_NONE) << CS_XOR("menu style initialization completed");

	// setup values to save/load cheat variables into/from files and load default configuration
	if (!C::Setup(CS_XOR(CS_CONFIGURATION_DEFAULT_FILE_NAME)))
		// this error is not critical, only show that
		L_PRINT(LOG_WARNING) << CS_XOR("failed to setup and/or load default configuration");
	else
		L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_GREEN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("configuration system initialization completed");

#ifdef __linux__
	EnableVulkanMenu();
#endif

SetupDebugLog("Setup() completed successfully\n");
	L_PRINT(LOG_NONE) << L::SetColor(LOG_COLOR_FORE_CYAN | LOG_COLOR_FORE_INTENSITY) << CS_XOR("cs2 initialization completed, version: ") << CS_STRINGIFY(CS_VERSION);
	
	return true;
}

// @todo: some of those may crash while closing process, because we dont have any dependencies from the game modules, it means them can be unloaded and destruct interfaces etc before our module | modify ldrlist?
static void Destroy()
{
#ifdef __linux__
	DestroyNativeInputHook();
#endif
	// restore window messages processor to original
	IPT::Destroy();

	// restore hooks
	H::Destroy();

	// destroy renderer backend
	D::Destroy();

#ifdef CS_LOG_CONSOLE
	L::DetachConsole();
#endif
#ifdef CS_LOG_FILE
	L::CloseFile();
#endif
}

#ifdef _WIN32
DWORD WINAPI PanicThread(LPVOID lpParameter)
#else
void* WINAPI PanicThread(LPVOID lpParameter)
#endif
{
	// don't let proceed unload until user press specified key
	while (!IPT::IsKeyReleased(C_GET(unsigned int, Vars.nPanicKey)))
		::Sleep(500UL);

	// call detach code and exit this thread
	FreeLibraryAndExitThread(static_cast<HMODULE>(lpParameter), EXIT_SUCCESS);
#ifdef _WIN32
	return 0;
#else
	return nullptr;
#endif
}

#ifdef _WIN32
extern "C" BOOL WINAPI _CRT_INIT(HMODULE hModule, DWORD dwReason, LPVOID lpReserved);

BOOL APIENTRY CoreEntryPoint(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	// process destroy of the cheat before crt calls atexit table
	if (dwReason == DLL_PROCESS_DETACH)
		Destroy();

	if (dwReason == DLL_PROCESS_ATTACH)
	{	// dispatch reason for c-runtime, initialize/destroy static variables, TLS etc
		if (!_CRT_INIT(hModule, dwReason, lpReserved))
			return FALSE;

		CORE::hProcess = MEM::GetModuleBaseHandle(static_cast<const char*>(nullptr));

		// basic process check
		if (CORE::hProcess == nullptr)
			return FALSE;

		/*
		 * check did all game modules have been loaded
		 * @note: navsystem.dll is the last loaded module
		 */
		if (MEM::GetModuleBaseHandle(NAVSYSTEM_DLL) == nullptr)
			return FALSE;

		// save our module handle
		CORE::hDll = hModule;

		// check did we perform main initialization successfully
		if (!Setup(hModule))
		{
			// undo the things we've done
			Destroy();
			return FALSE;
		}

		// create panic thread, it isn't critical error if it fails
		HANDLE hThread = CreateThread(nullptr, 0U, &PanicThread, hModule, 0UL, nullptr);
		
		if (hThread != nullptr)
			CloseHandle(hThread);

	}

	return TRUE;
}
#else
extern "C" bool Setup_Linux() { return Setup(nullptr); }
extern "C" void Destroy_Linux() { Destroy(); }
#endif
