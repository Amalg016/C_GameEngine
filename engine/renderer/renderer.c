#include "renderer.h"

// Backend headers — brought in at compile time based on the target.
#ifdef USE_OPENGL
#   include "opengl/opengl_renderer.h"
#else
#   include "vulkan/vulkan_renderer.h"
#endif

#include <stddef.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Factory — the ONE place that knows about concrete backends.
// ---------------------------------------------------------------------------

Renderer renderer_create(RendererBackend backend, Platform *platform) {
    (void)backend;   // currently enforced at compile time

#ifdef USE_OPENGL
    return opengl_renderer_create(platform);
#else
    return vulkan_renderer_create(platform);
#endif
}

void renderer_destroy(Renderer *r) {
    if (r == nullptr) return;

#ifdef USE_OPENGL
    opengl_renderer_destroy(r);
#else
    vulkan_renderer_destroy(r);
#endif
}

// ---------------------------------------------------------------------------
// Convenience dispatchers — forward every call through the vtable.
// ---------------------------------------------------------------------------

bool renderer_init(Renderer *r) {
    if (r == nullptr || r->api.init == nullptr) {
        fprintf(stderr, "[renderer] init: null renderer or missing backend\n");
        return false;
    }
    return r->api.init(r);
}

void renderer_shutdown(Renderer *r) {
    if (r != nullptr && r->api.shutdown != nullptr) {
        r->api.shutdown(r);
    }
}

bool renderer_begin_frame(Renderer *r) {
    if (r == nullptr || r->api.begin_frame == nullptr) return false;
    return r->api.begin_frame(r);
}

void renderer_end_frame(Renderer *r) {
    if (r != nullptr && r->api.end_frame != nullptr) {
        r->api.end_frame(r);
    }
}

void renderer_draw_quad(Renderer *r) {
    if (r != nullptr && r->api.draw_quad != nullptr) {
        r->api.draw_quad(r);
    }
}

void renderer_draw_sprite(Renderer *r, float x, float y, float w, float h) {
    if (r != nullptr && r->api.draw_sprite != nullptr) {
        r->api.draw_sprite(r, x, y, w, h);
    }
}

void *renderer_load_texture(Renderer *r, const char *path) {
    if (r != nullptr && r->api.load_texture != nullptr) {
        return r->api.load_texture(r, path);
    }
    return nullptr;
}

void renderer_destroy_texture(Renderer *r, void *gpu_data) {
    if (r != nullptr && r->api.destroy_texture != nullptr) {
        r->api.destroy_texture(r, gpu_data);
    }
}

void renderer_bind_texture(Renderer *r, void *gpu_data) {
    if (r != nullptr && r->api.bind_texture != nullptr) {
        r->api.bind_texture(r, gpu_data);
    }
}
