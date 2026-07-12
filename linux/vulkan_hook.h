#pragma once

#ifdef __linux__

#ifdef __cplusplus
extern "C" {
#endif

void InstallVulkanHook();
void EnableVulkanMenu();

void LoadCheatPreviewTexture();
void UnloadCheatPreviewTexture();

extern void* g_SDLWindow;
extern bool g_MenuOpen;
extern void* g_PreviewTexture;

#ifdef __cplusplus
}
#endif

#endif // __linux__
