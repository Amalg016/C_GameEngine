#include "vulkan_device.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Validation layer support (debug builds only)
// ---------------------------------------------------------------------------

#ifdef NDEBUG
static const bool ENABLE_VALIDATION = false;
#else
static const bool ENABLE_VALIDATION = true;
#endif

static const char *VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};
static const uint32_t VALIDATION_LAYER_COUNT = 1;

static const char *DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
static const uint32_t DEVICE_EXTENSION_COUNT = 1;

// ---------------------------------------------------------------------------
// Debug messenger callback
// ---------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
               VkDebugUtilsMessageTypeFlagsEXT              type,
               const VkDebugUtilsMessengerCallbackDataEXT  *data,
               void                                        *user) {
    (void)type; (void)user;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "[vulkan validation] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

static VkResult create_debug_messenger(VkInstance instance,
                                       VkDebugUtilsMessengerEXT *out) {
    VkDebugUtilsMessengerCreateInfoEXT ci = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    PFN_vkCreateDebugUtilsMessengerEXT fn =
        (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

    return fn != nullptr ? fn(instance, &ci, nullptr, out) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroy_debug_messenger(VkInstance instance,
                                    VkDebugUtilsMessengerEXT messenger) {
    PFN_vkDestroyDebugUtilsMessengerEXT fn =
        (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn != nullptr) fn(instance, messenger, nullptr);
}

// ---------------------------------------------------------------------------
// Instance creation
// ---------------------------------------------------------------------------

static bool create_instance(VulkanContext *ctx,
                            const char **glfw_exts, uint32_t glfw_ext_count) {
    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "GameEngine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName        = "GameEngine",
        .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion         = VK_API_VERSION_1_3,
    };

    // Merge GLFW extensions + debug utils if validation is on.
    uint32_t ext_count = glfw_ext_count;
    const char **extensions = malloc(sizeof(char*) * (glfw_ext_count + 1));
    if (extensions == nullptr) return false;
    memcpy(extensions, glfw_exts, sizeof(char*) * glfw_ext_count);

    if (ENABLE_VALIDATION) {
        extensions[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    VkInstanceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app_info,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = extensions,
    };

    if (ENABLE_VALIDATION) {
        ci.enabledLayerCount   = VALIDATION_LAYER_COUNT;
        ci.ppEnabledLayerNames = VALIDATION_LAYERS;
    }

    VkResult res = vkCreateInstance(&ci, nullptr, &ctx->instance);
    free(extensions);

    if (res != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] vkCreateInstance failed (%d)\n", res);
        return false;
    }

    if (ENABLE_VALIDATION) {
        if (create_debug_messenger(ctx->instance, &ctx->debug_messenger) != VK_SUCCESS) {
            fprintf(stderr, "[vulkan] warning: could not create debug messenger\n");
        }
    }

    printf("[vulkan] instance created\n");
    return true;
}

// ---------------------------------------------------------------------------
// Physical device selection
// ---------------------------------------------------------------------------

typedef struct {
    bool     found;
    uint32_t graphics;
    uint32_t present;
} QueueFamilies;

static QueueFamilies find_queue_families(VkPhysicalDevice dev,
                                         VkSurfaceKHR     surface) {
    QueueFamilies qf = { .found = false };

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    VkQueueFamilyProperties *props = malloc(sizeof(*props) * count);
    if (props == nullptr) return qf;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props);

    bool has_gfx = false, has_prs = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            qf.graphics = i;
            has_gfx = true;
        }
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
        if (present) {
            qf.present = i;
            has_prs = true;
        }
        if (has_gfx && has_prs) break;
    }

    free(props);
    qf.found = has_gfx && has_prs;
    return qf;
}

static bool check_device_extensions(VkPhysicalDevice dev) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
    VkExtensionProperties *available = malloc(sizeof(*available) * count);
    if (available == nullptr) return false;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, available);

    bool all_found = true;
    for (uint32_t i = 0; i < DEVICE_EXTENSION_COUNT; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < count; ++j) {
            if (strcmp(DEVICE_EXTENSIONS[i], available[j].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) { all_found = false; break; }
    }

    free(available);
    return all_found;
}

static bool pick_physical_device(VulkanContext *ctx) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &count, nullptr);
    if (count == 0) {
        fprintf(stderr, "[vulkan] no GPUs with Vulkan support\n");
        return false;
    }

    VkPhysicalDevice *devices = malloc(sizeof(*devices) * count);
    if (devices == nullptr) return false;
    vkEnumeratePhysicalDevices(ctx->instance, &count, devices);

    // Prefer discrete GPU.
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count; ++i) {
        QueueFamilies qf = find_queue_families(devices[i], ctx->surface);
        if (!qf.found || !check_device_extensions(devices[i])) continue;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            ctx->physical_device = devices[i];
            ctx->graphics_family = qf.graphics;
            ctx->present_family  = qf.present;
            printf("[vulkan] using discrete GPU: %s\n", props.deviceName);
            free(devices);
            return true;
        }
        if (fallback == VK_NULL_HANDLE) {
            fallback = devices[i];
            ctx->graphics_family = qf.graphics;
            ctx->present_family  = qf.present;
        }
    }

    if (fallback != VK_NULL_HANDLE) {
        ctx->physical_device = fallback;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(fallback, &props);
        printf("[vulkan] using GPU: %s\n", props.deviceName);
        free(devices);
        return true;
    }

    free(devices);
    fprintf(stderr, "[vulkan] no suitable GPU found\n");
    return false;
}

// ---------------------------------------------------------------------------
// Logical device creation
// ---------------------------------------------------------------------------

static bool create_logical_device(VulkanContext *ctx) {
    float priority = 1.0f;

    // Unique queue families — at most 2.
    uint32_t unique[2] = { ctx->graphics_family, ctx->present_family };
    uint32_t unique_count = (ctx->graphics_family == ctx->present_family) ? 1 : 2;

    VkDeviceQueueCreateInfo queue_cis[2];
    for (uint32_t i = 0; i < unique_count; ++i) {
        queue_cis[i] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = unique[i],
            .queueCount       = 1,
            .pQueuePriorities = &priority,
        };
    }

    VkPhysicalDeviceFeatures features = {0};

    VkDeviceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = unique_count,
        .pQueueCreateInfos       = queue_cis,
        .enabledExtensionCount   = DEVICE_EXTENSION_COUNT,
        .ppEnabledExtensionNames = DEVICE_EXTENSIONS,
        .pEnabledFeatures        = &features,
    };

    if (vkCreateDevice(ctx->physical_device, &ci, nullptr, &ctx->device) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create logical device\n");
        return false;
    }

    vkGetDeviceQueue(ctx->device, ctx->graphics_family, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->device, ctx->present_family,  0, &ctx->present_queue);

    printf("[vulkan] logical device created\n");
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vulkan_device_create(VulkanContext *ctx,
                          const char **instance_extensions,
                          uint32_t      extension_count) {
    if (!create_instance(ctx, instance_extensions, extension_count))
        return false;

    // Create surface from GLFWwindow*.
    GLFWwindow *win = (GLFWwindow *)ctx->window_handle;
    if (glfwCreateWindowSurface(ctx->instance, win, nullptr, &ctx->surface) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create window surface\n");
        return false;
    }

    if (!pick_physical_device(ctx))
        return false;

    return create_logical_device(ctx);
}

void vulkan_device_destroy(VulkanContext *ctx) {
    if (ctx->device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx->device, nullptr);
    }
    if (ctx->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, nullptr);
    }
    if (ENABLE_VALIDATION && ctx->debug_messenger != VK_NULL_HANDLE) {
        destroy_debug_messenger(ctx->instance, ctx->debug_messenger);
    }
    if (ctx->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx->instance, nullptr);
    }
}
