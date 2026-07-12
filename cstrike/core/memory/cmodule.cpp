
#include "../pointer/pointer.hpp"
#include "cmodule.hpp"
#include "../fnv1a.hpp"
#ifdef _WIN32
#include <wtypes.h>
#include <Psapi.h>
#else
#include "../../../linux/linux_compat.h"
#include <stdio.h>
#include <string.h>
#endif
CModule::CModule(const char* name) {
    m_Name = name;
    m_Hash = fnv1a::Hash(name);
    m_Handle = nullptr;

    m_Begin = m_Size = 0;
}

CModule::~CModule() {
    if (!m_Handle) {
        return;
    }

#ifndef _WIN32
    dlclose(m_Handle);
#endif
}

bool CModule::Retrieve() {
    if (m_Handle) {
        return true;
    }

    InitializeHandle();
    InitializeBounds();

    return m_Handle != nullptr;
}

uintptr_t CModule::GetInterface(const char* version) {
    uintptr_t rv = 0;
    if (m_Handle) {
        CPointer pCreateInterface = GetProcAddress("CreateInterface");
        if (!pCreateInterface.IsValid()) {
            return rv;
        }

        // Used internally to register classes.
        struct InterfaceReg {
            std::add_pointer_t<uintptr_t()> m_CreateFn;
            const char* m_pName;
            InterfaceReg* m_pNext;  // For the global list.
        };

        InterfaceReg* s_pInterfaceRegs =
#ifdef _WIN32
            pCreateInterface.Absolute(3, 0).Dereference(1).Get<InterfaceReg*>()
#elif __linux__
            pCreateInterface.Absolute(1, 0).Absolute(19, 0).Dereference(1).Get<InterfaceReg*>()
#endif
            ;

        uint32_t versionHash = fnv1a::Hash(version);
        for (; s_pInterfaceRegs; s_pInterfaceRegs = s_pInterfaceRegs->m_pNext) {
            if (fnv1a::Hash(s_pInterfaceRegs->m_pName) == versionHash) {
                rv = s_pInterfaceRegs->m_CreateFn();
                break;
            }
        }
    }

    return rv;
}

uintptr_t CModule::GetProcAddress(const char* procName) {
    uintptr_t rv = 0;
    if (m_Handle) {
#ifdef _WIN32
        rv = reinterpret_cast<uintptr_t>(::GetProcAddress(static_cast<HMODULE>(m_Handle), procName));
#else
        rv = reinterpret_cast<uintptr_t>(dlsym(m_Handle, procName));
#endif
    }
    return rv;
}

uintptr_t CModule::FindPattern(const std::span<const int>& pattern) const {
    uintptr_t rv = 0;
    if (m_Handle) {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(m_Begin);

        // Faster than pattern[] in debug builds because of _STL_VERIFY.
        const int* patternData = pattern.data();
        const size_t patternSize = pattern.size();

        for (size_t i = 0; i < m_Size - patternSize; ++i) {
            bool found = true;
            for (size_t j = 0; j < patternSize; ++j) {
                if (bytes[i + j] != patternData[j] && patternData[j] != -1) {
                    found = false;
                    break;
                }
            }

            if (found) {
                rv = reinterpret_cast<uintptr_t>(&bytes[i]);
                break;
            }
        }
    }

    return rv;
}

void CModule::InitializeHandle() {
#ifdef _WIN32
    m_Handle = GetModuleHandle(GetName());
#else
    m_Handle = dlopen(GetName(), RTLD_LAZY | RTLD_NOLOAD);
#endif
}

void CModule::InitializeBounds() {
    if (!m_Handle) {
        return;
    }

#ifdef _WIN32
    MODULEINFO mi;
    BOOL status = GetModuleInformation(GetCurrentProcess(), static_cast<HMODULE>(m_Handle), &mi, sizeof(mi));
    if (status != 0)
        SetBounds(reinterpret_cast<uintptr_t>(m_Handle), mi.SizeOfImage);
#else
    // parse /proc/self/maps to find base + size of this .so
    Dl_info info;
    if (!dladdr(m_Handle, &info) || !info.dli_fbase)
        return;

    uintptr_t base = 0, end = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;

    char line[512];
    const char* soname = m_Name.c_str();
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, soname)) continue;
        uintptr_t lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) {
            if (!base) base = lo;
            end = hi;
        }
    }
    fclose(f);

    if (base && end > base)
        SetBounds(base, end - base);
#endif
}

void CModule::SetBounds(uintptr_t begin, uintptr_t size) {
    m_Begin = begin;
    m_Size = size;
}
