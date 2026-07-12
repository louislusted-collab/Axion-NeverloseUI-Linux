#include "ShadowVMT.h"
#include <cstdint>
#include <iostream>
#ifdef _WIN32
#include <memoryapi.h>
#include "../spoofcall/lazy_importer.hpp"
#define VPROTECT(addr, size, prot, old) LI_FN(VirtualProtect).safe()(addr, size, prot, old)
#define VQUERY(addr, info, sz)          LI_FN(VirtualQuery).safe()(reinterpret_cast<LPCVOID>(addr), info, sz)
#else
#include <cstdio>
#include <dlfcn.h>
#define VPROTECT(addr, size, prot, old) VirtualProtect(addr, size, prot, old)

namespace
{
	bool IsReadableRange(const void* address, std::size_t size)
	{
		if (address == nullptr || size == 0)
			return false;

		const auto begin = reinterpret_cast<std::uintptr_t>(address);
		if (begin > UINTPTR_MAX - size)
			return false;
		const auto end = begin + size;

		FILE* maps = std::fopen("/proc/self/maps", "r");
		if (maps == nullptr)
			return false;

		char line[512];
		bool readable = false;
		while (std::fgets(line, sizeof(line), maps) != nullptr) {
			unsigned long regionBegin = 0;
			unsigned long regionEnd = 0;
			char permissions[5] = {};
			if (std::sscanf(line, "%lx-%lx %4s", &regionBegin, &regionEnd, permissions) == 3 &&
				permissions[0] == 'r' && begin >= regionBegin && end <= regionEnd) {
				readable = true;
				break;
			}
		}

		std::fclose(maps);
		return readable;
	}

	bool IsExecutableAddress(std::uintptr_t address)
	{
		Dl_info info{};
		return address != 0 && dladdr(reinterpret_cast<void*>(address), &info) != 0 && info.dli_fbase != nullptr;
	}
}
#endif

ShadowVMT::ShadowVMT()
    : class_base(nullptr), vftbl_len(0), new_vftbl(nullptr), old_vftbl(nullptr)
{
}
ShadowVMT::ShadowVMT(void* base)
    : class_base(base), vftbl_len(0), new_vftbl(nullptr), old_vftbl(nullptr)
{
}
ShadowVMT::~ShadowVMT()
{
    UnhookAll();
}

bool ShadowVMT::Setup(void* base)
{
    if(base != nullptr)
        class_base = base;

    if(class_base == nullptr)
        return false;

#ifdef __linux__
    if (!IsReadableRange(class_base, sizeof(std::uintptr_t)))
        return false;
#endif

    old_vftbl = *(std::uintptr_t**)class_base;
    vftbl_len = CalcVtableLength(old_vftbl);

    if(vftbl_len == 0)
        return false;

    new_vftbl = new std::uintptr_t[vftbl_len + 1]();

    std::memcpy(&new_vftbl[1], old_vftbl, vftbl_len * sizeof(std::uintptr_t));

    try {
        DWORD old;
        VPROTECT(class_base, sizeof(uintptr_t), PAGE_READWRITE, &old);
        new_vftbl[0] = old_vftbl[-1];
        *(std::uintptr_t**)class_base = &new_vftbl[1];
        VPROTECT(class_base, sizeof(uintptr_t), old, &old);
    } catch(...) {
        delete[] new_vftbl;
        return false;
    }

    return true;
}

std::size_t ShadowVMT::CalcVtableLength(std::uintptr_t* vftbl_start)
{
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION memInfo = { NULL };
    int m_nSize = -1;
    do {
        m_nSize++;
        VQUERY(vftbl_start[m_nSize], &memInfo, sizeof(memInfo));
    } while (memInfo.Protect == PAGE_EXECUTE_READ || memInfo.Protect == PAGE_EXECUTE_READWRITE);
    return m_nSize;
#else
    // Itanium ABI vtables are not required to be null-terminated. Stop at the
    // first entry which is unreadable or is not an address in a loaded image.
    // The cap prevents a malformed table from walking indefinitely.
    if (!IsReadableRange(vftbl_start, sizeof(std::uintptr_t)))
        return 0;

    constexpr std::size_t kMaximumVtableEntries = 512;
    std::size_t size = 0;
    while (size < kMaximumVtableEntries &&
           IsReadableRange(vftbl_start + size, sizeof(std::uintptr_t)) &&
           IsExecutableAddress(vftbl_start[size])) {
        ++size;
    }
    return size;
#endif
}
