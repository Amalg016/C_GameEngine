#include "engine.h"
#include "../platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    LuaHost          *lua_host;       // created lazily on first script load
    HierarchyContext  hctx;           // stored here so LuaHost can reference
    CameraContext     cam_ctx;        // stored here so LuaHost can reference
    Clock             clock;
    EngineCallbacks   callbacks;
    char             *current_scene;    // filepath of the currently loaded scene
    double            fixed_hz;         // fixed update rate in Hz
    bool              renderer_alive;   // true between engine_init and shutdown
    bool              hctx_inited;      // true after hierarchy_init()
    bool              cam_ctx_inited;   // true after camera_init()
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

    // Unload the current scene first — releases asset references held by
    // Sprite components so the AssetManager can free GPU resources cleanly.
    scene_unload(engine);

    // Lua host must be destroyed before the world (it holds World pointers).
    if (engine->lua_host != nullptr) {
        lua_host_destroy(engine->lua_host);
        engine->lua_host = nullptr;
    }

    // Scene name.
    free(engine->current_scene);
    engine->current_scene = nullptr;

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

Platform *engine_get_platform(Engine *engine) {
    return engine != nullptr ? engine->platform : nullptr;
}

LuaHost *engine_get_lua_host(Engine *engine) {
    return engine != nullptr ? engine->lua_host : nullptr;
}

HierarchyContext *engine_get_hctx(Engine *engine) {
    if (engine == nullptr) return nullptr;
    if (!engine->hctx_inited) {
        engine->hctx = hierarchy_init(engine->world);
        engine->hctx_inited = true;
    }
    return &engine->hctx;
}

CameraContext *engine_get_cam_ctx(Engine *engine) {
    if (engine == nullptr) return nullptr;
    if (!engine->cam_ctx_inited) {
        engine->cam_ctx = camera_init(engine->world);
        engine->cam_ctx_inited = true;
    }
    return &engine->cam_ctx;
}

bool engine_load_script(Engine *engine, const char *path) {
    if (engine == nullptr || path == nullptr) return false;

    // Lazily create the LuaHost on first script load.
    if (engine->lua_host == nullptr) {
        // Ensure hierarchy + camera contexts are initialised.
        HierarchyContext *hctx = engine_get_hctx(engine);
        CameraContext *cam_ctx = engine_get_cam_ctx(engine);

        engine->lua_host = lua_host_create(
            engine->world, hctx, cam_ctx,
            engine->asset_manager, &engine->renderer);

        if (engine->lua_host == nullptr) {
            fprintf(stderr, "[engine] failed to create LuaHost\n");
            return false;
        }
    }

    return lua_host_load_script(engine->lua_host, path);
}

// ---------------------------------------------------------------------------
// Scene management convenience wrappers
// ---------------------------------------------------------------------------

bool engine_load_scene(Engine *engine, const char *filepath) {
    if (engine == nullptr || filepath == nullptr) return false;

    // Ensure hierarchy + camera contexts are initialised before loading.
    engine_get_hctx(engine);
    engine_get_cam_ctx(engine);

    // The sprite render and movement systems in the app layer query the
    // LuaHost for Sprite/Velocity ComponentIds.  If no Lua script has been
    // loaded yet, the LuaHost won't exist and those systems silently skip
    // rendering.  Create it here so scene_load() can register the component
    // types on it and the app-level systems can find them.
    if (engine->lua_host == nullptr) {
        engine->lua_host = lua_host_create(
            engine->world, &engine->hctx, &engine->cam_ctx,
            engine->asset_manager, &engine->renderer);

        if (engine->lua_host == nullptr) {
            fprintf(stderr, "[engine] warning: failed to create LuaHost "
                    "for scene loading\n");
        }
    }
    // Unload any existing scene before loading the new one.
    scene_unload(engine);

    if (!scene_load(engine, filepath)) return false;

    // Track the currently loaded scene.
    free(engine->current_scene);
    engine->current_scene = strdup(filepath);

    return true;
}

bool engine_save_scene(Engine *engine, const char *filepath) {
    if (engine == nullptr || filepath == nullptr) return false;
    return scene_save(engine, filepath);
}

void engine_unload_scene(Engine *engine) {
    if (engine == nullptr) return;
    scene_unload(engine);

    free(engine->current_scene);
    engine->current_scene = nullptr;
}

bool engine_switch_scene(Engine *engine, const char *filepath) {
    if (engine == nullptr || filepath == nullptr) return false;

    // Ensure subsystems are ready (same as engine_load_scene).
    engine_get_hctx(engine);
    engine_get_cam_ctx(engine);

    if (engine->lua_host == nullptr) {
        engine->lua_host = lua_host_create(
            engine->world, &engine->hctx, &engine->cam_ctx,
            engine->asset_manager, &engine->renderer);
    }

    if (!scene_switch(engine, filepath)) {
        // The old scene was unloaded but the new one failed.
        free(engine->current_scene);
        engine->current_scene = nullptr;
        return false;
    }

    // Track the new scene.
    free(engine->current_scene);
    engine->current_scene = strdup(filepath);

    return true;
}

const char *engine_get_current_scene(const Engine *engine) {
    return engine != nullptr ? engine->current_scene : nullptr;
}
