// used: __readfsdword
#ifdef _WIN32
#include <intrin.h>
#include <d3d11.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <stdio.h>
#include <string.h>
#endif

#include "memory.h"

// used: l_print
#include "log.h"
// used: chartohexint
#include "crt.h"
// used: pe64
#include "pe64.h"
#include "../core/spoofcall/invoker.h"
#include "../core/spoofcall/lazy_importer.hpp"

bool MEM::Setup()
{
	bool bSuccess = true;

#ifdef __linux__
	// Dump all loaded .so names so we can verify module names
	{
		FILE* f = fopen("/proc/self/maps", "r");
		FILE* out = fopen("/tmp/cs2_modules.txt", "w");
		if (f && out) {
			char line[512]; char prev[256] = {};
			while (fgets(line, sizeof(line), f)) {
				char* p = strrchr(line, '/');
				if (p && strstr(p, ".so")) {
					char* nl = strchr(p, '\n'); if (nl) *nl = 0;
					if (strcmp(p, prev) != 0) {
						fprintf(out, "%s\n", p);
						strncpy(prev, p, sizeof(prev)-1);
					}
				}
			}
			fclose(f); fclose(out);
		}
	}
#endif

	const void* hSDL3 = spoof_call<void*>(_fake_addr, &GetModuleBaseHandle, SDL3_DLL);
	const void* hDbgHelp = spoof_call<void*>(_fake_addr, &GetModuleBaseHandle, DBGHELP_DLL);
	const void* hTier0 = GetModuleBaseHandle(TIER0_DLL);

	L_PRINT(LOG_INFO) << CS_XOR("[MEM] hSDL3=") << (std::uintptr_t)hSDL3
	                  << CS_XOR(" hDbgHelp=") << (std::uintptr_t)hDbgHelp
	                  << CS_XOR(" hTier0=") << (std::uintptr_t)hTier0;

#ifdef _WIN32
	if (hSDL3 == nullptr || hDbgHelp == nullptr)
		return false;
#else
	// On Linux dbghelp is actually libtier0, SDL3 is libSDL3 — don't hard-fail here
	if (hSDL3 == nullptr) {
		L_PRINT(LOG_ERROR) << CS_XOR("[MEM] SDL3 not found");
		return false;
	}
#endif

#ifdef _WIN32
	fnUnDecorateSymbolName = reinterpret_cast<decltype(fnUnDecorateSymbolName)>(GetExportAddress(hDbgHelp, CS_XOR("UnDecorateSymbolName")));
	bSuccess &= (fnUnDecorateSymbolName != nullptr);
#else
	// Linux: use abi::__cxa_demangle from <cxxabi.h> instead — set via separate setup
	fnUnDecorateSymbolName = nullptr; // not needed, schema uses cxa_demangle directly
#endif

	fnSetRelativeMouseMode = reinterpret_cast<decltype(fnSetRelativeMouseMode)>(GetExportAddress(hSDL3, "SDL_SetRelativeMouseMode"));
	if (!fnSetRelativeMouseMode) // SDL3 renamed it
		fnSetRelativeMouseMode = reinterpret_cast<decltype(fnSetRelativeMouseMode)>(GetExportAddress(hSDL3, "SDL_SetWindowRelativeMouseMode"));
#ifdef _WIN32
	bSuccess &= (fnSetRelativeMouseMode != nullptr);
#else
	if (!fnSetRelativeMouseMode) L_PRINT(LOG_WARNING) << CS_XOR("[MEM] SDL_SetRelativeMouseMode not found (non-fatal)");
#endif

	fnSetWindowGrab = reinterpret_cast<decltype(fnSetWindowGrab)>(GetExportAddress(hSDL3, "SDL_SetWindowGrab"));
	if (!fnSetWindowGrab)
		fnSetWindowGrab = reinterpret_cast<decltype(fnSetWindowGrab)>(GetExportAddress(hSDL3, "SDL_SetWindowMouseGrab"));
#ifdef _WIN32
	bSuccess &= (fnSetWindowGrab != nullptr);
#else
	if (!fnSetWindowGrab) L_PRINT(LOG_WARNING) << CS_XOR("[MEM] SDL_SetWindowGrab not found (non-fatal)");
#endif

	fnWarpMouseInWindow = reinterpret_cast<decltype(fnWarpMouseInWindow)>(GetExportAddress(hSDL3, "SDL_WarpMouseInWindow"));
#ifdef _WIN32
	bSuccess &= (fnWarpMouseInWindow != nullptr);
#else
	if (!fnWarpMouseInWindow) L_PRINT(LOG_WARNING) << CS_XOR("[MEM] SDL_WarpMouseInWindow not found (non-fatal)");
#endif
	L_PRINT(LOG_INFO) << CS_XOR("[Memory] SDL3 functions done");

#ifdef _WIN32
	fnCreateMaterial = reinterpret_cast<decltype(fnCreateMaterial)>(FindPattern(MATERIAL_SYSTEM2_DLL, CS_XOR("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 81 EC ? ? ? ? 48 8D 0D")));
	bSuccess &= (fnCreateMaterial != nullptr);
	L_PRINT(LOG_INFO) << CS_XOR("[Memory] Loaded fnCreateMaterial");
#else
	// Windows-only byte pattern — skip on Linux, find via other means later
	fnCreateMaterial = nullptr;
	L_PRINT(LOG_WARNING) << CS_XOR("[MEM] fnCreateMaterial skipped on Linux");
#endif

#ifdef _WIN32
	load_key_value = reinterpret_cast<decltype(load_key_value)>(GetExportAddress(hTier0, CS_XOR("?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2@Z")));
	bSuccess &= (load_key_value != nullptr);
	L_PRINT(LOG_INFO) << CS_XOR("[Memory] Loaded load_key_value");
#else
	// Linux mangled name for LoadKV3 — look up via nm/dlsym; non-fatal if missing
	load_key_value = reinterpret_cast<decltype(load_key_value)>(
	    hTier0 ? dlsym(RTLD_DEFAULT, "_Z7LoadKV3P11KeyValues3PV10CUtlStringPKcRK8KV3ID_tS6_") : nullptr);
	if (!load_key_value)
		L_PRINT(LOG_WARNING) << CS_XOR("[Memory] load_key_value not found on Linux (non-fatal)");
#endif

	return bSuccess;
}

#pragma region memory_allocation

/*
 * overload global new/delete operators with our allocators
 * - @note: ensure that all sdk classes that can be instantiated have an overloaded constructor and/or game allocator, otherwise marked as non-constructible
 */
#ifdef _WIN32
void* __cdecl operator new(const std::size_t nSize)
{
	return MEM::HeapAlloc(nSize);
}

void* __cdecl operator new[](const std::size_t nSize)
{
	return MEM::HeapAlloc(nSize);
}

void __cdecl operator delete(void* pMemory) noexcept
{
	MEM::HeapFree(pMemory);
}

void __cdecl operator delete[](void* pMemory) noexcept
{
	MEM::HeapFree(pMemory);
}
#endif

#ifdef _WIN32
void* MEM::HeapAlloc(const std::size_t nSize)
{
	const HANDLE hHeap = ::GetProcessHeap();
	return ::HeapAlloc(hHeap, 0UL, nSize);
}

void MEM::HeapFree(void* pMemory)
{
	if (pMemory != nullptr)
	{
		const HANDLE hHeap = ::GetProcessHeap();
		::HeapFree(hHeap, 0UL, pMemory);
	}
}

void* MEM::HeapRealloc(void* pMemory, const std::size_t nNewSize)
{
	if (pMemory == nullptr)
		return HeapAlloc(nNewSize);

	if (nNewSize == 0UL)
	{
		HeapFree(pMemory);
		return nullptr;
	}

	const HANDLE hHeap = ::GetProcessHeap();
	return ::HeapReAlloc(hHeap, 0UL, pMemory, nNewSize);
}
#else // __linux__
void* MEM::HeapAlloc(const std::size_t nSize) { return malloc(nSize); }
void  MEM::HeapFree(void* pMemory) { if (pMemory) free(pMemory); }
void* MEM::HeapRealloc(void* pMemory, const std::size_t nNewSize)
{
	if (pMemory == nullptr) return malloc(nNewSize);
	if (nNewSize == 0UL) { free(pMemory); return nullptr; }
	return realloc(pMemory, nNewSize);
}
#endif

#pragma endregion

// @todo: move to win.cpp (or platform.cpp?) except getsectioninfo
#pragma region memory_get

#ifdef _WIN32
void* MEM::GetModuleBaseHandle(const wchar_t* wszModuleName)
{
	const _PEB* pPEB = reinterpret_cast<_PEB*>(__readgsqword(0x60));

	if (wszModuleName == nullptr)
		return pPEB->ImageBaseAddress;

	void* pModuleBase = nullptr;
	for (LIST_ENTRY* pListEntry = pPEB->Ldr->InMemoryOrderModuleList.Flink; pListEntry != &pPEB->Ldr->InMemoryOrderModuleList; pListEntry = pListEntry->Flink)
	{
		const _LDR_DATA_TABLE_ENTRY* pEntry = CONTAINING_RECORD(pListEntry, _LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

		if (pEntry->FullDllName.Buffer != nullptr && CRT::StringCompare(wszModuleName, pEntry->BaseDllName.Buffer) == 0)
		{
			pModuleBase = pEntry->DllBase;
			break;
		}
	}

	if (pModuleBase == nullptr)
		L_PRINT(LOG_ERROR) << CS_XOR("module base not found: \"") << wszModuleName << CS_XOR("\"");

	return pModuleBase;
}

const wchar_t* MEM::GetModuleBaseFileName(const void* hModuleBase)
{
	const _PEB* pPEB = reinterpret_cast<_PEB*>(__readgsqword(0x60));

	if (hModuleBase == nullptr)
		hModuleBase = pPEB->ImageBaseAddress;

	::EnterCriticalSection(pPEB->LoaderLock);

	const wchar_t* wszModuleName = nullptr;
	for (LIST_ENTRY* pListEntry = pPEB->Ldr->InMemoryOrderModuleList.Flink; pListEntry != &pPEB->Ldr->InMemoryOrderModuleList; pListEntry = pListEntry->Flink)
	{
		const _LDR_DATA_TABLE_ENTRY* pEntry = CONTAINING_RECORD(pListEntry, _LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

		if (pEntry->DllBase == hModuleBase)
		{
			wszModuleName = pEntry->BaseDllName.Buffer;
			break;
		}
	}

	::LeaveCriticalSection(pPEB->LoaderLock);

	return wszModuleName;
}
#else // __linux__
#include <link.h>
#include <elf.h>
#include <string.h>

static const char* _linux_basename(const char* path)
{
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static bool _linux_module_name_matches(const char* szModuleName, const char* candidate)
{
	if (!szModuleName || !candidate)
		return false;

	const char* candidateBase = _linux_basename(candidate);
	size_t expectedLen = strlen(szModuleName);

	if (strcmp(candidateBase, szModuleName) == 0)
		return true;

	// allow versioned sonames like libtier0.so.1, libclient.so.2, libfoo.so.0.0.0
	if (strncmp(candidateBase, szModuleName, expectedLen) == 0)
	{
		const char* suffix = candidateBase + expectedLen;
		if (*suffix == '.' || *suffix == '\0')
			return true;
	}

	return false;
}

static void* _linux_find_module_base(const char* szModuleName)
{
	struct ModuleSearchContext
	{
		const char* name;
		void* base;
	};
	ModuleSearchContext ctx{ szModuleName, nullptr };

	auto callback = [](struct dl_phdr_info* info, size_t, void* data) -> int {
		auto* ctx = reinterpret_cast<ModuleSearchContext*>(data);
		if (info->dlpi_name && info->dlpi_name[0] != '\0' && _linux_module_name_matches(ctx->name, info->dlpi_name))
		{
			ctx->base = reinterpret_cast<void*>(info->dlpi_addr);
			return 1;
		}
		return 0;
	};

	dl_iterate_phdr(callback, &ctx);
	return ctx.base;
}

void* MEM::GetModuleBaseHandle(const wchar_t* wszModuleName)
{
	if (wszModuleName == nullptr)
	{
		return nullptr;
	}

	char nbuf[512];
	int i = 0;
	while (wszModuleName[i] && i < 511)
	{
		nbuf[i] = static_cast<char>(static_cast<unsigned char>(wszModuleName[i]));
		i++;
	}
	nbuf[i] = '\0';

	return MEM::GetModuleBaseHandle(nbuf);
}

void* MEM::GetModuleBaseHandle(const char* szModuleName)
{
	if (!szModuleName)
		return nullptr;

	return _linux_find_module_base(szModuleName);
}

const wchar_t* MEM::GetModuleBaseFileName(const void* hModuleBase)
{
	if (hModuleBase == nullptr)
		return nullptr;

	static thread_local wchar_t moduleName[512];
	moduleName[0] = L'\0';

	struct ModuleNameContext
	{
		const void* base;
		wchar_t* buffer;
		size_t capacity;
	};

	auto callback = [](struct dl_phdr_info* info, size_t, void* data) -> int {
		auto* ctx = reinterpret_cast<ModuleNameContext*>(data);
		if (reinterpret_cast<const void*>(info->dlpi_addr) != ctx->base)
			return 0;

		const char* name = _linux_basename(info->dlpi_name);
		size_t length = strlen(name);
		size_t i = 0;
		for (; i + 1 < ctx->capacity && i < length; ++i)
			ctx->buffer[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
		ctx->buffer[i] = L'\0';
		return 1;
	};

	ModuleNameContext ctx{ hModuleBase, moduleName, static_cast<size_t>(std::size(moduleName)) };
	dl_iterate_phdr(callback, &ctx);

	return moduleName[0] != L'\0' ? moduleName : nullptr;
}
#endif

#ifdef _WIN32
void* MEM::GetExportAddress(const void* hModuleBase, const char* szProcedureName)
{
	const auto pBaseAddress = static_cast<const std::uint8_t*>(hModuleBase);

	const auto pIDH = static_cast<const IMAGE_DOS_HEADER*>(hModuleBase);
	if (pIDH->e_magic != IMAGE_DOS_SIGNATURE)
		return nullptr;

	const auto pINH = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pBaseAddress + pIDH->e_lfanew);
	if (pINH->Signature != IMAGE_NT_SIGNATURE)
		return nullptr;

	const IMAGE_OPTIONAL_HEADER64* pIOH = &pINH->OptionalHeader;
	const std::uintptr_t nExportDirectorySize = pIOH->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
	const std::uintptr_t uExportDirectoryAddress = pIOH->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;

	if (nExportDirectorySize == 0U || uExportDirectoryAddress == 0U)
	{
		L_PRINT(LOG_ERROR) << CS_XOR("module has no exports: \"") << GetModuleBaseFileName(hModuleBase) << CS_XOR("\"");
		return nullptr;
	}

	const auto pIED = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(pBaseAddress + uExportDirectoryAddress);
	const auto pNamesRVA = reinterpret_cast<const std::uint32_t*>(pBaseAddress + pIED->AddressOfNames);
	const auto pNameOrdinalsRVA = reinterpret_cast<const std::uint16_t*>(pBaseAddress + pIED->AddressOfNameOrdinals);
	const auto pFunctionsRVA = reinterpret_cast<const std::uint32_t*>(pBaseAddress + pIED->AddressOfFunctions);

	// Perform binary search to find the export by name
	std::size_t nRight = pIED->NumberOfNames, nLeft = 0U;
	while (nRight != nLeft)
	{
		// Avoid INT_MAX/2 overflow
		const std::size_t uMiddle = nLeft + ((nRight - nLeft) >> 1U);
		const int iResult = CRT::StringCompare(szProcedureName, reinterpret_cast<const char*>(pBaseAddress + pNamesRVA[uMiddle]));

		if (iResult == 0)
		{
			const std::uint32_t uFunctionRVA = pFunctionsRVA[pNameOrdinalsRVA[uMiddle]];

#ifdef _DEBUG
			L_PRINT(LOG_INFO) << CS_XOR("export found: \"") << reinterpret_cast<const char*>(pBaseAddress + pNamesRVA[uMiddle]) << CS_XOR("\" in \"") << GetModuleBaseFileName(hModuleBase) << CS_XOR("\" at: ") << L::AddFlags(LOG_MODE_INT_SHOWBASE | LOG_MODE_INT_FORMAT_HEX) << uFunctionRVA;
#else
			L_PRINT(LOG_INFO) << CS_XOR("export found: ") << szProcedureName;
#endif // _DEBUG

			// Check if it's a forwarded export
			if (uFunctionRVA >= uExportDirectoryAddress && uFunctionRVA - uExportDirectoryAddress < nExportDirectorySize)
			{
				// Forwarded exports are not supported
				break;
			}

			return const_cast<std::uint8_t*>(pBaseAddress) + uFunctionRVA;
		}

		if (iResult > 0)
			nLeft = uMiddle + 1;
		else
			nRight = uMiddle;
	}

	L_PRINT(LOG_ERROR) << CS_XOR("export not found: ") << szProcedureName;

	// Export not found
	return nullptr;
}
#else // __linux__
void* MEM::GetExportAddress(const void* hModuleBase, const char* szProcedureName)
{
	if (!hModuleBase || !szProcedureName)
		return nullptr;

	struct ExportSearchContext
	{
		const void* moduleBase;
		const char* procName;
		void* result;
	};
	ExportSearchContext ctx{ hModuleBase, szProcedureName, nullptr };

	auto callback = [](struct dl_phdr_info* info, size_t, void* data) -> int {
		auto* ctx = reinterpret_cast<ExportSearchContext*>(data);
		if (reinterpret_cast<const void*>(info->dlpi_addr) != ctx->moduleBase)
			return 0;

		if (!info->dlpi_name || info->dlpi_name[0] == '\0')
			return 0;

		void* handle = dlopen(info->dlpi_name, RTLD_LAZY | RTLD_NOLOAD);
		if (!handle)
			return 0;

		ctx->result = dlsym(handle, ctx->procName);
		dlclose(handle);
		return ctx->result ? 1 : 0;
	};

	dl_iterate_phdr(callback, &ctx);

	if (ctx.result)
		return ctx.result;

	// Try global symbol table as fallback.
	return dlsym(RTLD_DEFAULT, szProcedureName);
}
#endif // _WIN32

#ifdef _WIN32
bool MEM::GetSectionInfo(const void* hModuleBase, const char* szSectionName, std::uint8_t** ppSectionStart, std::size_t* pnSectionSize)
{
	const auto pBaseAddress = static_cast<const std::uint8_t*>(hModuleBase);

	const auto pIDH = static_cast<const IMAGE_DOS_HEADER*>(hModuleBase);
	if (pIDH->e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	const auto pINH = reinterpret_cast<const IMAGE_NT_HEADERS*>(pBaseAddress + pIDH->e_lfanew);
	if (pINH->Signature != IMAGE_NT_SIGNATURE)
		return false;

	const IMAGE_SECTION_HEADER* pISH = IMAGE_FIRST_SECTION(pINH);

	// go through all code sections
	for (WORD i = 0U; i < pINH->FileHeader.NumberOfSections; i++, pISH++)
	{
		// @test: use case insensitive comparison instead?
		if (CRT::StringCompareN(szSectionName, reinterpret_cast<const char*>(pISH->Name), IMAGE_SIZEOF_SHORT_NAME) == 0)
		{
			if (ppSectionStart != nullptr)
				*ppSectionStart = const_cast<std::uint8_t*>(pBaseAddress) + pISH->VirtualAddress;

			if (pnSectionSize != nullptr)
				*pnSectionSize = pISH->SizeOfRawData;

			return true;
		}
	}

	L_PRINT(LOG_ERROR) << CS_XOR("code section not found: \"") << szSectionName << CS_XOR("\"");
	return false;
}
#else // __linux__
bool MEM::GetSectionInfo(const void*, const char*, std::uint8_t**, std::size_t*) { return false; }
#endif // _WIN32

#pragma endregion

#pragma region memory_search
UTILPtr MEM::FindPatterns(const wchar_t* wszModuleName, const char* szPattern)
{
	// convert pattern string to byte array
	const std::size_t nApproximateBufferSize = (CRT::StringLength(szPattern) >> 1U) + 1U;
	std::uint8_t* arrByteBuffer = static_cast<std::uint8_t*>(MEM_STACKALLOC(nApproximateBufferSize));
	char* szMaskBuffer = static_cast<char*>(MEM_STACKALLOC(nApproximateBufferSize));
	PatternToBytes(szPattern, arrByteBuffer, szMaskBuffer);

	// @test: use search with straight in-place conversion? do not think it will be faster, cuz of bunch of new checks that gonna be performed for each iteration
	return FindPattern(wszModuleName, reinterpret_cast<const char*>(arrByteBuffer), szMaskBuffer);
}
std::uint8_t* MEM::FindPattern(const wchar_t* wszModuleName, const char* szPattern)
{
	// convert pattern string to byte array
	const std::size_t nApproximateBufferSize = (CRT::StringLength(szPattern) >> 1U) + 1U;
	std::uint8_t* arrByteBuffer = static_cast<std::uint8_t*>(MEM_STACKALLOC(nApproximateBufferSize));
	char* szMaskBuffer = static_cast<char*>(MEM_STACKALLOC(nApproximateBufferSize));
	PatternToBytes(szPattern, arrByteBuffer, szMaskBuffer);

	// @test: use search with straight in-place conversion? do not think it will be faster, cuz of bunch of new checks that gonna be performed for each iteration
	return FindPattern(wszModuleName, reinterpret_cast<const char*>(arrByteBuffer), szMaskBuffer);
}
#ifdef _WIN32
std::uint8_t* MEM::FindPattern(const wchar_t* wszModuleName, const char* szBytePattern, const char* szByteMask)
{
	const void* hModuleBase = spoof_call<void*>(_fake_addr, &GetModuleBaseHandle, wszModuleName);

	if (hModuleBase == nullptr)
	{
		L_PRINT(LOG_ERROR) << CS_XOR("failed to get module handle for: \"") << wszModuleName << CS_XOR("\"");
		return nullptr;
	}

	const auto pBaseAddress = static_cast<const std::uint8_t*>(hModuleBase);

	const auto pIDH = static_cast<const IMAGE_DOS_HEADER*>(hModuleBase);
	if (pIDH->e_magic != IMAGE_DOS_SIGNATURE)
	{
		L_PRINT(LOG_ERROR) << CS_XOR("failed to get module size, image is invalid");
		return nullptr;
	}

	const auto pINH = reinterpret_cast<const IMAGE_NT_HEADERS*>(pBaseAddress + pIDH->e_lfanew);
	if (pINH->Signature != IMAGE_NT_SIGNATURE)
	{
		L_PRINT(LOG_ERROR) << CS_XOR("failed to get module size, image is invalid");
		return nullptr;
	}

	const std::uint8_t* arrByteBuffer = reinterpret_cast<const std::uint8_t*>(szBytePattern);
	const std::size_t nByteCount = CRT::StringLength(szByteMask);

	std::uint8_t* pFoundAddress = nullptr;
	pFoundAddress = FindPatternEx(pBaseAddress, pINH->OptionalHeader.SizeOfImage, arrByteBuffer, nByteCount, szByteMask);

	if (pFoundAddress == nullptr)
	{
		char* szPattern = static_cast<char*>(MEM_STACKALLOC((nByteCount << 1U) + nByteCount));
		[[maybe_unused]] const std::size_t nConvertedPatternLength = BytesToPattern(arrByteBuffer, nByteCount, szPattern);
		L_PRINT(LOG_ERROR) << CS_XOR("pattern not found: \"") << szPattern << CS_XOR("\"");
		MEM_STACKFREE(szPattern);
	}
	return pFoundAddress;
}
#else // __linux__
static std::vector<std::pair<const std::uint8_t*, std::size_t>> _linux_get_module_ranges(const char* moduleName)
{
	struct ModuleRangesContext
	{
		const char* name;
		std::vector<std::pair<const std::uint8_t*, std::size_t>> ranges;
	};

	auto callback = [](struct dl_phdr_info* info, size_t, void* data) -> int {
		auto* ctx = reinterpret_cast<ModuleRangesContext*>(data);
		if (!info->dlpi_name || info->dlpi_name[0] == '\0')
			return 0;

		if (!_linux_module_name_matches(ctx->name, info->dlpi_name))
			return 0;

		for (size_t idx = 0; idx < info->dlpi_phnum; ++idx)
		{
			const ElfW(Phdr)& phdr = info->dlpi_phdr[idx];
			if (phdr.p_type != PT_LOAD)
				continue;
			if (!(phdr.p_flags & PF_R))
				continue;

			const std::uint8_t* segmentStart = reinterpret_cast<const std::uint8_t*>(info->dlpi_addr + phdr.p_vaddr);
			const std::size_t segmentSize = static_cast<std::size_t>(phdr.p_memsz);
			if (segmentSize == 0)
				continue;

			ctx->ranges.emplace_back(segmentStart, segmentSize);
		}

		return 1; // stop after module found
	};

	ModuleRangesContext ctx{ moduleName, {} };
	dl_iterate_phdr(callback, &ctx);
	return ctx.ranges;
}

static std::uint8_t* _linux_search_module_segments(const void* moduleBase, const char* moduleName, const std::uint8_t* arrByteBuffer, const std::size_t nByteCount, const char* szByteMask)
{
	if (!moduleBase || !moduleName || !arrByteBuffer || nByteCount == 0)
		return nullptr;

	std::vector<std::pair<const std::uint8_t*, std::size_t>> ranges = _linux_get_module_ranges(moduleName);
	if (ranges.empty())
		return nullptr;

	for (const auto& range : ranges)
	{
		const std::uint8_t* segmentStart = range.first;
		std::size_t segmentSize = range.second;
		if (segmentSize < nByteCount)
			continue;

		std::uint8_t* found = MEM::FindPatternEx(segmentStart, segmentSize, arrByteBuffer, nByteCount, szByteMask);
		if (found)
			return found;
	}

	return nullptr;
}

std::uint8_t* MEM::FindPattern(const wchar_t* wszModuleName, const char* szBytePattern, const char* szByteMask)
{
	char nbuf[512]; int i = 0;
	while (wszModuleName[i] && i < 511) { nbuf[i] = static_cast<char>(static_cast<unsigned char>(wszModuleName[i])); i++; }
	nbuf[i] = '\0';
	const void* hModuleBase = MEM::GetModuleBaseHandle(nbuf);
	if (!hModuleBase) return nullptr;

	const std::uint8_t* arrByteBuffer = reinterpret_cast<const std::uint8_t*>(szBytePattern);
	const std::size_t nByteCount = CRT::StringLength(szByteMask);
	return _linux_search_module_segments(hModuleBase, nbuf, arrByteBuffer, nByteCount, szByteMask);
}
#endif

// @todo: msvc poorly optimizes this, it looks even better w/o optimization at all
std::uint8_t* MEM::FindPatternEx(const std::uint8_t* pRegionStart, const std::size_t nRegionSize, const std::uint8_t* arrByteBuffer, const std::size_t nByteCount, const char* szByteMask)
{
	if (pRegionStart == nullptr || arrByteBuffer == nullptr || nRegionSize == 0 || nByteCount == 0)
		return nullptr;

	const std::uint8_t* const pRegionEnd = pRegionStart + nRegionSize;
	const bool bIsMaskUsed = (szByteMask != nullptr);

	for (const std::uint8_t* pCurrentAddress = pRegionStart; pCurrentAddress + nByteCount <= pRegionEnd; ++pCurrentAddress)
	{
		if ((bIsMaskUsed && szByteMask[0] == '?') || *pCurrentAddress == arrByteBuffer[0])
		{
			std::size_t nComparedBytes = 1U;
			for (; nComparedBytes < nByteCount; ++nComparedBytes)
			{
				if (bIsMaskUsed && szByteMask[nComparedBytes] == '?')
					continue;
				if (pCurrentAddress[nComparedBytes] != arrByteBuffer[nComparedBytes])
					break;
			}

			if (nComparedBytes == nByteCount)
				return const_cast<std::uint8_t*>(pCurrentAddress);
		}
	}

	return nullptr;
}

std::vector<std::uint8_t*> MEM::FindPatternAllOccurrencesEx(const std::uint8_t* pRegionStart, const std::size_t nRegionSize, const std::uint8_t* arrByteBuffer, const std::size_t nByteCount, const char* szByteMask)
{
	if (pRegionStart == nullptr || arrByteBuffer == nullptr || nRegionSize == 0 || nByteCount == 0 || nRegionSize < nByteCount)
		return {};

	const std::uint8_t* const pRegionEnd = pRegionStart + nRegionSize;
	const bool bIsMaskUsed = (szByteMask != nullptr);

	// container for addresses of the all found occurrences
	std::vector<std::uint8_t*> vecOccurrences = {};

	for (std::uint8_t* pCurrentByte = const_cast<std::uint8_t*>(pRegionStart); pCurrentByte < pRegionEnd; ++pCurrentByte)
	{
		// do a first byte check before entering the loop, otherwise if there two consecutive bytes of first byte in the buffer, we may skip both and fail the search
		if ((!bIsMaskUsed || *szByteMask != '?') && *pCurrentByte != *arrByteBuffer)
			continue;

		// check for bytes sequence match
		bool bSequenceMatch = true;
		for (std::size_t i = 1U; i < nByteCount; i++)
		{
			// compare sequence and continue on wildcard or skip forward on first mismatched byte
			if ((!bIsMaskUsed || szByteMask[i] != '?') && pCurrentByte[i] != arrByteBuffer[i])
			{
				// skip non suitable bytes
				pCurrentByte += i - 1U;

				bSequenceMatch = false;
				break;
			}
		}

		// check did we found address
		if (bSequenceMatch)
			vecOccurrences.push_back(pCurrentByte);
	}

	return vecOccurrences;
}

#pragma endregion

#pragma region memory_extra

std::size_t MEM::PatternToBytes(const char* szPattern, std::uint8_t* pOutByteBuffer, char* szOutMaskBuffer)
{
	std::uint8_t* pCurrentByte = pOutByteBuffer;

	while (*szPattern != '\0')
	{
		// check is a wildcard
		if (*szPattern == '?')
		{
			++szPattern;
#ifdef CS_PARANOID
			CS_ASSERT(*szPattern == '\0' || *szPattern == ' ' || *szPattern == '?'); // we're expect that next character either terminating null, whitespace or part of double wildcard (note that it's required if your pattern written without whitespaces)
#endif

			// ignore that
			*pCurrentByte++ = 0U;
			*szOutMaskBuffer++ = '?';
		}
		// check is not space
		else if (*szPattern != ' ')
		{
			// convert two consistent numbers in a row to byte value
			std::uint8_t uByte = static_cast<std::uint8_t>(CRT::CharToHexInt(*szPattern) << 4);

			++szPattern;
#ifdef CS_PARANOID
			CS_ASSERT(*szPattern != '\0' && *szPattern != '?' && *szPattern != ' '); // we're expect that byte always represented by two numbers in a row
#endif

			uByte |= static_cast<std::uint8_t>(CRT::CharToHexInt(*szPattern));

			*pCurrentByte++ = uByte;
			*szOutMaskBuffer++ = 'x';
		}

		++szPattern;
	}

	// zero terminate both buffers
	*pCurrentByte = 0U;
	*szOutMaskBuffer = '\0';

	return pCurrentByte - pOutByteBuffer;
}

std::size_t MEM::BytesToPattern(const std::uint8_t* pByteBuffer, const std::size_t nByteCount, char* szOutBuffer)
{
	char* szCurrentPattern = szOutBuffer;

	for (std::size_t i = 0U; i < nByteCount; i++)
	{
		// manually convert byte to chars
		const char* szHexByte = &CRT::_TWO_DIGITS_HEX_LUT[pByteBuffer[i] * 2U];
		*szCurrentPattern++ = szHexByte[0];
		*szCurrentPattern++ = szHexByte[1];
		*szCurrentPattern++ = ' ';
	}
	*--szCurrentPattern = '\0';

	return szCurrentPattern - szOutBuffer;
}

#pragma endregion

#ifdef __linux__
static wchar_t _mw[512];
static const wchar_t* _n2w(const char* s) {
    size_t i = 0;
    while (s[i] && i + 1 < 512) { _mw[i] = (wchar_t)(unsigned char)s[i]; i++; }
    _mw[i] = L'\0';
    return _mw;
}
std::uint8_t* MEM::FindPattern(const char* szModuleName, const char* szPattern) {
    return MEM::FindPattern(_n2w(szModuleName), szPattern);
}
UTILPtr MEM::FindPatterns(const char* szModuleName, const char* szPattern) {
    return MEM::FindPatterns(_n2w(szModuleName), szPattern);
}
std::uint8_t* MEM::FindPattern(const char* szModuleName, const char* szBytePattern, const char* szByteMask) {
    return MEM::FindPattern(_n2w(szModuleName), szBytePattern, szByteMask);
}
#endif
