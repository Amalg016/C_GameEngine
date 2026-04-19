#ifndef VULKAN_TYPES_H
#define VULKAN_TYPES_H

// ---------------------------------------------------------------------------
// Internal Vulkan state — NEVER include this header outside the vulkan/ folder.
// ---------------------------------------------------------------------------

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct VulkanSwapchain {
    VkSwapchainKHR   handle;
    VkFormat         image_format;
    VkExtent2D       extent;

    uint32_t         image_count;
    VkImage         *images;         // owned by the swapchain
    VkImageView     *image_views;    // we create these

    VkFramebuffer   *framebuffers;
} VulkanSwapchain;

typedef struct VulkanContext {
    // --- instance / surface ---
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR             surface;

    // --- device ---
    VkPhysicalDevice         physical_device;
    VkDevice                 device;
    VkQueue                  graphics_queue;
    VkQueue                  present_queue;
    uint32_t                 graphics_family;
    uint32_t                 present_family;

    // --- swapchain ---
    VulkanSwapchain          swapchain;

    // --- pipeline ---
    VkRenderPass             render_pass;
    VkPipelineLayout         pipeline_layout;
    VkPipeline               graphics_pipeline;

    // --- command submission ---
    VkCommandPool            command_pool;
    VkCommandBuffer          command_buffers[MAX_FRAMES_IN_FLIGHT];

    // --- synchronisation ---
    VkSemaphore              image_available[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore              render_finished[MAX_FRAMES_IN_FLIGHT];
    VkFence                  in_flight[MAX_FRAMES_IN_FLIGHT];

    uint32_t                 current_frame;
    uint32_t                 image_index;
    bool                     framebuffer_resized;

    // --- back-pointer to GLFW window (cast from void*) ---
    void                    *window_handle;
} VulkanContext;

#endif // VULKAN_TYPES_H
