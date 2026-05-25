#ifndef ENGINE_CORE_ENGINE_H
#define ENGINE_CORE_ENGINE_H

#include "../renderer/renderer.h"   // RendererBackend

#include <stdint.h>
#include <stdbool.h>

// Forward declarations to prevent transitive header pollution (Rule 2)
typedef struct AssetManager AssetManager;
typedef struct Clock Clock;
typedef struct World World;
typedef struct Input Input;
typedef struct Platform Platform;
typedef struct HierarchyContext HierarchyContext;
typedef struct CameraContext CameraContext;
typedef struct LuaHost LuaHost;
typedef uint32_t Entity;
typedef uint8_t ComponentId;

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
[[nodiscard]] Engine *engine_create(const EngineConfig *config);

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

/// Access the engine's ECS world.
World *engine_get_world(Engine *engine);

/// Access the engine's platform (e.g. for framebuffer size queries).
Platform *engine_get_platform(Engine *engine);

/// Access the engine's input state (keyboard, mouse, scroll).
/// Valid after engine_create().  Updated each frame by engine_run().
Input *engine_get_input(Engine *engine);

/// Access the engine's hierarchy context.
/// Lazily initialises on first call.
HierarchyContext *engine_get_hctx(Engine *engine);

/// Access the engine's camera context.
/// Lazily initialises on first call.
CameraContext *engine_get_cam_ctx(Engine *engine);

/// Access the engine's Lua scripting host.
LuaHost *engine_get_lua_host(Engine *engine);

/// Convenience: load a Lua script via the engine's LuaHost.
/// The LuaHost is created lazily on first call.
/// Returns false if the script fails to load.
bool engine_load_script(Engine *engine, const char *path);

/// Load a scene from a JSON file, replacing the current ECS state.
/// Convenience wrapper around scene_load().
bool engine_load_scene(Engine *engine, const char *filepath);

/// Unload the current scene, releasing all asset references and clearing
/// the ECS.  Safe to call even if no scene is loaded.
/// Convenience wrapper around scene_unload().
void engine_unload_scene(Engine *engine);

/// Switch from the current scene to a new one.
/// Unloads the current scene (releasing assets), then loads the new one.
/// This is the primary API for scene transitions at runtime.
/// Returns true on success, false on file/parse errors.
bool engine_switch_scene(Engine *engine, const char *filepath);

/// Save the current ECS state to a JSON scene file.
/// Convenience wrapper around scene_save().
bool engine_save_scene(Engine *engine, const char *filepath);

/// Get the filepath of the currently loaded scene.
/// Returns nullptr if no scene has been loaded.
const char *engine_get_current_scene(const Engine *engine);

#endif // ENGINE_CORE_ENGINE_H
