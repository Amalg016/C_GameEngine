#include "vulkan_renderer.h"
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "vulkan_swapchain.h"
#include "vulkan_types.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Swapchain recreation (resize handling)
// ---------------------------------------------------------------------------

static void framebuffer_resize_callback(GLFWwindow *win, int w, int h) {
  (void)w;
  (void)h;
  VulkanContext *ctx = (VulkanContext *)glfwGetWindowUserPointer(win);
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

  return true;
}

// ---------------------------------------------------------------------------
// RendererAPI callbacks
// ---------------------------------------------------------------------------

static bool vulkan_init(Renderer *self) {
  VulkanContext *ctx = (VulkanContext *)self->backend_data;

  // Register resize callback.
  GLFWwindow *win = (GLFWwindow *)ctx->window_handle;
  glfwSetWindowUserPointer(win, ctx);
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

  // Pipeline (render pass + shaders).
  if (!vulkan_pipeline_create(ctx, "shaders/triangle.vert.spv",
                              "shaders/triangle.frag.spv"))
    return false;

  // Framebuffers.
  if (!vulkan_framebuffers_create(ctx))
    return false;

  // Command pool, buffers, sync objects.
  if (!vulkan_commands_create(ctx))
    return false;

  printf("[vulkan] renderer fully initialised\n");
  return true;
}

static void vulkan_shutdown(Renderer *self) {
  VulkanContext *ctx = (VulkanContext *)self->backend_data;
  if (ctx->device != VK_NULL_HANDLE)
    vkDeviceWaitIdle(ctx->device);

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
  VkClearValue clear = {.color = {{0.01f, 0.01f, 0.02f, 1.0f}}};

  VkRenderPassBeginInfo rp_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = ctx->render_pass,
      .framebuffer = ctx->swapchain.framebuffers[ctx->image_index],
      .renderArea = {.offset = {0, 0}, .extent = ctx->swapchain.extent},
      .clearValueCount = 1,
      .pClearValues = &clear,
  };

  vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

  // Set dynamic viewport + scissor.
  VkViewport vp = {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float)ctx->swapchain.extent.width,
      .height = (float)ctx->swapchain.extent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(cmd, 0, 1, &vp);

  VkRect2D scissor = {
      .offset = {0, 0},
      .extent = ctx->swapchain.extent,
  };
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  return true;
}

static void vulkan_draw_triangle(Renderer *self) {
  VulkanContext *ctx = (VulkanContext *)self->backend_data;
  VkCommandBuffer cmd = ctx->command_buffers[ctx->current_frame];

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    ctx->graphics_pipeline);
  vkCmdDraw(cmd, 3, 1, 0, 0);
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
  VkSemaphore wait_sems[] = {ctx->image_available[frame]};
  VkSemaphore signal_sems[] = {ctx->render_finished[frame]};
  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  VkSubmitInfo submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = wait_sems,
      .pWaitDstStageMask = wait_stages,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = signal_sems,
  };

  if (vkQueueSubmit(ctx->graphics_queue, 1, &submit, ctx->in_flight[frame]) !=
      VK_SUCCESS) {
    fprintf(stderr, "[vulkan] failed to submit draw command\n");
    return;
  }

  // Present.
  VkSwapchainKHR swapchains[] = {ctx->swapchain.handle};

  VkPresentInfoKHR present = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = signal_sems,
      .swapchainCount = 1,
      .pSwapchains = swapchains,
      .pImageIndices = &ctx->image_index,
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
// Public API
// ---------------------------------------------------------------------------

Renderer vulkan_renderer_create(Platform *platform) {
  VulkanContext *ctx = calloc(1, sizeof(VulkanContext));
  if (ctx == nullptr) {
    fprintf(stderr, "[vulkan] failed to allocate VulkanContext\n");
    return (Renderer){0};
  }

  ctx->window_handle = platform_get_window_handle(platform);

  Renderer r = {
      .api =
          {
              .init = vulkan_init,
              .shutdown = vulkan_shutdown,
              .begin_frame = vulkan_begin_frame,
              .end_frame = vulkan_end_frame,
              .draw_triangle = vulkan_draw_triangle,
          },
      .backend_data = ctx,
  };
  return r;
}

void vulkan_renderer_destroy(Renderer *r) {
  free(r->backend_data);
  r->backend_data = nullptr;
}
