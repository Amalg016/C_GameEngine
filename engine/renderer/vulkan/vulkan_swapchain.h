#ifndef VULKAN_SWAPCHAIN_H
#define VULKAN_SWAPCHAIN_H

#include "vulkan_types.h"
#include <stdbool.h>
#include <stdint.h>

/// Create (or recreate) the swapchain, image views and framebuffers.
bool vulkan_swapchain_create(VulkanContext *ctx,
                             uint32_t width, uint32_t height);

/// Destroy swapchain resources (image views, framebuffers, swapchain).
void vulkan_swapchain_destroy(VulkanContext *ctx);

#endif // VULKAN_SWAPCHAIN_H
