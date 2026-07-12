#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include "../../../linux/linux_compat.h"
#endif
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <chrono>
#include <random>
#include <numeric>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <regex>
#include <cmath>
#include <fstream>
#include <cassert>
#ifdef _WIN32
#include <process.h>
#endif
#ifdef _WIN32
#include <DbgHelp.h>
#endif
#include <filesystem>
#ifdef _WIN32
#include <libloaderapi.h>
#include <Psapi.h>
#endif
#ifdef _WIN32
#include <corecrt_math_defines.h>
#else
#define M_PI 3.14159265358979323846
#endif
#include <numbers>
#include <iomanip>
#include <iosfwd>
#include <set>
#include <unordered_set>
#include <list>
#ifdef _WIN32
#include <TlHelp32.h>
#endif
#include <cinttypes>
#include <cstring>
class InlineHook
{
    std::vector<BYTE> og_bytes;
    uintptr_t original = 0;
    uintptr_t source = 0;
    bool bEnabled = false;
public:
    InlineHook(){}

    void Hook(void* src, void* dest, const size_t len);
    void Unhook();

    template<typename T>
    T GetOg()
    {
        return (T)original;
    }
};
