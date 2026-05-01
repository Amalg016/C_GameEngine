#include "../engine/core/engine.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// App-defined ECS Components
// ---------------------------------------------------------------------------

/// Velocity — rate of change of local position per second.
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
    Renderer         *renderer;
    AssetManager     *am;
    World            *world;
    HierarchyContext  hctx;          // hierarchy ComponentIds
    ComponentId       c_velocity;
    ComponentId       c_sprite;
    double            fixed_time;
    double            frame_time;
    uint64_t          fixed_ticks;
} AppState;

// ---------------------------------------------------------------------------
// Systems
// ---------------------------------------------------------------------------

/// Movement system: LocalTransform.pos += Velocity * dt  (fixed rate)
/// Only moves root/individual entities — children follow via hierarchy.
static void system_movement(AppState *app, double dt) {
    ComponentPool *vel_pool = world_get_pool(app->world, app->c_velocity);
    if (vel_pool == nullptr) return;

    for (uint32_t i = 0; i < vel_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(vel_pool, i);
        Velocity *vel    = (Velocity *)component_pool_get_dense(vel_pool, i);

        Entity ent = entity_make(ent_idx, 0);
        LocalTransform *lt = (LocalTransform *)world_get_component(
            app->world, ent, app->hctx.c_local_transform);

        if (lt == nullptr) continue;

        lt->x += vel->dx * (float)dt;
        lt->y += vel->dy * (float)dt;

        // Bounce off NDC edges.
        // Use the just-updated local position (which equals world pos for
        // root entities) — NOT the stale WorldTransform.
        float half_w = lt->sx * 0.5f;
        float half_h = lt->sy * 0.5f;

        if (lt->x + half_w > 1.0f) {
            lt->x = 1.0f - half_w;      // clamp to boundary
            vel->dx = -vel->dx;
        } else if (lt->x - half_w < -1.0f) {
            lt->x = -1.0f + half_w;
            vel->dx = -vel->dx;
        }

        if (lt->y + half_h > 1.0f) {
            lt->y = 1.0f - half_h;
            vel->dy = -vel->dy;
        } else if (lt->y - half_h < -1.0f) {
            lt->y = -1.0f + half_h;
            vel->dy = -vel->dy;
        }
    }
}

/// Linear interpolation helper.
static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Sprite render system: bind texture + draw at interpolated world position.
static void system_sprite_render(AppState *app, float alpha) {
    ComponentPool *sprite_pool = world_get_pool(app->world, app->c_sprite);
    if (sprite_pool == nullptr) return;

    for (uint32_t i = 0; i < sprite_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(sprite_pool, i);
        Sprite *spr      = (Sprite *)component_pool_get_dense(sprite_pool, i);

        Entity ent = entity_make(ent_idx, 0);

        // Use world transform for the current position.
        WorldTransform *wt = (WorldTransform *)world_get_component(
            app->world, ent, app->hctx.c_world_transform);
        if (wt == nullptr) continue;

        // Interpolate with previous position if available.
        float render_x = wt->x;
        float render_y = wt->y;

        PreviousPosition *pp = (PreviousPosition *)world_get_component(
            app->world, ent, app->hctx.c_prev_position);
        if (pp != nullptr) {
            render_x = lerpf(pp->x, wt->x, alpha);
            render_y = lerpf(pp->y, wt->y, alpha);
        }

        // Bind the sprite's texture.
        void *gpu_data = asset_manager_get_data(app->am, spr->texture);
        renderer_bind_texture(app->renderer, gpu_data);

        // Draw at interpolated world position with world scale.
        renderer_draw_sprite(app->renderer, render_x, render_y, wt->sx, wt->sy);
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
    hierarchy_snapshot_positions(app->world, &app->hctx);

    // 2. Run movement (modifies LocalTransform).
    system_movement(app, dt);

    // 3. Propagate transforms (LocalTransform → WorldTransform).
    hierarchy_update_transforms(app->world, &app->hctx);

    // Throttled diagnostic.
    if (app->fixed_ticks % 60 == 0) {
        printf("[demo] sim=%.2fs\n", app->fixed_time);
    }
}

/// Called once per frame with the real (variable) frame delta.
static void on_update(void *user_data, double dt) {
    AppState *app = (AppState *)user_data;
    app->frame_time += dt;
    (void)app;
}

/// Called once per frame, inside begin/end frame.
static void on_render(void *user_data, double alpha) {
    AppState *app = (AppState *)user_data;
    system_sprite_render(app, (float)alpha);
}

// ---------------------------------------------------------------------------
// Helper — spawn a sprite entity with LocalTransform + WorldTransform +
//          PreviousPosition + Sprite.
// ---------------------------------------------------------------------------

static Entity spawn_sprite(World *world, const HierarchyContext *hctx,
                            ComponentId c_sprite,
                            float x, float y, float sx, float sy,
                            AssetHandle texture) {
    Entity e = world_entity_create(world);

    LocalTransform lt = { .x = x, .y = y, .sx = sx, .sy = sy };
    world_add_component(world, e, hctx->c_local_transform, &lt);

    WorldTransform wt = { .x = x, .y = y, .sx = sx, .sy = sy };
    world_add_component(world, e, hctx->c_world_transform, &wt);

    PreviousPosition pp = { .x = x, .y = y };
    world_add_component(world, e, hctx->c_prev_position, &pp);

    Sprite spr = { .texture = texture };
    world_add_component(world, e, c_sprite, &spr);

    return e;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    printf("=== GameEngine starting ===\n");

    EngineConfig config = {
        .title   = "GameEngine — Hierarchy Demo",
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

    AssetManager *am    = engine_get_asset_manager(engine);
    Renderer     *r     = engine_get_renderer(engine);
    World        *world = engine_get_world(engine);

    // -----------------------------------------------------------------------
    // Initialise hierarchy + register app components
    // -----------------------------------------------------------------------

    HierarchyContext hctx = hierarchy_init(world);
    ComponentId c_velocity = ECS_REGISTER(world, Velocity);
    ComponentId c_sprite   = ECS_REGISTER(world, Sprite);

    // -----------------------------------------------------------------------
    // Load texture
    // -----------------------------------------------------------------------

    AssetHandle tex = asset_manager_load_texture(am, "assets/images/file.png");
    if (tex == ASSET_HANDLE_INVALID) {
        fprintf(stderr, "failed to load texture\n");
        engine_destroy(engine);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Spawn parent + child entities
    // -----------------------------------------------------------------------

    // Parent: bouncing sprite at centre, 0.4×0.4 NDC.
    Entity parent = spawn_sprite(world, &hctx, c_sprite,
                                  0.0f, 0.0f, 0.4f, 0.4f, tex);

    // Give parent a velocity so it bounces.
    Velocity parent_vel = { .dx = 0.3f, .dy = 0.2f };
    world_add_component(world, parent, c_velocity, &parent_vel);

    // Child: smaller sprite attached to parent with a local offset.
    // Position (0.8, 0.6) is relative to parent, scale (0.5, 0.5) means
    // half the parent's size.
    Entity child = spawn_sprite(world, &hctx, c_sprite,
                                 0.8f, 0.6f, 0.5f, 0.5f, tex);

    // Attach child to parent.
    hierarchy_set_parent(world, &hctx, child, parent);

    // Do an initial propagation so WorldTransform is correct for frame 0.
    hierarchy_update_transforms(world, &hctx);

    printf("[demo] parent=%u  child=%u (attached)\n", parent, child);

    // -----------------------------------------------------------------------
    // Set up callbacks and run
    // -----------------------------------------------------------------------

    AppState app_state = {
        .renderer    = r,
        .am          = am,
        .world       = world,
        .hctx        = hctx,
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
