#include "../engine/core/engine.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Demo components
// ---------------------------------------------------------------------------

typedef struct Position {
    float x, y;
} Position;

typedef struct Velocity {
    float dx, dy;
} Velocity;

// ---------------------------------------------------------------------------
// Application state — passed to every callback via user_data.
// ---------------------------------------------------------------------------

typedef struct AppState {
    Renderer     *renderer;
    World        *world;
    ComponentId   c_pos;
    ComponentId   c_vel;
    double        fixed_time;       // total simulated time from fixed updates
    double        frame_time;       // total wall-clock time from variable updates
    uint64_t      fixed_ticks;      // number of fixed updates executed
} AppState;

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

/// Called at a locked rate (default 60 Hz).
/// Physics, collision, deterministic game logic go here.
static void on_fixed_update(void *user_data, double dt) {
    AppState *app = (AppState *)user_data;
    app->fixed_time += dt;
    app->fixed_ticks++;

    // --- ECS movement system: Position += Velocity * dt --------------------
    ComponentPool *vel_pool = world_get_pool(app->world, app->c_vel);
    if (vel_pool == nullptr) return;

    for (uint32_t i = 0; i < vel_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(vel_pool, i);
        Velocity *vel    = (Velocity *)component_pool_get_dense(vel_pool, i);

        // Look up position by entity index (generation 0 for pool lookup).
        Entity ent   = entity_make(ent_idx, 0);
        Position *pos = (Position *)component_pool_get(
                            world_get_pool(app->world, app->c_pos), ent);

        if (pos != nullptr) {
            pos->x += vel->dx * (float)dt;
            pos->y += vel->dy * (float)dt;
        }
    }

    // Throttled diagnostic — print once per second of sim-time.
    if (app->fixed_ticks % 60 == 0) {
        // Print position of entity at dense index 0 as a sample.
        ComponentPool *pos_pool = world_get_pool(app->world, app->c_pos);
        if (pos_pool != nullptr && pos_pool->count > 0) {
            Position *p = (Position *)component_pool_get_dense(pos_pool, 0);
            printf("[ecs_demo] sim=%.2fs  entity_0 pos=(%.1f, %.1f)\n",
                   app->fixed_time, p->x, p->y);
        }
    }
}

/// Called once per frame with the real (variable) frame delta.
/// Input processing, animations, camera smoothing go here.
static void on_update(void *user_data, double dt) {
    AppState *app = (AppState *)user_data;
    app->frame_time += dt;
    (void)app; // will be used for input/animation later
}

/// Called once per frame, inside begin/end frame.
/// `alpha` is the interpolation factor between fixed steps.
static void on_render(void *user_data, double alpha) {
    AppState *app = (AppState *)user_data;
    (void)alpha;  // will be used for position interpolation later

    renderer_draw_quad(app->renderer);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    printf("=== GameEngine starting ===\n");

    EngineConfig config = {
        .title   = "GameEngine",
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

    // Initialise the renderer — GPU is now ready for asset loading.
    if (!engine_init(engine)) {
        fprintf(stderr, "failed to init engine\n");
        engine_destroy(engine);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Demonstrate the asset manager: "load once, use many"
    // -----------------------------------------------------------------------

    AssetManager *am = engine_get_asset_manager(engine);
    Renderer     *r  = engine_get_renderer(engine);

    // First load — this actually loads the PNG from disk into GPU memory.
    AssetHandle tex_a = asset_manager_load_texture(am, "assets/images/file.png");
    if (tex_a == ASSET_HANDLE_INVALID) {
        fprintf(stderr, "failed to load texture\n");
    }

    // Second load of the SAME path — cache hit, no GPU upload, ref bumped.
    AssetHandle tex_b = asset_manager_load_texture(am, "assets/images/file.png");

    printf("[demo] tex_a = %u, tex_b = %u (same handle: %s)\n",
           tex_a, tex_b, tex_a == tex_b ? "YES ✓" : "NO ✗");
    printf("[demo] ref_count = %u (expected: 2)\n",
           asset_manager_get_ref_count(am, tex_a));

    // Bind the texture so the renderer uses it for drawing.
    void *gpu_data = asset_manager_get_data(am, tex_a);
    renderer_bind_texture(r, gpu_data);

    // Release one reference — texture stays alive (ref_count → 1).
    asset_manager_release(am, tex_b);
    printf("[demo] after one release: ref_count = %u (expected: 1)\n",
           asset_manager_get_ref_count(am, tex_a));

    // -----------------------------------------------------------------------
    // Demonstrate the ECS: register components, spawn entities
    // -----------------------------------------------------------------------

    World *world = engine_get_world(engine);

    ComponentId c_pos = ECS_REGISTER(world, Position);
    ComponentId c_vel = ECS_REGISTER(world, Velocity);

    // Spawn a few entities with position + velocity.
    for (int i = 0; i < 5; ++i) {
        Entity e = world_entity_create(world);

        ECS_ADD(world, e, c_pos, Position,
                { .x = (float)(i * 10), .y = (float)(i * 5) });
        ECS_ADD(world, e, c_vel, Velocity,
                { .dx = (float)(i + 1), .dy = (float)(i + 1) * 0.5f });

        printf("[ecs_demo] spawned entity %u  pos=(%.0f, %.0f)  vel=(%.0f, %.1f)\n",
               e,
               ((Position *)world_get_component(world, e, c_pos))->x,
               ((Position *)world_get_component(world, e, c_pos))->y,
               ((Velocity *)world_get_component(world, e, c_vel))->dx,
               ((Velocity *)world_get_component(world, e, c_vel))->dy);
    }

    // -----------------------------------------------------------------------
    // Set up callbacks and run the main loop.
    // -----------------------------------------------------------------------

    AppState app_state = {
        .renderer    = r,
        .world       = world,
        .c_pos       = c_pos,
        .c_vel       = c_vel,
        .fixed_time  = 0.0,
        .frame_time  = 0.0,
        .fixed_ticks = 0,
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
    // Print final timing stats from the clock.
    // -----------------------------------------------------------------------

    const Clock *clk = engine_get_clock(engine);
    if (clk != nullptr) {
        printf("[demo] total frames: %lu  elapsed: %.2fs  avg fps: %.1f\n",
               (unsigned long)clk->frame_count,
               clk->elapsed,
               clk->frame_count > 0 ? (double)clk->frame_count / clk->elapsed : 0.0);
    }

    // Release the last reference — GPU resources freed.
    asset_manager_release(am, tex_a);

    engine_destroy(engine);

    printf("=== GameEngine shut down cleanly ===\n");
    return EXIT_SUCCESS;
}
