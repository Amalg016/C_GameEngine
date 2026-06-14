#ifndef ENGINE_CORE_SCRIPTING_LUA_HOST_H
#define ENGINE_CORE_SCRIPTING_LUA_HOST_H

#ifdef EDITOR_BUILD
#include <lua5.4/lua.h>
#endif

#include "../ecs/ecs.h"
#include "../asset_manager.h"
#include "../../renderer/renderer.h"

#include <stdbool.h>
#include <stdint.h>

// Forward declarations.
typedef struct Input Input;
typedef struct Engine Engine;

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

// ---------------------------------------------------------------------------
// Script Component — per-entity Lua script attachments.
//
// Each entity can have up to MAX_SCRIPTS_PER_ENTITY scripts attached.
// Each script file returns a module table; per-entity instances inherit
// from the module via __index and hold their own state on `self`.
// ---------------------------------------------------------------------------

#define SCRIPT_PATH_MAX         256
#define MAX_SCRIPTS_PER_ENTITY  8

/// A single script attachment slot.
typedef struct ScriptSlot {
    char path[SCRIPT_PATH_MAX];   // path to the .lua file (for serialization)
    int  instance_ref;            // Lua registry ref to the per-entity instance
    bool initialized;             // true after on_init() has been called
} ScriptSlot;

/// ECS component: holds all scripts attached to an entity.
typedef struct ScriptComponent {
    ScriptSlot slots[MAX_SCRIPTS_PER_ENTITY];
    uint8_t    count;             // number of scripts attached (0..8)
} ScriptComponent;

// --- Lifecycle -------------------------------------------------------------

/// Create a new LuaHost.  Opens the Lua VM and registers C bindings.
/// Returns nullptr on failure.
LuaHost *lua_host_create(Engine           *engine,
                         World            *world,
                         HierarchyContext *hctx,
                         CameraContext    *cam_ctx,
                         AssetManager     *am,
                         Renderer         *renderer,
                         Input            *input);

/// Execute a Lua script file (e.g. "scripts/demo.lua").
/// Returns false if the file cannot be loaded or a parse/runtime error occurs.
bool lua_host_load_script(LuaHost *host, const char *path);

/// Close the Lua VM and free all associated memory.
void lua_host_destroy(LuaHost *host);

// --- Per-frame hooks (global Lua functions) --------------------------------
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

// --- Script Component System -----------------------------------------------
// Per-entity Lua scripts with lifecycle callbacks (on_init, on_update, etc.).

/// Get the ScriptComponent's ComponentId (lazily registers on first call).
ComponentId lua_host_get_script_id(LuaHost *host);
bool        lua_host_script_registered(LuaHost *host);

/// Attach a Lua script to an entity.  Loads the module (if not cached),
/// creates a per-entity instance table, and appends it to the entity's
/// ScriptComponent slot array.  Returns false if the slot limit is reached
/// or the script fails to load.
bool lua_host_attach_script(LuaHost *host, Entity entity,
                            const char *script_path);

/// Detach a specific Lua script from an entity by path.  Calls on_destroy()
/// if defined, releases the Lua registry reference, and compacts the slot
/// array.  If path is nullptr, detaches ALL scripts from the entity.
void lua_host_detach_script(LuaHost *host, Entity entity,
                            const char *script_path);

/// Call on_init() on all script instances that haven't been initialized yet.
void lua_host_scripts_init(LuaHost *host);

/// Call on_update(dt) on all script instances.
void lua_host_scripts_update(LuaHost *host, double dt);

/// Call on_fixed_update(dt) on all script instances.
void lua_host_scripts_fixed_update(LuaHost *host, double dt);

/// Release all script instance Lua references (for scene unload).
/// Does NOT remove the ScriptComponent from the pool — world_clear() does that.
void lua_host_scripts_clear(LuaHost *host);

// --- Editor-only introspection ---------------------------------------------

#ifdef EDITOR_BUILD

/// Get the raw lua_State pointer for read-only introspection (editor only).
/// WARNING: Do not modify Lua state through this pointer; use only for
/// table iteration and value reads in the inspector panel.
lua_State *lua_host_get_state(LuaHost *host);

#endif // EDITOR_BUILD

#endif // ENGINE_CORE_SCRIPTING_LUA_HOST_H
