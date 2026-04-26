#include "engine.h"
#include "../platform/platform.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Engine internals
// ---------------------------------------------------------------------------

struct Engine {
    Platform *platform;
    Renderer  renderer;
    bool      renderer_alive;   // true between renderer_init and shutdown
};

// ---------------------------------------------------------------------------
// engine_create — stand up platform + renderer from a simple config.
// ---------------------------------------------------------------------------

Engine *engine_create(const EngineConfig *config) {
    if (config == nullptr) {
        fprintf(stderr, "[engine] null config\n");
        return nullptr;
    }

    Engine *engine = calloc(1, sizeof(Engine));
    if (engine == nullptr) {
        fprintf(stderr, "[engine] failed to allocate Engine\n");
        return nullptr;
    }

    // --- platform -----------------------------------------------------------
    engine->platform = platform_create(config->title,
                                       config->width,
                                       config->height);
    if (engine->platform == nullptr) {
        fprintf(stderr, "[engine] platform creation failed\n");
        free(engine);
        return nullptr;
    }

    // --- renderer -----------------------------------------------------------
    engine->renderer = renderer_create(config->backend, engine->platform);
    if (engine->renderer.backend_data == nullptr) {
        fprintf(stderr, "[engine] renderer creation failed\n");
        platform_destroy(engine->platform);
        free(engine);
        return nullptr;
    }

    printf("[engine] created successfully (backend=%s)\n",
           config->backend == RENDERER_BACKEND_VULKAN ? "vulkan" : "opengl");

    return engine;
}

// ---------------------------------------------------------------------------
// engine_run — the main loop.  Backend-agnostic.
// ---------------------------------------------------------------------------

void engine_run(Engine *engine) {
    if (engine == nullptr) {
        fprintf(stderr, "[engine] null engine — aborting\n");
        return;
    }

    if (!renderer_init(&engine->renderer)) {
        fprintf(stderr, "[engine] renderer initialisation failed\n");
        return;
    }
    engine->renderer_alive = true;

    printf("[engine] entering main loop\n");

    while (!platform_should_close(engine->platform)) {
        platform_poll_events(engine->platform);

        if (renderer_begin_frame(&engine->renderer)) {
            renderer_draw_triangle(&engine->renderer);
            renderer_end_frame(&engine->renderer);
        }
    }

    printf("[engine] main loop finished\n");

    renderer_shutdown(&engine->renderer);
    engine->renderer_alive = false;
}

// ---------------------------------------------------------------------------
// engine_destroy — tear down in the correct order.
// ---------------------------------------------------------------------------

void engine_destroy(Engine *engine) {
    if (engine == nullptr) return;

    // Safety: if the app skipped engine_run(), the renderer may still be live.
    if (engine->renderer_alive) {
        renderer_shutdown(&engine->renderer);
        engine->renderer_alive = false;
    }

    renderer_destroy(&engine->renderer);
    platform_destroy(engine->platform);

    printf("[engine] destroyed\n");

    free(engine);
}
