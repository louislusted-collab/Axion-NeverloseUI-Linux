#ifdef __linux__

#include "vulkan_hook.h"

#include <atomic>

void* g_SDLWindow = nullptr;
bool g_MenuOpen = false;
void* g_PreviewTexture = nullptr;

namespace {
std::atomic<bool> g_MenuReady{false};
}

extern "C" void InstallVulkanHook()
{
    // Safe Linux fallback: hooking is intentionally disabled.
    // This keeps the project linkable while the original Vulkan hook source
    // is unavailable or being recovered.
}

extern "C" void EnableVulkanMenu()
{
    g_MenuReady.store(true, std::memory_order_release);
}

extern "C" void LoadCheatPreviewTexture()
{
    // No renderer is installed in this fallback implementation.
    g_PreviewTexture = nullptr;
}

extern "C" void UnloadCheatPreviewTexture()
{
    g_PreviewTexture = nullptr;
}

#endif // __linux__
