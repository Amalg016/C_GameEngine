#ifndef ENGINE_CORE_SCRIPTING_LUA_HOST_H
#define ENGINE_CORE_SCRIPTING_LUA_HOST_H

#include "../ecs/ecs.h"
#include "../asset_manager.h"
#include "../../renderer/renderer.h"

#include <stdbool.h>

// ---------------------------------------------------------------------------
// LuaHost — owns the Lua VM and provides the bridge between C and Lua.
//
// The host manages a single lua_State.  After creation, call
// lua_host_load_script() to execute a Lua file, then invoke the per-frame
// hooks (on_init, on_update, etc.) from the engine main loop.
//
// A global table `engine` is registered in the Lua namespace with bindings
// for entity creation, component manipulation, asset loading, etc.
//
// Usage:
//   LuaHost *lua = lua_host_create(world, &hctx, &cam_ctx, am, renderer);
//   lua_host_load_script(lua, "scripts/demo.lua");
//   lua_host_on_init(lua);             // calls Lua on_init() if defined
//   // ... in the main loop:
//   lua_host_on_fixed_update(lua, dt);
//   lua_host_on_update(lua, dt);
//   lua_host_on_render(lua, alpha);
//   // ... on shutdown:
//   lua_host_destroy(lua);
// ---------------------------------------------------------------------------

/// Opaque handle — internals are hidden.
typedef struct LuaHost LuaHost;

// --- Lifecycle -------------------------------------------------------------

/// Create a new LuaHost.  Opens the Lua VM and registers C bindings.
/// Returns nullptr on failure.
LuaHost *lua_host_create(World            *world,
                         HierarchyContext *hctx,
                         CameraContext    *cam_ctx,
                         AssetManager     *am,
                         Renderer         *renderer);

/// Execute a Lua script file (e.g. "scripts/demo.lua").
/// Returns false if the file cannot be loaded or a parse/runtime error occurs.
bool lua_host_load_script(LuaHost *host, const char *path);

/// Close the Lua VM and free all associated memory.
void lua_host_destroy(LuaHost *host);

// --- Per-frame hooks -------------------------------------------------------
// Each of these calls the corresponding global Lua function if it exists.
// Errors are caught and printed to stderr; they never propagate to the caller.

/// Call Lua `on_init()`.  Typically once, after loading the script.
void lua_host_on_init(LuaHost *host);

/// Call Lua `on_fixed_update(dt)`.  Fixed-rate physics/logic.
void lua_host_on_fixed_update(LuaHost *host, double dt);

/// Call Lua `on_update(dt)`.  Per-frame, variable delta.
void lua_host_on_update(LuaHost *host, double dt);

/// Call Lua `on_render(alpha)`.  Inside begin/end frame.
void lua_host_on_render(LuaHost *host, double alpha);

#endif // ENGINE_CORE_SCRIPTING_LUA_HOST_H
