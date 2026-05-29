#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#include "../renderer.h"
#include "../../platform/platform.h"

/// Create a Renderer backed by Vulkan.
/// The returned Renderer owns its backend_data and must be cleaned up via
/// renderer_shutdown() + vulkan_renderer_destroy().
Renderer vulkan_renderer_create(Platform *platform);

/// Free the backend allocation.  Call AFTER renderer_shutdown().
void vulkan_renderer_destroy(Renderer *r);

// ---------------------------------------------------------------------------
// Editor-only accessors — expose Vulkan internals for ImGui integration.
// These are compiled ONLY when EDITOR_BUILD is defined.
// ---------------------------------------------------------------------------

#ifdef EDITOR_BUILD

#include <vulkan/vulkan.h>

/// Get the VkInstance from the renderer's Vulkan backend.
VkInstance vulkan_renderer_get_instance(Renderer *r);

/// Get the VkPhysicalDevice from the renderer's Vulkan backend.
VkPhysicalDevice vulkan_renderer_get_physical_device(Renderer *r);

/// Get the VkDevice from the renderer's Vulkan backend.
VkDevice vulkan_renderer_get_device(Renderer *r);

/// Get the graphics queue family index.
uint32_t vulkan_renderer_get_graphics_family(Renderer *r);

/// Get the graphics VkQueue.
VkQueue vulkan_renderer_get_graphics_queue(Renderer *r);

/// Get the main VkRenderPass.
VkRenderPass vulkan_renderer_get_render_pass(Renderer *r);

/// Get the currently recording VkCommandBuffer for the current frame.
VkCommandBuffer vulkan_renderer_get_current_command_buffer(Renderer *r);

/// Get the swapchain image count.
uint32_t vulkan_renderer_get_image_count(Renderer *r);

/// Transition rendering from offscreen game viewport to editor main pass.
void vulkan_renderer_transition_to_editor(Renderer *r);

/// Get the descriptor set representing the offscreen game viewport texture.
void *vulkan_renderer_get_editor_viewport_texture(Renderer *r);

/// Get the offscreen game viewport width and height.
void vulkan_renderer_get_offscreen_size(Renderer *r, uint32_t *w, uint32_t *h);

/// Perform GPU-based entity picking at offscreen coordinates (x, y).
uint32_t vulkan_renderer_pick_entity(Renderer *r, uint32_t x, uint32_t y);

#endif // EDITOR_BUILD

#endif // VULKAN_RENDERER_H
