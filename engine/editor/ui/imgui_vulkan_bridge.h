#ifndef ENGINE_EDITOR_IMGUI_VULKAN_BRIDGE_H
#define ENGINE_EDITOR_IMGUI_VULKAN_BRIDGE_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// ImGui Vulkan/GLFW Backend Bridge — C-callable wrapper
//
// The Dear ImGui backend implementations (imgui_impl_glfw, imgui_impl_vulkan)
// are C++ source files.  This thin bridge exposes their functionality through
// a pure C interface so that the rest of the editor module (written in C23)
// can initialise and drive ImGui without touching any C++ headers.
//
// Compiled as C++ (imgui_vulkan_bridge.cpp), exposed as extern "C".
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

/// Vulkan handles required to initialise the ImGui Vulkan backend.
/// Assembled by the caller (editor.c) from renderer accessor functions.
typedef struct ImGuiBridgeInitInfo {
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    uint32_t         queue_family;
    VkQueue          queue;
    VkRenderPass     render_pass;
    uint32_t         image_count;
} ImGuiBridgeInitInfo;

#ifdef __cplusplus
extern "C" {
#endif

/// Initialise the ImGui GLFW and Vulkan backends.
/// Creates a dedicated VkDescriptorPool for ImGui internally.
/// Must be called AFTER igCreateContext().
/// `glfw_window` is a raw GLFWwindow* passed as void* to avoid GLFW
/// header inclusion in C code.
/// Returns true on success.
bool imgui_bridge_init(void *glfw_window, const ImGuiBridgeInitInfo *info);

/// Shut down both backends and destroy the internal descriptor pool.
/// Must be called BEFORE igDestroyContext().
void imgui_bridge_shutdown(void);

/// Begin a new backend frame (calls ImGui_ImplVulkan_NewFrame,
/// ImGui_ImplGlfw_NewFrame).  Call BEFORE igNewFrame().
void imgui_bridge_new_frame(void);

/// Record ImGui draw commands into the given Vulkan command buffer.
/// `draw_data` is an ImDrawData* obtained from igGetDrawData() after
/// igRender().  Passed as void* to avoid cimgui header dependency here.
/// Must be called while a render pass is active on `cmd`.
void imgui_bridge_render(void *draw_data, VkCommandBuffer cmd);

/// Register a Vulkan texture view with ImGui. Returns a VkDescriptorSet cast to void*.
void *imgui_bridge_add_texture(VkSampler sampler, VkImageView image_view, VkImageLayout image_layout);

/// Unregister a Vulkan texture view descriptor set from ImGui.
void imgui_bridge_remove_texture(void *descriptor_set);

#ifdef __cplusplus
}
#endif

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_IMGUI_VULKAN_BRIDGE_H
