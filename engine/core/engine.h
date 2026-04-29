#ifndef ENGINE_CORE_ENGINE_H
#define ENGINE_CORE_ENGINE_H

#include "../renderer/renderer.h"   // RendererBackend
#include "asset_manager.h"          // AssetManager, AssetHandle
#include "clock.h"                  // Clock

#include <stdint.h>

// ---------------------------------------------------------------------------
// Engine — owns the full application lifecycle.
//
// Usage:
//   1. engine_create()  — creates platform + renderer (not yet initialised)
//   2. engine_init()    — initialises the renderer (GPU ready for asset loading)
//   3. load assets via engine_get_asset_manager()
//   4. engine_set_callbacks() — register update/render hooks
//   5. engine_run()     — blocks in the main loop until window close
//   6. release assets
//   7. engine_destroy() — tears everything down
// ---------------------------------------------------------------------------

/// Application-level configuration — the ONLY thing the app needs to provide.
typedef struct EngineConfig {
    const char     *title;      // window title
    uint32_t        width;      // initial window width
    uint32_t        height;     // initial window height
    RendererBackend backend;    // which renderer to use
} EngineConfig;

/// Application callbacks — set these before calling engine_run().
///
/// on_fixed_update — called at a locked rate (default 60 Hz).
///                   Physics, collision, deterministic game logic.
///                   `dt` is always the fixed timestep (e.g. 1/60).
///
/// on_update       — called once per frame with the real frame delta.
///                   Input processing, animations, camera smoothing.
///
/// on_render       — called once per frame, inside begin/end frame.
///                   `alpha` (0.0–1.0) is the interpolation factor between
///                   the previous and current fixed step — use it to
///                   interpolate positions for buttery-smooth rendering.
typedef struct EngineCallbacks {
    void *user_data;                                        // forwarded to every callback
    void (*on_fixed_update)(void *user_data, double dt);    // fixed rate
    void (*on_update)(void *user_data, double dt);          // per-frame, variable dt
    void (*on_render)(void *user_data, double alpha);       // inside begin/end frame
} EngineCallbacks;

/// Opaque engine handle — internals are hidden from the app.
typedef struct Engine Engine;

/// Create the engine: platform + renderer are stood up internally.
/// Returns nullptr on failure.
Engine *engine_create(const EngineConfig *config);

/// Initialise the renderer backend (GPU resources become available).
/// Must be called before loading assets or calling engine_run().
/// Returns false on failure.
bool engine_init(Engine *engine);

/// Register application callbacks.  Must be called before engine_run().
void engine_set_callbacks(Engine *engine, const EngineCallbacks *callbacks);

/// Set the fixed update rate in Hz (default: 60).  Call before engine_run().
void engine_set_fixed_timestep(Engine *engine, double hz);

/// Run the main loop.  Blocks until the window is closed.
/// engine_init() must have been called first.
void engine_run(Engine *engine);

/// Tear down renderer, platform, and free all engine memory.
void engine_destroy(Engine *engine);

/// Access the engine's asset manager (e.g. from app code).
AssetManager *engine_get_asset_manager(Engine *engine);

/// Access the engine's renderer (needed for bind_texture etc.).
Renderer *engine_get_renderer(Engine *engine);

/// Access the engine's clock (read-only, for diagnostics / debug overlays).
const Clock *engine_get_clock(const Engine *engine);

#endif // ENGINE_CORE_ENGINE_H
