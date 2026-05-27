// ---------------------------------------------------------------------------
// ImGui Vulkan/GLFW Backend Bridge — C++ Implementation
//
// This translation unit is compiled with g++ (C++11 or later).  It is the
// only C++ file authored by the engine — everything else in the editor is
// pure C23.  The extern "C" functions defined here are called from
// imgui_layer.c through the bridge header.
// ---------------------------------------------------------------------------

#ifdef EDITOR_BUILD

#include "imgui_vulkan_bridge.h"

// Dear ImGui backend headers (C++).
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

// GLFW (needed for ImGui_ImplGlfw_InitForVulkan).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>

// ---------------------------------------------------------------------------
// Internal state — the descriptor pool dedicated to ImGui.
// ---------------------------------------------------------------------------

static VkDescriptorPool s_imgui_pool    = VK_NULL_HANDLE;
static VkDevice         s_device        = VK_NULL_HANDLE;
static bool             s_bridge_active = false;

// ---------------------------------------------------------------------------
// Bridge implementation
// ---------------------------------------------------------------------------

extern "C" bool imgui_bridge_init(void *glfw_window,
                                  const ImGuiBridgeInitInfo *info) {
    if (glfw_window == nullptr || info == nullptr) return false;

    s_device = info->device;

    // ---- Create a dedicated descriptor pool for ImGui ---------------------
    // Generous sizing — ImGui uses combined image samplers for font textures
    // and any user textures displayed in panels.
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    };

    VkDescriptorPoolCreateInfo pool_ci = {};
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets       = 1000;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = pool_sizes;

    if (vkCreateDescriptorPool(s_device, &pool_ci, nullptr, &s_imgui_pool)
            != VK_SUCCESS) {
        std::fprintf(stderr, "[imgui_bridge] failed to create descriptor pool\n");
        return false;
    }

    // ---- Initialise the GLFW backend --------------------------------------
    if (!ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow *>(glfw_window),
                                      true)) {
        std::fprintf(stderr, "[imgui_bridge] ImGui_ImplGlfw_InitForVulkan failed\n");
        vkDestroyDescriptorPool(s_device, s_imgui_pool, nullptr);
        s_imgui_pool = VK_NULL_HANDLE;
        return false;
    }

    // ---- Initialise the Vulkan backend ------------------------------------
    ImGui_ImplVulkan_InitInfo vk_info = {};
    vk_info.Instance        = info->instance;
    vk_info.PhysicalDevice  = info->physical_device;
    vk_info.Device          = info->device;
    vk_info.QueueFamily     = info->queue_family;
    vk_info.Queue           = info->queue;
    vk_info.DescriptorPool  = s_imgui_pool;
    vk_info.MinImageCount   = 2;
    vk_info.ImageCount      = info->image_count;
    vk_info.PipelineInfoMain.RenderPass  = info->render_pass;
    vk_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&vk_info)) {
        std::fprintf(stderr, "[imgui_bridge] ImGui_ImplVulkan_Init failed\n");
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(s_device, s_imgui_pool, nullptr);
        s_imgui_pool = VK_NULL_HANDLE;
        return false;
    }

    s_bridge_active = true;
    std::printf("[imgui_bridge] backends initialised\n");
    return true;
}

extern "C" void imgui_bridge_shutdown(void) {
    s_bridge_active = false;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    if (s_imgui_pool != VK_NULL_HANDLE && s_device != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(s_device, s_imgui_pool, nullptr);
        s_imgui_pool = VK_NULL_HANDLE;
    }

    std::printf("[imgui_bridge] backends shut down\n");
}

extern "C" void imgui_bridge_new_frame(void) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

extern "C" void imgui_bridge_render(void *draw_data, VkCommandBuffer cmd) {
    if (draw_data == nullptr) return;
    ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData *>(draw_data),
                                    cmd, VK_NULL_HANDLE);
}

extern "C" void *imgui_bridge_add_texture(VkSampler sampler, VkImageView image_view, VkImageLayout image_layout) {
    if (!s_bridge_active) return nullptr;
    return (void *)ImGui_ImplVulkan_AddTexture(sampler, image_view, image_layout);
}

extern "C" void imgui_bridge_remove_texture(void *descriptor_set) {
    if (!s_bridge_active || descriptor_set == nullptr) return;
    ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(descriptor_set));
}

#endif // EDITOR_BUILD
