#include "opengl_renderer.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// OpenGL / WebGL stub backend
//
// Implements the full RendererAPI contract but does nothing useful yet.
// This will be fleshed out when Emscripten + OpenGL ES / WebGL support is
// added.
// ---------------------------------------------------------------------------

typedef struct OpenGLContext {
    // Placeholder — will hold GL state in the future.
    int dummy;
} OpenGLContext;

// ---- RendererAPI callbacks ------------------------------------------------

static bool opengl_init(Renderer *self) {
    (void)self;
    printf("[opengl] init (stub)\n");
    return true;
}

static void opengl_shutdown(Renderer *self) {
    (void)self;
    printf("[opengl] shutdown (stub)\n");
}

static bool opengl_begin_frame(Renderer *self) {
    (void)self;
    // Would call glClear, set up frame state, etc.
    return true;
}

static void opengl_end_frame(Renderer *self) {
    (void)self;
    // Would call glfwSwapBuffers or emscripten equivalent.
}

static void opengl_draw_triangle(Renderer *self) {
    (void)self;
    // Would issue a glDrawArrays call.
}

// ---- Public API -----------------------------------------------------------

Renderer opengl_renderer_create(Platform *platform) {
    (void)platform;

    OpenGLContext *ctx = calloc(1, sizeof(OpenGLContext));
    if (ctx == nullptr) {
        fprintf(stderr, "[opengl] failed to allocate OpenGLContext\n");
        return (Renderer){0};
    }

    Renderer r = {
        .api = {
            .init           = opengl_init,
            .shutdown       = opengl_shutdown,
            .begin_frame    = opengl_begin_frame,
            .end_frame      = opengl_end_frame,
            .draw_triangle  = opengl_draw_triangle,
        },
        .backend_data = ctx,
    };
    return r;
}

void opengl_renderer_destroy(Renderer *r) {
    free(r->backend_data);
    r->backend_data = nullptr;
}
