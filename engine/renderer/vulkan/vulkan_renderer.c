#include "vulkan_renderer.h"
#include "vulkan_buffer.h"
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "vulkan_swapchain.h"
#include "vulkan_texture.h"
#include "vulkan_types.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void vulkan_update_descriptor_sets(VulkanContext *ctx,
                                           VkImageView    view,
                                           VkSampler      sampler);

#ifdef EDITOR_BUILD
static void vulkan_offscreen_destroy(VulkanContext *ctx);
static bool vulkan_offscreen_create(VulkanContext *ctx, uint32_t width, uint32_t height);
#endif

// ---------------------------------------------------------------------------
// Quad geometry — a full-screen quad with UVs
// ---------------------------------------------------------------------------

static const Vertex QUAD_VERTICES[] = {
    // pos            uv           color
    {{ -0.5f, -0.5f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }},  // top-left
    {{  0.5f, -0.5f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }},  // top-right
    {{  0.5f,  0.5f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }},  // bottom-right
    {{ -0.5f,  0.5f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }},  // bottom-left
};

static const uint16_t QUAD_INDICES[] = {
    0, 1, 2,   // first triangle
    2, 3, 0,   // second triangle
};

// ---------------------------------------------------------------------------
// Swapchain recreation (resize handling)
// ---------------------------------------------------------------------------

static void framebuffer_resize_callback(GLFWwindow *win, int w, int h) {
    (void)w;
    (void)h;
    VulkanContext *ctx = (VulkanContext *)platform_get_backend_data_from_window(win);
    if (ctx != nullptr)
        ctx->framebuffer_resized = true;
}

static bool recreate_swapchain(VulkanContext *ctx) {
    int w = 0, h = 0;
    GLFWwindow *win = (GLFWwindow *)ctx->window_handle;
    glfwGetFramebufferSize(win, &w, &h);

    // Minimised — wait until window is visible again.
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(win, &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(ctx->device);

    vulkan_swapchain_destroy(ctx);

    if (!vulkan_swapchain_create(ctx, (uint32_t)w, (uint32_t)h))
        return false;
    if (!vulkan_framebuffers_create(ctx))
        return false;

#ifdef EDITOR_BUILD
    if (!vulkan_offscreen_create(ctx, (uint32_t)w, (uint32_t)h))
        return false;
#endif

    return true;
}

// ---------------------------------------------------------------------------
// Vertex / index buffer creation
// ---------------------------------------------------------------------------

static bool create_geometry_buffers(VulkanContext *ctx) {
    // --- Vertex buffer ---
    VkDeviceSize vb_size = sizeof(QUAD_VERTICES);

    if (!vulkan_buffer_create(ctx, vb_size,
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               &ctx->vertex_buffer,
                               &ctx->vertex_buffer_memory)) {
        return false;
    }

    if (!vulkan_staging_upload(ctx, ctx->vertex_buffer,
                                QUAD_VERTICES, vb_size)) {
        return false;
    }

    // --- Index buffer ---
    VkDeviceSize ib_size = sizeof(QUAD_INDICES);
    ctx->index_count = sizeof(QUAD_INDICES) / sizeof(QUAD_INDICES[0]);

    if (!vulkan_buffer_create(ctx, ib_size,
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               &ctx->index_buffer,
                               &ctx->index_buffer_memory)) {
        return false;
    }

    if (!vulkan_staging_upload(ctx, ctx->index_buffer,
                                QUAD_INDICES, ib_size)) {
        return false;
    }

    printf("[vulkan] geometry buffers created (4 vertices, %u indices)\n",
           ctx->index_count);
    return true;
}

static void destroy_geometry_buffers(VulkanContext *ctx) {
    vulkan_buffer_destroy(ctx, ctx->index_buffer, ctx->index_buffer_memory);
    ctx->index_buffer        = VK_NULL_HANDLE;
    ctx->index_buffer_memory = VK_NULL_HANDLE;

    vulkan_buffer_destroy(ctx, ctx->vertex_buffer, ctx->vertex_buffer_memory);
    ctx->vertex_buffer        = VK_NULL_HANDLE;
    ctx->vertex_buffer_memory = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// 1×1 white fallback texture (used until a real texture is bound)
// ---------------------------------------------------------------------------

static bool create_fallback_texture(VulkanContext *ctx) {
    // Create a 1×1 RGBA white image.
    uint8_t white_pixel[4] = { 255, 255, 255, 255 };

    VkImageCreateInfo img_ci = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = VK_FORMAT_R8G8B8A8_SRGB,
        .extent      = { .width = 1, .height = 1, .depth = 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage       fallback_image  = VK_NULL_HANDLE;
    VkDeviceMemory fallback_memory = VK_NULL_HANDLE;

    if (vkCreateImage(ctx->device, &img_ci, nullptr, &fallback_image) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create fallback image\n");
        return false;
    }

    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(ctx->device, fallback_image, &reqs);

    uint32_t mem_type = vulkan_find_memory_type(ctx->physical_device,
                                                reqs.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyImage(ctx->device, fallback_image, nullptr);
        return false;
    }

    VkMemoryAllocateInfo alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc, nullptr, &fallback_memory) != VK_SUCCESS) {
        vkDestroyImage(ctx->device, fallback_image, nullptr);
        return false;
    }
    vkBindImageMemory(ctx->device, fallback_image, fallback_memory, 0);

    // Upload the pixel via staging buffer.
    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkDeviceSize pixel_size = 4;

    vulkan_buffer_create(ctx, pixel_size,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &staging_buf, &staging_mem);

    void *mapped = nullptr;
    vkMapMemory(ctx->device, staging_mem, 0, pixel_size, 0, &mapped);
    __builtin_memcpy(mapped, white_pixel, 4);
    vkUnmapMemory(ctx->device, staging_mem);

    // Transition UNDEFINED → TRANSFER_DST
    VkCommandBuffer cmd = vulkan_begin_single_command(ctx);

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = fallback_image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {
        .imageSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount     = 1,
        },
        .imageExtent = { 1, 1, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging_buf, fallback_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition TRANSFER_DST → SHADER_READ_ONLY
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vulkan_end_single_command(ctx, cmd);
    vulkan_buffer_destroy(ctx, staging_buf, staging_mem);

    // Image view.
    VkImageViewCreateInfo iv_ci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = fallback_image,
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

    VkImageView fallback_view = VK_NULL_HANDLE;
    if (vkCreateImageView(ctx->device, &iv_ci, nullptr, &fallback_view) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create fallback image view\n");
        return false;
    }

    // Sampler.
    VkSamplerCreateInfo sampler_ci = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_NEAREST,
        .minFilter    = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };

    VkSampler fallback_sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(ctx->device, &sampler_ci, nullptr, &fallback_sampler) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create fallback sampler\n");
        return false;
    }

    // Store in context for later cleanup.
    ctx->fallback_image   = fallback_image;
    ctx->fallback_memory  = fallback_memory;
    ctx->fallback_view    = fallback_view;
    ctx->fallback_sampler = fallback_sampler;

    printf("[vulkan] 1×1 fallback texture created\n");
    return true;
}

static void destroy_fallback_texture(VulkanContext *ctx) {
    if (ctx->device == VK_NULL_HANDLE) return;
    if (ctx->fallback_sampler != VK_NULL_HANDLE)
        vkDestroySampler(ctx->device, ctx->fallback_sampler, nullptr);
    if (ctx->fallback_view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx->device, ctx->fallback_view, nullptr);
    if (ctx->fallback_image != VK_NULL_HANDLE)
        vkDestroyImage(ctx->device, ctx->fallback_image, nullptr);
    if (ctx->fallback_memory != VK_NULL_HANDLE)
        vkFreeMemory(ctx->device, ctx->fallback_memory, nullptr);
}

// ---------------------------------------------------------------------------
// Descriptor pool + sets
// ---------------------------------------------------------------------------

static bool create_descriptor_resources(VulkanContext *ctx) {
    // Pool.
    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_FRAMES_IN_FLIGHT,
    };

    VkDescriptorPoolCreateInfo pool_ci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };

    if (vkCreateDescriptorPool(ctx->device, &pool_ci, nullptr,
                               &ctx->descriptor_pool) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create descriptor pool\n");
        return false;
    }

    // Allocate one set per frame-in-flight.
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        layouts[i] = ctx->descriptor_set_layout;

    VkDescriptorSetAllocateInfo alloc = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = ctx->descriptor_pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts        = layouts,
    };

    if (vkAllocateDescriptorSets(ctx->device, &alloc,
                                 ctx->descriptor_sets) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to allocate descriptor sets\n");
        return false;
    }

    // Write the fallback texture into each descriptor set initially.
    vulkan_update_descriptor_sets(ctx,
                                  ctx->fallback_view,
                                  ctx->fallback_sampler);

    printf("[vulkan] descriptor pool + sets ready\n");
    return true;
}

/// Update all per-frame descriptor sets to point to a new image view/sampler.
static void vulkan_update_descriptor_sets(VulkanContext *ctx,
                                           VkImageView    view,
                                           VkSampler      sampler) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorImageInfo img_info = {
            .sampler     = sampler,
            .imageView   = view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = ctx->descriptor_sets[i],
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &img_info,
        };

        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, nullptr);
    }
}

static void destroy_descriptor_resources(VulkanContext *ctx) {
    if (ctx->device == VK_NULL_HANDLE) return;
    if (ctx->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, nullptr);
        ctx->descriptor_pool = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// RendererAPI callbacks
// ---------------------------------------------------------------------------

static bool vulkan_init(Renderer *self) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;

    // Register resize callback via the platform's shared user pointer.
    GLFWwindow *win = (GLFWwindow *)ctx->window_handle;
    platform_set_backend_data_from_window(win, ctx);
    glfwSetFramebufferSizeCallback(win, framebuffer_resize_callback);

    // Instance + device.
    uint32_t ext_count = 0;
    const char **exts = platform_get_vulkan_extensions(&ext_count);
    if (!vulkan_device_create(ctx, exts, ext_count))
        return false;

    // Swapchain.
    int fb_w, fb_h;
    glfwGetFramebufferSize(win, &fb_w, &fb_h);
    if (!vulkan_swapchain_create(ctx, (uint32_t)fb_w, (uint32_t)fb_h))
        return false;

    // Pipeline (render pass + descriptor layout + shaders).
    if (!vulkan_pipeline_create(ctx, "shaders/quad.vert.spv",
                                "shaders/quad.frag.spv"))
        return false;

    // Framebuffers.
    if (!vulkan_framebuffers_create(ctx))
        return false;

    // Command pool, buffers, sync objects.
    if (!vulkan_commands_create(ctx))
        return false;

    // Geometry buffers (quad).
    if (!create_geometry_buffers(ctx))
        return false;

    // Fallback 1×1 white texture.
    if (!create_fallback_texture(ctx))
        return false;

    // Descriptor pool + sets (initially bound to fallback).
    if (!create_descriptor_resources(ctx))
        return false;

#ifdef EDITOR_BUILD
    if (!vulkan_offscreen_create(ctx, (uint32_t)fb_w, (uint32_t)fb_h))
        return false;
#endif

    printf("[vulkan] renderer fully initialised\n");
    return true;
}

static void vulkan_shutdown(Renderer *self) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    if (ctx->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(ctx->device);

#ifdef EDITOR_BUILD
    vulkan_offscreen_destroy(ctx);
#endif

    destroy_descriptor_resources(ctx);
    destroy_fallback_texture(ctx);
    destroy_geometry_buffers(ctx);
    vulkan_commands_destroy(ctx);
    vulkan_pipeline_destroy(ctx);
    vulkan_swapchain_destroy(ctx);
    vulkan_device_destroy(ctx);

    printf("[vulkan] renderer shut down\n");
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

static bool vulkan_begin_frame(Renderer *self) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    uint32_t frame = ctx->current_frame;

    // Wait for the previous frame using this slot to finish.
    vkWaitForFences(ctx->device, 1, &ctx->in_flight[frame], VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image.
    VkResult res = vkAcquireNextImageKHR(ctx->device, ctx->swapchain.handle,
                                         UINT64_MAX, ctx->image_available[frame],
                                         VK_NULL_HANDLE, &ctx->image_index);

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain(ctx);
        return false; // skip this frame
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "[vulkan] failed to acquire swapchain image\n");
        return false;
    }

    vkResetFences(ctx->device, 1, &ctx->in_flight[frame]);

    // Begin recording.
    VkCommandBuffer cmd = ctx->command_buffers[frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    // Begin render pass.
#ifdef EDITOR_BUILD
    VkClearValue offscreen_clears[2];
    offscreen_clears[0].color.float32[0] = 0.0f;
    offscreen_clears[0].color.float32[1] = 0.0f;
    offscreen_clears[0].color.float32[2] = 0.0f;
    offscreen_clears[0].color.float32[3] = 0.0f;
    offscreen_clears[1].color.uint32[0] = 0;
    offscreen_clears[1].color.uint32[1] = 0;
    offscreen_clears[1].color.uint32[2] = 0;
    offscreen_clears[1].color.uint32[3] = 0;

    VkRenderPassBeginInfo rp_info = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = ctx->offscreen_render_pass,
        .framebuffer = ctx->offscreen_framebuffer,
        .renderArea  = {.offset = {0, 0}, .extent = {ctx->offscreen_w, ctx->offscreen_h}},
        .clearValueCount = 2,
        .pClearValues    = offscreen_clears,
    };

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)ctx->offscreen_w,
        .height   = (float)ctx->offscreen_h,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {ctx->offscreen_w, ctx->offscreen_h},
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
#else
    VkClearValue clear = {.color = {{0.01f, 0.01f, 0.02f, 1.0f}}};
    VkRenderPassBeginInfo rp_info = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = ctx->render_pass,
        .framebuffer = ctx->swapchain.framebuffers[ctx->image_index],
        .renderArea  = {.offset = {0, 0}, .extent = ctx->swapchain.extent},
        .clearValueCount = 1,
        .pClearValues    = &clear,
    };

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)ctx->swapchain.extent.width,
        .height   = (float)ctx->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = ctx->swapchain.extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
#endif

    // Default VP to identity — overwritten if camera_update + set_view_projection is called.
    float identity[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1,
    };
    memcpy(ctx->view_proj, identity, sizeof(identity));

    return true;
}

static void vulkan_draw_quad(Renderer *self) {
    // Draw the quad at identity transform (scale=1, translate=0) for
    // backward compatibility.
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    uint32_t frame = ctx->current_frame;
    VkCommandBuffer cmd = ctx->command_buffers[frame];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->graphics_pipeline);

    // Push constants: mat4 view_proj + vec2 scale + vec2 translate + uint32_t entity_id.
    struct { float vp[16]; float scale[2]; float translate[2]; uint32_t entity_id; } pc;
    memcpy(pc.vp, ctx->view_proj, sizeof(pc.vp));
    pc.scale[0] = 1.0f;  pc.scale[1] = 1.0f;
    pc.translate[0] = 0.0f;  pc.translate[1] = 0.0f;
    pc.entity_id = 0;
    vkCmdPushConstants(cmd, ctx->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    // Bind vertex buffer.
    VkBuffer     vbufs[]  = { ctx->vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);

    // Bind index buffer.
    vkCmdBindIndexBuffer(cmd, ctx->index_buffer, 0, VK_INDEX_TYPE_UINT16);

    // Bind descriptor set (texture).
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->pipeline_layout, 0, 1,
                            &ctx->descriptor_sets[frame], 0, nullptr);

    // Draw the indexed quad.
    vkCmdDrawIndexed(cmd, ctx->index_count, 1, 0, 0, 0);
}

/// Draw a textured sprite at (x, y) with size (w, h) in world space.
static void vulkan_draw_sprite(Renderer *self,
                                float x, float y, float w, float h,
                                uint32_t entity_index) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    uint32_t frame = ctx->current_frame;
    VkCommandBuffer cmd = ctx->command_buffers[frame];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->graphics_pipeline);

    // Push constants: mat4 view_proj + vec2 scale + vec2 translate + uint32_t entity_id.
    struct { float vp[16]; float scale[2]; float translate[2]; uint32_t entity_id; } pc;
    memcpy(pc.vp, ctx->view_proj, sizeof(pc.vp));
    pc.scale[0] = w;  pc.scale[1] = h;
    pc.translate[0] = x;  pc.translate[1] = y;
    pc.entity_id = entity_index;
    vkCmdPushConstants(cmd, ctx->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    // Bind vertex buffer.
    VkBuffer     vbufs[]  = { ctx->vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);

    // Bind index buffer.
    vkCmdBindIndexBuffer(cmd, ctx->index_buffer, 0, VK_INDEX_TYPE_UINT16);

    // Bind descriptor set (texture).
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->pipeline_layout, 0, 1,
                            &ctx->descriptor_sets[frame], 0, nullptr);

    // Draw the indexed quad.
    vkCmdDrawIndexed(cmd, ctx->index_count, 1, 0, 0, 0);
}

/// Store the view-projection matrix for the current frame.
static void vulkan_set_view_projection(Renderer *self, const float *mat4x4) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    if (mat4x4 != nullptr) {
        memcpy(ctx->view_proj, mat4x4, 16 * sizeof(float));
    }
}

static void vulkan_end_frame(Renderer *self) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    uint32_t frame = ctx->current_frame;
    VkCommandBuffer cmd = ctx->command_buffers[frame];

    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to record command buffer\n");
        return;
    }

    // Submit.
    VkSemaphore wait_sems[]   = { ctx->image_available[frame] };
    VkSemaphore signal_sems[] = { ctx->render_finished[frame] };
    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = wait_sems,
        .pWaitDstStageMask    = wait_stages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = signal_sems,
    };

    if (vkQueueSubmit(ctx->graphics_queue, 1, &submit, ctx->in_flight[frame]) !=
        VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to submit draw command\n");
        return;
    }

    // Present.
    VkSwapchainKHR swapchains[] = { ctx->swapchain.handle };

    VkPresentInfoKHR present = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = signal_sems,
        .swapchainCount     = 1,
        .pSwapchains        = swapchains,
        .pImageIndices      = &ctx->image_index,
    };

    VkResult res = vkQueuePresentKHR(ctx->present_queue, &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ||
        ctx->framebuffer_resized) {
        ctx->framebuffer_resized = false;
        recreate_swapchain(ctx);
    }

    ctx->current_frame = (frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ---------------------------------------------------------------------------
// Asset-manager texture callbacks
// ---------------------------------------------------------------------------

/// Load a texture from disk — returns a heap-allocated VulkanTexture*.
static void *vulkan_load_texture(Renderer *self, const char *path) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;

    VulkanTexture *tex = calloc(1, sizeof(VulkanTexture));
    if (tex == nullptr) return nullptr;

    if (!vulkan_texture_create_from_file(ctx, path, tex)) {
        free(tex);
        return nullptr;
    }
    return tex;
}

/// Destroy a texture loaded by vulkan_load_texture.
static void vulkan_destroy_texture_cb(Renderer *self, void *gpu_data) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    VulkanTexture *tex = (VulkanTexture *)gpu_data;
    if (tex == nullptr) return;

    vkDeviceWaitIdle(ctx->device);
    vulkan_texture_destroy(ctx, tex);
    free(tex);
}

/// Bind a texture for subsequent draw calls — updates descriptor sets.
static void vulkan_bind_texture(Renderer *self, void *gpu_data) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    VulkanTexture *tex = (VulkanTexture *)gpu_data;

    if (tex != nullptr) {
        vulkan_update_descriptor_sets(ctx, tex->view, tex->sampler);
    } else {
        // Rebind fallback.
        vulkan_update_descriptor_sets(ctx, ctx->fallback_view,
                                      ctx->fallback_sampler);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Renderer vulkan_renderer_create(Platform *platform) {
    VulkanContext *ctx = calloc(1, sizeof(VulkanContext));
    if (ctx == nullptr) {
        fprintf(stderr, "[vulkan] failed to allocate VulkanContext\n");
        return (Renderer){};
    }

    ctx->window_handle = platform_get_window_handle(platform);

    Renderer r = {
        .api = {
            .init                = vulkan_init,
            .shutdown            = vulkan_shutdown,
            .begin_frame         = vulkan_begin_frame,
            .end_frame           = vulkan_end_frame,
            .draw_quad           = vulkan_draw_quad,
            .set_view_projection = vulkan_set_view_projection,
            .draw_sprite         = vulkan_draw_sprite,
            .load_texture        = vulkan_load_texture,
            .destroy_texture     = vulkan_destroy_texture_cb,
            .bind_texture        = vulkan_bind_texture,
        },
        .backend_data = ctx,
    };
    return r;
}

void vulkan_renderer_destroy(Renderer *r) {
    free(r->backend_data);
    r->backend_data = nullptr;
}

// ---------------------------------------------------------------------------
// Editor-only accessors (compiled only when EDITOR_BUILD is defined)
// ---------------------------------------------------------------------------

#ifdef EDITOR_BUILD

VkInstance vulkan_renderer_get_instance(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->instance;
}

VkPhysicalDevice vulkan_renderer_get_physical_device(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->physical_device;
}

VkDevice vulkan_renderer_get_device(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->device;
}

uint32_t vulkan_renderer_get_graphics_family(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->graphics_family;
}

VkQueue vulkan_renderer_get_graphics_queue(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->graphics_queue;
}

VkRenderPass vulkan_renderer_get_render_pass(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->render_pass;
}

VkCommandBuffer vulkan_renderer_get_current_command_buffer(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->command_buffers[ctx->current_frame];
}

uint32_t vulkan_renderer_get_image_count(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    return ctx->swapchain.image_count;
}

#include "../../editor/ui/imgui_vulkan_bridge.h"

static void vulkan_offscreen_destroy(VulkanContext *ctx) {
    if (ctx->device == VK_NULL_HANDLE) return;

    if (ctx->offscreen_descriptor_set != nullptr) {
        imgui_bridge_remove_texture(ctx->offscreen_descriptor_set);
        ctx->offscreen_descriptor_set = nullptr;
    }
    if (ctx->offscreen_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(ctx->device, ctx->offscreen_framebuffer, nullptr);
        ctx->offscreen_framebuffer = VK_NULL_HANDLE;
    }
    if (ctx->offscreen_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->device, ctx->offscreen_sampler, nullptr);
        ctx->offscreen_sampler = VK_NULL_HANDLE;
    }
    if (ctx->offscreen_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, ctx->offscreen_image_view, nullptr);
        ctx->offscreen_image_view = VK_NULL_HANDLE;
    }
    if (ctx->offscreen_image != VK_NULL_HANDLE) {
        vkDestroyImage(ctx->device, ctx->offscreen_image, nullptr);
        ctx->offscreen_image = VK_NULL_HANDLE;
    }
    if (ctx->offscreen_image_memory != VK_NULL_HANDLE) {
        vkFreeMemory(ctx->device, ctx->offscreen_image_memory, nullptr);
        ctx->offscreen_image_memory = VK_NULL_HANDLE;
    }
    if (ctx->picking_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, ctx->picking_image_view, nullptr);
        ctx->picking_image_view = VK_NULL_HANDLE;
    }
    if (ctx->picking_image != VK_NULL_HANDLE) {
        vkDestroyImage(ctx->device, ctx->picking_image, nullptr);
        ctx->picking_image = VK_NULL_HANDLE;
    }
    if (ctx->picking_image_memory != VK_NULL_HANDLE) {
        vkFreeMemory(ctx->device, ctx->picking_image_memory, nullptr);
        ctx->picking_image_memory = VK_NULL_HANDLE;
    }
    if (ctx->picking_staging_buffer != VK_NULL_HANDLE) {
        vulkan_buffer_destroy(ctx, ctx->picking_staging_buffer, ctx->picking_staging_buffer_memory);
        ctx->picking_staging_buffer = VK_NULL_HANDLE;
        ctx->picking_staging_buffer_memory = VK_NULL_HANDLE;
    }
}

static bool vulkan_offscreen_create(VulkanContext *ctx, uint32_t width, uint32_t height) {
    vulkan_offscreen_destroy(ctx);

    ctx->offscreen_w = width;
    ctx->offscreen_h = height;

    // --- Color image ---
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = ctx->swapchain.image_format,
        .extent        = { .width = width, .height = height, .depth = 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(ctx->device, &image_info, nullptr, &ctx->offscreen_image) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create offscreen image\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, ctx->offscreen_image, &mem_reqs);

    uint32_t mem_type = vulkan_find_memory_type(ctx->physical_device,
                                                mem_reqs.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc_info, nullptr, &ctx->offscreen_image_memory) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to allocate offscreen image memory\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    vkBindImageMemory(ctx->device, ctx->offscreen_image, ctx->offscreen_image_memory, 0);

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = ctx->offscreen_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = ctx->swapchain.image_format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &view_info, nullptr, &ctx->offscreen_image_view) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create offscreen image view\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    VkSamplerCreateInfo sampler_info = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .maxLod       = 0.0f,
    };

    if (vkCreateSampler(ctx->device, &sampler_info, nullptr, &ctx->offscreen_sampler) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create offscreen sampler\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    // --- Picking image ---
    VkImageCreateInfo picking_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R32_UINT,
        .extent        = { .width = width, .height = height, .depth = 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(ctx->device, &picking_info, nullptr, &ctx->picking_image) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create picking image\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    VkMemoryRequirements pick_reqs;
    vkGetImageMemoryRequirements(ctx->device, ctx->picking_image, &pick_reqs);

    uint32_t pick_mem_type = vulkan_find_memory_type(ctx->physical_device,
                                                    pick_reqs.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (pick_mem_type == UINT32_MAX) {
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    VkMemoryAllocateInfo pick_alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = pick_reqs.size,
        .memoryTypeIndex = pick_mem_type,
    };

    if (vkAllocateMemory(ctx->device, &pick_alloc, nullptr, &ctx->picking_image_memory) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to allocate picking image memory\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    vkBindImageMemory(ctx->device, ctx->picking_image, ctx->picking_image_memory, 0);

    VkImageViewCreateInfo pick_view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = ctx->picking_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R32_UINT,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &pick_view_info, nullptr, &ctx->picking_image_view) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create picking image view\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    // --- Picking Staging Buffer ---
    if (!vulkan_buffer_create(ctx, sizeof(uint32_t),
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &ctx->picking_staging_buffer,
                               &ctx->picking_staging_buffer_memory)) {
        fprintf(stderr, "[vulkan] failed to create picking staging buffer\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    VkFramebufferCreateInfo framebuffer_info = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = ctx->offscreen_render_pass,
        .attachmentCount = 2,
        .pAttachments    = (VkImageView[]){ ctx->offscreen_image_view, ctx->picking_image_view },
        .width           = width,
        .height          = height,
        .layers          = 1,
    };

    if (vkCreateFramebuffer(ctx->device, &framebuffer_info, nullptr, &ctx->offscreen_framebuffer) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create offscreen framebuffer\n");
        vulkan_offscreen_destroy(ctx);
        return false;
    }

    ctx->offscreen_descriptor_set = nullptr;
    return true;
}

void vulkan_renderer_transition_to_editor(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    uint32_t frame = ctx->current_frame;
    VkCommandBuffer cmd = ctx->command_buffers[frame];

    vkCmdEndRenderPass(cmd);

    VkClearValue clear = {.color = {{0.05f, 0.05f, 0.07f, 1.0f}}};

    VkRenderPassBeginInfo rp_info = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = ctx->render_pass,
        .framebuffer = ctx->swapchain.framebuffers[ctx->image_index],
        .renderArea  = {.offset = {0, 0}, .extent = ctx->swapchain.extent},
        .clearValueCount = 1,
        .pClearValues    = &clear,
    };

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)ctx->swapchain.extent.width,
        .height   = (float)ctx->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = ctx->swapchain.extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void *vulkan_renderer_get_editor_viewport_texture(Renderer *r) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    if (ctx->offscreen_descriptor_set == nullptr && ctx->offscreen_image_view != VK_NULL_HANDLE) {
        ctx->offscreen_descriptor_set = imgui_bridge_add_texture(ctx->offscreen_sampler,
                                                                 ctx->offscreen_image_view,
                                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return ctx->offscreen_descriptor_set;
}

void vulkan_renderer_get_offscreen_size(Renderer *r, uint32_t *w, uint32_t *h) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    if (ctx != nullptr) {
        if (w != nullptr) *w = ctx->offscreen_w;
        if (h != nullptr) *h = ctx->offscreen_h;
    }
}

uint32_t vulkan_renderer_pick_entity(Renderer *r, uint32_t x, uint32_t y) {
    VulkanContext *ctx = (VulkanContext *)r->backend_data;
    if (ctx == nullptr || ctx->picking_image == VK_NULL_HANDLE || ctx->picking_staging_buffer == VK_NULL_HANDLE) {
        return 0; // ENTITY_INVALID
    }

    if (x >= ctx->offscreen_w || y >= ctx->offscreen_h) {
        return 0; // ENTITY_INVALID
    }

    VkCommandBuffer cmd = vulkan_begin_single_command(ctx);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { .x = (int32_t)x, .y = (int32_t)y, .z = 0 },
        .imageExtent = { .width = 1, .height = 1, .depth = 1 },
    };

    vkCmdCopyImageToBuffer(cmd, ctx->picking_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           ctx->picking_staging_buffer, 1, &region);

    vulkan_end_single_command(ctx, cmd);

    void *data = nullptr;
    vkMapMemory(ctx->device, ctx->picking_staging_buffer_memory, 0, sizeof(uint32_t), 0, &data);
    uint32_t picked_id = 0; // ENTITY_INVALID
    if (data != nullptr) {
        picked_id = *(uint32_t *)data;
        vkUnmapMemory(ctx->device, ctx->picking_staging_buffer_memory);
    }

    return picked_id;
}

#endif // EDITOR_BUILD

