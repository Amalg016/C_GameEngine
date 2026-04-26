#ifndef VULKAN_BUFFER_H
#define VULKAN_BUFFER_H

#include "vulkan_types.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Vulkan buffer helpers — create, destroy, and staging-upload utilities.
// ---------------------------------------------------------------------------

/// Find a memory type index that satisfies both the type filter and the
/// required property flags.  Returns UINT32_MAX on failure.
uint32_t vulkan_find_memory_type(VkPhysicalDevice      physical_device,
                                 uint32_t               type_filter,
                                 VkMemoryPropertyFlags  properties);

/// Create a VkBuffer + VkDeviceMemory pair.
bool vulkan_buffer_create(VulkanContext         *ctx,
                           VkDeviceSize           size,
                           VkBufferUsageFlags     usage,
                           VkMemoryPropertyFlags  mem_props,
                           VkBuffer              *out_buffer,
                           VkDeviceMemory        *out_memory);

/// Destroy a buffer + memory pair.
void vulkan_buffer_destroy(VulkanContext  *ctx,
                            VkBuffer       buffer,
                            VkDeviceMemory memory);

/// Upload `size` bytes from `data` into `dst_buffer` via a temporary staging
/// buffer.  Blocks until the copy is complete.
bool vulkan_staging_upload(VulkanContext *ctx,
                            VkBuffer       dst_buffer,
                            const void    *data,
                            VkDeviceSize   size);

/// Begin a single-use command buffer for transfer operations.
VkCommandBuffer vulkan_begin_single_command(VulkanContext *ctx);

/// End + submit + wait + free a single-use command buffer.
void vulkan_end_single_command(VulkanContext   *ctx,
                                VkCommandBuffer  cmd);

#endif // VULKAN_BUFFER_H
