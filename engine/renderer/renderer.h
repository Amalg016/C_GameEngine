#ifndef ENGINE_RENDERER_H
#define ENGINE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Renderer abstraction layer
//
// The engine talks to renderers exclusively through RendererAPI function
// pointers.  No backend-specific types or headers appear here — backends
// fill in the vtable and store their own state behind the opaque
// `backend_data` pointer.
// ---------------------------------------------------------------------------

/// Renderer backend selector.
typedef enum RendererBackend {
    RENDERER_BACKEND_VULKAN,
    RENDERER_BACKEND_OPENGL,
} RendererBackend;

// Forward declaration.
typedef struct Platform Platform;

typedef struct Renderer Renderer;

typedef struct RendererAPI {
    bool (*init)(Renderer *self);
    void (*shutdown)(Renderer *self);
    bool (*begin_frame)(Renderer *self);
    void (*end_frame)(Renderer *self);
    void (*draw_quad)(Renderer *self);

    /// Set the view-projection matrix for the current frame.
    /// Must be called after begin_frame() and before any draw calls.
    /// `mat4x4` points to 16 floats in column-major order.
    void (*set_view_projection)(Renderer *self, const float *mat4x4);

    /// Draw a textured sprite at (x, y) with size (w, h) in NDC.
    /// The currently bound texture is used.
    void (*draw_sprite)(Renderer *self, float x, float y, float w, float h,
                        uint32_t entity_index,
                        float uv_x, float uv_y, float uv_w, float uv_h,
                        const float color[4]);

    // --- Asset loading callbacks (used by the asset manager) ---------------
    /// Load a texture from `path`.  Returns a heap-allocated opaque pointer
    /// to backend-specific GPU data (e.g. VulkanTexture*), or nullptr on
    /// failure.
    void *(*load_texture)(Renderer *self, const char *path);

    /// Destroy the GPU texture and free the allocation returned by
    /// load_texture.
    void  (*destroy_texture)(Renderer *self, void *gpu_data);

    /// Bind a loaded texture for subsequent draw calls.  `gpu_data` is the
    /// pointer returned by load_texture.
    void  (*bind_texture)(Renderer *self, void *gpu_data);

    /// Query the pixel dimensions of a loaded texture.
    /// Returns true on success, false if gpu_data is nullptr.
    bool  (*get_texture_size)(Renderer *self, void *gpu_data,
                              uint32_t *out_w, uint32_t *out_h);

#ifdef EDITOR_BUILD
    /// Register a loaded GPU texture with ImGui for display in editor panels.
    /// Returns an opaque ImGui texture ID (cast to void*), or nullptr.
    void *(*register_imgui_texture)(Renderer *self, void *gpu_data);

    /// Unregister a texture previously registered with register_imgui_texture.
    void  (*unregister_imgui_texture)(Renderer *self, void *imgui_tex_id);

    /// Flush all queued debug draw commands.
    void  (*flush_debug_draw)(Renderer *self);
#endif
} RendererAPI;

struct Renderer {
    RendererAPI api;
    void       *backend_data;   // owned by the backend
};

// --- Factory ---------------------------------------------------------------
// These are the ONLY backend-aware functions.  Everything else goes through
// the vtable.

/// Create a renderer for the given backend.  Dispatches to the correct
/// backend _create() at compile time.
Renderer renderer_create(RendererBackend backend, Platform *platform);

/// Destroy the renderer's backend data.  Call AFTER renderer_shutdown().
void renderer_destroy(Renderer *r);

// --- Convenience wrappers (vtable dispatch) --------------------------------

bool  renderer_init(Renderer *r);
void  renderer_shutdown(Renderer *r);
bool  renderer_begin_frame(Renderer *r);
void  renderer_end_frame(Renderer *r);
void  renderer_draw_quad(Renderer *r);
void  renderer_set_view_projection(Renderer *r, const float *mat4x4);
void  renderer_draw_sprite(Renderer *r, float x, float y, float w, float h,
                           uint32_t entity_index,
                           float uv_x, float uv_y, float uv_w, float uv_h,
                           const float color[4]);

void *renderer_load_texture(Renderer *r, const char *path);
void  renderer_destroy_texture(Renderer *r, void *gpu_data);
void  renderer_bind_texture(Renderer *r, void *gpu_data);
bool  renderer_get_texture_size(Renderer *r, void *gpu_data,
                                uint32_t *out_w, uint32_t *out_h);

#ifdef EDITOR_BUILD
void *renderer_register_imgui_texture(Renderer *r, void *gpu_data);
void  renderer_unregister_imgui_texture(Renderer *r, void *imgui_tex_id);
void  renderer_flush_debug_draw(Renderer *r);
#endif

#endif // ENGINE_RENDERER_H
