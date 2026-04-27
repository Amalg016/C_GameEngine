#include "engine.h"
#include "../platform/platform.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Adapter functions for the asset manager callbacks.
//
// The asset manager's callback signatures take (void *backend_ctx, ...),
// but the Renderer API expects (Renderer *self, ...).  These thin adapters
// bridge the gap.
// ---------------------------------------------------------------------------

static void *am_load_texture(void *backend_ctx, const char *path) {
    Renderer *r = (Renderer *)backend_ctx;
    return renderer_load_texture(r, path);
}

static void am_destroy_texture(void *backend_ctx, void *gpu_data) {
    Renderer *r = (Renderer *)backend_ctx;
    renderer_destroy_texture(r, gpu_data);
}

// ---------------------------------------------------------------------------
// Engine internals
// ---------------------------------------------------------------------------

struct Engine {
    Platform      *platform;
    Renderer       renderer;
    AssetManager  *asset_manager;
    bool           renderer_alive;   // true between renderer_init and shutdown
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

    // --- asset manager ------------------------------------------------------
    engine->asset_manager = asset_manager_create();
    if (engine->asset_manager == nullptr) {
        fprintf(stderr, "[engine] asset manager creation failed\n");
        renderer_destroy(&engine->renderer);
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

    // Wire asset manager callbacks now that the renderer is initialised.
    AssetManagerCallbacks cbs = {
        .backend_ctx     = &engine->renderer,
        .load_texture    = am_load_texture,
        .destroy_texture = am_destroy_texture,
    };
    asset_manager_set_callbacks(engine->asset_manager, &cbs);

    printf("[engine] entering main loop\n");

    while (!platform_should_close(engine->platform)) {
        platform_poll_events(engine->platform);

        if (renderer_begin_frame(&engine->renderer)) {
            renderer_draw_quad(&engine->renderer);
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

    // Asset manager must be destroyed BEFORE the renderer — it may need
    // to free GPU resources via the renderer callbacks.
    asset_manager_destroy(engine->asset_manager);

    renderer_destroy(&engine->renderer);
    platform_destroy(engine->platform);

    printf("[engine] destroyed\n");

    free(engine);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

AssetManager *engine_get_asset_manager(Engine *engine) {
    return engine != nullptr ? engine->asset_manager : nullptr;
}

Renderer *engine_get_renderer(Engine *engine) {
    return engine != nullptr ? &engine->renderer : nullptr;
}
