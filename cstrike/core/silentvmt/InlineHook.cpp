#include "InlineHook.h"
#include <cstdint>
#include <cstring>
#ifdef _WIN32
#include <memoryapi.h>
#include "../spoofcall/lazy_importer.hpp"
#define VPROTECT(a,s,p,o) LI_FN(VirtualProtect).safe()(a,s,p,o)
#define VALLOC(a,s,t,p)   LI_FN(VirtualAlloc).safe()(a,s,t,p)
#else
#define VPROTECT(a,s,p,o) VirtualProtect(a,s,p,o)
#define VALLOC(a,s,t,p)   VirtualAlloc(a,s,t,p)
#endif

static bool detour(BYTE* src, BYTE* dst, const uintptr_t len)
{
    if (len < 5) return false;
    DWORD curProtection = 0;
    VPROTECT(src, len, PAGE_EXECUTE_READWRITE, &curProtection);
    memset(src, 0x90, len);
    uintptr_t relativeAddress = ((uintptr_t)dst - (uintptr_t)src) - 5;
    *src = 0xE9;
    *reinterpret_cast<uintptr_t*>(src + 1) = relativeAddress;
    DWORD temp = 0;
    VPROTECT(src, len, curProtection, &temp);
    return true;
}

static BYTE* trampHook(BYTE* src, BYTE* dst, const uintptr_t len)
{
    if (len < 5) return nullptr;
    void* gateway = VALLOC(nullptr, len + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!gateway) return nullptr;
    memcpy(gateway, src, len);
    intptr_t gatewayRelativeAddr = ((intptr_t)src - (intptr_t)gateway) - 5;
    *reinterpret_cast<char*>((intptr_t)gateway + len) = 0xE9;
    *reinterpret_cast<intptr_t*>((intptr_t)gateway + len + 1) = gatewayRelativeAddr;
    return (BYTE*)gateway;
}

void InlineHook::Hook(void* src, void* dest, const size_t len)
{
    const BYTE* src_bytes = (BYTE*)src;
    for (size_t i = 0; i < len; i++)
        og_bytes.push_back(src_bytes[i]);

    source   = reinterpret_cast<uintptr_t>(src);
    original = reinterpret_cast<uintptr_t>(trampHook((BYTE*)src, (BYTE*)dest, len));

    if (original)
        bEnabled = true;
}

void InlineHook::Unhook()
{
    BYTE* bytes = reinterpret_cast<BYTE*>(source);
    size_t i = 0;
    for (const BYTE& b : og_bytes)
        bytes[i++] = b;
}
