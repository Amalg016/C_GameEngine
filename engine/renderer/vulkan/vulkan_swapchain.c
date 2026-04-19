#include "vulkan_swapchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice dev,
                                                VkSurfaceKHR     surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &count, nullptr);
    VkSurfaceFormatKHR *formats = malloc(sizeof(*formats) * count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &count, formats);

    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format     == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }
    free(formats);
    return chosen;
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice dev,
                                            VkSurfaceKHR     surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &count, nullptr);
    VkPresentModeKHR *modes = malloc(sizeof(*modes) * count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &count, modes);

    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;   // always available
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosen = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    free(modes);
    return chosen;
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR *caps,
                                uint32_t width, uint32_t height) {
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }
    VkExtent2D ext = { width, height };
    if (ext.width  < caps->minImageExtent.width)  ext.width  = caps->minImageExtent.width;
    if (ext.width  > caps->maxImageExtent.width)  ext.width  = caps->maxImageExtent.width;
    if (ext.height < caps->minImageExtent.height) ext.height = caps->minImageExtent.height;
    if (ext.height > caps->maxImageExtent.height) ext.height = caps->maxImageExtent.height;
    return ext;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vulkan_swapchain_create(VulkanContext *ctx,
                             uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
                                              ctx->surface, &caps);

    VkSurfaceFormatKHR fmt  = choose_surface_format(ctx->physical_device, ctx->surface);
    VkPresentModeKHR   mode = choose_present_mode(ctx->physical_device, ctx->surface);
    VkExtent2D         ext  = choose_extent(&caps, width, height);

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = ctx->surface,
        .minImageCount    = image_count,
        .imageFormat      = fmt.format,
        .imageColorSpace  = fmt.colorSpace,
        .imageExtent      = ext,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };

    uint32_t families[] = { ctx->graphics_family, ctx->present_family };
    if (ctx->graphics_family != ctx->present_family) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(ctx->device, &ci, nullptr,
                             &ctx->swapchain.handle) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create swapchain\n");
        return false;
    }

    ctx->swapchain.image_format = fmt.format;
    ctx->swapchain.extent       = ext;

    // Retrieve images.
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle,
                            &ctx->swapchain.image_count, nullptr);
    ctx->swapchain.images = malloc(sizeof(VkImage) * ctx->swapchain.image_count);
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle,
                            &ctx->swapchain.image_count, ctx->swapchain.images);

    // Create image views.
    ctx->swapchain.image_views =
        malloc(sizeof(VkImageView) * ctx->swapchain.image_count);

    for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
        VkImageViewCreateInfo iv_ci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = ctx->swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = fmt.format,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        if (vkCreateImageView(ctx->device, &iv_ci, nullptr,
                              &ctx->swapchain.image_views[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vulkan] failed to create image view %u\n", i);
            return false;
        }
    }

    printf("[vulkan] swapchain created (%ux%u, %u images)\n",
           ext.width, ext.height, ctx->swapchain.image_count);
    return true;
}

void vulkan_swapchain_destroy(VulkanContext *ctx) {
    if (ctx->device == VK_NULL_HANDLE) return;

    if (ctx->swapchain.framebuffers != nullptr) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            vkDestroyFramebuffer(ctx->device,
                                 ctx->swapchain.framebuffers[i], nullptr);
        }
        free(ctx->swapchain.framebuffers);
        ctx->swapchain.framebuffers = nullptr;
    }

    if (ctx->swapchain.image_views != nullptr) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            vkDestroyImageView(ctx->device,
                               ctx->swapchain.image_views[i], nullptr);
        }
        free(ctx->swapchain.image_views);
        ctx->swapchain.image_views = nullptr;
    }

    free(ctx->swapchain.images);
    ctx->swapchain.images = nullptr;

    if (ctx->swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain.handle, nullptr);
        ctx->swapchain.handle = VK_NULL_HANDLE;
    }
}
