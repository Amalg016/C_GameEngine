#ifndef ENGINE_CORE_ENGINE_H
#define ENGINE_CORE_ENGINE_H

// Forward declarations — engine.h knows nothing about specific backends.
typedef struct Renderer Renderer;
typedef struct Platform Platform;

/// Run the main engine loop.  Returns when the window is closed.
void engine_run(Renderer *renderer, Platform *platform);

#endif // ENGINE_CORE_ENGINE_H
