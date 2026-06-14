#include "../engine/core/engine.h"
#include "../engine/platform/platform.h"
#include "../engine/core/asset_manager.h"
#include "../engine/core/sprite.h"
#include "../engine/core/animation.h"
#include "../engine/core/clock.h"
#include "../engine/core/input.h"
#include "../engine/core/scripting/lua_host.h"
#include "../engine/core/platformer_controller.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Application state — passed to every callback via user_data.
// ---------------------------------------------------------------------------

typedef struct AppState {
    Engine           *engine;
    Renderer         *renderer;
    AssetManager     *am;
    World            *world;
    HierarchyContext *hctx;          // engine-managed
    CameraContext    *cam_ctx;       // engine-managed
    LuaHost          *lua_host;
    double            fixed_time;
    double            frame_time;
    uint64_t          fixed_ticks;
} AppState;

// ---------------------------------------------------------------------------
// Lua host internal accessors — file-scope externs.
// ---------------------------------------------------------------------------

extern bool        lua_host_velocity_registered(LuaHost *host);
extern ComponentId lua_host_get_velocity_id(LuaHost *host);
extern bool        lua_host_sprite_registered(LuaHost *host);
extern ComponentId lua_host_get_sprite_id(LuaHost *host);
extern bool        lua_host_animator_registered(LuaHost *host);
extern ComponentId lua_host_get_animator_id(LuaHost *host);

// ---------------------------------------------------------------------------
// Systems (remain in C for performance)
// ---------------------------------------------------------------------------

/// Linear interpolation helper.
static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Movement system: LocalTransform.pos += Velocity * dt  (fixed rate)
/// Only processes entities that have both LocalTransform and a Velocity
/// component (registered by the Lua host).
static void system_movement(AppState *app, double dt) {
    LuaHost *lua = app->lua_host;
    if (lua == nullptr) return;

    if (!lua_host_velocity_registered(lua)) return;
    ComponentId c_vel = lua_host_get_velocity_id(lua);

    // Reuse the LuaVelocity layout (matches Lua bindings).
    typedef struct { float dx, dy; } Velocity;

    ComponentPool *vel_pool = world_get_pool(app->world, c_vel);
    if (vel_pool == nullptr) return;

    for (uint32_t i = 0; i < vel_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(vel_pool, i);
        Velocity *vel    = (Velocity *)component_pool_get_dense(vel_pool, i);

        Entity ent = world_entity_from_index(app->world, ent_idx);
        LocalTransform *lt = (LocalTransform *)world_get_component(
            app->world, ent, app->hctx->c_local_transform);

        if (lt == nullptr) continue;

        lt->x += vel->dx * (float)dt;
        lt->y += vel->dy * (float)dt;

        // Bounce off world-space edges (±5 units for an ortho_size of 5).
        float half_w = lt->sx * 0.5f;
        float half_h = lt->sy * 0.5f;
        float bound  = 5.0f;

        if (lt->x + half_w > bound) {
            lt->x = bound - half_w;
            vel->dx = -vel->dx;
        } else if (lt->x - half_w < -bound) {
            lt->x = -bound + half_w;
            vel->dx = -vel->dx;
        }

        if (lt->y + half_h > bound) {
            lt->y = bound - half_h;
            vel->dy = -vel->dy;
        } else if (lt->y - half_h < -bound) {
            lt->y = -bound + half_h;
            vel->dy = -vel->dy;
        }
    }
}

/// Sprite render system: bind texture + draw at interpolated world position.
/// Uses the Sprite ComponentId registered by the Lua bindings.
static void system_sprite_render(AppState *app, float alpha) {
    LuaHost *lua = app->lua_host;
    if (lua == nullptr) return;

    if (!lua_host_sprite_registered(lua)) return;
    ComponentId c_sprite = lua_host_get_sprite_id(lua);

    // Reuse the LuaSprite layout (matches Lua bindings / scene.c).
    typedef struct { Sprite sprite; } SpriteComp;

    ComponentPool *sprite_pool = world_get_pool(app->world, c_sprite);
    if (sprite_pool == nullptr) return;

    for (uint32_t i = 0; i < sprite_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(sprite_pool, i);
        SpriteComp *sc   = (SpriteComp *)component_pool_get_dense(sprite_pool, i);

        Entity ent = world_entity_from_index(app->world, ent_idx);

        // Use world transform for the current position.
        WorldTransform *wt = (WorldTransform *)world_get_component(
            app->world, ent, app->hctx->c_world_transform);
        if (wt == nullptr) continue;

        // Interpolate with previous position if available.
        float render_x = wt->x;
        float render_y = wt->y;

        PreviousPosition *pp = (PreviousPosition *)world_get_component(
            app->world, ent, app->hctx->c_prev_position);
        if (pp != nullptr) {
            render_x = lerpf(pp->x, wt->x, alpha);
            render_y = lerpf(pp->y, wt->y, alpha);
        }

        // Bind the sprite's texture.
        void *gpu_data = asset_manager_get_data(app->am, sc->sprite.texture);
        renderer_bind_texture(app->renderer, gpu_data);

        // Draw at interpolated world position with world scale + UV sub-region.
        renderer_draw_sprite(app->renderer, render_x, render_y, wt->sx, wt->sy,
                             ent,
                             sc->sprite.uv_rect.x, sc->sprite.uv_rect.y,
                             sc->sprite.uv_rect.w, sc->sprite.uv_rect.h);
    }
}

/// Animation system: advance Animator components and update Sprite UVs.
/// Runs per-frame with the variable delta time.
static void system_animation_update(AppState *app, double dt) {
    LuaHost *lua = app->lua_host;
    if (lua == nullptr) return;

    if (!lua_host_animator_registered(lua)) return;
    if (!lua_host_sprite_registered(lua)) return;

    ComponentId c_anim   = lua_host_get_animator_id(lua);
    ComponentId c_sprite = lua_host_get_sprite_id(lua);

    typedef struct { Sprite sprite; } SpriteComp;
    typedef struct { Animator animator; } AnimComp;

    ComponentPool *anim_pool = world_get_pool(app->world, c_anim);
    if (anim_pool == nullptr) return;

    for (uint32_t i = 0; i < anim_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(anim_pool, i);
        AnimComp *ac     = (AnimComp *)component_pool_get_dense(anim_pool, i);

        Entity ent = world_entity_from_index(app->world, ent_idx);
        SpriteComp *sc = (SpriteComp *)world_get_component(
            app->world, ent, c_sprite);

        if (sc == nullptr) continue;

        animator_update(&ac->animator, (float)dt, &sc->sprite);
    }
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

/// Called at a locked rate (default 60 Hz).
static void on_fixed_update(void *user_data, double dt) {
    AppState *app = (AppState *)user_data;
    app->fixed_time += dt;
    app->fixed_ticks++;

    // 1. Snapshot world positions BEFORE physics (for interpolation).
    hierarchy_snapshot_positions(app->world, app->hctx);

    // 2. Run movement (modifies LocalTransform).
    system_movement(app, dt);

    // Run platformer system update (modifies LocalTransform under physics/collision sweeps)
    platformer_system_update(app->world, engine_get_input(app->engine), app->hctx, dt);


    // 3. Propagate transforms (LocalTransform → WorldTransform).
    hierarchy_update_transforms(app->world, app->hctx);

    // 4. Update camera matrices.
    Platform *plat = engine_get_platform(app->engine);
    uint32_t fb_w = 800, fb_h = 600;
    platform_get_framebuffer_size(plat, &fb_w, &fb_h);
    float aspect = (fb_h > 0) ? (float)fb_w / (float)fb_h : 1.0f;
    camera_update(app->world, app->cam_ctx, app->hctx, aspect);

    // 5. Lua fixed update hook.
    lua_host_on_fixed_update(app->lua_host, dt);

    // 6. Per-entity script fixed update.
    lua_host_scripts_fixed_update(app->lua_host, dt);

    // Throttled diagnostic.
    if (app->fixed_ticks % 60 == 0) {
        printf("[demo] sim=%.2fs\n", app->fixed_time);
    }
}

static void system_platformer_input_queue(AppState *app) {
    ComponentId c_plat_ctrl = platformer_controller_get_id();
    if (c_plat_ctrl == UINT8_MAX) return;

    ComponentPool *ctrl_pool = world_get_pool(app->world, c_plat_ctrl);
    if (ctrl_pool == nullptr) return;

    const Input *input = engine_get_input(app->engine);
    if (!input_is_game_active(input)) return;

    for (uint32_t i = 0; i < ctrl_pool->count; ++i) {
        PlatformerController *ctrl = (PlatformerController *)component_pool_get_dense(ctrl_pool, i);
        if (input_key_pressed(input, ctrl->key_jump) || input_gamepad_pressed(input, INPUT_GAMEPAD_BUTTON_A)) {
            ctrl->jump_queued = true;
        }
        if (input_key_pressed(input, ctrl->key_dash) || 
            input_gamepad_pressed(input, INPUT_GAMEPAD_BUTTON_X) || 
            input_gamepad_pressed(input, INPUT_GAMEPAD_BUTTON_RIGHT_BUMPER)) {
            ctrl->dash_queued = true;
        }
    }
}

/// Called once per frame with the real (variable) frame delta.
static void on_update(void *user_data, double dt) {
    AppState *app = (AppState *)user_data;
    app->frame_time += dt;

    // Queue platformer inputs before updating standard systems
    system_platformer_input_queue(app);

    // Advance animation playback (modifies Sprite components).
    system_animation_update(app, dt);

    // Lua per-frame update hook.
    lua_host_on_update(app->lua_host, dt);

    // Per-entity script update.
    lua_host_scripts_update(app->lua_host, dt);
}

/// Called once per frame, inside begin/end frame.
static void on_render(void *user_data, double alpha) {
    AppState *app = (AppState *)user_data;

    // Push the camera's VP matrix to the renderer.
    const Mat4 *vp = camera_get_view_proj(app->world, app->cam_ctx);
    if (vp != nullptr) {
        renderer_set_view_projection(app->renderer, (const float *)vp);
    }

    system_sprite_render(app, (float)alpha);

    // Lua render hook.
    lua_host_on_render(app->lua_host, alpha);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    printf("=== GameEngine starting ===\n");

    EngineConfig config = {
        .title   = "GameEngine — Lua Demo",
        .width   = 800,
        .height  = 600,
#ifdef USE_OPENGL
        .backend = RENDERER_BACKEND_OPENGL,
#else
        .backend = RENDERER_BACKEND_VULKAN,
#endif
    };

    Engine *engine = engine_create(&config);
    if (engine == nullptr) {
        fprintf(stderr, "failed to create engine\n");
        return EXIT_FAILURE;
    }

    if (!engine_init(engine)) {
        fprintf(stderr, "failed to init engine\n");
        engine_destroy(engine);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Scene setup — choose ONE of the two paths below.
    // -----------------------------------------------------------------------

    // // --- Path A: Load scene from Lua script (current default) --------------
    // if (!engine_load_script(engine, "scripts/demo.lua")) {
    //     fprintf(stderr, "failed to load Lua script\n");
    //     engine_destroy(engine);
    //     return EXIT_FAILURE;
    // }

    // // Call Lua on_init() to spawn entities.
    // LuaHost *lua_host = engine_get_lua_host(engine);
    // lua_host_on_init(lua_host);

    // // Initial transform propagation so WorldTransform is correct for frame 0.
    // HierarchyContext *hctx = engine_get_hctx(engine);
    // hierarchy_update_transforms(engine_get_world(engine), hctx);

    // Save the current scene to JSON (demonstrates the serializer).
    // engine_save_scene(engine, "scenes/demo_saved.json");

    // --- Path B: Load scene directly from JSON (uncomment to use) ----------
    // To use JSON scenes instead of Lua, comment out Path A above and
    // uncomment the following:
    //
    HierarchyContext *hctx = engine_get_hctx(engine);
    if (!engine_load_scene(engine, "scenes/demo.json")) {
        fprintf(stderr, "failed to load scene\n");
        engine_destroy(engine);
        return EXIT_FAILURE;
    }
    LuaHost *lua_host = engine_get_lua_host(engine);
    

    // -----------------------------------------------------------------------
    // Set up callbacks and run
    // -----------------------------------------------------------------------

    AppState app_state = {
        .engine        = engine,
        .renderer      = engine_get_renderer(engine),
        .am            = engine_get_asset_manager(engine),
        .world         = engine_get_world(engine),
        .hctx          = hctx,
        .cam_ctx       = engine_get_cam_ctx(engine),
        .lua_host      = lua_host,
        .fixed_time    = 0.0,
        .frame_time    = 0.0,
        .fixed_ticks   = 0,
    };

    EngineCallbacks callbacks = {
        .user_data       = &app_state,
        .on_fixed_update = on_fixed_update,
        .on_update       = on_update,
        .on_render       = on_render,
    };
    engine_set_callbacks(engine, &callbacks);

    engine_run(engine);

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------

    const Clock *clk = engine_get_clock(engine);
    if (clk != nullptr) {
        printf("[demo] total frames: %lu  elapsed: %.2fs  avg fps: %.1f\n",
               (unsigned long)clk->frame_count,
               clk->elapsed,
               clk->frame_count > 0 ? (double)clk->frame_count / clk->elapsed : 0.0);
    }

    engine_destroy(engine);

    printf("=== GameEngine shut down cleanly ===\n");
    return EXIT_SUCCESS;
}
