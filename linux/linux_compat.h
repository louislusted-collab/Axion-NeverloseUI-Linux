#pragma once
#ifdef __linux__

// Pull in features.h FIRST so __GLIBC_PREREQ is defined before anything else
#include <features.h>
#include <stdint.h>
#include <stddef.h>
#include <cstdint>    // std::uint8_t, std::uint16_t, etc. for C++ code
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <alloca.h>

// Basic Windows types using C99 fixed-width
typedef uint8_t             BYTE;
typedef uint16_t            WORD;
typedef uint32_t            DWORD;
typedef uint64_t            QWORD;
typedef int                 BOOL;
typedef void*               HANDLE;
typedef void*               HMODULE;
typedef void*               HINSTANCE;
typedef int32_t             LONG;
typedef int64_t             LONGLONG;
typedef uint32_t            ULONG;
typedef wchar_t             WCHAR;
typedef const char*         LPCSTR;
typedef char*               LPSTR;
typedef const wchar_t*      LPCWSTR;
typedef wchar_t*            LPWSTR;
typedef void*               LPVOID;
typedef const void*         LPCVOID;
typedef size_t              SIZE_T;
typedef uint32_t            UINT;
typedef int32_t             INT;
typedef float               FLOAT;
typedef DWORD*              LPDWORD;
typedef unsigned int        UINT_PTR;
typedef unsigned long long  UINT64;

// MSVC __intN types
typedef int8_t              __int8;
typedef int16_t             __int16;
typedef int32_t             __int32;
typedef int64_t             __int64;

// Windows compat aliases
#ifndef VOID
#define VOID void
#endif
typedef unsigned char       byte;
typedef unsigned char       UCHAR;
typedef unsigned short      USHORT;

// Misc Windows compat
#define _MAX_PATH           MAX_PATH

// Windows virtual key codes
#define VK_LBUTTON   0x01
#define VK_RBUTTON   0x02
#define VK_CANCEL    0x03
#define VK_MBUTTON   0x04
#define VK_XBUTTON1  0x05
#define VK_XBUTTON2  0x06
#define VK_BACK      0x08
#define VK_TAB       0x09
#define VK_CLEAR     0x0C
#define VK_RETURN    0x0D
#define VK_SHIFT     0x10
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_PAUSE     0x13
#define VK_ESCAPE    0x1B
#define VK_SPACE     0x20
#define VK_PRIOR     0x21
#define VK_NEXT      0x22
#define VK_END       0x23
#define VK_HOME      0x24
#define VK_LEFT      0x25
#define VK_UP        0x26
#define VK_RIGHT     0x27
#define VK_DOWN      0x28
#define VK_INSERT    0x2D
#define VK_DELETE    0x2E
#define VK_F1        0x70
#define VK_F2        0x71
#define VK_F3        0x72
#define VK_F4        0x73
#define VK_F5        0x74
#define VK_F6        0x75
#define VK_F7        0x76
#define VK_F8        0x77
#define VK_F9        0x78
#define VK_F10       0x79
#define VK_F11       0x7A
#define VK_F12       0x7B
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5

// Windows message types (used in draw.h OnWndProc)
typedef unsigned int UINT_MSG;
typedef uintptr_t    WPARAM;
typedef intptr_t     LPARAM;
typedef void*        HWND;
typedef long         LRESULT;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define PAGE_EXECUTE_READWRITE 0x40
#define MEM_COMMIT   0x1000
#define MEM_RESERVE  0x2000
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef DLL_PROCESS_ATTACH
#define DLL_PROCESS_ATTACH 1
#endif
#ifndef DLL_PROCESS_DETACH
#define DLL_PROCESS_DETACH 0
#endif

// calling conventions — no-ops on x86-64 Linux
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __thiscall
#define __thiscall
#endif
#ifndef __vectorcall
#define __vectorcall
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef CALLBACK
#define CALLBACK
#endif

// __declspec is MSVC-only
#ifndef __declspec
#define __declspec(x)
#endif

// Windows file I/O constants
#define GENERIC_READ                0x80000000UL
#define GENERIC_WRITE               0x40000000UL
#define FILE_SHARE_READ             0x00000001UL
#define OPEN_EXISTING               3UL
#define CREATE_ALWAYS               2UL
#define FILE_ATTRIBUTE_NORMAL       0x00000080UL
#define INVALID_HANDLE_VALUE        ((HANDLE)(uintptr_t)-1)
#define INVALID_FILE_SIZE           ((DWORD)0xFFFFFFFFUL)
#define INVALID_SET_FILE_POINTER    ((DWORD)0xFFFFFFFFUL)
#define FILE_BEGIN                  0
#define FILE_CURRENT                1
#define FILE_END                    2
#define ERROR_ALREADY_EXISTS        183UL

// Wide-string to narrow helper
static inline void _w2n(const wchar_t* w, char* buf, size_t n) {
    size_t i = 0;
    while (i + 1 < n && w[i]) { buf[i] = (char)(w[i] & 0xFF); i++; }
    buf[i] = '\0';
}

// Windows HANDLE-based file I/O — backed by FILE*
static inline HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD, void*, DWORD disp, DWORD, void*) {
    char nbuf[MAX_PATH*2]; _w2n(path, nbuf, sizeof(nbuf));
    // For CREATE_ALWAYS, unlink existing file first to avoid permission issues
    // (e.g. file owned by root from previous sudo injection)
    if (disp == CREATE_ALWAYS) {
        unlink(nbuf);
    }
    const char* mode = (disp == CREATE_ALWAYS) ? ((access & GENERIC_READ) ? "w+b" : "wb")
                                                : ((access & GENERIC_WRITE) ? "r+b" : "rb");
    FILE* f = fopen(nbuf, mode);
    return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}
static inline DWORD GetFileSize(HANDLE h, DWORD*) {
    FILE* f = (FILE*)h;
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, pos, SEEK_SET);
    return (sz < 0) ? INVALID_FILE_SIZE : (DWORD)sz;
}
static inline BOOL ReadFile(HANDLE h, void* buf, DWORD n, DWORD*, void*) {
    return (BOOL)(fread(buf, 1, n, (FILE*)h) == n);
}
static inline BOOL WriteFile(HANDLE h, const void* buf, DWORD n, DWORD*, void*) {
    return (BOOL)(fwrite(buf, 1, n, (FILE*)h) == n);
}
static inline DWORD SetFilePointer(HANDLE h, LONG off, LONG*, DWORD origin) {
    int whence = (origin == FILE_BEGIN) ? SEEK_SET : (origin == FILE_END) ? SEEK_END : SEEK_CUR;
    if (fseek((FILE*)h, (long)off, whence) != 0) return INVALID_SET_FILE_POINTER;
    long pos = ftell((FILE*)h);
    return (pos < 0) ? INVALID_SET_FILE_POINTER : (DWORD)pos;
}
static inline BOOL SetEndOfFile(HANDLE h) {
    long pos = ftell((FILE*)h);
    return (pos >= 0) ? (ftruncate(fileno((FILE*)h), (off_t)pos) == 0) : FALSE;
}
// CloseHandle is used for both file HANDLEs and thread HANDLEs on Windows.
// On Linux: files are FILE*, threads are faked via pthread_t cast.
// We can't distinguish them here safely, so just no-op — files are closed via fclose in SetEndOfFile wrappers.
// Config code explicitly calls CloseHandle on file HANDLEs; we override below.
static inline BOOL CloseHandle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    return fclose((FILE*)h) == 0;
}

// Directory and file-system ops (wchar → narrow)
#include <sys/stat.h>
#include <dirent.h>
static inline BOOL CreateDirectoryW(const wchar_t* path, void*) {
    char nbuf[MAX_PATH*2]; _w2n(path, nbuf, sizeof(nbuf));
    return mkdir(nbuf, 0777) == 0;
}
static inline DWORD GetLastError() { return (DWORD)errno; }
static inline BOOL DeleteFileW(const wchar_t* path) {
    char nbuf[MAX_PATH*2]; _w2n(path, nbuf, sizeof(nbuf));
    return unlink(nbuf) == 0;
}

// FindFirstFileW / FindNextFileW / FindClose — backed by dirent
struct WIN32_FIND_DATAW {
    DWORD    dwFileAttributes;
    wchar_t  cFileName[MAX_PATH];
    wchar_t  cAlternateFileName[14];
    char     _cFileNameA[MAX_PATH]; // internal narrow copy
};
struct _LinuxFindHandle {
    DIR* dir;
    char path[MAX_PATH*2];
    char pattern[MAX_PATH*2]; // simple *.ext match
    WIN32_FIND_DATAW data;
};
static inline bool _match_pattern(const char* name, const char* pat) {
    if (!pat || pat[0] == '*') {
        const char* dot = strrchr(pat, '.');
        if (!dot) return true; // "*" matches everything
        const char* ext = strrchr(name, '.');
        return ext && strcmp(ext, dot) == 0;
    }
    return strcmp(name, pat) == 0;
}
static inline HANDLE FindFirstFileW(const wchar_t* wszPath, WIN32_FIND_DATAW* data) {
    char narrow[MAX_PATH*2]; _w2n(wszPath, narrow, sizeof(narrow));
    char dir[MAX_PATH*2], pat[MAX_PATH*2];
    const char* slash = strrchr(narrow, '/');
    if (!slash) slash = strrchr(narrow, '\\');
    if (slash) {
        size_t dlen = slash - narrow;
        strncpy(dir, narrow, dlen); dir[dlen] = '\0';
        strncpy(pat, slash+1, MAX_PATH*2);
    } else { strcpy(dir, "."); strcpy(pat, narrow); }
    DIR* d = opendir(dir);
    if (!d) return INVALID_HANDLE_VALUE;
    _LinuxFindHandle* h = (_LinuxFindHandle*)malloc(sizeof(_LinuxFindHandle));
    h->dir = d;
    strncpy(h->path, dir, MAX_PATH*2);
    strncpy(h->pattern, pat, MAX_PATH*2);
    // find first matching entry
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (_match_pattern(ent->d_name, h->pattern)) {
            strncpy(h->data._cFileNameA, ent->d_name, MAX_PATH);
            mbstowcs(h->data.cFileName, ent->d_name, MAX_PATH);
            h->data.cFileName[MAX_PATH-1] = 0;
            h->data.dwFileAttributes = 0;
            *data = h->data;
            return (HANDLE)h;
        }
    }
    closedir(d); free(h); return INVALID_HANDLE_VALUE;
}
static inline BOOL FindNextFileW(HANDLE hFind, WIN32_FIND_DATAW* data) {
    _LinuxFindHandle* h = (_LinuxFindHandle*)hFind;
    struct dirent* ent;
    while ((ent = readdir(h->dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (_match_pattern(ent->d_name, h->pattern)) {
            strncpy(h->data._cFileNameA, ent->d_name, MAX_PATH);
            mbstowcs(h->data.cFileName, ent->d_name, MAX_PATH);
            h->data.cFileName[MAX_PATH-1] = 0;
            h->data.dwFileAttributes = 0;
            *data = h->data;
            return TRUE;
        }
    }
    return FALSE;
}
static inline BOOL FindClose(HANDLE hFind) {
    _LinuxFindHandle* h = (_LinuxFindHandle*)hFind;
    closedir(h->dir); free(h); return TRUE;
}

// Windows NT types
typedef void*    PVOID;
typedef BYTE     BOOLEAN;
typedef void*    LIST_ENTRY; // simplified stub
typedef int16_t  SHORT;
typedef uint16_t USHORT_T; // keep distinct from USHORT already defined

// Keyboard state — GetAsyncKeyState not available on Linux (no direct HW access)
static inline SHORT GetAsyncKeyState(int /*vKey*/) { return 0; }
static inline SHORT GetKeyState(int /*vKey*/) { return 0; }

// Command-line
static inline wchar_t* GetCommandLineW() { return nullptr; }

// HRESULT helpers
typedef long HRESULT;
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr)    ((HRESULT)(hr) < 0)

// VirtualProtect → mprotect (make RWX for simplicity)
static inline BOOL VirtualProtect(void* addr, size_t size, DWORD, DWORD* old) {
    if (old) *old = 0;
    uintptr_t base = (uintptr_t)addr & ~(uintptr_t)4095;
    size_t    span = size + ((uintptr_t)addr - base);
    return mprotect((void*)base, span, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

// VirtualAlloc / VirtualFree
static inline void* VirtualAlloc(void*, size_t size, DWORD, DWORD) {
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}
static inline BOOL VirtualFree(void* addr, size_t size, DWORD) {
    return munmap(addr, size) == 0;
}

// Module helpers
static inline HMODULE GetModuleHandleA(const char* name) {
    return dlopen(name ? name : nullptr, RTLD_LAZY | RTLD_NOLOAD);
}
static inline HMODULE GetModuleHandleW(const wchar_t*) { return nullptr; }
static inline HMODULE LoadLibraryA(const char* n) { return dlopen(n, RTLD_LAZY); }
static inline BOOL FreeLibrary(HMODULE h) { return dlclose(h) == 0; }
static inline void* GetProcAddress(HMODULE h, const char* n) { return dlsym(h, n); }

// Thread
struct _ThreadEntry { void(*fn)(void*); void* arg; };
static inline void* _thread_tramp(void* p) {
    _ThreadEntry* e = (_ThreadEntry*)p;
    void(*fn)(void*) = e->fn; void* arg = e->arg; free(e);
    fn(arg); return nullptr;
}
static inline HANDLE CreateThread(void*, size_t, void*(*fn)(void*), void* arg, DWORD, DWORD*) {
    pthread_t t;
    pthread_create(&t, nullptr, fn, arg);
    pthread_detach(t);
    return (HANDLE)(uintptr_t)t;
}
static inline void Sleep(DWORD ms) { usleep(ms * 1000); }
static inline void FreeLibraryAndExitThread(HMODULE h, DWORD) {
    if (h)
        dlclose(h);
    pthread_exit(nullptr);
}
static inline HANDLE GetCurrentProcess() { return (HANDLE)(uintptr_t)-1; }

// MODULEINFO — filled by /proc/self/maps, not Windows API
struct MODULEINFO { void* lpBaseOfDll; DWORD SizeOfImage; void* EntryPoint; };
static inline BOOL GetModuleInformation(HANDLE, HMODULE, MODULEINFO*, DWORD) { return FALSE; }

// _malloca / _freea
#define _malloca(n) alloca(n)
#define _freea(p)   ((void)0)

// Compiler intrinsics stubs
#define __readfsdword(x)  ((DWORD)0)
#define __readgsqword(x)  ((uint64_t)0)

// DbgHelp stub
static inline DWORD UnDecorateSymbolName(const char*, char* out, DWORD sz, DWORD) {
    if (out && sz)
        out[0] = '\0';
    return 0;
}

// Math defines (corecrt_math_defines.h equivalent)
#ifndef M_PI
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_E         2.71828182845904523536
#define M_SQRT2     1.41421356237309504880
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#endif

#endif // __linux__
