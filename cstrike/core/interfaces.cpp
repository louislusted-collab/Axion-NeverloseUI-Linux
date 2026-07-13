// used: [d3d] api
#ifdef _WIN32
#include <d3d11.h>
#endif

#include "interfaces.h"

// used: findpattern, callvirtual, getvfunc...
#include "../utilities/memory.h"

// used: l_print
#include "../utilities/log.h"

// used: iswapchaindx11
#include "../sdk/interfaces/iswapchaindx11.h"
#include "hooks.h"
#include "../sdk/datatypes/resourceutils.h"
#include "../cstrike/sdk/interfaces/events.h"
#include "../sdk/interfaces/imaterialsystem.h"
#pragma region interfaces_get

using InstantiateInterfaceFn_t = void* (*)();

class CInterfaceRegister
{
public:
	InstantiateInterfaceFn_t fnCreate;
	const char* szName;
	CInterfaceRegister* pNext;
};

static const CInterfaceRegister* GetRegisterList(const wchar_t* wszModuleName)
{
	void* hModule = MEM::GetModuleBaseHandle(wszModuleName);
	if (hModule == nullptr)
		return nullptr;

	std::uint8_t* pCreateInterface = reinterpret_cast<std::uint8_t*>(MEM::GetExportAddress(hModule, CS_XOR("CreateInterface")));

	if (pCreateInterface == nullptr)
	{
		L_PRINT(LOG_ERROR) << CS_XOR("failed to get \"CreateInterface\" address");
		return nullptr;
	}

#ifdef _WIN32
	// Windows: CreateInterface starts with  mov rax,[rip+X]  at offset 0 (7-byte insn)
	return *reinterpret_cast<CInterfaceRegister**>(MEM::ResolveRelativeAddress(pCreateInterface, 0x3, 0x7));
#else
	// Native builds have a normal compiler-generated prologue. Locate the
	// RIP-relative `mov rbx, [rip + list]` instead of relying on one fixed
	// prologue length, which changes between game updates and build types.
	for (std::size_t offset = 0; offset + 7 <= 48; ++offset) {
		if (pCreateInterface[offset] == 0x48 && pCreateInterface[offset + 1] == 0x8B &&
			pCreateInterface[offset + 2] == 0x1D) {
			return *reinterpret_cast<CInterfaceRegister**>(
				MEM::ResolveRelativeAddress(pCreateInterface + offset, 0x3, 0x7));
		}
	}
	L_PRINT(LOG_ERROR) << CS_XOR("failed to locate native CreateInterface register list");
	return nullptr;
#endif
}

#ifdef __linux__
static wchar_t _irl_wbuf[512];
static const CInterfaceRegister* GetRegisterList(const char* szModuleName)
{
	size_t i = 0;
	while (szModuleName[i] && i + 1 < 512) { _irl_wbuf[i] = (wchar_t)(unsigned char)szModuleName[i]; i++; }
	_irl_wbuf[i] = L'\0';
	return GetRegisterList(_irl_wbuf);
}
#endif

template <typename T = void*>
T* Capture(const CInterfaceRegister* pModuleRegister, const char* szInterfaceName)
{
	for (const CInterfaceRegister* pRegister = pModuleRegister; pRegister != nullptr; pRegister = pRegister->pNext)
	{
		if (const std::size_t nInterfaceNameLength = CRT::StringLength(szInterfaceName);
			// found needed interface
			CRT::StringCompareN(szInterfaceName, pRegister->szName, nInterfaceNameLength) == 0 &&
			// and we've given full name with hardcoded digits
			(CRT::StringLength(pRegister->szName) == nInterfaceNameLength ||
			// or it contains digits after name
			CRT::StringToInteger<int>(pRegister->szName + nInterfaceNameLength, nullptr, 10) > 0))
		{
			// capture our interface
			void* pInterface = pRegister->fnCreate();

#ifdef _DEBUG
			// log interface address
			L_PRINT(LOG_INFO) << CS_XOR("captured \"") << pRegister->szName << CS_XOR("\" interface at address: ") << L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX) << reinterpret_cast<std::uintptr_t>(pInterface);
#else
			L_PRINT(LOG_INFO) << CS_XOR("captured \"") << pRegister->szName << CS_XOR("\" interface");
#endif

			return static_cast<T*>(pInterface);
		}
	}

	L_PRINT(LOG_ERROR) << CS_XOR("failed to find interface \"") << szInterfaceName << CS_XOR("\"");
	return nullptr;
}

#pragma endregion

bool I::Setup()
{
	bool bSuccess = true;

#pragma region interface_game_exported
	const auto pTier0Handle = MEM::GetModuleBaseHandle(TIER0_DLL);
	if (pTier0Handle == nullptr)
		return false;

	MemAlloc = *reinterpret_cast<IMemAlloc**>(MEM::GetExportAddress(pTier0Handle, CS_XOR("g_pMemAlloc")));
	bSuccess &= (MemAlloc != nullptr);

	const auto pSchemaSystemRegisterList = GetRegisterList(SCHEMASYSTEM_DLL);
	if (pSchemaSystemRegisterList == nullptr)
		return false;

	SchemaSystem = Capture<ISchemaSystem>(pSchemaSystemRegisterList, SCHEMA_SYSTEM);
	bSuccess &= (SchemaSystem != nullptr);

	const auto pInputSystemRegisterList = GetRegisterList(INPUTSYSTEM_DLL);
	if (pInputSystemRegisterList == nullptr)
		return false;

	InputSystem = Capture<IInputSystem>(pInputSystemRegisterList, INPUT_SYSTEM_VERSION);
	bSuccess &= (InputSystem != nullptr);

	const auto pEngineRegisterList = GetRegisterList(ENGINE2_DLL);
	if (pEngineRegisterList == nullptr)
		return false;

	GameResourceService = Capture<IGameResourceService>(pEngineRegisterList, GAME_RESOURCE_SERVICE_CLIENT);
	bSuccess &= (GameResourceService != nullptr);

	Engine = Capture<IEngineClient>(pEngineRegisterList, SOURCE2_ENGINE_TO_CLIENT);
	bSuccess &= (Engine != nullptr);

	NetworkClientService = Capture<INetworkClientService>(pEngineRegisterList, NETWORK_CLIENT_SERVICE);
	bSuccess &= (NetworkClientService != nullptr);

	const auto pTier0RegisterList = GetRegisterList(TIER0_DLL);
	if (pTier0RegisterList == nullptr)
		return false;
	Cvar = Capture<IEngineCVar>(pTier0RegisterList, ENGINE_CVAR);
	bSuccess &= (Cvar != nullptr);

	const auto pClientRegister = GetRegisterList(CLIENT_DLL);
	if (pClientRegister == nullptr)
		return false;
	Client = Capture<ISource2Client>(pClientRegister, SOURCE2_CLIENT);
	bSuccess &= (Client != nullptr);

	const auto pLocalizeRegisterList = GetRegisterList(LOCALIZE_DLL);
	if (pLocalizeRegisterList == nullptr)
		return false;
	Localize = Capture<CLocalize>(pLocalizeRegisterList, LOCALIZE);
	bSuccess &= (Localize != nullptr);

	/* material sys */
	const auto pMaterialSystem2Register = GetRegisterList(MATERIAL_SYSTEM2_DLL);
	if (pMaterialSystem2Register == nullptr)
		return false;
	MaterialSystem2 = Capture<material_system_t>(pMaterialSystem2Register, MATERIAL_SYSTEM2);
	bSuccess &= (MaterialSystem2 != nullptr);
	
	const auto pResourceSystemRegisterList = GetRegisterList(RESOURCESYSTEM_DLL);
	if (pResourceSystemRegisterList == nullptr)
		return false;

	ResourceSystem = Capture<IResourceSystem>(pResourceSystemRegisterList, RESOURCE_SYSTEM);
	bSuccess &= (ResourceSystem != nullptr);

	if (ResourceSystem != nullptr)
	{
		ResourceHandleUtils = reinterpret_cast<CResourceHandleUtils*>(ResourceSystem->QueryInterface(RESOURCE_HANDLE_UTILS));
		bSuccess &= (ResourceHandleUtils != nullptr);
	}
	/* //render game sys 
	const auto pRenderSysRegister = GetRegisterList(RENDERSYSTEM_DLL);
	if (pRenderSysRegister == nullptr)
		return false;

	RenderGameSystem = Capture<CRenderGameSystem>(pRenderSysRegister, RENDERSYS_SYSTEM);
	bSuccess &= (RenderGameSystem != nullptr);*/
#pragma endregion

#ifdef _WIN32
	Trace = *reinterpret_cast<i_trace**>(MEM::GetAbsoluteAddress(MEM::FindPattern(CLIENT_DLL, CS_XOR("4C 8B 3D ? ? ? ? 24 C9 0C 49 66 0F 7F 45 ?")), 0x3));
	bSuccess &= (Trace != nullptr);
	L_PRINT(LOG_INFO) << CS_XOR("captured Trace interface at address: ") << L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX) << reinterpret_cast<std::uintptr_t>(Trace);

	if (auto* pGameEventBase = MEM::GetVFunc<std::uint8_t*>(Client, 14U))
		GameEvent = *reinterpret_cast<IGameEventManager2**>(MEM::ResolveRelativeAddress(pGameEventBase + 0x3E, 0x3, 0x7));
	bSuccess &= (GameEvent != nullptr);

	SwapChain = **reinterpret_cast<ISwapChainDx11***>(MEM::ResolveRelativeAddress(MEM::FindPattern(RENDERSYSTEM_DLL, CS_XOR("66 0F 7F 05 ? ? ? ? 66 0F 7F 0D ? ? ? ? 48 89 35")), 0x4, 0x8));
	bSuccess &= (SwapChain != nullptr);
	if (SwapChain != nullptr)
	{
		if (FAILED(SwapChain->pDXGISwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&Device)))
		{
			L_PRINT(LOG_ERROR) << CS_XOR("failed to get device from swapchain");
			return false;
		}
		else
			Device->GetImmediateContext(&DeviceContext);
	}
	bSuccess &= (Device != nullptr && DeviceContext != nullptr);

	Input = *reinterpret_cast<CCSGOInput**>(MEM::ResolveRelativeAddress(MEM::FindPattern(CLIENT_DLL, CS_XOR("48 8B 0D ? ? ? ? E8 ? ? ? ? 8B BE ? ? ? ? 44 8B F0 85 FF 78 04 FF C7 EB 03")), 0x3, 0x7));
	// Optional in native menu-only mode. Gameplay input hooks stay disabled until
	// the Linux class layout and vtable indices are verified.

	GlobalVars = *reinterpret_cast<IGlobalVars**>(MEM::ResolveRelativeAddress(MEM::FindPattern(CLIENT_DLL, CS_XOR("48 89 0D ? ? ? ? 48 89 41")), 0x3, 0x7));
	bSuccess &= (GlobalVars != nullptr);
#else
	// Linux: capture via sig scans and CreateInterface
	L_PRINT(LOG_INFO) << CS_XOR("[I::Setup] Linux: capturing remaining interfaces via patterns");

	// Input (CCSGOInput*) - sig scan in libclient.so. The current native
	// signature resolves a global CCSGOInput* storage slot, not the object
	// itself. Treating the slot as an object produced an all-zero vtable and
	// made every CreateMove/third-person hook silently unavailable.
	{
		bool foundCurrentStorage = false;
		auto addr = MEM::FindPattern(CLIENT_DLL,
			CS_XOR("F3 41 0F 7E 06 F3 0F 7E 4D B0 48 8D 05 ? ? ? ? 0F 58 C1"));
		if (addr != nullptr)
		{
			foundCurrentStorage = true;
			auto* inputTable = MEM::ResolveRelativeAddress(addr + 0xA, 0x3, 0x7) + 0x10;
			Input = *reinterpret_cast<CCSGOInput**>(inputTable);
			L_PRINT(LOG_INFO) << CS_XOR("native Input storage at: ")
				<< L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX)
				<< reinterpret_cast<std::uintptr_t>(inputTable);
		}
		if (Input == nullptr && !foundCurrentStorage) {
			// Older builds exposed the object through a short LEA accessor.
			addr = MEM::FindPattern(CLIENT_DLL,
				CS_XOR("48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 48 89 E5 41 55"));
			if (addr != nullptr)
				Input = reinterpret_cast<CCSGOInput*>(MEM::ResolveRelativeAddress(addr, 0x3, 0x7));
		}
	}
	bSuccess &= (Input != nullptr);
	if (Input != nullptr)
		L_PRINT(LOG_INFO) << CS_XOR("captured Input at: ") << L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX) << reinterpret_cast<std::uintptr_t>(Input);
	else
		L_PRINT(LOG_WARNING) << CS_XOR("[I::Setup] Linux: Input not captured, some features may crash");

	// GlobalVars - the Windows engine vfunc index is not valid on native Linux.
	// Only accept a verified native pattern result; this interface is optional for
	// menu-only mode.
	{
		auto addr = MEM::FindPattern(CLIENT_DLL, CS_XOR("48 89 0D ? ? ? ? 48 89 41"));
		if (addr != nullptr)
			GlobalVars = *reinterpret_cast<IGlobalVars**>(MEM::ResolveRelativeAddress(addr, 0x3, 0x7));
	}
	if (GlobalVars != nullptr)
		L_PRINT(LOG_INFO) << CS_XOR("captured GlobalVars at: ") << L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX) << reinterpret_cast<std::uintptr_t>(GlobalVars);
	else
		L_PRINT(LOG_WARNING) << CS_XOR("[I::Setup] Linux: GlobalVars not captured, will retry at LevelInit");

	// Trace
	{
		auto addr = MEM::FindPattern(CLIENT_DLL, CS_XOR("4C 8B 3D ? ? ? ? 24 C9 0C 49 66 0F 7F 45"));
		if (addr != nullptr)
			Trace = *reinterpret_cast<i_trace**>(MEM::GetAbsoluteAddress(addr, 0x3));
	}
	if (Trace != nullptr)
		L_PRINT(LOG_INFO) << CS_XOR("captured Trace at: ") << L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX) << reinterpret_cast<std::uintptr_t>(Trace);
	else
		L_PRINT(LOG_WARNING) << CS_XOR("[I::Setup] Linux: Trace not captured");

	// GameEvent - disabled on Linux, offset 0x3E is wrong
	// @todo: find correct offset for Linux
	L_PRINT(LOG_WARNING) << CS_XOR("[I::Setup] Linux: GameEvent not captured (needs offset fix)");
#endif

	return bSuccess;
}

#ifdef _WIN32
bool I::CreateRenderTarget(IDXGISwapChain* pSwapChain)  {
	SwapChain->pDXGISwapChain = pSwapChain;

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	if (FAILED(SwapChain->pDXGISwapChain->GetDesc(&swapChainDesc))) {
	L_PRINT(LOG_ERROR) << (CS_XOR("Failed to get swap chain description."));
		return false;
	}

	if (FAILED(SwapChain->pDXGISwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<PVOID*>(&Device)))) {
		L_PRINT(LOG_ERROR) << (CS_XOR("Failed to get device from swap chain."));
		return false;
	}

	Device->GetImmediateContext(&DeviceContext);

	ID3D11Texture2D* back_buffer;
	if (FAILED(SwapChain->pDXGISwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<PVOID*>(&back_buffer)))) {
		L_PRINT(LOG_ERROR) << (CS_XOR("Failed to get buffer from swap chain."));
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC desc;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;

	if (FAILED(Device->CreateRenderTargetView(back_buffer, &desc, &RenderTargetView))) {
		back_buffer->Release();
		L_PRINT(LOG_ERROR) << (CS_XOR("Failed to create render target view."));
		return false;
	}
	back_buffer->Release();

	return true;
}
void I::DestroyRenderTarget()
{
	if (RenderTargetView != nullptr)
	{
		RenderTargetView->Release();
		RenderTargetView = nullptr;
	}
}
#endif
