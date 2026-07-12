// ============================================================================
// STEP 1: FORCE WIPE CONFLICTING SDK MACROS BEFORE HEADERS LOAD
// ============================================================================
#ifndef _WIN32
#undef FastASCIIToLower
#undef FastASCIIToUpper
#undef V_FixSlashes
#undef V_strlower
#endif

// ============================================================================
// STEP 2: LOAD HEADER AND COMPATIBILITY LIBRARIES
// ============================================================================
#include "strtools.h"
#include <wchar.h>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <cctype>

#ifdef _WIN32
#include <tchar.h>
#include <wtypes.h>
#else
typedef uint8_t BYTE;
typedef int64_t __int64;
#undef Q_strlen
#undef Q_strstr
#undef PATHSEPARATOR
#define Q_strlen strlen
#define Q_strstr strstr
#define V_strncpy strncpy
#define V_strncat strncat
#define V_stricmp strcasecmp
#define V_snprintf snprintf
#define CORRECT_PATH_SEPARATOR '/'
#define INCORRECT_PATH_SEPARATOR '\\'
#define PATHSEPARATOR(c) ((c) == '/' || (c) == '\\')
#define CP_UTF8 65001
#endif

// ============================================================================
// STEP 3: BASE STRING MATCHING METHODS
// ============================================================================
char const* V_stristr(char const* pStr, char const* pSearch) {
	if (!pStr || !pSearch) return 0;
	char const* pLetter = pStr;
	while (*pLetter != 0) {
		if (FastASCIIToLower((unsigned char)*pLetter) == FastASCIIToLower((unsigned char)*pSearch)) {
			char const* pMatch = pLetter + 1;
			char const* pTest = pSearch + 1;
			while (*pTest != 0) {
				if (*pMatch == 0) return 0;
				if (FastASCIIToLower((unsigned char)*pMatch) != FastASCIIToLower((unsigned char)*pTest)) break;
				++pMatch; ++pTest;
			}
			if (*pTest == 0) return pLetter;
		}
		++pLetter;
	}
	return 0;
}

char* V_stristr(char* pStr, char const* pSearch) {
	return (char*)V_stristr((char const*)pStr, pSearch);
}

const char* V_strnistr(const char* pStr, const char* pSearch, size_t n) {
	if (!pStr || !pSearch) return 0;
	const char* pLetter = pStr;
	while (*pLetter != 0) {
		if (n <= 0) return 0;
		if (FastASCIIToLower(*pLetter) == FastASCIIToLower(*pSearch)) {
			size_t n1 = n - 1;
			const char* pMatch = pLetter + 1;
			const char* pTest = pSearch + 1;
			while (*pTest != 0) {
				if (n1 <= 0) return 0;
				if (*pMatch == 0) return 0;
				if (FastASCIIToLower(*pMatch) != FastASCIIToLower(*pTest)) break;
				++pMatch; ++pTest; --n1;
			}
			if (*pTest == 0) return pLetter;
		}
		++pLetter; --n;
	}
	return 0;
}

const char* V_strnchr(const char* pStr, char c, size_t n) {
	const char* pLetter = pStr;
	const char* pLast = pStr + n;
	while ((pLetter < pLast) && (*pLetter != 0)) {
		if (*pLetter == c) return pLetter;
		++pLetter;
	}
	return NULL;
}

bool V_isspace(int c) {
	switch (c) {
		case ' ': case 9: case '\r': case '\n': case 11: case '\f': return true;
		default: return false;
	}
}

size_t V_StrTrim(char* pStr) {
	char* pSource = pStr; char* pDest = pStr;
	while (*pSource != 0 && V_isspace(*pSource)) pSource++;
	char* pLastWhiteBlock = NULL; char* pStart = pDest;
	while (*pSource != 0) {
		*pDest = *pSource++;
		if (V_isspace(*pDest)) { if (pLastWhiteBlock == NULL) pLastWhiteBlock = pDest; }
		else { pLastWhiteBlock = NULL; }
		pDest++;
	}
	*pDest = 0;
	if (pLastWhiteBlock != NULL) { pDest = pLastWhiteBlock; *pLastWhiteBlock = 0; }
	return pDest - pStart;
}

int V_UTF8ToUnicode(const char* pUTF8, wchar_t* pwchDest, int cubDestSizeInBytes) {
	if (pwchDest && cubDestSizeInBytes > 0) pwchDest[0] = 0;
	if (!pUTF8) return 0;
	int cchResult = 0;
	#ifdef _WIN32
	cchResult = MultiByteToWideChar(CP_UTF8, 0, pUTF8, -1, pwchDest, cubDestSizeInBytes / sizeof(wchar_t));
	#else
	cchResult = mbstowcs(pwchDest, pUTF8, cubDestSizeInBytes / sizeof(wchar_t));
	#endif
	if (pwchDest && cubDestSizeInBytes > 0) pwchDest[(cubDestSizeInBytes / sizeof(wchar_t)) - 1] = 0;
	return cchResult;
}

int V_UnicodeToUTF8(const wchar_t* pUnicode, char* pUTF8, int cubDestSizeInBytes) {
	if (cubDestSizeInBytes > 0 && pUTF8) pUTF8[0] = 0;
	int cchResult = 0;
	#ifdef _WIN32
	cchResult = WideCharToMultiByte(CP_UTF8, 0, pUnicode, -1, pUTF8, cubDestSizeInBytes, NULL, NULL);
	#else
	if (pUnicode && pUTF8) cchResult = wcstombs(pUTF8, pUnicode, cubDestSizeInBytes);
	#endif
	if (cubDestSizeInBytes > 0 && pUTF8) pUTF8[cubDestSizeInBytes - 1] = 0;
	return cchResult;
}

int V_UTF8CharLength(const unsigned char input) {
	if ((input & 0xFE) == 0xFC) return 6;
	if ((input & 0xFC) == 0xF8) return 5;
	if ((input & 0xF8) == 0xF0) return 4;
	if ((input & 0xF0) == 0xE0) return 3;
	if ((input & 0xE0) == 0xC0) return 2;
	return 1;
}

bool V_IsValidUTF8(const char* pszString) {
	char c; const char* it;
	while (true) {
		while (true) {
			c = *pszString; it = pszString++;
			if (c < 0) break;
			if (!c) return true;
		}
		if ((*pszString & 0xC0) != 0x80) break;
		pszString = it + 2;
		if (c >= 0xE0u) {
			int n = (*pszString & 0x3F) | (((*pszString & 0x3F) | ((c & 0xF) << 6)) << 6);
			if ((*pszString & 0xC0) != 0x80) return false;
			pszString = it + 3;
			if (c >= 0xF0u) {
				if ((*pszString & 0xC0) != 0x80 || ((n << 6) | (*pszString & 0x3Fu)) > 0x10FFFF) return false;
				pszString = it + 4;
			} else if ((n - 0xD800) <= 0x7FF) return false;
		} else if (c < 0xC2u) return false;
	}
	return false;
}
bool V_StringMatchesPattern(const char* pszSource, const char* pszPattern, int nFlags) {
	bool bExact = true; (void)nFlags;
	while (1) {
		if ((*pszPattern) == 0) return ((*pszSource) == 0);
		if ((*pszPattern) == '*') { pszPattern++; if ((*pszPattern) == 0) return true; bExact = false; continue; }
		ptrdiff_t nLength = 0;
		while ((*pszPattern) != '*' && (*pszPattern) != 0) { nLength++; pszPattern++; }
		while (1) {
			const char* pszStartPattern = pszPattern - nLength; const char* pszSearch = pszSource;
			for (ptrdiff_t i = 0; i < nLength; i++, pszSearch++, pszStartPattern++) {
				if ((*pszSearch) == 0) return false;
				if ((*pszSearch) != (*pszStartPattern)) break;
			}
			if (pszSearch - pszSource == nLength) break;
			if (bExact == true) return false;
			pszSource++;
		}
		pszSource += nLength;
	}
}

bool V_ComparePath(const char* a, const char* b) {
	if (strlen(a) != strlen(b)) return false;
	for (; *a; a++, b++) {
		if (*a == *b) continue;
		if (FastASCIIToLower(*a) == FastASCIIToLower(*b)) continue;
		if ((*a == '/' || *a == '\\') && (*b == '/' || *b == '\\')) continue;
		return false;
	}
	return true;
}

void V_FixSlashes(char* pName, char cSeperator) {
	while (*pName) { if (*pName == '\\' || *pName == '/') *pName = cSeperator; pName++; }
}

void V_AppendSlash(char* pStr, size_t strSize, char separator) {
	size_t len = strlen(pStr); (void)strSize;
	if (len > 0 && !PATHSEPARATOR(pStr[len - 1])) { pStr[len] = separator; pStr[len + 1] = 0; }
}

void V_StripTrailingSlash(char* ppath) {
	size_t len = strlen(ppath);
	if (len > 0 && PATHSEPARATOR(ppath[len - 1])) ppath[len - 1] = 0;
}

bool V_RemoveDotSlashes(char* pFilename, char separator) {
	char* pIn = pFilename; char* pOut = pFilename;
	if (*pIn && PATHSEPARATOR(*pIn)) { *pOut = *pIn; ++pIn; ++pOut; }
	bool bPrevPathSep = false;
	while (*pIn) {
		bool bIsPathSep = PATHSEPARATOR(*pIn);
		if (!bIsPathSep || !bPrevPathSep) *pOut++ = *pIn;
		bPrevPathSep = bIsPathSep; ++pIn;
	}
	*pOut = 0; pIn = pFilename; pOut = pFilename;
	while (*pIn) {
		if (pIn[0] == '.' && PATHSEPARATOR(pIn[1]) && (pIn == pFilename || pIn[-1] != '.')) pIn += 2;
		else { *pOut = *pIn; ++pIn; ++pOut; }
	}
	*pOut = 0; size_t len = strlen(pFilename);
	if (len > 2 && pFilename[len - 1] == '.' && PATHSEPARATOR(pFilename[len - 2])) pFilename[len - 2] = 0;
	pIn = pFilename;
	while (*pIn) {
		if (pIn[0] == '.' && pIn[1] == '.' && (pIn == pFilename || PATHSEPARATOR(pIn[-1])) && (pIn[2] == 0 || PATHSEPARATOR(pIn[2]))) {
			char* pEndOfDots = pIn + 2; char* pStart = pIn - 2;
			while (1) { if (pStart < pFilename) return false; if (PATHSEPARATOR(*pStart)) break; --pStart; }
			memmove(pStart, pEndOfDots, strlen(pEndOfDots) + 1); pIn = pFilename;
		} else ++pIn;
	}
	V_FixSlashes(pFilename, separator); return true;
}

bool V_NormalizePath(char* pfilePath, char separator) {
	char v2 = *pfilePath; char v3 = 0; char* v5 = pfilePath; char* i; char v7;
	for (i = pfilePath; v2; v3 = v7) {
		if (v2 == '\\' || v2 == '/') { v7 = 1; if (!v3) *pfilePath++ = separator; }
		else { v7 = 0; *pfilePath++ = v2; }
		v2 = *++i;
	}
	*pfilePath = 0; char* v8 = v5; char v9 = *v5; char* j; char v11; __int64 v12;
	for (j = v5; *j; v9 = *j) {
		if (v9 == '.' && ((v11 = j[1], v11 == '\\') || v11 == '/') && (j == v5 || (*(j - 1) != '.'))) v12 = 2;
		else { *v8++ = v9; v12 = 1; }
		j += v12;
	}
	*v8 = 0; __int64 v13 = -1; do ++v13; while (v5[v13]);
	if (v13 > 2 && v5[v13 - 1] == '.') {
		char v14 = v5[v13 - 2];
		if (v14 == '\\' || v14 == '/') v5[v13 - 2] = 0;
	}
	char v15 = *v5; char* v16 = v5;
	if (*v5) {
		do {
			// FIXED: Changed v16 to *v16 across the whole conditional check
			if (v15 == '.' && *v16 == '.' && (v16 == v5 || *(v16 - 1) == '\\' || *(v16 - 1) == '/') && (*v16 == 0 || *v16 == '\\' || *v16 == '/')) {
				char* k = v16 - 2; for (; ; --k) { if (k < v5) return false; if (*k == '\\' || *k == '/') break; }
				BYTE* v19 = (BYTE*)v16 + 2; __int64 v21 = -1; do ++v21; while (v19[v21]);
				memmove(k, v19, v21 + 1); v16 = v5;
			} else ++v16;
			v15 = *v16;
		} while (*v16);
		// FIXED: Changed *v5 to *pfilePath to correctly iterate path characters
		for (char* l = pfilePath; *l; ++l) { if (*l == '/' || *l == '\\') *l = separator; }
	}
	return true;
}

bool V_IsAbsolutePath(const char* pStr) {
	if (!pStr || *pStr == '\0') return false;
	return *pStr == '/' || *pStr == '\\';
}

bool V_IsValidPath(const char* pStr) {
	if (!pStr) return false;
	if (strlen(pStr) <= 0 || V_IsAbsolutePath(pStr) || strstr(pStr, "..")) return false;
	return true;
}

#if defined(_MSC_VER) && _MSC_VER >= 1900
bool
#else
void
#endif
V_MakeAbsolutePath(char* pOut, size_t outLen, const char* pPath, const char* pStartingDir) {
	if (V_IsAbsolutePath(pPath)) { strncpy(pOut, pPath, outLen); pOut[outLen - 1] = '\0'; }
	else {
		if (pStartingDir && V_IsAbsolutePath(pStartingDir)) { strncpy(pOut, pStartingDir, outLen); pOut[outLen - 1] = '\0'; }
		else { if (outLen > 0) pOut[0] = '\0'; }
		if (pStartingDir) { V_AppendSlash(pOut, outLen); strncat(pOut, pStartingDir, outLen); }
		V_AppendSlash(pOut, outLen); strncat(pOut, pPath, outLen);
	}
	V_FixSlashes(pOut);
	bool bRet = true;
	if (!V_RemoveDotSlashes(pOut)) { strncpy(pOut, pPath, outLen); V_FixSlashes(pOut); bRet = false; }
	#if defined(_MSC_VER) && _MSC_VER >= 1900
	return bRet;
	#endif
}

size_t V_StripLastDir(char* dirName, size_t maxLen) {
	if (!dirName || *dirName == '\0') return 0;
	size_t len = strlen(dirName);
	if (!strcasecmp(dirName, "./") || !strcasecmp(dirName, ".\\")) return len;
	if (PATHSEPARATOR(dirName[len - 1])) len--;
	while (len > 0) { if (PATHSEPARATOR(dirName[len - 1])) { dirName[len] = '\0'; return len; } len--; }
	if (len == 0) {
		int ret = snprintf(dirName, maxLen, ".%c", '/');
		if (ret < 0) { dirName[0] = '\0'; return 0; }
		return ret;
	}
	return len;
}

const char* V_UnqualifiedFileName(const char* in) {
	if (!in || !in) return in;
	const char* out = in + strlen(in) - 1;
	while ((out > in) && (!PATHSEPARATOR(*(out - 1)))) out--;
	return out;
}

void V_ComposeFileName(const char* path, const char* filename, char* dest, size_t destSize) {
	strncpy(dest, path, destSize); V_FixSlashes(dest); V_AppendSlash(dest, destSize);
	strncat(dest, filename, destSize); V_FixSlashes(dest);
}

void V_StripExtension(const char* in, char* out, size_t outSize) {
	if (!in || !out || !outSize) return;
	size_t end = strlen(in) - 1;
	while (end > 0 && in[end] != '.' && !PATHSEPARATOR(in[end])) --end;
	if (end > 0 && !PATHSEPARATOR(in[end]) && end < outSize) {
		size_t nChars = std::min(end, outSize - 1);
		if (out != in) memcpy(out, in, nChars);
		out[nChars] = 0;
	} else { if (out != in) { strncpy(out, in, outSize); out[outSize - 1] = '\0'; } }
}

const char* V_GetFileExtension(const char* path) {
	if (!path) return NULL;
	size_t len = strlen(path);
	if (len <= 1) return NULL;
	const char* src = path + len - 1;
	while (src != path && *(src - 1) != '.') src--;
	if (src == path || PATHSEPARATOR(*src)) return NULL;
	return src;
}

void V_ExtractFileExtension(const char* path, char* dest, size_t destSize) {
	if (!dest || destSize == 0) return;
	dest[0] = '\0';
	const char* extension = V_GetFileExtension(path);
	if (NULL != extension) { strncpy(dest, extension, destSize); dest[destSize - 1] = '\0'; }
}

void V_FileBase(const char* in, char* out, size_t maxlen) {
	if (!in || !out || maxlen == 0) { if (out && maxlen > 0) *out = 0; return; }
	size_t len = strlen(in); if (len == 0) { *out = 0; return; }
	size_t end = len - 1;
	while (end > 0 && in[end] != '.' && !PATHSEPARATOR(in[end])) end--;
	if (in[end] != '.') end = len - 1; else end--;
	size_t start = len - 1;
	while (start > 0 && !PATHSEPARATOR(in[start])) start--;
	if (!PATHSEPARATOR(in[start])) start = 0; else start++;
	if (end < start) { *out = 0; return; }
	len = end - start + 1; size_t maxcopy = std::min(len + 1, maxlen);
	strncpy(out, &in[start], maxcopy); out[maxcopy - 1] = '\0';
}
