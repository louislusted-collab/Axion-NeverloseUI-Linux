#pragma once

#ifdef __linux__

#ifdef __cplusplus
extern "C" {
#endif

void InstallVulkanHook();
void EnableVulkanMenu();
bool InstallNativeInputHook();
void DestroyNativeInputHook();

// Queues a relative mouse delta for CS2's next SDL input sample. The native
// legitbot uses the same input path as physical mouse movement so CreateMove
// sees the adjustment when it builds the user command.
// Returns true when the delta was accepted by the in-process SDL relative-
// mouse hook. This never creates a kernel input device or moves the desktop
// pointer.
bool QueueNativeAimDelta(float x, float y);
void ClearNativeAimDelta();

// Schedules a schema-resolved pawn angle for the next CCSGOInput::CreateMove.
// The destination remains inside CS2; no OS or kernel input device is used.
bool QueueNativeAimAngles(void* destination, float pitch, float yaw, float roll);
unsigned long long GetNativeCreateMoveCalls();
unsigned long long GetNativeAimAngleApplications();
bool IsNativeInputHookInstalled();

// Keeps CS2's native third-person input flag synchronized during the game's
// SDL input sample. Writing this only from Vulkan present is too late for the
// current Linux camera update and the game can discard it before use.
void SetNativeThirdPersonInput(bool enabled);

// Native movement state consumed by CCSGOInput::CreateMove. These use the
// verified input axes/button block and never create a kernel input device.
void SetNativeBhopInput(bool enabled, bool spaceHeld, bool onGround);
void SetNativeStrafeInput(bool enabled, float forward, float left);

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
