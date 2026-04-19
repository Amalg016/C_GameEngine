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

typedef struct Renderer Renderer;

typedef struct RendererAPI {
    bool (*init)(Renderer *self);
    void (*shutdown)(Renderer *self);
    bool (*begin_frame)(Renderer *self);
    void (*end_frame)(Renderer *self);
    void (*draw_triangle)(Renderer *self);
} RendererAPI;

struct Renderer {
    RendererAPI api;
    void       *backend_data;   // owned by the backend
};

// Convenience wrappers — keep call sites tidy.
bool renderer_init(Renderer *r);
void renderer_shutdown(Renderer *r);
bool renderer_begin_frame(Renderer *r);
void renderer_end_frame(Renderer *r);
void renderer_draw_triangle(Renderer *r);

#endif // ENGINE_RENDERER_H
