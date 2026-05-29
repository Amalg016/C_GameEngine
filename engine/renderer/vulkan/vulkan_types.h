#ifndef VULKAN_TYPES_H
#define VULKAN_TYPES_H

// ---------------------------------------------------------------------------
// Internal Vulkan state — NEVER include this header outside the vulkan/ folder.
// ---------------------------------------------------------------------------

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_FRAMES_IN_FLIGHT 2

// ---------------------------------------------------------------------------
// Vertex format — position + UV + color
// ---------------------------------------------------------------------------

typedef struct Vertex {
    float pos[2];    // x, y
    float uv[2];     // u, v
    float color[3];  // r, g, b
} Vertex;

// ---------------------------------------------------------------------------
// Texture — wraps a VkImage + view + sampler.
// ---------------------------------------------------------------------------

typedef struct VulkanTexture {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkSampler      sampler;
    uint32_t       width;
    uint32_t       height;
} VulkanTexture;

// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------

typedef struct VulkanSwapchain {
    VkSwapchainKHR   handle;
    VkFormat         image_format;
    VkExtent2D       extent;

    uint32_t         image_count;
    VkImage         *images;         // owned by the swapchain
    VkImageView     *image_views;    // we create these

    VkFramebuffer   *framebuffers;
} VulkanSwapchain;

// ---------------------------------------------------------------------------
// Main Vulkan context
// ---------------------------------------------------------------------------

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

    // --- descriptors ---
    VkDescriptorSetLayout    descriptor_set_layout;
    VkDescriptorPool         descriptor_pool;
    VkDescriptorSet          descriptor_sets[MAX_FRAMES_IN_FLIGHT];

    // --- vertex / index buffers ---
    VkBuffer                 vertex_buffer;
    VkDeviceMemory           vertex_buffer_memory;
    VkBuffer                 index_buffer;
    VkDeviceMemory           index_buffer_memory;
    uint32_t                 index_count;

    // --- fallback 1×1 texture (used when no real texture is bound) ---
    VkImage                  fallback_image;
    VkDeviceMemory           fallback_memory;
    VkImageView              fallback_view;
    VkSampler                fallback_sampler;

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

    // --- current view-projection matrix (set per frame by the camera) ---
    float                    view_proj[16];

#ifdef EDITOR_BUILD
    // --- offscreen game viewport rendering ---
    VkImage                  offscreen_image;
    VkDeviceMemory           offscreen_image_memory;
    VkImageView              offscreen_image_view;
    VkSampler                offscreen_sampler;
    VkFramebuffer            offscreen_framebuffer;
    VkRenderPass             offscreen_render_pass;
    VkDescriptorSet          offscreen_descriptor_set;
    uint32_t                 offscreen_w;
    uint32_t                 offscreen_h;

    // --- GPU picking ---
    VkImage                  picking_image;
    VkDeviceMemory           picking_image_memory;
    VkImageView              picking_image_view;
    VkBuffer                 picking_staging_buffer;
    VkDeviceMemory           picking_staging_buffer_memory;
#endif

    // --- back-pointer to GLFW window (cast from void*) ---
    void                    *window_handle;
} VulkanContext;

#endif // VULKAN_TYPES_H
