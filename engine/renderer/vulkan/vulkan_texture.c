#include "vulkan_texture.h"
#include "vulkan_buffer.h"

#include <stb_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Image layout transition helper
// ---------------------------------------------------------------------------

static void transition_image_layout(VulkanContext *ctx,
                                    VkImage        image,
                                    VkImageLayout  old_layout,
                                    VkImageLayout  new_layout) {
    VkCommandBuffer cmd = vulkan_begin_single_command(ctx);

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        fprintf(stderr, "[vulkan] unsupported layout transition\n");
        vulkan_end_single_command(ctx, cmd);
        return;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vulkan_end_single_command(ctx, cmd);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vulkan_texture_create_from_file(VulkanContext  *ctx,
                                     const char     *path,
                                     VulkanTexture  *out) {
    // Load image from disk.
    int width, height, channels;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        fprintf(stderr, "[vulkan] failed to load image: %s\n", path);
        return false;
    }

    VkDeviceSize image_size = (VkDeviceSize)width * (VkDeviceSize)height * 4;

    printf("[vulkan] loaded image: %s (%dx%d, %d channels)\n",
           path, width, height, channels);

    // Create staging buffer and upload pixel data.
    VkBuffer       staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    if (!vulkan_buffer_create(ctx, image_size,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &staging_buf, &staging_mem)) {
        stbi_image_free(pixels);
        return false;
    }

    void *mapped = nullptr;
    vkMapMemory(ctx->device, staging_mem, 0, image_size, 0, &mapped);
    memcpy(mapped, pixels, (size_t)image_size);
    vkUnmapMemory(ctx->device, staging_mem);

    stbi_image_free(pixels);

    // Create the VkImage.
    VkImageCreateInfo img_ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R8G8B8A8_SRGB,
        .extent        = { .width = (uint32_t)width,
                           .height = (uint32_t)height,
                           .depth = 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(ctx->device, &img_ci, nullptr, &out->image) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create texture image\n");
        vulkan_buffer_destroy(ctx, staging_buf, staging_mem);
        return false;
    }

    // Allocate and bind memory.
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(ctx->device, out->image, &reqs);

    uint32_t mem_type = vulkan_find_memory_type(ctx->physical_device,
                                                reqs.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyImage(ctx->device, out->image, nullptr);
        vulkan_buffer_destroy(ctx, staging_buf, staging_mem);
        return false;
    }

    VkMemoryAllocateInfo alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc, nullptr, &out->memory) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to allocate texture memory\n");
        vkDestroyImage(ctx->device, out->image, nullptr);
        vulkan_buffer_destroy(ctx, staging_buf, staging_mem);
        return false;
    }
    vkBindImageMemory(ctx->device, out->image, out->memory, 0);

    // Transition UNDEFINED → TRANSFER_DST, copy, then → SHADER_READ_ONLY.
    transition_image_layout(ctx, out->image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer → image.
    VkCommandBuffer cmd = vulkan_begin_single_command(ctx);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { (uint32_t)width, (uint32_t)height, 1 },
    };

    vkCmdCopyBufferToImage(cmd, staging_buf, out->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    vulkan_end_single_command(ctx, cmd);

    // Clean up staging.
    vulkan_buffer_destroy(ctx, staging_buf, staging_mem);

    // Transition to shader-readable.
    transition_image_layout(ctx, out->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create image view.
    VkImageViewCreateInfo iv_ci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &iv_ci, nullptr, &out->view) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create texture image view\n");
        vulkan_texture_destroy(ctx, out);
        return false;
    }

    // Create sampler.
    VkSamplerCreateInfo sampler_ci = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .maxLod       = 0.0f,
    };

    if (vkCreateSampler(ctx->device, &sampler_ci, nullptr, &out->sampler) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create texture sampler\n");
        vulkan_texture_destroy(ctx, out);
        return false;
    }

    out->width  = (uint32_t)width;
    out->height = (uint32_t)height;

    printf("[vulkan] texture created: %s (%ux%u)\n", path, out->width, out->height);
    return true;
}

void vulkan_texture_destroy(VulkanContext *ctx, VulkanTexture *tex) {
    if (ctx->device == VK_NULL_HANDLE) return;

    if (tex->sampler != VK_NULL_HANDLE)
        vkDestroySampler(ctx->device, tex->sampler, nullptr);
    if (tex->view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx->device, tex->view, nullptr);
    if (tex->image != VK_NULL_HANDLE)
        vkDestroyImage(ctx->device, tex->image, nullptr);
    if (tex->memory != VK_NULL_HANDLE)
        vkFreeMemory(ctx->device, tex->memory, nullptr);

    *tex = (VulkanTexture){};
}
