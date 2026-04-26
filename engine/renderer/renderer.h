#ifndef ENGINE_RENDERER_H
#define ENGINE_RENDERER_H

#include <stdbool.h>

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

bool renderer_init(Renderer *r);
void renderer_shutdown(Renderer *r);
bool renderer_begin_frame(Renderer *r);
void renderer_end_frame(Renderer *r);
void renderer_draw_quad(Renderer *r);

#endif // ENGINE_RENDERER_H
