#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "platform.h"
#include "../core/input.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Platform internals — GLFW stays confined to this translation unit.
// ---------------------------------------------------------------------------

/// Wrapper stored in the GLFW window user pointer.
/// Multiple subsystems (input, Vulkan resize callback) need the user pointer,
/// so we aggregate them here instead of fighting over the single slot.
typedef struct PlatformUserData {
    Input *input;           // engine input state (set by platform_set_input)
    void  *backend_data;    // renderer backend context (e.g. VulkanContext*)
} PlatformUserData;

struct Platform {
    GLFWwindow       *window;
    uint32_t          width;
    uint32_t          height;
    PlatformUserData  user_data;
};

// ---------------------------------------------------------------------------
// GLFW input callbacks — forward events to the Input system.
// ---------------------------------------------------------------------------

static void glfw_key_callback(GLFWwindow *win, int key, int /*scancode*/,
                               int action, int /*mods*/) {
    PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
    if (ud != nullptr && ud->input != nullptr)
        input_on_key(ud->input, key, action);
}

static void glfw_mouse_button_callback(GLFWwindow *win, int button,
                                        int action, int /*mods*/) {
    PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
    if (ud != nullptr && ud->input != nullptr)
        input_on_mouse_button(ud->input, button, action);
}

static void glfw_cursor_pos_callback(GLFWwindow *win, double x, double y) {
    PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
    if (ud != nullptr && ud->input != nullptr)
        input_on_cursor_pos(ud->input, x, y);
}

static void glfw_scroll_callback(GLFWwindow *win, double xoff, double yoff) {
    PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
    if (ud != nullptr && ud->input != nullptr)
        input_on_scroll(ud->input, xoff, yoff);
}

// ---- lifecycle ------------------------------------------------------------

Platform *platform_create(const char *title, uint32_t width, uint32_t height) {
    if (!glfwInit()) {
        fprintf(stderr, "[platform] failed to initialise GLFW\n");
        return nullptr;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // no OpenGL context
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GLFWwindow *win = glfwCreateWindow((int)width, (int)height, title,
                                       nullptr, nullptr);
    if (win == nullptr) {
        fprintf(stderr, "[platform] failed to create GLFW window\n");
        glfwTerminate();
        return nullptr;
    }

    Platform *p = malloc(sizeof(Platform));
    if (p == nullptr) {
        glfwDestroyWindow(win);
        glfwTerminate();
        return nullptr;
    }

    *p = (Platform){
        .window = win,
        .width  = width,
        .height = height,
        .user_data = {},
    };

    // Point the GLFW user pointer at our wrapper struct.
    glfwSetWindowUserPointer(win, &p->user_data);

    return p;
}

void platform_destroy(Platform *p) {
    if (p == nullptr) return;
    if (p->window != nullptr) glfwDestroyWindow(p->window);
    glfwTerminate();
    free(p);
}

// ---- input wiring ---------------------------------------------------------

void platform_set_input(Platform *p, Input *input) {
    if (p == nullptr) return;

    p->user_data.input = input;

    glfwSetKeyCallback(p->window, glfw_key_callback);
    glfwSetMouseButtonCallback(p->window, glfw_mouse_button_callback);
    glfwSetCursorPosCallback(p->window, glfw_cursor_pos_callback);
    glfwSetScrollCallback(p->window, glfw_scroll_callback);

    printf("[platform] input callbacks registered\n");
}

// ---- backend data wiring --------------------------------------------------

void platform_set_backend_data(Platform *p, void *data) {
    if (p == nullptr) return;
    p->user_data.backend_data = data;
}

void *platform_get_backend_data_from_window(void *glfw_window) {
    if (glfw_window == nullptr) return nullptr;
    PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(
        (GLFWwindow *)glfw_window);
    return ud != nullptr ? ud->backend_data : nullptr;
}

void platform_set_backend_data_from_window(void *glfw_window, void *data) {
    if (glfw_window == nullptr) return;
    PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(
        (GLFWwindow *)glfw_window);
    if (ud != nullptr) ud->backend_data = data;
}

// ---- queries --------------------------------------------------------------

bool platform_should_close(const Platform *p) {
    return p == nullptr || glfwWindowShouldClose(p->window);
}

void platform_poll_events(const Platform *p) {
    (void)p;
    glfwPollEvents();
}

const char **platform_get_vulkan_extensions(uint32_t *count) {
    return glfwGetRequiredInstanceExtensions(count);
}

void *platform_get_window_handle(const Platform *p) {
    return p != nullptr ? p->window : nullptr;
}

void platform_get_framebuffer_size(const Platform *p,
                                   uint32_t *width, uint32_t *height) {
    if (p == nullptr) return;
    int w, h;
    glfwGetFramebufferSize(p->window, &w, &h);
    if (width  != nullptr) *width  = (uint32_t)w;
    if (height != nullptr) *height = (uint32_t)h;
}

// ---- timing ---------------------------------------------------------------

double platform_get_time(void) {
    return glfwGetTime();
}

