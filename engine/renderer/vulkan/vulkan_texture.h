#ifndef VULKAN_TEXTURE_H
#define VULKAN_TEXTURE_H

#include "vulkan_types.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Vulkan texture loading — loads image files into GPU textures.
// The VulkanTexture struct is defined in vulkan_types.h.
// ---------------------------------------------------------------------------

/// Load a texture from an image file (PNG, JPG, BMP — anything stb_image
/// supports).  Returns true on success and fills `out`.
bool vulkan_texture_create_from_file(VulkanContext  *ctx,
                                     const char     *path,
                                     VulkanTexture  *out);

/// Destroy all GPU resources associated with a texture.
void vulkan_texture_destroy(VulkanContext *ctx, VulkanTexture *tex);

#endif // VULKAN_TEXTURE_H
