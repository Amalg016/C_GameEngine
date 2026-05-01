#include "../engine/core/engine.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// ECS Components
// ---------------------------------------------------------------------------

/// 2D transform — position and size in NDC (-1..1).
typedef struct Transform {
    float x, y;             // current centre position
    float prev_x, prev_y;   // position at the previous fixed step (for interpolation)
    float w, h;             // width and height
} Transform;

/// Velocity — rate of change of position per second.
typedef struct Velocity {
    float dx, dy;
} Velocity;

/// Sprite — references a loaded texture via asset handle.
typedef struct Sprite {
    AssetHandle texture;
} Sprite;

// ---------------------------------------------------------------------------
// Application state — passed to every callback via user_data.
// ---------------------------------------------------------------------------

typedef struct AppState {
    Renderer     *renderer;
    AssetManager *am;
    World        *world;
    ComponentId   c_transform;
    ComponentId   c_velocity;
    ComponentId   c_sprite;
    double        fixed_time;       // total simulated time from fixed updates
    double        frame_time;       // total wall-clock time from variable updates
    uint64_t      fixed_ticks;      // number of fixed updates executed
} AppState;

// ---------------------------------------------------------------------------
// Systems
// ---------------------------------------------------------------------------

/// Movement system: Position += Velocity * dt  (fixed rate)
///
/// Snapshots current position into prev_x/prev_y BEFORE updating, so the
/// render system can interpolate between the two for smooth motion.
static void system_movement(AppState *app, double dt) {
    ComponentPool *vel_pool = world_get_pool(app->world, app->c_velocity);
    if (vel_pool == nullptr) return;

    for (uint32_t i = 0; i < vel_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(vel_pool, i);
        Velocity *vel    = (Velocity *)component_pool_get_dense(vel_pool, i);

        Entity ent = entity_make(ent_idx, 0);
        Transform *t = (Transform *)component_pool_get(
                            world_get_pool(app->world, app->c_transform), ent);

        if (t != nullptr) {
            // Snapshot for interpolation.
            t->prev_x = t->x;
            t->prev_y = t->y;

            t->x += vel->dx * (float)dt;
            t->y += vel->dy * (float)dt;

            // Bounce off NDC edges.
            if (t->x + t->w * 0.5f > 1.0f || t->x - t->w * 0.5f < -1.0f)
                vel->dx = -vel->dx;
            if (t->y + t->h * 0.5f > 1.0f || t->y - t->h * 0.5f < -1.0f)
                vel->dy = -vel->dy;
        }
    }
}

/// Linear interpolation helper.
static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Sprite render system: bind texture + draw_sprite for each entity.
/// Uses `alpha` to interpolate between the previous and current physics
/// position, giving buttery-smooth motion regardless of frame rate.
static void system_sprite_render(AppState *app, float alpha) {
    ComponentPool *sprite_pool = world_get_pool(app->world, app->c_sprite);
    if (sprite_pool == nullptr) return;

    for (uint32_t i = 0; i < sprite_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(sprite_pool, i);
        Sprite *spr      = (Sprite *)component_pool_get_dense(sprite_pool, i);

        Entity ent = entity_make(ent_idx, 0);
        Transform *t = (Transform *)component_pool_get(
                            world_get_pool(app->world, app->c_transform), ent);

        if (t == nullptr) continue;

        // Interpolate between previous and current physics position.
        float render_x = lerpf(t->prev_x, t->x, alpha);
        float render_y = lerpf(t->prev_y, t->y, alpha);

        // Bind the sprite's texture.
        void *gpu_data = asset_manager_get_data(app->am, spr->texture);
        renderer_bind_texture(app->renderer, gpu_data);

        // Draw the sprite at the interpolated position.
        renderer_draw_sprite(app->renderer, render_x, render_y, t->w, t->h);
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

    // Run movement system.
    system_movement(app, dt);

    // Throttled diagnostic — print once per second of sim-time.
    if (app->fixed_ticks % 60 == 0) {
        ComponentPool *t_pool = world_get_pool(app->world, app->c_transform);
        if (t_pool != nullptr && t_pool->count > 0) {
            Transform *t = (Transform *)component_pool_get_dense(t_pool, 0);
            printf("[ecs_demo] sim=%.2fs  sprite pos=(%.2f, %.2f)\n",
                   app->fixed_time, t->x, t->y);
        }
    }
}

/// Called once per frame with the real (variable) frame delta.
static void on_update(void *user_data, double dt) {
    AppState *app = (AppState *)user_data;
    app->frame_time += dt;
    (void)app; // will be used for input/animation later
}

/// Called once per frame, inside begin/end frame.
static void on_render(void *user_data, double alpha) {
    AppState *app = (AppState *)user_data;

    // Run the sprite render system with interpolation alpha.
    system_sprite_render(app, (float)alpha);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    printf("=== GameEngine starting ===\n");

    EngineConfig config = {
        .title   = "GameEngine — Sprite Demo",
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

    AssetManager *am = engine_get_asset_manager(engine);
    Renderer     *r  = engine_get_renderer(engine);
    World        *world = engine_get_world(engine);

    // -----------------------------------------------------------------------
    // Register ECS components
    // -----------------------------------------------------------------------

    ComponentId c_transform = ECS_REGISTER(world, Transform);
    ComponentId c_velocity  = ECS_REGISTER(world, Velocity);
    ComponentId c_sprite    = ECS_REGISTER(world, Sprite);

    // -----------------------------------------------------------------------
    // Load the texture via the asset manager
    // -----------------------------------------------------------------------

    AssetHandle tex = asset_manager_load_texture(am, "assets/images/file.png");
    if (tex == ASSET_HANDLE_INVALID) {
        fprintf(stderr, "failed to load texture\n");
        engine_destroy(engine);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Spawn a sprite entity
    // -----------------------------------------------------------------------

    Entity sprite_entity = world_entity_create(world);

    ECS_ADD(world, sprite_entity, c_transform, Transform, {
        .x = 0.0f, .y = 0.0f,         // centre of screen
        .prev_x = 0.0f, .prev_y = 0.0f,
        .w = 0.5f, .h = 0.5f,         // half the screen
    });

    ECS_ADD(world, sprite_entity, c_velocity, Velocity, {
        .dx = 0.3f, .dy = 0.2f,   // slow drift
    });

    ECS_ADD(world, sprite_entity, c_sprite, Sprite, {
        .texture = tex,
    });

    printf("[demo] spawned sprite entity %u with file.png\n", sprite_entity);

    // -----------------------------------------------------------------------
    // Set up callbacks and run
    // -----------------------------------------------------------------------

    AppState app_state = {
        .renderer    = r,
        .am          = am,
        .world       = world,
        .c_transform = c_transform,
        .c_velocity  = c_velocity,
        .c_sprite    = c_sprite,
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
    // Cleanup
    // -----------------------------------------------------------------------

    const Clock *clk = engine_get_clock(engine);
    if (clk != nullptr) {
        printf("[demo] total frames: %lu  elapsed: %.2fs  avg fps: %.1f\n",
               (unsigned long)clk->frame_count,
               clk->elapsed,
               clk->frame_count > 0 ? (double)clk->frame_count / clk->elapsed : 0.0);
    }

    asset_manager_release(am, tex);
    engine_destroy(engine);

    printf("=== GameEngine shut down cleanly ===\n");
    return EXIT_SUCCESS;
}
