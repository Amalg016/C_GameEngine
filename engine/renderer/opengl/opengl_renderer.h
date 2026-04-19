#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#include "../renderer.h"
#include "../../platform/platform.h"

/// Create a Renderer backed by OpenGL (stub — for future Emscripten/WebGL).
Renderer opengl_renderer_create(Platform *platform);

/// Free the backend allocation.  Call AFTER renderer_shutdown().
void opengl_renderer_destroy(Renderer *r);

#endif // OPENGL_RENDERER_H
