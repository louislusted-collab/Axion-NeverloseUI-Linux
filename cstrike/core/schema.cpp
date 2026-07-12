#include "schema.h"

// used: [stl] vector
#include <vector>
// used: [stl] find_if
#include <algorithm>
#include <cstring>
#include <string>

// used: getworkingpath
#include "../core.h"

// used: ischemasystem
#include "interfaces.h"
#include "../sdk/interfaces/ischemasystem.h"

// used: l_print
#include "../utilities/log.h"

struct SchemaData_t
{
	FNV1A_t uHashedFieldName = 0x0ULL;
	std::uint32_t uOffset = 0x0U;
};

static std::vector<SchemaData_t> vecSchemaData;

bool SCHEMA::Setup(const wchar_t* wszFileName, const char* szModuleName)
{
#ifdef __linux__
	// On Linux: Schema hash table traversal (UtlTSHash) uses different offsets.
	// Schema dump is only used for debugging offsets, skip it on Linux.
	L_PRINT(LOG_WARNING) << CS_XOR("Linux: skipping SCHEMA::Setup (UtlTSHash offsets differ)");
	return true;
#endif
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

	CRT::String_t<64> szTimeBuffer(CS_XOR("[%d-%m-%Y %T] asphyxia | schema dump\n\n"), &timePoint);
	::WriteFile(hOutFile, szTimeBuffer.Data(), szTimeBuffer.Length(), nullptr, nullptr);
#else
	FILE* hOutFile = fopen("/tmp/cs2_schema_dump.txt", "w");
	if (!hOutFile) return false;
	const std::time_t time = std::time(nullptr);
	std::tm timePoint; localtime_r(&time, &timePoint);
	char szTimeBuf[64]; strftime(szTimeBuf, sizeof(szTimeBuf), "[%d-%m-%Y %T] schema dump\n\n", &timePoint);
	fputs(szTimeBuf, hOutFile);
#endif

	CSchemaSystemTypeScope* pTypeScope = I::SchemaSystem->FindTypeScopeForModule(szModuleName);
	if (pTypeScope == nullptr)
		return false;

	const int nTableSize = pTypeScope->hashClasses.Count();
	L_PRINT(LOG_INFO) << CS_XOR("found \"") << nTableSize << CS_XOR("\" schema classes in module");

	UtlTSHashHandle_t* pElements = new UtlTSHashHandle_t[nTableSize + 1U];
	const auto nElements = pTypeScope->hashClasses.GetElements(0, nTableSize, pElements);

	for (int i = 0; i < nElements; i++)
	{
		const UtlTSHashHandle_t hElement = pElements[i];

		if (hElement == 0)
			continue;

		CSchemaClassBinding* pClassBinding = pTypeScope->hashClasses[hElement];
		if (pClassBinding == nullptr)
			continue;

		SchemaClassInfoData_t* pDeclaredClassInfo;
		pTypeScope->FindDeclaredClass(&pDeclaredClassInfo, pClassBinding->szBinaryName);

		if (pDeclaredClassInfo == nullptr)
			continue;

		if (pDeclaredClassInfo->nFieldSize == 0)
			continue;

		CRT::String_t<MAX_PATH> szClassBuffer(CS_XOR("class %s\n"), pDeclaredClassInfo->szName);
#ifdef _WIN32
		::WriteFile(hOutFile, szClassBuffer.Data(), szClassBuffer.Length(), nullptr, nullptr);
#else
		fputs(szClassBuffer.Data(), hOutFile);
#endif

		for (auto j = 0; j < pDeclaredClassInfo->nFieldSize; j++)
		{
			SchemaClassFieldData_t* pFields = pDeclaredClassInfo->pFields;
			CRT::String_t<MAX_PATH> szFieldClassBuffer(CS_XOR("%s->%s"), pClassBinding->szBinaryName, pFields[j].szName);
			vecSchemaData.emplace_back(FNV1A::Hash(szFieldClassBuffer.Data()), pFields[j].nSingleInheritanceOffset);

			CRT::String_t<MAX_PATH> szFieldBuffer(CS_XOR("    %s %s = 0x%X\n"), pFields[j].pSchemaType->szName, pFields[j].szName, pFields[j].nSingleInheritanceOffset);
#ifdef _WIN32
			::WriteFile(hOutFile, szFieldBuffer.Data(), szFieldBuffer.Length(), nullptr, nullptr);
#else
			fputs(szFieldBuffer.Data(), hOutFile);
#endif
		}
		#ifdef _DEBUG
		L_PRINT(LOG_INFO) << CS_XOR("dumped \"") << pDeclaredClassInfo->szName << CS_XOR("\" (total: ") << pDeclaredClassInfo->nFieldSize << CS_XOR(" fields)");
		#endif
	}

	delete[] pElements;

#ifdef _WIN32
	::CloseHandle(hOutFile);
#else
	fclose(hOutFile);
#endif

	return true;
}

std::uint32_t SCHEMA::GetOffset(const FNV1A_t uHashedFieldName)
{
#ifdef __linux__
	// On Linux: schema wasn't dumped, return 0 and let the caller handle it
	// The caller should use static offsets instead of schema lookups
	return 0U;
#else
	if (const auto it = std::ranges::find_if(vecSchemaData, [uHashedFieldName](const SchemaData_t& data)
		{ return data.uHashedFieldName == uHashedFieldName; });
		it != vecSchemaData.end())
		return it->uOffset;

	L_PRINT(LOG_ERROR) << CS_XOR("failed to find offset for field with hash: ") << L::AddFlags(LOG_MODE_INT_FORMAT_HEX | LOG_MODE_INT_SHOWBASE) << uHashedFieldName;
	CS_ASSERT(false); // schema field not found
	return 0U;
#endif
}

std::uint32_t SCHEMA::GetOffset(const char* szQualifiedFieldName)
{
	if (szQualifiedFieldName == nullptr)
		return 0U;

	const FNV1A_t hash = FNV1A::Hash(szQualifiedFieldName);
	if (const auto it = std::ranges::find_if(vecSchemaData, [hash](const SchemaData_t& data)
		{ return data.uHashedFieldName == hash; }); it != vecSchemaData.end())
		return it->uOffset;

	const char* separator = std::strstr(szQualifiedFieldName, "->");
	if (separator == nullptr || separator == szQualifiedFieldName || separator[2] == '\0')
		return 0U;

	if (I::SchemaSystem == nullptr)
		return 0U;

	const std::string className(szQualifiedFieldName, static_cast<std::size_t>(separator - szQualifiedFieldName));
	const char* fieldName = separator + 2;
	CSchemaSystemTypeScope* scope = I::SchemaSystem->FindTypeScopeForModule(CLIENT_DLL);
	if (scope == nullptr)
		return 0U;

	SchemaClassInfoData_t* classInfo = nullptr;
	scope->FindDeclaredClass(&classInfo, className.c_str());
	if (classInfo == nullptr || classInfo->pFields == nullptr)
		return 0U;

	for (int i = 0; i < classInfo->nFieldSize; ++i)
	{
		const SchemaClassFieldData_t& field = classInfo->pFields[i];
		if (field.szName != nullptr && std::strcmp(field.szName, fieldName) == 0)
		{
			vecSchemaData.emplace_back(hash, field.nSingleInheritanceOffset);
			return field.nSingleInheritanceOffset;
		}
	}

	L_PRINT(LOG_WARNING) << CS_XOR("native schema field not found: ") << szQualifiedFieldName;
	return 0U;
}

// @todo: optimize this, this is really poorly do and can be done much better?
std::uint32_t SCHEMA::GetForeignOffset(const char* szModulenName, const FNV1A_t uHashedClassName, const FNV1A_t uHashedFieldName)
{
	CSchemaSystemTypeScope* pTypeScope = I::SchemaSystem->FindTypeScopeForModule(szModulenName);
	if (pTypeScope == nullptr)
		return false;

	const int nTableSize = pTypeScope->hashClasses.Count();
	// allocate memory for elements
	UtlTSHashHandle_t* pElements = new UtlTSHashHandle_t[nTableSize + 1U];
	const auto nElements = pTypeScope->hashClasses.GetElements(0, nTableSize, pElements);
	std::uint32_t uOffset = 0x0;

	for (int i = 0; i < nElements; i++)
	{
		const UtlTSHashHandle_t hElement = pElements[i];

		if (hElement == 0)
			continue;

		CSchemaClassBinding* pClassBinding = pTypeScope->hashClasses[hElement];
		if (pClassBinding == nullptr)
			continue;

		SchemaClassInfoData_t* pDeclaredClassInfo;
		pTypeScope->FindDeclaredClass(&pDeclaredClassInfo, pClassBinding->szBinaryName);

		if (pDeclaredClassInfo == nullptr)
			continue;

		if (pDeclaredClassInfo->nFieldSize == 0)
			continue;

		for (auto j = 0; j < pDeclaredClassInfo->nFieldSize; j++)
		{
			SchemaClassFieldData_t* pFields = pDeclaredClassInfo->pFields;
			if (pFields == nullptr)
				continue;

			SchemaClassFieldData_t field = pFields[j];
			if (FNV1A::Hash(pClassBinding->szBinaryName) == uHashedClassName && FNV1A::Hash(field.szName) == uHashedFieldName)
				uOffset = field.nSingleInheritanceOffset;
		}
	}

	if (uOffset == 0x0)
		L_PRINT(LOG_WARNING) << CS_XOR("failed to find offset for field with hash: ") << L::AddFlags(LOG_MODE_INT_FORMAT_HEX | LOG_MODE_INT_SHOWBASE) << uHashedFieldName;

	return uOffset;
}
