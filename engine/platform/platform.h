#ifndef ENGINE_PLATFORM_H
#define ENGINE_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Platform abstraction — wraps windowing / input via GLFW.
// Nothing in this header exposes GLFW or any graphics API.
// ---------------------------------------------------------------------------

typedef struct Platform Platform;
typedef struct Input    Input;

/// Create a window.  Returns nullptr on failure.
[[nodiscard]] Platform *platform_create(const char *title, uint32_t width, uint32_t height);

/// Destroy the window and free resources.
void platform_destroy(Platform *p);

/// Wire the Input system to the platform's GLFW callbacks.
/// Must be called after platform_create(), before the main loop.
void platform_set_input(Platform *p, Input *input);

/// Store an opaque backend pointer (e.g. VulkanContext*) in the GLFW
/// window user data.  The renderer resize callback retrieves it via
/// platform_get_backend_data_from_window().
void platform_set_backend_data(Platform *p, void *data);

/// Retrieve the backend pointer from a raw GLFWwindow* (for use inside
/// GLFW callbacks that only have the window handle).
void *platform_get_backend_data_from_window(void *glfw_window);

/// Store the backend pointer via a raw GLFWwindow* handle.
/// Used by the renderer init when it doesn't have the Platform*.
void platform_set_backend_data_from_window(void *glfw_window, void *data);

/// Returns true when the user has requested closing the window.
bool platform_should_close(const Platform *p);

/// Poll OS events (input, resize, etc.).
void platform_poll_events(const Platform *p);

/// Return the required Vulkan instance extensions for surface creation.
/// `count` receives the number of extension name strings returned.
const char **platform_get_vulkan_extensions(uint32_t *count);

/// Opaque access to the underlying window handle — only backends should
/// use this, and they cast to GLFWwindow* internally.
void *platform_get_window_handle(const Platform *p);

/// Query the framebuffer size in pixels.
void platform_get_framebuffer_size(const Platform *p,
                                   uint32_t *width, uint32_t *height);

/// Returns elapsed time in seconds since GLFW init (high-res monotonic clock).
double platform_get_time(void);

#endif // ENGINE_PLATFORM_H
