#pragma once

#ifdef __linux__

#ifdef __cplusplus
extern "C" {
#endif

void InstallVulkanHook();
void EnableVulkanMenu();

// Loads/unloads cs2.png after the ImGui Vulkan backend is ready.
// AXION_PREVIEW_PNG may be used to override the image path.
void LoadCheatPreviewTexture();
void UnloadCheatPreviewTexture();

extern void* g_SDLWindow;
extern bool g_MenuOpen;
extern void* g_PreviewTexture;

#ifdef __cplusplus
}
#endif

#endif // __linux__
