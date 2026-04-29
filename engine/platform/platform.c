#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Platform internals — GLFW stays confined to this translation unit.
// ---------------------------------------------------------------------------

struct Platform {
    GLFWwindow *window;
    uint32_t    width;
    uint32_t    height;
};

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
    };
    return p;
}

void platform_destroy(Platform *p) {
    if (p == nullptr) return;
    if (p->window != nullptr) glfwDestroyWindow(p->window);
    glfwTerminate();
    free(p);
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
