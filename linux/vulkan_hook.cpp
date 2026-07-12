#ifdef __linux__
#include "linux_compat.h"
#include "vulkan_hook.h"

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <funchook.h>

// stb_image implementation already lives in cstrike/utilities/draw.cpp.
#include "../dependencies/stb_image.h"

#include "../dependencies/imgui/imgui.h"
#include "../dependencies/imgui/imgui_settings.h"
#include "../dependencies/imgui/backends/imgui_impl_sdl3.h"
#include "../dependencies/imgui/backends/imgui_impl_vulkan.h"
#include "../cstrike/font.h"

#include "../cstrike/utilities/inputsystem.h"
#include "../cstrike/utilities/log.h"
#include "../cstrike/utilities/draw.h"
#include "../cstrike/core/menu.h"
#include "native_esp.h"

void* g_SDLWindow = nullptr;
bool  g_MenuOpen  = false;
static std::atomic<bool> g_MenuReady{false};

// Linux/Vulkan preview texture exposed to menu.cpp as an ImTextureID-compatible
// VkDescriptorSet. The actual Vulkan resources remain private to this file.
void* g_PreviewTexture = nullptr;

struct PreviewTextureState
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

static PreviewTextureState preview_texture{};

static VkDevice          vk_device          = VK_NULL_HANDLE;
static VkPhysicalDevice  vk_physical_device = VK_NULL_HANDLE;
static VkInstance        vk_instance        = VK_NULL_HANDLE;
static VkQueue           vk_queue           = VK_NULL_HANDLE;
static uint32_t          vk_queue_family    = (uint32_t)-1;
static VkRenderPass      vk_render_pass     = VK_NULL_HANDLE;
static VkDescriptorPool  vk_descriptor_pool = VK_NULL_HANDLE;
static VkExtent2D        vk_extent          = {};
static VkFormat          vk_format          = VK_FORMAT_B8G8R8A8_UNORM;
static uint32_t          vk_min_image_count = 2;
static bool              vk_imgui_inited    = false;
static bool              vk_sdl_inited      = false;

static ImGui_ImplVulkanH_Frame          frames[8]          = {};
static ImGui_ImplVulkanH_FrameSemaphores frame_sems[8]     = {};

static VkResult (*orig_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*) = nullptr;
static VkResult (*orig_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*,
                                             const VkAllocationCallbacks*, VkSwapchainKHR*) = nullptr;
static VkResult (*orig_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t,
                                              VkSemaphore, VkFence, uint32_t*) = nullptr;
static VkResult (*orig_vkAcquireNextImage2KHR)(VkDevice,
                                               const VkAcquireNextImageInfoKHR*, uint32_t*) = nullptr;
static bool (*orig_SDL_PollEvent)(SDL_Event*) = nullptr;
static int (*orig_SDL_PeepEvents)(SDL_Event*, int, SDL_EventAction, Uint32, Uint32) = nullptr;
static funchook_t* late_hooks = nullptr;
static VkInstance late_probe_instance = VK_NULL_HANDLE;
static VkDevice late_probe_device = VK_NULL_HANDLE;

struct QueueRecord { VkQueue queue = VK_NULL_HANDLE; uint32_t family = UINT32_MAX; };
static QueueRecord queue_records[32] = {};

static void VulkanDebug(const char* msg)
{
    FILE* f = fopen("/tmp/cs2_vulkan_debug.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

static SDL_Window* FindCS2Window()
{
    if (g_SDLWindow)
        return static_cast<SDL_Window*>(g_SDLWindow);

    SDL_Window* window = SDL_GetKeyboardFocus();
    if (!window)
        window = SDL_GetMouseFocus();

    if (!window) {
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        for (int i = 0; windows && i < count; ++i) {
            int width = 0, height = 0;
            if (windows[i] && SDL_GetWindowSizeInPixels(windows[i], &width, &height) &&
                width > 0 && height > 0) {
                window = windows[i];
                break;
            }
        }
        SDL_free(windows);
    }

    g_SDLWindow = window;
    return window;
}


static void PreviewDebug(const char* format, ...)
{
    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    VulkanDebug(buffer);
}

static std::string ResolvePreviewPngPath()
{
    std::vector<std::string> candidates;

    if (const char* override_path = std::getenv("AXION_PREVIEW_PNG");
        override_path != nullptr && *override_path != '\0')
    {
        candidates.emplace_back(override_path);
    }

    Dl_info module_info{};
    if (dladdr(reinterpret_cast<void*>(&ResolvePreviewPngPath), &module_info) != 0 &&
        module_info.dli_fname != nullptr)
    {
        std::string module_path(module_info.dli_fname);
        const std::size_t slash = module_path.find_last_of('/');
        if (slash != std::string::npos)
            candidates.emplace_back(module_path.substr(0, slash + 1) + "cs2.png");
    }

    candidates.emplace_back("./cs2.png");
    candidates.emplace_back("/home/louis/cheat/Axion-NeverloseUI/cs2.png");

    for (const std::string& candidate : candidates)
    {
        if (!candidate.empty() && access(candidate.c_str(), R_OK) == 0)
            return candidate;
    }

    return {};
}

static bool FindMemoryType(uint32_t type_bits,
                           VkMemoryPropertyFlags required_properties,
                           uint32_t* memory_type_index)
{
    if (vk_physical_device == VK_NULL_HANDLE || memory_type_index == nullptr)
        return false;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(vk_physical_device, &properties);

    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        const bool type_supported = (type_bits & (1u << index)) != 0;
        const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
        if (type_supported && (flags & required_properties) == required_properties)
        {
            *memory_type_index = index;
            return true;
        }
    }

    return false;
}

static void DestroyPreviewTextureResources(bool remove_descriptor)
{
    g_PreviewTexture = nullptr;

    if (vk_device == VK_NULL_HANDLE)
    {
        preview_texture = {};
        return;
    }

    if (remove_descriptor && preview_texture.descriptor != VK_NULL_HANDLE &&
        vk_imgui_inited && ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplVulkan_RemoveTexture(preview_texture.descriptor);
    }

    preview_texture.descriptor = VK_NULL_HANDLE;

    if (preview_texture.sampler != VK_NULL_HANDLE)
        vkDestroySampler(vk_device, preview_texture.sampler, nullptr);
    if (preview_texture.view != VK_NULL_HANDLE)
        vkDestroyImageView(vk_device, preview_texture.view, nullptr);
    if (preview_texture.image != VK_NULL_HANDLE)
        vkDestroyImage(vk_device, preview_texture.image, nullptr);
    if (preview_texture.memory != VK_NULL_HANDLE)
        vkFreeMemory(vk_device, preview_texture.memory, nullptr);

    preview_texture = {};
}

static bool LoadPreviewTextureInternal()
{
    if (g_PreviewTexture != nullptr)
        return true;

    if (!vk_imgui_inited || vk_device == VK_NULL_HANDLE ||
        vk_physical_device == VK_NULL_HANDLE || vk_queue == VK_NULL_HANDLE ||
        vk_queue_family == UINT32_MAX)
    {
        return false;
    }

    const std::string image_path = ResolvePreviewPngPath();
    if (image_path.empty())
    {
        VulkanDebug("[VULKAN] cs2.png not found. Set AXION_PREVIEW_PNG to its full path.");
        return false;
    }

    int width = 0;
    int height = 0;
    int source_channels = 0;
    stbi_uc* pixels = stbi_load(image_path.c_str(), &width, &height, &source_channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        PreviewDebug("[VULKAN] Failed to decode preview PNG: %s (%s)",
                     image_path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
        if (pixels != nullptr)
            stbi_image_free(pixels);
        return false;
    }

    const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) *
                                    static_cast<VkDeviceSize>(height) * 4u;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    auto fail = [&](const char* step) -> bool
    {
        PreviewDebug("[VULKAN] Preview texture setup failed at: %s", step);

        if (command_buffer != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE)
            vkFreeCommandBuffers(vk_device, command_pool, 1, &command_buffer);
        if (command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(vk_device, command_pool, nullptr);
        if (staging_buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(vk_device, staging_buffer, nullptr);
        if (staging_memory != VK_NULL_HANDLE)
            vkFreeMemory(vk_device, staging_memory, nullptr);

        stbi_image_free(pixels);
        DestroyPreviewTextureResources(false);
        return false;
    };

    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = image_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk_device, &buffer_info, nullptr, &staging_buffer) != VK_SUCCESS)
        return fail("vkCreateBuffer");

    VkMemoryRequirements staging_requirements{};
    vkGetBufferMemoryRequirements(vk_device, staging_buffer, &staging_requirements);

    uint32_t staging_memory_type = 0;
    if (!FindMemoryType(staging_requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &staging_memory_type))
    {
        return fail("host-visible staging memory type");
    }

    VkMemoryAllocateInfo staging_allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    staging_allocation.allocationSize = staging_requirements.size;
    staging_allocation.memoryTypeIndex = staging_memory_type;
    if (vkAllocateMemory(vk_device, &staging_allocation, nullptr, &staging_memory) != VK_SUCCESS)
        return fail("vkAllocateMemory(staging)");
    if (vkBindBufferMemory(vk_device, staging_buffer, staging_memory, 0) != VK_SUCCESS)
        return fail("vkBindBufferMemory");

    void* mapped_memory = nullptr;
    if (vkMapMemory(vk_device, staging_memory, 0, image_size, 0, &mapped_memory) != VK_SUCCESS ||
        mapped_memory == nullptr)
    {
        return fail("vkMapMemory");
    }
    std::memcpy(mapped_memory, pixels, static_cast<std::size_t>(image_size));
    vkUnmapMemory(vk_device, staging_memory);
    stbi_image_free(pixels);
    pixels = nullptr;

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1u};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk_device, &image_info, nullptr, &preview_texture.image) != VK_SUCCESS)
        return fail("vkCreateImage");

    VkMemoryRequirements image_requirements{};
    vkGetImageMemoryRequirements(vk_device, preview_texture.image, &image_requirements);

    uint32_t image_memory_type = 0;
    if (!FindMemoryType(image_requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        &image_memory_type))
    {
        return fail("device-local image memory type");
    }

    VkMemoryAllocateInfo image_allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    image_allocation.allocationSize = image_requirements.size;
    image_allocation.memoryTypeIndex = image_memory_type;
    if (vkAllocateMemory(vk_device, &image_allocation, nullptr, &preview_texture.memory) != VK_SUCCESS)
        return fail("vkAllocateMemory(image)");
    if (vkBindImageMemory(vk_device, preview_texture.image, preview_texture.memory, 0) != VK_SUCCESS)
        return fail("vkBindImageMemory");

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = vk_queue_family;
    if (vkCreateCommandPool(vk_device, &pool_info, nullptr, &command_pool) != VK_SUCCESS)
        return fail("vkCreateCommandPool");

    VkCommandBufferAllocateInfo command_allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_allocation.commandPool = command_pool;
    command_allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vk_device, &command_allocation, &command_buffer) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS)
        return fail("vkBeginCommandBuffer");

    VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = preview_texture.image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_transfer);

    VkBufferImageCopy copy_region{};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1u};
    vkCmdCopyBufferToImage(command_buffer,
                           staging_buffer,
                           preview_texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);

    VkImageMemoryBarrier to_shader_read{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.image = preview_texture.image;
    to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader_read.subresourceRange.levelCount = 1;
    to_shader_read.subresourceRange.layerCount = 1;
    to_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_shader_read);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
        return fail("vkEndCommandBuffer");

    VkSubmitInfo upload_submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    upload_submit.commandBufferCount = 1;
    upload_submit.pCommandBuffers = &command_buffer;
    if (vkQueueSubmit(vk_queue, 1, &upload_submit, VK_NULL_HANDLE) != VK_SUCCESS)
        return fail("vkQueueSubmit(upload)");
    if (vkQueueWaitIdle(vk_queue) != VK_SUCCESS)
        return fail("vkQueueWaitIdle(upload)");

    vkFreeCommandBuffers(vk_device, command_pool, 1, &command_buffer);
    command_buffer = VK_NULL_HANDLE;
    vkDestroyCommandPool(vk_device, command_pool, nullptr);
    command_pool = VK_NULL_HANDLE;
    vkDestroyBuffer(vk_device, staging_buffer, nullptr);
    staging_buffer = VK_NULL_HANDLE;
    vkFreeMemory(vk_device, staging_memory, nullptr);
    staging_memory = VK_NULL_HANDLE;

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = preview_texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk_device, &view_info, nullptr, &preview_texture.view) != VK_SUCCESS)
        return fail("vkCreateImageView");

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
    sampler_info.maxAnisotropy = 1.0f;
    if (vkCreateSampler(vk_device, &sampler_info, nullptr, &preview_texture.sampler) != VK_SUCCESS)
        return fail("vkCreateSampler");

    preview_texture.descriptor = ImGui_ImplVulkan_AddTexture(
        preview_texture.sampler,
        preview_texture.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (preview_texture.descriptor == VK_NULL_HANDLE)
        return fail("ImGui_ImplVulkan_AddTexture");

    g_PreviewTexture = reinterpret_cast<void*>(preview_texture.descriptor);
    PreviewDebug("[VULKAN] Loaded preview PNG: %s (%dx%d)", image_path.c_str(), width, height);
    return true;
}

static unsigned int SDLKeycodeToVK(SDL_Keycode key)
{
    if (key >= SDLK_A && key <= SDLK_Z) return 'A' + (key - SDLK_A);
    if (key >= SDLK_0 && key <= SDLK_9) return '0' + (key - SDLK_0);
    switch (key) {
    case SDLK_RETURN:    return VK_RETURN;
    case SDLK_ESCAPE:    return VK_ESCAPE;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB:       return VK_TAB;
    case SDLK_SPACE:     return VK_SPACE;
    case SDLK_INSERT:    return VK_INSERT;
    case SDLK_DELETE:    return VK_DELETE;
    case SDLK_HOME:      return VK_HOME;
    case SDLK_END:       return VK_END;
    case SDLK_PAGEUP:    return VK_PRIOR;
    case SDLK_PAGEDOWN:  return VK_NEXT;
    case SDLK_LEFT:      return VK_LEFT;
    case SDLK_RIGHT:     return VK_RIGHT;
    case SDLK_UP:        return VK_UP;
    case SDLK_DOWN:      return VK_DOWN;
    case SDLK_LSHIFT:    return VK_LSHIFT;
    case SDLK_RSHIFT:    return VK_RSHIFT;
    case SDLK_LCTRL:     return VK_LCONTROL;
    case SDLK_RCTRL:     return VK_RCONTROL;
    case SDLK_LALT:      return VK_MENU;
    case SDLK_RALT:      return VK_MENU;
    case SDLK_F1 ... SDLK_F12: return VK_F1 + (key - SDLK_F1);
    default:             return 0;
    }
}

static unsigned int SDLMouseButtonToVK(uint8_t button)
{
    switch (button) {
    case SDL_BUTTON_LEFT:   return VK_LBUTTON;
    case SDL_BUTTON_RIGHT:  return VK_RBUTTON;
    case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
    case SDL_BUTTON_X1:     return VK_XBUTTON1;
    case SDL_BUTTON_X2:     return VK_XBUTTON2;
    default:                return 0;
    }
}

static void ProcessSDLEvent(SDL_Event* event)
{
    if (!event) return;
    FindCS2Window();

    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        unsigned int vk = SDLKeycodeToVK(event->key.key);
        if (vk < 256)
            IPT::arrKeyState[vk] = (event->type == SDL_EVENT_KEY_DOWN) ? IPT::KEY_STATE_DOWN : IPT::KEY_STATE_RELEASED;
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        unsigned int vk = SDLMouseButtonToVK(event->button.button);
        if (vk < 256)
            IPT::arrKeyState[vk] = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? IPT::KEY_STATE_DOWN : IPT::KEY_STATE_RELEASED;
        break;
    }
    default: break;
    }

    if (ImGui::GetCurrentContext() && g_SDLWindow)
        ImGui_ImplSDL3_ProcessEvent(event);

    if (g_MenuOpen) {
        switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:      case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_KEY_DOWN:          case SDL_EVENT_KEY_UP:
            event->type = (SDL_EventType)0;
        default: break;
        }
    }
}

static void DestroyFrameResources()
{
    if (vk_device == VK_NULL_HANDLE) return;
    for (int i = 0; i < 8; ++i) {
        auto& f = frames[i];
        if (f.Framebuffer)        { vkDestroyFramebuffer(vk_device, f.Framebuffer, nullptr); f.Framebuffer = VK_NULL_HANDLE; }
        if (f.BackbufferView)     { vkDestroyImageView(vk_device, f.BackbufferView, nullptr); f.BackbufferView = VK_NULL_HANDLE; }
        if (f.CommandBuffer)      { vkFreeCommandBuffers(vk_device, f.CommandPool, 1, &f.CommandBuffer); f.CommandBuffer = VK_NULL_HANDLE; }
        if (f.CommandPool)        { vkDestroyCommandPool(vk_device, f.CommandPool, nullptr); f.CommandPool = VK_NULL_HANDLE; }
        if (f.Fence)              { vkDestroyFence(vk_device, f.Fence, nullptr); f.Fence = VK_NULL_HANDLE; }
        f.Backbuffer = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 8; ++i) {
        auto& fs = frame_sems[i];
        if (fs.ImageAcquiredSemaphore)   { vkDestroySemaphore(vk_device, fs.ImageAcquiredSemaphore, nullptr); fs.ImageAcquiredSemaphore = VK_NULL_HANDLE; }
        if (fs.RenderCompleteSemaphore)  { vkDestroySemaphore(vk_device, fs.RenderCompleteSemaphore, nullptr); fs.RenderCompleteSemaphore = VK_NULL_HANDLE; }
    }
    if (vk_descriptor_pool) { vkDestroyDescriptorPool(vk_device, vk_descriptor_pool, nullptr); vk_descriptor_pool = VK_NULL_HANDLE; }
    if (vk_render_pass)     { vkDestroyRenderPass(vk_device, vk_render_pass, nullptr); vk_render_pass = VK_NULL_HANDLE; }
    vk_imgui_inited = false;
}

static bool EnsureFrameResources(VkSwapchainKHR swapchain)
{
    if (frames[0].Framebuffer != VK_NULL_HANDLE) return vk_imgui_inited;
    DestroyFrameResources();

    if (vk_device == VK_NULL_HANDLE || vk_queue == VK_NULL_HANDLE ||
        vk_queue_family == UINT32_MAX || vk_extent.width == 0 || vk_extent.height == 0)
        return false;

    uint32_t img_count = 0;
    if (vkGetSwapchainImagesKHR(vk_device, swapchain, &img_count, nullptr) != VK_SUCCESS || img_count == 0)
        return false;
    if (img_count > 8) img_count = 8;

    VkImage backbuffers[8] = {};
    if (vkGetSwapchainImagesKHR(vk_device, swapchain, &img_count, backbuffers) != VK_SUCCESS)
        return false;

    for (uint32_t i = 0; i < img_count; ++i) {
        auto* f  = &frames[i];
        auto* fs = &frame_sems[i];
        f->Backbuffer = backbuffers[i];

        VkCommandPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pool_ci.queueFamilyIndex = vk_queue_family;
        pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(vk_device, &pool_ci, nullptr, &f->CommandPool) != VK_SUCCESS) return false;

        VkCommandBufferAllocateInfo alloc_ci = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        alloc_ci.commandPool = f->CommandPool;
        alloc_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_ci.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(vk_device, &alloc_ci, &f->CommandBuffer) != VK_SUCCESS) return false;

        VkFenceCreateInfo fence_ci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(vk_device, &fence_ci, nullptr, &f->Fence) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo sem_ci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(vk_device, &sem_ci, nullptr, &fs->ImageAcquiredSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(vk_device, &sem_ci, nullptr, &fs->RenderCompleteSemaphore) != VK_SUCCESS) return false;
    }

    VkAttachmentDescription attachment = {};
    attachment.format = vk_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp_ci.attachmentCount = 1; rp_ci.pAttachments = &attachment;
    rp_ci.subpassCount = 1; rp_ci.pSubpasses = &subpass;

    if (vkCreateRenderPass(vk_device, &rp_ci, nullptr, &vk_render_pass) != VK_SUCCESS) return false;

    VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vk_format;
    view_ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    for (uint32_t i = 0; i < img_count; ++i) {
        view_ci.image = frames[i].Backbuffer;
        if (vkCreateImageView(vk_device, &view_ci, nullptr, &frames[i].BackbufferView) != VK_SUCCESS) return false;
    }

    VkFramebufferCreateInfo fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fb_ci.renderPass = vk_render_pass;
    fb_ci.attachmentCount = 1; fb_ci.layers = 1;

    for (uint32_t i = 0; i < img_count; ++i) {
        VkImageView att = frames[i].BackbufferView;
        fb_ci.pAttachments = &att;
        if (vkCreateFramebuffer(vk_device, &fb_ci, nullptr, &frames[i].Framebuffer) != VK_SUCCESS) return false;
    }

    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 } };
    VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;
    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &vk_descriptor_pool) != VK_SUCCESS) return false;

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = vk_instance;
        init_info.PhysicalDevice = vk_physical_device;
        init_info.Device = vk_device;
        init_info.QueueFamily = vk_queue_family;
        init_info.Queue = vk_queue;
        init_info.DescriptorPool = vk_descriptor_pool;
        init_info.MinImageCount = vk_min_image_count;
        init_info.ImageCount = img_count;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        if (vk_extent.width > 0 && vk_extent.height > 0) {
            ImGui::GetIO().DisplaySize.x = (float)vk_extent.width;
            ImGui::GetIO().DisplaySize.y = (float)vk_extent.height;
        }

        if (!ImGui_ImplVulkan_Init(&init_info, vk_render_pass)) return false;

        VkCommandPool font_pool;
        VkCommandPoolCreateInfo fp_ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        fp_ci.queueFamilyIndex = vk_queue_family;
        vkCreateCommandPool(vk_device, &fp_ci, nullptr, &font_pool);

        VkCommandBuffer font_cmd;
        VkCommandBufferAllocateInfo fa_ci = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        fa_ci.commandPool = font_pool;
        fa_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        fa_ci.commandBufferCount = 1;
        vkAllocateCommandBuffers(vk_device, &fa_ci, &font_cmd);

        VkCommandBufferBeginInfo begin_ci = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(font_cmd, &begin_ci);
        ImGui_ImplVulkan_CreateFontsTexture(font_cmd);
        vkEndCommandBuffer(font_cmd);

        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &font_cmd;
        vkQueueSubmit(vk_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(vk_queue);

        vkFreeCommandBuffers(vk_device, font_pool, 1, &font_cmd);
        vkDestroyCommandPool(vk_device, font_pool, nullptr);

        vk_imgui_inited = true;
        LoadCheatPreviewTexture();
    }
    return vk_imgui_inited;
}

static VkResult Hooked_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pInfo)
{
    static bool first = false;
    if (!first) { first = true; VulkanDebug("[VULKAN] vkQueuePresentKHR intercepted"); }

    if (!g_MenuReady.load() || !vk_device || !pInfo || pInfo->swapchainCount == 0)
        return orig_vkQueuePresentKHR(queue, pInfo);

    if (vk_queue == VK_NULL_HANDLE) vk_queue = queue;

    VkSwapchainKHR swapchain = pInfo->pSwapchains[0];
    uint32_t image_index = pInfo->pImageIndices ? pInfo->pImageIndices[0] : 0;
    if (image_index >= 8) return orig_vkQueuePresentKHR(queue, pInfo);

    SDL_Window* window = FindCS2Window();
    if (!window) return orig_vkQueuePresentKHR(queue, pInfo);

    if (vk_extent.width == 0 || vk_extent.height == 0) {
        int width = 0, height = 0;
        if (SDL_GetWindowSizeInPixels(window, &width, &height) && width > 0 && height > 0) {
            vk_extent.width = static_cast<uint32_t>(width);
            vk_extent.height = static_cast<uint32_t>(height);
            PreviewDebug("[VULKAN] late drawable size: %dx%d", width, height);
        }
    }

    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        if (!ImGui_ImplSDL3_InitForVulkan(window)) {
            ImGui::DestroyContext();
            return orig_vkQueuePresentKHR(queue, pInfo);
        }
        vk_sdl_inited = true;
        ImFontConfig font_cfg{};
        font_cfg.FontDataOwnedByAtlas = false;
        font::lexend_regular = io.Fonts->AddFontFromMemoryTTF(
            lexend_regular, sizeof(lexend_regular), 14.f, &font_cfg,
            io.Fonts->GetGlyphRangesCyrillic());
        font::lexend_bold = io.Fonts->AddFontFromMemoryTTF(
            lexend_bold, sizeof(lexend_bold), 15.f, &font_cfg,
            io.Fonts->GetGlyphRangesCyrillic());
        font::lexend_general_bold = font::lexend_bold;
        font::icomoon = io.Fonts->AddFontFromMemoryTTF(
            icomoon, sizeof(icomoon), 20.f, &font_cfg,
            io.Fonts->GetGlyphRangesCyrillic());
        font::icomoon_widget = io.Fonts->AddFontFromMemoryTTF(
            icomoon_widget, sizeof(icomoon_widget), 15.f, &font_cfg,
            io.Fonts->GetGlyphRangesCyrillic());
        font::icomoon_widget2 = io.Fonts->AddFontFromMemoryTTF(
            icomoon, sizeof(icomoon), 16.f, &font_cfg,
            io.Fonts->GetGlyphRangesCyrillic());

        if (!font::lexend_regular || !font::lexend_bold || !font::icomoon || !font::icomoon_widget) {
            ImFont* fallback = io.Fonts->AddFontDefault();
            font::lexend_regular = fallback;
            font::lexend_bold = fallback;
            font::lexend_general_bold = fallback;
            font::icomoon = fallback;
            font::icomoon_widget = fallback;
            font::icomoon_widget2 = fallback;
        }

        FONT::pVisual = font::lexend_regular;
        FONT::pEspName = font::lexend_regular;
        FONT::pEspFlagsName = font::lexend_regular;
        FONT::pEspWepName = font::lexend_regular;
        FONT::pEspHealth = font::lexend_regular;
        FONT::pEspIcons = font::icomoon_widget;
        FONT::pExtra = font::lexend_regular;
        FONT::pMenuTabsDesc = font::lexend_regular;
        for (ImFont*& menu_font : FONT::pMenu)
            menu_font = font::lexend_regular;
        io.Fonts->Build();

        g_MenuOpen = true;
        MENU::bMainWindowOpened = true;
        MENU::animMenuDimBackground.SetSwitch(true);
        VulkanDebug("[VULKAN] ImGui context initialized; menu opened");
    }

    if (!EnsureFrameResources(swapchain) || frames[image_index].Framebuffer == VK_NULL_HANDLE)
        return orig_vkQueuePresentKHR(queue, pInfo);

    auto* fd  = &frames[image_index];
    auto* fsd = &frame_sems[image_index];

    vkWaitForFences(vk_device, 1, &fd->Fence, VK_TRUE, ~0ull);
    vkResetFences(vk_device, 1, &fd->Fence);

    vkResetCommandBuffer(fd->CommandBuffer, 0);
    VkCommandBufferBeginInfo begin_ci = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(fd->CommandBuffer, &begin_ci);

    VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp_begin.renderPass = vk_render_pass;
    rp_begin.framebuffer = fd->Framebuffer;
    rp_begin.renderArea.extent = vk_extent;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::GetIO().MouseDrawCursor = g_MenuOpen;

    if (ImGui::IsKeyPressed(ImGuiKey_Insert, false)) {
        g_MenuOpen = !g_MenuOpen;
        MENU::animMenuDimBackground.Switch();
    }
    MENU::bMainWindowOpened = g_MenuOpen;
    LinuxNativeEsp::Render();
    MENU::RenderMainWindow();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    vkEndCommandBuffer(fd->CommandBuffer);

    std::vector<VkPipelineStageFlags> wait_stages(pInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = pInfo->waitSemaphoreCount;
    submit.pWaitSemaphores = pInfo->pWaitSemaphores;
    submit.pWaitDstStageMask = wait_stages.empty() ? nullptr : wait_stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &fd->CommandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &fsd->RenderCompleteSemaphore;
    if (vkQueueSubmit(vk_queue, 1, &submit, fd->Fence) != VK_SUCCESS) {
        vkDeviceWaitIdle(vk_device);
        UnloadCheatPreviewTexture();
        if (vk_imgui_inited) {
            ImGui_ImplVulkan_Shutdown();
            vk_imgui_inited = false;
        }
        DestroyFrameResources();
        return orig_vkQueuePresentKHR(queue, pInfo);
    }

    VkPresentInfoKHR hooked_present = *pInfo;
    hooked_present.waitSemaphoreCount = 1;
    hooked_present.pWaitSemaphores = &fsd->RenderCompleteSemaphore;
    return orig_vkQueuePresentKHR(queue, &hooked_present);
}

static VkResult Hooked_CreateSwapchainKHR(VkDevice device,
                                           const VkSwapchainCreateInfoKHR* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkSwapchainKHR* pSwapchain)
{
    VulkanDebug("[VULKAN] vkCreateSwapchainKHR intercepted");
    vk_device = device;
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);

    if (ImGui::GetCurrentContext()) {
        UnloadCheatPreviewTexture();
        if (vk_imgui_inited)  ImGui_ImplVulkan_Shutdown();
        if (vk_sdl_inited)    ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        vk_imgui_inited = false;
        vk_sdl_inited = false;
    }
    DestroyFrameResources();

    if (pCreateInfo) {
        vk_extent = pCreateInfo->imageExtent;
        vk_format = pCreateInfo->imageFormat;
        vk_min_image_count = pCreateInfo->minImageCount;
    }
    return orig_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}

static void CaptureLateDevice(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return;

    vk_device = device;
    void* vulkan = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
    auto get_device_queue = vulkan
        ? reinterpret_cast<PFN_vkGetDeviceQueue>(dlsym(vulkan, "vkGetDeviceQueue"))
        : nullptr;
    if (get_device_queue && vk_queue_family != UINT32_MAX) {
        VkQueue graphics_queue = VK_NULL_HANDLE;
        get_device_queue(device, vk_queue_family, 0, &graphics_queue);
        if (graphics_queue != VK_NULL_HANDLE)
            vk_queue = graphics_queue;
    }
    if (vulkan)
        dlclose(vulkan);
}

static VkResult Late_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                         uint64_t timeout, VkSemaphore semaphore,
                                         VkFence fence, uint32_t* image_index)
{
    CaptureLateDevice(device);
    return orig_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, image_index);
}

static VkResult Late_AcquireNextImage2KHR(VkDevice device,
                                          const VkAcquireNextImageInfoKHR* info,
                                          uint32_t* image_index)
{
    CaptureLateDevice(device);
    return orig_vkAcquireNextImage2KHR(device, info, image_index);
}

// ── SDL event hooks ────────────────────────────────────────────────────────
static bool Hooked_SDL_PollEvent(SDL_Event* event)
{
    bool result = orig_SDL_PollEvent(event);
    if (result) ProcessSDLEvent(event);
    return result;
}

static int Late_SDL_PeepEvents(SDL_Event* events, int numevents,
                               SDL_EventAction action, Uint32 minType, Uint32 maxType)
{
    const int result = orig_SDL_PeepEvents(events, numevents, action, minType, maxType);
    if (result > 0 && events && action != SDL_ADDEVENT) {
        for (int i = 0; i < result; ++i)
            ProcessSDLEvent(&events[i]);
    }
    return result;
}

// ── LD_PRELOAD exports ─────────────────────────────────────────────────────
extern "C" VkResult vkCreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc, VkInstance* inst)
{
    static auto real = reinterpret_cast<PFN_vkCreateInstance>(dlsym(RTLD_NEXT, "vkCreateInstance"));
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = real(ci, alloc, inst);
    if (r == VK_SUCCESS && inst) { vk_instance = *inst; VulkanDebug("[VULKAN] vkCreateInstance"); }
    return r;
}

extern "C" VkResult vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo* ci, const VkAllocationCallbacks* alloc, VkDevice* dev)
{
    static auto real = reinterpret_cast<PFN_vkCreateDevice>(dlsym(RTLD_NEXT, "vkCreateDevice"));
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = real(pd, ci, alloc, dev);
    if (r == VK_SUCCESS && dev) {
        vk_physical_device = pd; vk_device = *dev;
        if (ci && ci->queueCreateInfoCount > 0) vk_queue_family = ci->pQueueCreateInfos[0].queueFamilyIndex;
        VulkanDebug("[VULKAN] vkCreateDevice");
    }
    return r;
}

extern "C" void vkGetDeviceQueue(VkDevice dev, uint32_t family, uint32_t idx, VkQueue* q)
{
    static auto real = reinterpret_cast<PFN_vkGetDeviceQueue>(dlsym(RTLD_NEXT, "vkGetDeviceQueue"));
    if (!real) return;
    real(dev, family, idx, q);
    if (q && *q) { vk_device = dev; vk_queue_family = family; vk_queue = *q; }
}

extern "C" void vkGetDeviceQueue2(VkDevice dev, const VkDeviceQueueInfo2* info, VkQueue* q)
{
    static auto real = reinterpret_cast<PFN_vkGetDeviceQueue2>(dlsym(RTLD_NEXT, "vkGetDeviceQueue2"));
    if (!real) return;
    real(dev, info, q);
    if (info && q && *q) { vk_device = dev; vk_queue_family = info->queueFamilyIndex; vk_queue = *q; }
}

extern "C" VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    if (!orig_vkQueuePresentKHR)
        orig_vkQueuePresentKHR = reinterpret_cast<decltype(orig_vkQueuePresentKHR)>(dlsym(RTLD_NEXT, "vkQueuePresentKHR"));
    return orig_vkQueuePresentKHR ? Hooked_QueuePresentKHR(queue, info) : VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" VkResult vkCreateSwapchainKHR(VkDevice dev, const VkSwapchainCreateInfoKHR* info,
                                           const VkAllocationCallbacks* alloc, VkSwapchainKHR* sc)
{
    if (!orig_vkCreateSwapchainKHR)
        orig_vkCreateSwapchainKHR = reinterpret_cast<decltype(orig_vkCreateSwapchainKHR)>(dlsym(RTLD_NEXT, "vkCreateSwapchainKHR"));
    return orig_vkCreateSwapchainKHR ? Hooked_CreateSwapchainKHR(dev, info, alloc, sc) : VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance inst, const char* name)
{
    static auto real = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(RTLD_NEXT, "vkGetInstanceProcAddr"));
    if (!real || !name) return real ? real(inst, name) : nullptr;
    if (inst) vk_instance = inst;
    if (!strcmp(name, "vkCreateInstance"))    return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (!strcmp(name, "vkCreateDevice"))      return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (!strcmp(name, "vkGetDeviceQueue"))    return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue);
    if (!strcmp(name, "vkGetDeviceQueue2"))   return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue2);
    if (!strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    return real(inst, name);
}

extern "C" PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice dev, const char* name)
{
    static auto real = reinterpret_cast<PFN_vkVoidFunction(*)(VkDevice, const char*)>(dlsym(RTLD_NEXT, "vkGetDeviceProcAddr"));
    if (!real) return nullptr;
    if (!name) return real(dev, name);
    if (dev != VK_NULL_HANDLE) vk_device = dev;
    if (!strcmp(name, "vkGetDeviceQueue"))     return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue);
    if (!strcmp(name, "vkGetDeviceQueue2"))    return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue2);
    if (!strcmp(name, "vkQueuePresentKHR")) {
        if (!orig_vkQueuePresentKHR) orig_vkQueuePresentKHR = reinterpret_cast<decltype(orig_vkQueuePresentKHR)>(real(dev, name));
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
    }
    if (!strcmp(name, "vkCreateSwapchainKHR")) {
        if (!orig_vkCreateSwapchainKHR) orig_vkCreateSwapchainKHR = reinterpret_cast<decltype(orig_vkCreateSwapchainKHR)>(real(dev, name));
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateSwapchainKHR);
    }
    return real(dev, name);
}


void LoadCheatPreviewTexture()
{
    (void)LoadPreviewTextureInternal();
}

void UnloadCheatPreviewTexture()
{
    if (vk_device != VK_NULL_HANDLE && g_PreviewTexture != nullptr)
        vkDeviceWaitIdle(vk_device);
    DestroyPreviewTextureResources(true);
}

void InstallVulkanHook()
{
    L_PRINT(LOG_INFO) << "[VULKAN] InstallVulkanHook called";
    if (late_hooks != nullptr)
        return;

    void* vulkan = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!vulkan) {
        VulkanDebug("[VULKAN] late hook: libvulkan is not loaded");
        return;
    }

    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(dlsym(vulkan, "vkCreateInstance"));
    auto enumerate_devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(dlsym(vulkan, "vkEnumeratePhysicalDevices"));
    auto get_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(dlsym(vulkan, "vkGetPhysicalDeviceProperties"));
    auto get_queue_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(dlsym(vulkan, "vkGetPhysicalDeviceQueueFamilyProperties"));
    auto create_device = reinterpret_cast<PFN_vkCreateDevice>(dlsym(vulkan, "vkCreateDevice"));
    auto destroy_device = reinterpret_cast<PFN_vkDestroyDevice>(dlsym(vulkan, "vkDestroyDevice"));
    auto get_device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(vulkan, "vkGetDeviceProcAddr"));
    if (!create_instance || !enumerate_devices || !get_properties || !get_queue_properties ||
        !create_device || !destroy_device || !get_device_proc) {
        VulkanDebug("[VULKAN] late hook: missing loader functions");
        dlclose(vulkan);
        return;
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Axion probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    if (create_instance(&instance_info, nullptr, &late_probe_instance) != VK_SUCCESS) {
        VulkanDebug("[VULKAN] late hook: probe instance failed");
        dlclose(vulkan);
        return;
    }
    vk_instance = late_probe_instance;

    uint32_t device_count = 0;
    enumerate_devices(late_probe_instance, &device_count, nullptr);
    std::vector<VkPhysicalDevice> devices(device_count);
    if (device_count == 0 || enumerate_devices(late_probe_instance, &device_count, devices.data()) != VK_SUCCESS) {
        VulkanDebug("[VULKAN] late hook: no physical device");
        dlclose(vulkan);
        return;
    }

    vk_physical_device = devices[0];
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        get_properties(candidate, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            vk_physical_device = candidate;
            break;
        }
    }

    uint32_t family_count = 0;
    get_queue_properties(vk_physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    get_queue_properties(vk_physical_device, &family_count, families.data());
    vk_queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < family_count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vk_queue_family = i;
            break;
        }
    }
    if (vk_queue_family == UINT32_MAX) {
        VulkanDebug("[VULKAN] late hook: no graphics queue family");
        dlclose(vulkan);
        return;
    }

    const float priority = 1.f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = vk_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = &extension;
    if (create_device(vk_physical_device, &device_info, nullptr, &late_probe_device) != VK_SUCCESS) {
        VulkanDebug("[VULKAN] late hook: probe device failed");
        dlclose(vulkan);
        return;
    }

    orig_vkQueuePresentKHR = reinterpret_cast<decltype(orig_vkQueuePresentKHR)>(get_device_proc(late_probe_device, "vkQueuePresentKHR"));
    orig_vkCreateSwapchainKHR = reinterpret_cast<decltype(orig_vkCreateSwapchainKHR)>(get_device_proc(late_probe_device, "vkCreateSwapchainKHR"));
    orig_vkAcquireNextImageKHR = reinterpret_cast<decltype(orig_vkAcquireNextImageKHR)>(get_device_proc(late_probe_device, "vkAcquireNextImageKHR"));
    orig_vkAcquireNextImage2KHR = reinterpret_cast<decltype(orig_vkAcquireNextImage2KHR)>(get_device_proc(late_probe_device, "vkAcquireNextImage2KHR"));

    late_hooks = funchook_create();
    int result = late_hooks ? 0 : -1;
    if (result == 0) result = funchook_prepare(late_hooks, reinterpret_cast<void**>(&orig_vkQueuePresentKHR), reinterpret_cast<void*>(Hooked_QueuePresentKHR));
    if (result == 0) result = funchook_prepare(late_hooks, reinterpret_cast<void**>(&orig_vkAcquireNextImageKHR), reinterpret_cast<void*>(Late_AcquireNextImageKHR));
    if (result == 0 && orig_vkAcquireNextImage2KHR)
        result = funchook_prepare(late_hooks, reinterpret_cast<void**>(&orig_vkAcquireNextImage2KHR), reinterpret_cast<void*>(Late_AcquireNextImage2KHR));

    void* sdl = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD);
    if (result == 0 && sdl) {
        orig_SDL_PeepEvents = reinterpret_cast<decltype(orig_SDL_PeepEvents)>(dlsym(sdl, "SDL_PeepEvents"));
        if (orig_SDL_PeepEvents)
            result = funchook_prepare(late_hooks, reinterpret_cast<void**>(&orig_SDL_PeepEvents), reinterpret_cast<void*>(Late_SDL_PeepEvents));
    }
    if (sdl) dlclose(sdl);

    if (result == 0)
        result = funchook_install(late_hooks, 0);
    destroy_device(late_probe_device, nullptr);
    late_probe_device = VK_NULL_HANDLE;
    dlclose(vulkan);

    if (result != 0) {
        VulkanDebug("[VULKAN] late hook installation failed");
        funchook_destroy(late_hooks);
        late_hooks = nullptr;
        return;
    }
    VulkanDebug("[VULKAN] late hooks installed");
}

void EnableVulkanMenu()
{
    g_MenuReady.store(true);
    L_PRINT(LOG_INFO) << "[VULKAN] Menu rendering enabled";
}

#endif // __linux__
