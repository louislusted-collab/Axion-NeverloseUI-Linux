#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <cmath> // Cleaner standard C++ include

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"

#ifdef _WIN32
// ============================================================================
// WINDOWS ONLY DIRECTX 9 HEADERS
// ============================================================================
#include <d3dx9.h>
#include <d3d9.h>

#pragma comment ( lib, "d3d9.lib" )
#pragma comment ( lib, "d3dx9.lib" )
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

namespace blur {
    inline IDirect3DDevice9* device;
}

#else
// ============================================================================
// LINUX COMPATIBILITY LAYER / STUB
// ============================================================================
namespace blur {
    inline void* device = nullptr; // Dummy pointer so other files don't break
}
#endif

// This needs to remain visible on both platforms
extern void draw_blur( ImDrawList* drawList );
