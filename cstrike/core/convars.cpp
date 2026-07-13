// used: [stl] vector
#include <vector>
// used: [stl] find_if
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>

#include "convars.h"

// used: convar interface
#include "interfaces.h"
#include "../sdk/interfaces/ienginecvar.h"
// used: l_print
#include "../utilities/log.h"

// used: getworkingpath
#include "../core.h"

#ifdef __linux__
namespace
{
struct ReadableRange
{
	std::uintptr_t begin;
	std::uintptr_t end;
};

const std::vector<ReadableRange>& GetReadableRanges()
{
	static const std::vector<ReadableRange> ranges = [] {
		std::vector<ReadableRange> result;
		FILE* maps = std::fopen("/proc/self/maps", "r");
		if (maps == nullptr)
			return result;
		char line[1024];
		while (std::fgets(line, sizeof(line), maps) != nullptr)
		{
			unsigned long long begin = 0, end = 0;
			char permissions[5] = {};
			if (std::sscanf(line, "%llx-%llx %4s", &begin, &end, permissions) == 3 && permissions[0] == 'r')
				result.push_back({static_cast<std::uintptr_t>(begin), static_cast<std::uintptr_t>(end)});
		}
		std::fclose(maps);
		return result;
	}();
	return ranges;
}

bool IsReadable(const void* pointer, std::size_t size)
{
	if (pointer == nullptr || size == 0)
		return false;
	const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
	if (begin > std::numeric_limits<std::uintptr_t>::max() - size)
		return false;
	const auto end = begin + size;
	for (const auto& range : GetReadableRanges())
		if (begin >= range.begin && end <= range.end)
			return true;
	return false;
}

bool IsReadableString(const char* text)
{
	if (text == nullptr)
		return false;
	for (std::size_t index = 0; index < 256; ++index)
	{
		if (!IsReadable(text + index, 1))
			return false;
		const unsigned char character = static_cast<unsigned char>(text[index]);
		if (character == '\0')
			return index != 0;
		if (character < 0x20 || character == 0x7F)
			return false;
	}
	return false;
}

std::vector<CConVar*> GetNativeConVars()
{
	std::vector<CConVar*> result;
	if (I::Cvar == nullptr || !IsReadable(I::Cvar, 0x168))
		return result;

	const auto base = reinterpret_cast<const std::uint8_t*>(I::Cvar);
	// Current native VEngineCvar007 linked registry: allocation at 0x4A,
	// 0x10-byte node storage at 0x50, and the first uint16 index at 0x58.
	const std::uint16_t allocation = *reinterpret_cast<const std::uint16_t*>(base + 0x4A);
	const auto entries = (allocation & 0x7FFFU) != 0
		? *reinterpret_cast<const std::uint8_t* const*>(base + 0x50)
		: nullptr;
	std::uint16_t index = *reinterpret_cast<const std::uint16_t*>(base + 0x58);
	if (entries == nullptr || index == 0xFFFFU)
		return result;

	result.reserve(4096);
	std::unordered_set<CConVar*> seen;
	std::unordered_set<std::uint16_t> seenIndices;
	for (std::size_t visited = 0; index != 0xFFFFU && visited < 65536; ++visited)
	{
		if (!seenIndices.insert(index).second)
			break;
		const auto* node = entries + static_cast<std::size_t>(index) * 0x10;
		if (!IsReadable(node, 0x10))
			break;
		auto* convar = *reinterpret_cast<CConVar* const*>(node);
		index = *reinterpret_cast<const std::uint16_t*>(node + 0xA);
		if (convar == nullptr || !IsReadable(convar, 0x58))
			continue;
		const char* name = *reinterpret_cast<const char* const*>(convar);
		if (!IsReadableString(name) || !seen.insert(convar).second)
			continue;
		result.push_back(convar);
	}
	return result;
}
}
#endif

#ifdef _WIN32
inline static void WriteConVarType(HANDLE hFile, const uint32_t nType)
{
	switch ((EConVarType)nType)
	{
	case EConVarType_Bool:      ::WriteFile(hFile, "[bool] ",        7,  nullptr, nullptr); break;
	case EConVarType_Int16:     ::WriteFile(hFile, "[int16] ",       8,  nullptr, nullptr); break;
	case EConVarType_UInt16:    ::WriteFile(hFile, "[uint16] ",      9,  nullptr, nullptr); break;
	case EConVarType_Int32:     ::WriteFile(hFile, "[int32] ",       8,  nullptr, nullptr); break;
	case EConVarType_UInt32:    ::WriteFile(hFile, "[uint32] ",      9,  nullptr, nullptr); break;
	case EConVarType_Int64:     ::WriteFile(hFile, "[int64] ",       8,  nullptr, nullptr); break;
	case EConVarType_UInt64:    ::WriteFile(hFile, "[uint64] ",      9,  nullptr, nullptr); break;
	case EConVarType_Float32:   ::WriteFile(hFile, "[float32] ",     10, nullptr, nullptr); break;
	case EConVarType_Float64:   ::WriteFile(hFile, "[float64] ",     10, nullptr, nullptr); break;
	case EConVarType_String:    ::WriteFile(hFile, "[string] ",      9,  nullptr, nullptr); break;
	case EConVarType_Color:     ::WriteFile(hFile, "[color] ",       8,  nullptr, nullptr); break;
	case EConVarType_Vector2:   ::WriteFile(hFile, "[vector2] ",     10, nullptr, nullptr); break;
	case EConVarType_Vector3:   ::WriteFile(hFile, "[vector3] ",     10, nullptr, nullptr); break;
	case EConVarType_Vector4:   ::WriteFile(hFile, "[vector4] ",     10, nullptr, nullptr); break;
	case EConVarType_Qangle:    ::WriteFile(hFile, "[qangle] ",      9,  nullptr, nullptr); break;
	default:                    ::WriteFile(hFile, "[unknown-type] ", 15, nullptr, nullptr); break;
	}
}
inline static void WriteConVarFlags(HANDLE hFile, const uint32_t nFlags)
{
	if (nFlags & FCVAR_CLIENTDLL)    ::WriteFile(hFile, "[client.dll] ",  13, nullptr, nullptr);
	else if (nFlags & FCVAR_GAMEDLL) ::WriteFile(hFile, "[game's dll] ",  13, nullptr, nullptr);
	if (nFlags & FCVAR_PROTECTED)    ::WriteFile(hFile, "[protected] ",   12, nullptr, nullptr);
	if (nFlags & FCVAR_CHEAT)        ::WriteFile(hFile, "[cheat] ",        8, nullptr, nullptr);
	if (nFlags & FCVAR_HIDDEN)       ::WriteFile(hFile, "[hidden] ",       9, nullptr, nullptr);
	if (nFlags & FCVAR_DEVELOPMENTONLY) ::WriteFile(hFile, "[devonly] ",  10, nullptr, nullptr);
	::WriteFile(hFile, "\n", 1, nullptr, nullptr);
}
#else
inline static void WriteConVarType(FILE* hFile, const uint32_t nType)
{
	switch ((EConVarType)nType)
	{
	case EConVarType_Bool:      fputs("[bool] ",        hFile); break;
	case EConVarType_Int16:     fputs("[int16] ",       hFile); break;
	case EConVarType_UInt16:    fputs("[uint16] ",      hFile); break;
	case EConVarType_Int32:     fputs("[int32] ",       hFile); break;
	case EConVarType_UInt32:    fputs("[uint32] ",      hFile); break;
	case EConVarType_Int64:     fputs("[int64] ",       hFile); break;
	case EConVarType_UInt64:    fputs("[uint64] ",      hFile); break;
	case EConVarType_Float32:   fputs("[float32] ",     hFile); break;
	case EConVarType_Float64:   fputs("[float64] ",     hFile); break;
	case EConVarType_String:    fputs("[string] ",      hFile); break;
	case EConVarType_Color:     fputs("[color] ",       hFile); break;
	case EConVarType_Vector2:   fputs("[vector2] ",     hFile); break;
	case EConVarType_Vector3:   fputs("[vector3] ",     hFile); break;
	case EConVarType_Vector4:   fputs("[vector4] ",     hFile); break;
	case EConVarType_Qangle:    fputs("[qangle] ",      hFile); break;
	default:                    fputs("[unknown-type] ", hFile); break;
	}
}
inline static void WriteConVarFlags(FILE* hFile, const uint32_t nFlags)
{
	if (nFlags & FCVAR_CLIENTDLL)    fputs("[client.dll] ",  hFile);
	else if (nFlags & FCVAR_GAMEDLL) fputs("[game's dll] ",  hFile);
	if (nFlags & FCVAR_PROTECTED)    fputs("[protected] ",   hFile);
	if (nFlags & FCVAR_CHEAT)        fputs("[cheat] ",       hFile);
	if (nFlags & FCVAR_HIDDEN)       fputs("[hidden] ",      hFile);
	if (nFlags & FCVAR_DEVELOPMENTONLY) fputs("[devonly] ",  hFile);
	fputs("\n", hFile);
}
#endif

bool CONVAR::Dump(const wchar_t* wszFileName)
{
#ifdef __linux__
	wchar_t workingPath[MAX_PATH] = {};
	if (!CORE::GetWorkingPath(workingPath))
		return false;
	char outputPath[MAX_PATH * 2] = {};
	std::size_t position = 0;
	for (std::size_t index = 0; workingPath[index] != L'\0' && position + 1 < sizeof(outputPath); ++index)
		outputPath[position++] = static_cast<char>(workingPath[index]);
	for (std::size_t index = 0; wszFileName != nullptr && wszFileName[index] != L'\0' && position + 1 < sizeof(outputPath); ++index)
		outputPath[position++] = static_cast<char>(wszFileName[index]);
	outputPath[position] = '\0';

	FILE* output = std::fopen(outputPath, "w");
	if (output == nullptr)
		return false;
	auto convars = GetNativeConVars();
	std::ranges::sort(convars, [](const CConVar* left, const CConVar* right) {
		return std::strcmp(left->szName, right->szName) < 0;
	});
	std::fprintf(output, "Axion native Linux runtime CVar dump (%zu entries)\n\n", convars.size());
	if (convars.empty())
		std::fputs("Native CVar registry was not available in this game build.\n", output);
	for (const CConVar* convar : convars)
	{
		WriteConVarType(output, convar->nType);
		std::fprintf(output, "%s ", convar->szName);
		WriteConVarFlags(output, convar->nFlags);
	}
	std::fclose(output);
	return true;
#else
#ifdef _WIN32
	wchar_t wszDumpFilePath[MAX_PATH];
	if (!CORE::GetWorkingPath(wszDumpFilePath))
		return false;
	CRT::StringCat(wszDumpFilePath, wszFileName);
	HANDLE hOutFile = ::CreateFileW(wszDumpFilePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hOutFile == INVALID_HANDLE_VALUE)
		return false;
	const std::time_t time = std::time(nullptr);
	std::tm timePoint;
	localtime_s(&timePoint, &time);
	CRT::String_t<64> szTimeBuffer(CS_XOR("[%d-%m-%Y %T] asphyxia | convars dump\n\n"), &timePoint);
	::WriteFile(hOutFile, szTimeBuffer.Data(), szTimeBuffer.Length(), nullptr, nullptr);
#else
	FILE* hOutFile = fopen("/tmp/cs2_convars_dump.txt", "w");
	if (!hOutFile) return false;
	const std::time_t time = std::time(nullptr);
	std::tm timePoint; localtime_r(&time, &timePoint);
	char szTimeBuf[64]; strftime(szTimeBuf, sizeof(szTimeBuf), "[%d-%m-%Y %T] convars dump\n\n", &timePoint);
	fputs(szTimeBuf, hOutFile);
#endif

	for (int i = I::Cvar->listConvars.Head(); i != I::Cvar->listConvars.InvalidIndex(); i = I::Cvar->listConvars.Next(i))
	{
		CConVar* pConVar = I::Cvar->listConvars.Element(i);
		if (pConVar != nullptr)
		{
			WriteConVarType(hOutFile, pConVar->nType);
			CRT::String_t<526> szBuffer(CS_XOR("%s : \"%s\" "), pConVar->szName, pConVar->szDescription[0] == '\0' ? CS_XOR("no description") : pConVar->szDescription);
#ifdef _WIN32
			::WriteFile(hOutFile, szBuffer.Data(), szBuffer.Length(), nullptr, nullptr);
#else
			fputs(szBuffer.Data(), hOutFile);
#endif
			WriteConVarFlags(hOutFile, pConVar->nFlags);
		}
	}

#ifdef _WIN32
	::CloseHandle(hOutFile);
#else
	fclose(hOutFile);
#endif
	return true;
#endif
}

CConVar* CONVAR::Find(const char* name)
{
	if (name == nullptr)
		return nullptr;
#ifdef __linux__
	for (CConVar* convar : GetNativeConVars())
		if (std::strcmp(convar->szName, name) == 0)
			return convar;
	return nullptr;
#else
	return I::Cvar != nullptr ? I::Cvar->Find(FNV1A::Hash(name)) : nullptr;
#endif
}

bool CONVAR::Setup()
{
#ifdef __linux__
	m_pitch = Find("m_pitch");
	m_yaw = Find("m_yaw");
	sensitivity = Find("sensitivity");
	game_type = Find("game_type");
	game_mode = Find("game_mode");
	mp_teammates_are_enemies = Find("mp_teammates_are_enemies");
	sv_autobunnyhopping = Find("sv_autobunnyhopping");
	cam_idealdist = Find("cam_idealdist");
	cam_collision = Find("cam_collision");
	cam_snapto = Find("cam_snapto");
	cl_thirdperson = Find("cl_thirdperson");
	c_thirdpersonshoulder = Find("c_thirdpersonshoulder");
	c_thirdpersonshoulderaimdist = Find("c_thirdpersonshoulderaimdist");
	c_thirdpersonshoulderdist = Find("c_thirdpersonshoulderdist");
	c_thirdpersonshoulderheight = Find("c_thirdpersonshoulderheight");
	c_thirdpersonshoulderoffset = Find("c_thirdpersonshoulderoffset");
	cl_interpolate = Find("cl_interpolate");
	cl_interp_ratio = Find("cl_interp_ratio");
	if (GetNativeConVars().empty())
		L_PRINT(LOG_WARNING) << CS_XOR("Linux CVar registry layout did not validate; CVar-backed features will stay disabled");
	else
		L_PRINT(LOG_INFO) << CS_XOR("Linux CVar registry captured; runtime dump and third person are available");
	return true;
#endif
	bool bSuccess = true;

	m_pitch = I::Cvar->Find(FNV1A::HashConst("m_pitch"));
	bSuccess &= m_pitch != nullptr;

	m_yaw = I::Cvar->Find(FNV1A::HashConst("m_yaw"));
	bSuccess &= m_yaw != nullptr;

	sensitivity = I::Cvar->Find(FNV1A::HashConst("sensitivity"));
	bSuccess &= sensitivity != nullptr;

	game_type = I::Cvar->Find(FNV1A::HashConst("game_type"));
	bSuccess &= game_type != nullptr;

	game_mode = I::Cvar->Find(FNV1A::HashConst("game_mode"));
	bSuccess &= game_mode != nullptr;

	mp_teammates_are_enemies = I::Cvar->Find(FNV1A::HashConst("mp_teammates_are_enemies"));
	bSuccess &= mp_teammates_are_enemies != nullptr;

	sv_autobunnyhopping = I::Cvar->Find(FNV1A::HashConst("sv_autobunnyhopping"));
	bSuccess &= sv_autobunnyhopping != nullptr;

	cam_idealdist = I::Cvar->Find(FNV1A::HashConst("cam_idealdist")); // flaot
	bSuccess &= cam_idealdist != nullptr;

	cam_collision = I::Cvar->Find(FNV1A::HashConst("cam_collision")); // flaot
	bSuccess &= cam_collision != nullptr;

	cam_snapto = I::Cvar->Find(FNV1A::HashConst("cam_snapto")); // flaot
	bSuccess &= cam_snapto != nullptr;

	cl_thirdperson = I::Cvar->Find(FNV1A::HashConst("cl_thirdperson"));

	c_thirdpersonshoulder = I::Cvar->Find(FNV1A::HashConst("c_thirdpersonshoulder")); // flaot
	bSuccess &= c_thirdpersonshoulder != nullptr;

	c_thirdpersonshoulderaimdist = I::Cvar->Find(FNV1A::HashConst("c_thirdpersonshoulderaimdist")); // flaot
	bSuccess &= c_thirdpersonshoulderaimdist != nullptr;

	c_thirdpersonshoulderdist = I::Cvar->Find(FNV1A::HashConst("c_thirdpersonshoulderdist")); // flaot
	bSuccess &= c_thirdpersonshoulderdist != nullptr;

	c_thirdpersonshoulderheight = I::Cvar->Find(FNV1A::HashConst("c_thirdpersonshoulderheight")); // flaot
	bSuccess &= c_thirdpersonshoulderheight != nullptr;

	c_thirdpersonshoulderoffset = I::Cvar->Find(FNV1A::HashConst("c_thirdpersonshoulderoffset")); // flaot
	bSuccess &= c_thirdpersonshoulderoffset != nullptr;

	cl_interpolate = I::Cvar->Find(FNV1A::HashConst("cl_interpolate")); // flaot
	bSuccess &= cl_interpolate != nullptr;

	cl_interp_ratio = I::Cvar->Find(FNV1A::HashConst("cl_interp_ratio")); // flaot
	bSuccess &= cl_interp_ratio != nullptr;

	return bSuccess;
}
