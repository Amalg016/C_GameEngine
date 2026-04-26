#include "vulkan_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Memory type selection
// ---------------------------------------------------------------------------

uint32_t vulkan_find_memory_type(VkPhysicalDevice      physical_device,
                                 uint32_t               type_filter,
                                 VkMemoryPropertyFlags  properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fprintf(stderr, "[vulkan] failed to find suitable memory type\n");
    return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Buffer create / destroy
// ---------------------------------------------------------------------------

bool vulkan_buffer_create(VulkanContext         *ctx,
                           VkDeviceSize           size,
                           VkBufferUsageFlags     usage,
                           VkMemoryPropertyFlags  mem_props,
                           VkBuffer              *out_buffer,
                           VkDeviceMemory        *out_memory) {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(ctx->device, &ci, nullptr, out_buffer) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create buffer\n");
        return false;
    }

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(ctx->device, *out_buffer, &reqs);

    uint32_t mem_type = vulkan_find_memory_type(ctx->physical_device,
                                                reqs.memoryTypeBits,
                                                mem_props);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(ctx->device, *out_buffer, nullptr);
        return false;
    }

    VkMemoryAllocateInfo alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc, nullptr, out_memory) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to allocate buffer memory\n");
        vkDestroyBuffer(ctx->device, *out_buffer, nullptr);
        return false;
    }

    vkBindBufferMemory(ctx->device, *out_buffer, *out_memory, 0);
    return true;
}

void vulkan_buffer_destroy(VulkanContext  *ctx,
                            VkBuffer       buffer,
                            VkDeviceMemory memory) {
    if (ctx->device == VK_NULL_HANDLE) return;
    if (buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(ctx->device, buffer, nullptr);
    if (memory != VK_NULL_HANDLE)
        vkFreeMemory(ctx->device, memory, nullptr);
}

// ---------------------------------------------------------------------------
// Single-use command buffer helpers
// ---------------------------------------------------------------------------

VkCommandBuffer vulkan_begin_single_command(VulkanContext *ctx) {
    VkCommandBufferAllocateInfo alloc = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &alloc, &cmd);

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    return cmd;
}

void vulkan_end_single_command(VulkanContext   *ctx,
                                VkCommandBuffer  cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };

    vkQueueSubmit(ctx->graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->graphics_queue);

    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
}

// ---------------------------------------------------------------------------
// Staging upload — CPU → GPU via temporary staging buffer
// ---------------------------------------------------------------------------

bool vulkan_staging_upload(VulkanContext *ctx,
                            VkBuffer       dst_buffer,
                            const void    *data,
                            VkDeviceSize   size) {
    // Create a host-visible staging buffer.
    VkBuffer       staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    if (!vulkan_buffer_create(ctx, size,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &staging_buf, &staging_mem)) {
        return false;
    }

    // Map and copy data.
    void *mapped = nullptr;
    vkMapMemory(ctx->device, staging_mem, 0, size, 0, &mapped);
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(ctx->device, staging_mem);

    // Record + submit a copy command.
    VkCommandBuffer cmd = vulkan_begin_single_command(ctx);

    VkBufferCopy region = { .size = size };
    vkCmdCopyBuffer(cmd, staging_buf, dst_buffer, 1, &region);

    vulkan_end_single_command(ctx, cmd);

    // Clean up staging resources.
    vulkan_buffer_destroy(ctx, staging_buf, staging_mem);
    return true;
}
