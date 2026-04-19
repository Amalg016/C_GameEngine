#ifndef VULKAN_PIPELINE_H
#define VULKAN_PIPELINE_H

#include "vulkan_types.h"
#include <stdbool.h>

/// Create the render pass, pipeline layout, and graphics pipeline.
bool vulkan_pipeline_create(VulkanContext *ctx,
                            const char *vert_path,
                            const char *frag_path);

/// Destroy pipeline, layout, and render pass.
void vulkan_pipeline_destroy(VulkanContext *ctx);

/// Create one framebuffer per swapchain image (requires render pass).
bool vulkan_framebuffers_create(VulkanContext *ctx);

/// Create the command pool, allocate command buffers, and create sync
/// objects (semaphores + fences).
bool vulkan_commands_create(VulkanContext *ctx);

/// Destroy command pool and sync objects.
void vulkan_commands_destroy(VulkanContext *ctx);

#endif // VULKAN_PIPELINE_H
