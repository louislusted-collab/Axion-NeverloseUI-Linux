#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdio>

#include "config.h"
// used: getworkingpath
#include "../core.h"
// used: l_print
#include "../utilities/log.h"
// used: integertostring
#include "../utilities/crt.h"
// used: heapalloc, heapfree
#include "../utilities/memory.h"

// used: formatter implementation
#if defined(CS_CONFIGURATION_BINARY)
#include "../../extensions/binary.h"
#elif defined(CS_CONFIGURATION_JSON)
#include "../../extensions/json.h"
#elif defined(CS_CONFIGURATION_TOML)
#include "../../extensions/toml.h"
#endif

// default configurations working path
static wchar_t wszConfigurationsPath[MAX_PATH];

static bool IsValidFileIndex(const std::size_t nFileIndex)
{
	return nFileIndex < C::vecFileNames.size() && C::vecFileNames[nFileIndex] != nullptr;
}

#pragma region config_user_data_type

std::size_t C::UserDataType_t::GetSerializationSize() const
{
	std::size_t nTotalDataSize = 0U;

	for (const UserDataMember_t& member : vecMembers)
		nTotalDataSize += sizeof(FNV1A_t[2]) + member.nDataSize;

	return nTotalDataSize;
}

#pragma endregion

#pragma region config_variable_object

void C::VariableObject_t::SetStorage(const void* pValue)
{
	// check is available to store value in the local storage
	if (this->nStorageSize <= sizeof(this->storage.uLocal))
	{
		CRT::MemorySet(&this->storage.uLocal, 0U, sizeof(this->storage.uLocal));
		CRT::MemoryCopy(&this->storage.uLocal, pValue, this->nStorageSize);
	}
	// otherwise use heap memory to store it
	else
	{
		CS_ASSERT(this->storage.pHeap != nullptr); // tried to access non allocated storage

		CRT::MemorySet(this->storage.pHeap, 0U, this->nStorageSize);
		CRT::MemoryCopy(this->storage.pHeap, pValue, this->nStorageSize);
	}
}

std::size_t C::VariableObject_t::GetSerializationSize() const
{
	std::size_t nSerializationSize = this->nStorageSize;

	// denote a custom serialization size when it different from the storage size
	switch (this->uTypeHash)
	{
	// lookup for array data type
	case FNV1A::HashConst("bool[]"):
	case FNV1A::HashConst("int[]"):
	case FNV1A::HashConst("unsigned int[]"):
	case FNV1A::HashConst("float[]"):
	case FNV1A::HashConst("char[][]"):
		// arrays also serialize their size
		nSerializationSize += sizeof(std::size_t);
		break;
	// lookup for user-defined data type
	default:
	{
		for (const UserDataType_t& userType : vecUserTypes)
		{
			if (userType.uTypeHash == this->uTypeHash)
			{
				nSerializationSize = sizeof(std::size_t) + userType.GetSerializationSize();
				break;
			}
		}
		break;
	}
	}

	return nSerializationSize;
}

#pragma endregion

bool C::Setup(const wchar_t* wszDefaultFileName)
{
	if (!CORE::GetWorkingPath(wszConfigurationsPath))
		return false;

	CRT::StringCat(wszConfigurationsPath, CS_XOR(L"settings\\"));

	// create directory if it doesn't exist
	if (!::CreateDirectoryW(wszConfigurationsPath, nullptr))
	{
		if (::GetLastError() != ERROR_ALREADY_EXISTS)
		{
			L_PRINT(LOG_ERROR) << CS_XOR("failed to create configurations directory, because one or more intermediate directories don't exist");
			return false;
		}
	}

	// @note: define custom data types we want to serialize
	AddUserType(FNV1A::HashConst("KeyBind_t"),
	{ 
		UserDataMember_t{ FNV1A::HashConst("uKey"), FNV1A::HashConst("unsigned int"), &KeyBind_t::uKey },
		UserDataMember_t{ FNV1A::HashConst("nMode"), FNV1A::HashConst("int"), &KeyBind_t::nMode } 
	});

	AddUserType(FNV1A::HashConst("ColorPickerVar_t"),
	{
		UserDataMember_t{ FNV1A::HashConst("bRainbow"), FNV1A::HashConst("bool"), &ColorPickerVar_t::bRainbow },
		UserDataMember_t{ FNV1A::HashConst("flRainbowSpeed"), FNV1A::HashConst("float"), &ColorPickerVar_t::flRainbowSpeed },
		UserDataMember_t{ FNV1A::HashConst("colPrimary"), FNV1A::HashConst("Color_t"), &ColorPickerVar_t::colValue },
	});

	AddUserType(FNV1A::HashConst("TextOverlayVar_t"), 
	{ 
		UserDataMember_t{ FNV1A::HashConst("bEnable"), FNV1A::HashConst("bool"), &TextOverlayVar_t::bEnable },
		UserDataMember_t{ FNV1A::HashConst("flThickness"), FNV1A::HashConst("float"), &TextOverlayVar_t::flThickness },
		UserDataMember_t{ FNV1A::HashConst("colPrimary"), FNV1A::HashConst("Color_t"), &TextOverlayVar_t::colPrimary },
		UserDataMember_t{ FNV1A::HashConst("colOutline"), FNV1A::HashConst("Color_t"), &TextOverlayVar_t::colOutline }
	});

	AddUserType(FNV1A::HashConst("FrameOverlayVar_t"),
	{
		UserDataMember_t{ FNV1A::HashConst("bEnable"), FNV1A::HashConst("bool"), &FrameOverlayVar_t::bEnable },
		UserDataMember_t{ FNV1A::HashConst("flThickness"), FNV1A::HashConst("float"), &FrameOverlayVar_t::flThickness },
		UserDataMember_t{ FNV1A::HashConst("flRounding"), FNV1A::HashConst("float"), &FrameOverlayVar_t::flRounding },
		UserDataMember_t{ FNV1A::HashConst("colPrimary"), FNV1A::HashConst("Color_t"), &FrameOverlayVar_t::colPrimary },
		UserDataMember_t{ FNV1A::HashConst("colOutline"), FNV1A::HashConst("Color_t"), &FrameOverlayVar_t::colOutline } 
	});

	AddUserType(FNV1A::HashConst("BarOverlayVar_t"),
	{ 
		UserDataMember_t{ FNV1A::HashConst("bEnable"), FNV1A::HashConst("bool"), &BarOverlayVar_t::bEnable },
		UserDataMember_t{ FNV1A::HashConst("bGradient"), FNV1A::HashConst("bool"), &BarOverlayVar_t::bGradient },
		UserDataMember_t{ FNV1A::HashConst("bUseFactorColor"), FNV1A::HashConst("bool"), &BarOverlayVar_t::bUseFactorColor },
		UserDataMember_t{ FNV1A::HashConst("flThickness"), FNV1A::HashConst("float"), &BarOverlayVar_t::flThickness },
		UserDataMember_t{ FNV1A::HashConst("colPrimary"), FNV1A::HashConst("Color_t"), &BarOverlayVar_t::colPrimary },
		UserDataMember_t{ FNV1A::HashConst("colSecondary"), FNV1A::HashConst("Color_t"), &BarOverlayVar_t::colSecondary },
		UserDataMember_t{ FNV1A::HashConst("colBackground"), FNV1A::HashConst("Color_t"), &BarOverlayVar_t::colBackground },
		UserDataMember_t{ FNV1A::HashConst("colOutline"), FNV1A::HashConst("Color_t"), &BarOverlayVar_t::colOutline } 
	});

	// Discover existing configs before creating anything. The previous code
	// overwrote default.bin on every injection, so saved values never persisted.
	Refresh();
	std::size_t nDefaultIndex = C_INVALID_VARIABLE;
	for (std::size_t i = 0U; i < vecFileNames.size(); ++i)
	{
		if (CRT::StringCompare(vecFileNames[i], CS_XOR(CS_CONFIGURATION_DEFAULT_FILE_NAME CS_CONFIGURATION_FILE_EXTENSION)) == 0)
		{
			nDefaultIndex = i;
			break;
		}
	}

	if (nDefaultIndex == C_INVALID_VARIABLE)
	{
		if (!CreateFile(wszDefaultFileName))
			return false;
		nDefaultIndex = vecFileNames.size() - 1U;
	}
	else if (!LoadFile(nDefaultIndex))
	{
		// Old or malformed configs are replaced safely instead of entering the
		// unchecked legacy deserializer.
		if (!SaveFile(nDefaultIndex))
			return false;
	}

	return true;
}

#pragma region config_main

void C::Refresh()
{
	// clear and free previous stored file names
	for (wchar_t* wszFileName : vecFileNames)
		delete[] wszFileName;
	vecFileNames.clear();

	// make configuration files path filter
	wchar_t wszPathFilter[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszPathFilter, wszConfigurationsPath), CS_XOR(L"*" CS_CONFIGURATION_FILE_EXTENSION));

	// iterate through all files with our filter
	WIN32_FIND_DATAW findData;
	if (const HANDLE hFindFile = ::FindFirstFileW(wszPathFilter, &findData); hFindFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			vecFileNames.push_back(new wchar_t[CRT::StringLength(findData.cFileName) + 1U]);
			CRT::StringCopy(vecFileNames.back(), findData.cFileName);

			L_PRINT(LOG_INFO) << CS_XOR("found configuration file: \"") << findData.cFileName << CS_XOR("\"");
		} while (::FindNextFileW(hFindFile, &findData));

		::FindClose(hFindFile);
	}
}

void C::AddUserType(const FNV1A_t uTypeHash, const std::initializer_list<UserDataMember_t> vecUserMembers)
{
	if (vecUserMembers.size() == 0U)
		return;

	UserDataType_t userDataType;
	userDataType.uTypeHash = uTypeHash;

	for (const auto& userDataMember : vecUserMembers)
		userDataType.vecMembers.push_back(userDataMember);

	vecUserTypes.emplace_back(CRT::Move(userDataType));
}

bool C::SaveFileVariable(const std::size_t nFileIndex, const VariableObject_t& variable)
{
	if (!IsValidFileIndex(nFileIndex))
		return false;

	const wchar_t* wszFileName = vecFileNames[nFileIndex];

	wchar_t wszFilePath[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszFilePath, wszConfigurationsPath), wszFileName);

#if defined(CS_CONFIGURATION_BINARY)
	if (BIN::SaveVariable(wszFilePath, variable))
#elif defined(CS_CONFIGURATION_JSON)
	if (JSON::SaveVariable(wszFilePath, variable))
#elif defined(CS_CONFIGURATION_TOML)
	if (TOML::SaveVariable(wszFilePath, variable))
#endif
	{
		return true;
	}

	return false;
}

bool C::LoadFileVariable(const std::size_t nFileIndex, VariableObject_t& variable)
{
	if (!IsValidFileIndex(nFileIndex))
		return false;

	const wchar_t* wszFileName = vecFileNames[nFileIndex];

	wchar_t wszFilePath[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszFilePath, wszConfigurationsPath), wszFileName);

#if defined(CS_CONFIGURATION_BINARY)
	if (BIN::LoadVariable(wszFilePath, variable))
#elif defined(CS_CONFIGURATION_JSON)
	if (JSON::LoadVariable(wszFilePath, variable))
#elif defined(CS_CONFIGURATION_TOML)
	if (TOML::LoadVariable(wszFilePath, variable))
#endif
	{
		return true;
	}

	return false;
}

bool C::RemoveFileVariable(const std::size_t nFileIndex, const VariableObject_t& variable)
{
	if (!IsValidFileIndex(nFileIndex))
		return false;

	const wchar_t* wszFileName = vecFileNames[nFileIndex];

	wchar_t wszFilePath[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszFilePath, wszConfigurationsPath), wszFileName);

#if defined(CS_CONFIGURATION_BINARY)
	if (BIN::RemoveVariable(wszFilePath, variable))
#elif defined(CS_CONFIGURATION_JSON)
	if (JSON::RemoveVariable(wszFilePath, variable))
#elif defined(CS_CONFIGURATION_TOML)
	if (TOML::RemoveVariable(wszFilePath, variable))
#endif
	{
		return true;
	}

	return false;
}

bool C::CreateFile(const wchar_t* wszFileName)
{
	if (wszFileName == nullptr || *wszFileName == L'\0')
		return false;

	const wchar_t* wszFileExtension = CRT::StringCharR(wszFileName, L'.');

	// get length of the given filename and strip out extension if there any
	const std::size_t nFileNameLength = (wszFileExtension != nullptr ? wszFileExtension - wszFileName : CRT::StringLength(wszFileName));
	wchar_t* wszFullFileName = new wchar_t[nFileNameLength + CRT::StringLength(CS_CONFIGURATION_FILE_EXTENSION) + 1U];

	// copy filename without extension
	wchar_t* wszFullFileNameEnd = CRT::StringCopyN(wszFullFileName, wszFileName, nFileNameLength);
	*wszFullFileNameEnd = L'\0';
	// append correct extension to the filename
	CRT::StringCat(wszFullFileNameEnd, CS_XOR(CS_CONFIGURATION_FILE_EXTENSION));

	// add filename to the list
	vecFileNames.push_back(wszFullFileName);

	// create and save it by the index
	if (SaveFile(vecFileNames.size() - 1U))
	{
		L_PRINT(LOG_INFO) << CS_XOR("created configuration file: \"") << wszFullFileName << CS_XOR("\"");
		return true;
	}

	L_PRINT(LOG_WARNING) << CS_XOR("failed to create configuration file: \"") << wszFullFileName << CS_XOR("\"");
	delete[] vecFileNames.back();
	vecFileNames.pop_back();
	return false;
}

bool C::RenameFile(const std::size_t nFileIndex, const wchar_t* wszNewFileName)
{
	if (!IsValidFileIndex(nFileIndex) || wszNewFileName == nullptr || *wszNewFileName == L'\0')
		return false;
	if (CRT::StringCompare(vecFileNames[nFileIndex], CS_XOR(CS_CONFIGURATION_DEFAULT_FILE_NAME CS_CONFIGURATION_FILE_EXTENSION)) == 0)
		return false;
	for (const wchar_t* cursor = wszNewFileName; *cursor != L'\0'; ++cursor)
		if (*cursor == L'/' || *cursor == L'\\')
			return false;

	const wchar_t* extension = CRT::StringCharR(wszNewFileName, L'.');
	const std::size_t stemLength = extension != nullptr
		? static_cast<std::size_t>(extension - wszNewFileName)
		: CRT::StringLength(wszNewFileName);
	if (stemLength == 0U)
		return false;
	wchar_t* fullName = new wchar_t[stemLength + CRT::StringLength(CS_CONFIGURATION_FILE_EXTENSION) + 1U];
	wchar_t* end = CRT::StringCopyN(fullName, wszNewFileName, stemLength);
	*end = L'\0';
	CRT::StringCat(end, CS_XOR(CS_CONFIGURATION_FILE_EXTENSION));
	for (std::size_t i = 0; i < vecFileNames.size(); ++i)
	{
		if (i != nFileIndex && CRT::StringCompare(vecFileNames[i], fullName) == 0)
		{
			delete[] fullName;
			return false;
		}
	}

	wchar_t source[MAX_PATH] = {}, destination[MAX_PATH] = {};
	CRT::StringCat(CRT::StringCopy(source, wszConfigurationsPath), vecFileNames[nFileIndex]);
	CRT::StringCat(CRT::StringCopy(destination, wszConfigurationsPath), fullName);
#ifdef _WIN32
	const bool renamed = ::MoveFileExW(source, destination, MOVEFILE_WRITE_THROUGH) != FALSE;
#else
	auto narrow = [](const wchar_t* input, char* output, std::size_t capacity) {
		std::size_t i = 0;
		for (; input[i] != L'\0' && i + 1 < capacity; ++i)
			output[i] = static_cast<char>(input[i]);
		output[i] = '\0';
	};
	char sourcePath[MAX_PATH * 2] = {}, destinationPath[MAX_PATH * 2] = {};
	narrow(source, sourcePath, sizeof(sourcePath));
	narrow(destination, destinationPath, sizeof(destinationPath));
	const bool renamed = std::rename(sourcePath, destinationPath) == 0;
#endif
	if (!renamed)
	{
		delete[] fullName;
		return false;
	}
	delete[] vecFileNames[nFileIndex];
	vecFileNames[nFileIndex] = fullName;
	return true;
}

bool C::SaveFile(const std::size_t nFileIndex)
{
	if (!IsValidFileIndex(nFileIndex))
		return false;

	const wchar_t* wszFileName = vecFileNames[nFileIndex];

	wchar_t wszFilePath[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszFilePath, wszConfigurationsPath), wszFileName);

#if defined(CS_CONFIGURATION_BINARY)
	if (BIN::SaveFile(wszFilePath))
#elif defined(CS_CONFIGURATION_JSON)
	if (JSON::SaveFile(wszFilePath))
#elif defined(CS_CONFIGURATION_TOML)
	if (TOML::SaveFile(wszFilePath))
#endif
	{
		L_PRINT(LOG_INFO) << CS_XOR("saved configuration file: \"") << wszFileName << CS_XOR("\"");
		return true;
	}

	L_PRINT(LOG_WARNING) << CS_XOR("failed to save configuration file: \"") << wszFileName << CS_XOR("\"");
	return false;
}

bool C::LoadFile(const std::size_t nFileIndex)
{
	if (!IsValidFileIndex(nFileIndex))
		return false;

	const wchar_t* wszFileName = vecFileNames[nFileIndex];

	wchar_t wszFilePath[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszFilePath, wszConfigurationsPath), wszFileName);

#if defined(CS_CONFIGURATION_BINARY)
	if (BIN::LoadFile(wszFilePath))
#elif defined(CS_CONFIGURATION_JSON)
	if (JSON::LoadFile(wszFilePath))
#elif defined(CS_CONFIGURATION_TOML)
	if (TOML::LoadFile(wszFilePath))
#endif
	{
		L_PRINT(LOG_INFO) << CS_XOR("loaded configuration file: \"") << wszFileName << CS_XOR("\"");
		return true;
	}

	L_PRINT(LOG_WARNING) << CS_XOR("failed to load configuration file: \"") << wszFileName << CS_XOR("\"");
	return false;
}

void C::RemoveFile(const std::size_t nFileIndex)
{
	if (!IsValidFileIndex(nFileIndex))
		return;

	const wchar_t* wszFileName = vecFileNames[nFileIndex];

	// unable to delete default config
	if (CRT::StringCompare(wszFileName, CS_XOR(CS_CONFIGURATION_DEFAULT_FILE_NAME CS_CONFIGURATION_FILE_EXTENSION)) == 0)
	{
		L_PRINT(LOG_WARNING) << CS_XOR("unable to remove default configuration file: \"") << wszFileName << CS_XOR("\"");
		return;
	}

	wchar_t wszFilePath[MAX_PATH];
	CRT::StringCat(CRT::StringCopy(wszFilePath, wszConfigurationsPath), wszFileName);

	if (::DeleteFileW(wszFilePath))
	{
		// erase and free filename from the list
		delete[] vecFileNames[nFileIndex];
		vecFileNames.erase(vecFileNames.cbegin() + nFileIndex);

		L_PRINT(LOG_INFO) << CS_XOR("removed configuration file");
	}
}

#pragma endregion

#pragma region config_get

std::size_t C::GetVariableIndex(const FNV1A_t uNameHash)
{
	for (std::size_t i = 0U; i < vecVariables.size(); i++)
	{
		if (vecVariables[i].uNameHash == uNameHash)
			return i;
	}

	return C_INVALID_VARIABLE;
}

#pragma endregion

#pragma region config_user_types
void ColorPickerVar_t::UpdateRainbow()
{
	// @todo: improve + optimize this code
	// progress rainbow color
	if (this->bRainbow)
	{
		const float flTime = static_cast<float>(ImGui::GetTime());
		// create a rainbow color with copied alpha
		float arrRainbowColors[] = {
			static_cast<float>(sin(flTime * this->flRainbowSpeed) * 0.5 + 0.5),
			static_cast<float>(sin(flTime * this->flRainbowSpeed * MATH::_PI / 3) * 0.5 + 0.5),
			static_cast<float>(sin(flTime * this->flRainbowSpeed * MATH::_PI / 3) * 0.5 + 0.5),
			this->colValue.Base<COLOR_A>()
		};

		// set the rainbow color
		this->colValue = Color_t::FromBase4(arrRainbowColors);
	}
}
