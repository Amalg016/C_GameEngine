#include "../engine/core/engine.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Application state — passed to every callback via user_data.
// ---------------------------------------------------------------------------

typedef struct AppState {
    Renderer *renderer;
    double    fixed_time;       // total simulated time from fixed updates
    double    frame_time;       // total wall-clock time from variable updates
    uint64_t  fixed_ticks;      // number of fixed updates executed
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

    // Throttled diagnostic — print once per second of sim-time.
    if (app->fixed_ticks % 60 == 0) {
        printf("[fixed_update] sim_time=%.2fs  tick=%lu\n",
               app->fixed_time, (unsigned long)app->fixed_ticks);
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
    // Set up callbacks and run the main loop.
    // -----------------------------------------------------------------------

    AppState app_state = {
        .renderer    = r,
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
