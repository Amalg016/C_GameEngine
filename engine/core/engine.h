#ifndef ENGINE_CORE_ENGINE_H
#define ENGINE_CORE_ENGINE_H

#include "../renderer/renderer.h"   // RendererBackend
#include "asset_manager.h"          // AssetManager, AssetHandle

#include <stdint.h>

// ---------------------------------------------------------------------------
// Engine — owns the full application lifecycle.
//
// The app creates an Engine via engine_create(), hands it a config describing
// window properties and the desired renderer backend, then calls engine_run()
// which blocks until the window is closed.  engine_destroy() tears everything
// down in the correct order.
// ---------------------------------------------------------------------------

/// Application-level configuration — the ONLY thing the app needs to provide.
typedef struct EngineConfig {
    const char     *title;      // window title
    uint32_t        width;      // initial window width
    uint32_t        height;     // initial window height
    RendererBackend backend;    // which renderer to use
} EngineConfig;

/// Opaque engine handle — internals are hidden from the app.
typedef struct Engine Engine;

/// Create the engine: platform + renderer are stood up internally.
/// Returns nullptr on failure.
Engine *engine_create(const EngineConfig *config);

/// Run the main loop.  Blocks until the window is closed.
void engine_run(Engine *engine);

/// Tear down renderer, platform, and free all engine memory.
void engine_destroy(Engine *engine);

/// Access the engine's asset manager (e.g. from app code).
AssetManager *engine_get_asset_manager(Engine *engine);

/// Access the engine's renderer (needed for bind_texture etc.).
Renderer *engine_get_renderer(Engine *engine);

#endif // ENGINE_CORE_ENGINE_H
