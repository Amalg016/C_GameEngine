#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include "vulkan_types.h"
#include <stdbool.h>
#include <stdint.h>

/// Create VkInstance (+ debug messenger in debug builds) and pick a suitable
/// physical device.  Surface is created from the GLFWwindow stored in ctx.
bool vulkan_device_create(VulkanContext *ctx,
                          const char **instance_extensions,
                          uint32_t      extension_count);

/// Destroy instance, debug messenger and surface.
void vulkan_device_destroy(VulkanContext *ctx);

#endif // VULKAN_DEVICE_H
