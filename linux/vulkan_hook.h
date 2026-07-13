#pragma once

#ifdef __linux__

#ifdef __cplusplus
extern "C" {
#endif

void InstallVulkanHook();
void EnableVulkanMenu();

// Queues a relative mouse delta for CS2's next SDL input sample. The native
// legitbot uses the same input path as physical mouse movement so CreateMove
// sees the adjustment when it builds the user command.
// Returns true when the delta was accepted by either the kernel uinput device
// or the hooked SDL relative-mouse path.
bool QueueNativeAimDelta(float x, float y);

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
