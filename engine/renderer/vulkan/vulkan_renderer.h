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

#endif // VULKAN_RENDERER_H
