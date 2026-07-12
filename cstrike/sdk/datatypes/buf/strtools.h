#pragma once
#include <cstddef>
#include <string>
#include <cstdarg>

#ifdef _WIN32
#define CORRECT_PATH_SEPARATOR '\\'
#define CORRECT_PATH_SEPARATOR_S "\\"
#define INCORRECT_PATH_SEPARATOR '/'
#define INCORRECT_PATH_SEPARATOR_S "/"
#else
#define CORRECT_PATH_SEPARATOR '/'
#define CORRECT_PATH_SEPARATOR_S "/"
#define INCORRECT_PATH_SEPARATOR '\\'
#define INCORRECT_PATH_SEPARATOR_S "\\"
#endif

#define CHARACTERS_WHICH_SEPARATE_DIRECTORY_COMPONENTS_IN_PATHNAMES ":/\\"
#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')
#define COPY_ALL_CHARACTERS -1
#ifdef _WIN32
#define FastASCIIToUpper( c ) ( ( ( (c) >= 'a' ) && ( (c) <= 'z' ) ) ? ( (c) - 32 ) : (c) )
#define FastASCIIToLower( c ) ( ( ( (c) >= 'A' ) && ( (c) <= 'Z' ) ) ? ( (c) + 32 ) : (c) )
#else
// Safe clean inline functions for Linux instead of broken macros
inline unsigned char FastASCIIToLower(unsigned char c) {
	return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}
inline unsigned char FastASCIIToUpper(unsigned char c) {
	return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}
#endif


#define V_vsnprintf vsnprintf
#define V_snprintf snprintf
#define V_strlen strlen
#define V_strncat strncat
#define V_strcmp strcmp
#define V_strncmp strncmp
#define V_strstr strstr
#define V_strncpy strncpy
#define V_strcat strcat

#ifdef _WIN32
#define V_strlower _strlwr
#define V_stricmp _stricmp
#define V_strnicmp _strnicmp
#define V_strdup _strdup
#else
#include <strings.h>
#include <cstring>
#define V_stricmp strcasecmp
#define V_strnicmp strncasecmp
#define V_strdup strdup

// Safe inline implementation of _strlwr for Linux
#include <cctype>
inline char* V_strlower(char* str) {
	if (!str) return nullptr;
	for (char* p = str; *p; ++p) *p = (char)std::tolower((unsigned char)*p);
	return str;
}
#endif

#define Q_vsnprintf V_vsnprintf
#define Q_snprintf V_snprintf
#define Q_strlower V_strlower
#define Q_strlen V_strlen
#define Q_strncat V_strncat
#define Q_strnistr V_strnistr
#define Q_stricmp V_stricmp
#define Q_strnicmp V_strnicmp
#define Q_strncasecmp V_strnicmp
#define Q_strcasecmp V_stricmp
#define Q_strcmp V_strcmp
#define Q_strncmp V_strncmp
#define Q_strstr V_strstr
#define Q_strncpy V_strncpy
#define Q_strdup V_strdup
#define Q_strcat V_strcat

template <size_t maxLenInCharacters>
int V_vsprintf_safe(char(&pDest)[maxLenInCharacters], const char* pFormat, va_list params) {
	return V_vsnprintf(pDest, maxLenInCharacters, pFormat, params);
}

char const* V_stristr(char const* pStr, char const* pSearch);
const char* V_strnistr(const char* pStr, const char* pSearch, size_t n);
const char* V_strnchr(const char* pStr, char c, size_t n);
bool V_isspace(int c);

size_t V_StrTrim(char* pStr);
int V_UTF8ToUnicode(const char* pUTF8, wchar_t* pwchDest, int cubDestSizeInBytes);
int V_UnicodeToUTF8(const wchar_t* pUnicode, char* pUTF8, int cubDestSizeInBytes);
int V_UTF8CharLength(const unsigned char input);
bool V_IsValidUTF8(const char* pszString);

typedef enum {
	PATTERN_NONE = 0x00000000,
	PATTERN_DIRECTORY = 0x00000001
} TStringPattern;

bool V_StringMatchesPattern(const char* szString, const char* szPattern, int flags = 0);
bool V_ComparePath(const char* a, const char* b);
void V_FixSlashes(char* pname, char separator = CORRECT_PATH_SEPARATOR);
void V_AppendSlash(char* pStr, size_t strSize, char separator = CORRECT_PATH_SEPARATOR);
void V_StripTrailingSlash(char* ppath);
bool V_RemoveDotSlashes(char* pFilename, char separator = CORRECT_PATH_SEPARATOR);
bool V_NormalizePath(char* pfilePath, char separator);
bool V_IsAbsolutePath(const char* pPath);
bool V_IsValidPath(const char* pStr);

#if defined(_MSC_VER) && _MSC_VER >= 1900
bool
#else
void
#endif
V_MakeAbsolutePath(char* pOut, size_t outLen, const char* pPath, const char* pStartingDir = NULL);

inline void V_MakeAbsolutePath(char* pOut, size_t outLen, const char* pPath, const char* pStartingDir, bool bLowercaseName) {
	V_MakeAbsolutePath(pOut, outLen, pPath, pStartingDir);
	if (bLowercaseName) {
		V_strlower(pOut);
	}
}

size_t V_StripLastDir(char* dirName, size_t maxLen);
const char* V_UnqualifiedFileName(const char* in);
void V_ComposeFileName(const char* path, const char* filename, char* dest, size_t destSize);
void V_StripExtension(const char* in, char* out, size_t outLen);
void V_ExtractFileExtension(const char* path, char* dest, size_t destSize);
const char* V_GetFileExtension(const char* path);
void V_FileBase(const char* in, char* out, size_t maxlen);
