#include "vulkan_renderer.h"
#include "vulkan_types.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Forward declarations for pipeline (will be fully implemented in commit 5)
// ---------------------------------------------------------------------------
// vulkan_pipeline.h is not included yet — stubs for now.

// ---------------------------------------------------------------------------
// RendererAPI callbacks
// ---------------------------------------------------------------------------

static bool vulkan_init(Renderer *self) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;

    // Get required instance extensions from platform.
    uint32_t ext_count = 0;
    const char **exts = platform_get_vulkan_extensions(&ext_count);

    if (!vulkan_device_create(ctx, exts, ext_count)) return false;

    // Query initial framebuffer size for swapchain.
    // We stored the window handle — use GLFW directly here since we're
    // inside the vulkan backend.
    uint32_t fb_w = 800, fb_h = 600;
    // We'll get the real size via the swapchain surface caps.

    if (!vulkan_swapchain_create(ctx, fb_w, fb_h)) return false;

    printf("[vulkan] renderer initialised (device + swapchain)\n");
    return true;
}

static void vulkan_shutdown(Renderer *self) {
    VulkanContext *ctx = (VulkanContext *)self->backend_data;
    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);
    }

    vulkan_swapchain_destroy(ctx);
    vulkan_device_destroy(ctx);

    printf("[vulkan] renderer shut down\n");
}

static bool vulkan_begin_frame(Renderer *self) {
    (void)self;
    // stub — will acquire swapchain image in commit 5
    return true;
}

static void vulkan_end_frame(Renderer *self) {
    (void)self;
    // stub — will submit + present in commit 5
}

static void vulkan_draw_triangle(Renderer *self) {
    (void)self;
    // stub — will record draw commands in commit 5
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
        .api = {
            .init           = vulkan_init,
            .shutdown       = vulkan_shutdown,
            .begin_frame    = vulkan_begin_frame,
            .end_frame      = vulkan_end_frame,
            .draw_triangle  = vulkan_draw_triangle,
        },
        .backend_data = ctx,
    };
    return r;
}

void vulkan_renderer_destroy(Renderer *r) {
    free(r->backend_data);
    r->backend_data = nullptr;
}
