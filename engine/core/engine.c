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
// Default fixed timestep rate (Hz).
// ---------------------------------------------------------------------------

static const double DEFAULT_FIXED_HZ = 60.0;

// ---------------------------------------------------------------------------
// Engine internals
// ---------------------------------------------------------------------------

struct Engine {
    Platform         *platform;
    Renderer          renderer;
    AssetManager     *asset_manager;
    World            *world;
    Clock             clock;
    EngineCallbacks   callbacks;
    double            fixed_hz;         // fixed update rate in Hz
    bool              renderer_alive;   // true between engine_init and shutdown
};

// ---------------------------------------------------------------------------
// engine_create — stand up platform + renderer struct from a simple config.
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

    engine->fixed_hz = DEFAULT_FIXED_HZ;

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

    // --- ECS world ----------------------------------------------------------
    engine->world = world_create();
    if (engine->world == nullptr) {
        fprintf(stderr, "[engine] ECS world creation failed\n");
        asset_manager_destroy(engine->asset_manager);
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
// engine_init — initialise the renderer backend.
// ---------------------------------------------------------------------------

bool engine_init(Engine *engine) {
    if (engine == nullptr) {
        fprintf(stderr, "[engine] null engine\n");
        return false;
    }

    if (engine->renderer_alive) {
        return true; // already initialised
    }

    if (!renderer_init(&engine->renderer)) {
        fprintf(stderr, "[engine] renderer initialisation failed\n");
        return false;
    }
    engine->renderer_alive = true;

    // Wire asset manager callbacks now that the renderer is initialised.
    AssetManagerCallbacks cbs = {
        .backend_ctx     = &engine->renderer,
        .load_texture    = am_load_texture,
        .destroy_texture = am_destroy_texture,
    };
    asset_manager_set_callbacks(engine->asset_manager, &cbs);

    printf("[engine] renderer initialised — ready for asset loading\n");
    return true;
}

// ---------------------------------------------------------------------------
// engine_set_callbacks — register application hooks.
// ---------------------------------------------------------------------------

void engine_set_callbacks(Engine *engine, const EngineCallbacks *callbacks) {
    if (engine == nullptr) return;
    engine->callbacks = callbacks != nullptr
        ? *callbacks
        : (EngineCallbacks){0};
}

// ---------------------------------------------------------------------------
// engine_set_fixed_timestep — change the fixed update rate (Hz).
// ---------------------------------------------------------------------------

void engine_set_fixed_timestep(Engine *engine, double hz) {
    if (engine == nullptr) return;
    engine->fixed_hz = hz > 0.0 ? hz : DEFAULT_FIXED_HZ;
}

// ---------------------------------------------------------------------------
// engine_run — the main loop.  Fixed timestep + variable rendering.
//
// The "Fix Your Timestep" pattern:
//   1. clock_tick()  — measure frame delta, add to accumulator
//   2. Drain the accumulator in fixed-dt chunks → on_fixed_update
//   3. One variable-dt call per frame           → on_update
//   4. Render with interpolation alpha          → on_render
// ---------------------------------------------------------------------------

void engine_run(Engine *engine) {
    if (engine == nullptr) {
        fprintf(stderr, "[engine] null engine — aborting\n");
        return;
    }

    // Auto-init if the app forgot.
    if (!engine->renderer_alive) {
        if (!engine_init(engine)) return;
    }

    double fixed_dt = 1.0 / engine->fixed_hz;
    clock_start(&engine->clock, fixed_dt);

    printf("[engine] entering main loop (fixed_dt=%.4fs / %.0f Hz)\n",
           fixed_dt, engine->fixed_hz);

    while (!platform_should_close(engine->platform)) {
        platform_poll_events(engine->platform);
        clock_tick(&engine->clock);

        // ----- fixed update: drain accumulator at a constant rate ----------
        while (engine->clock.accumulator >= engine->clock.fixed_dt) {
            if (engine->callbacks.on_fixed_update) {
                engine->callbacks.on_fixed_update(
                    engine->callbacks.user_data,
                    engine->clock.fixed_dt);
            }
            engine->clock.accumulator -= engine->clock.fixed_dt;
        }

        // ----- variable update: once per frame -----------------------------
        if (engine->callbacks.on_update) {
            engine->callbacks.on_update(
                engine->callbacks.user_data,
                engine->clock.delta_time);
        }

        // ----- render ------------------------------------------------------
        if (renderer_begin_frame(&engine->renderer)) {
            double alpha = clock_get_alpha(&engine->clock);

            if (engine->callbacks.on_render) {
                engine->callbacks.on_render(
                    engine->callbacks.user_data,
                    alpha);
            } else {
                // Fallback: keep existing behaviour when no callback is set.
                renderer_draw_quad(&engine->renderer);
            }

            renderer_end_frame(&engine->renderer);
        }
    }

    printf("[engine] main loop finished — %lu frames, %.1fs elapsed\n",
           (unsigned long)engine->clock.frame_count,
           engine->clock.elapsed);
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

    // ECS world must be destroyed before asset manager / renderer.
    world_destroy(engine->world);

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

const Clock *engine_get_clock(const Engine *engine) {
    return engine != nullptr ? &engine->clock : nullptr;
}

World *engine_get_world(Engine *engine) {
    return engine != nullptr ? engine->world : nullptr;
}
