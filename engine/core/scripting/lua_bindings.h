#ifndef ENGINE_CORE_SCRIPTING_LUA_BINDINGS_H
#define ENGINE_CORE_SCRIPTING_LUA_BINDINGS_H

// ---------------------------------------------------------------------------
// Internal header — Lua ↔ Engine C bindings.
//
// Registers the global `engine` table in the Lua state with functions for
// entity management, component manipulation, asset loading, etc.
//
// This header is consumed only by lua_host.c; application code should use
// lua_host.h instead.
// ---------------------------------------------------------------------------

#include <lua5.4/lua.h>

// Forward-declare the host so bindings can access engine subsystems.
typedef struct LuaHost LuaHost;

/// Register all `engine.*` bindings into the Lua state.
/// The LuaHost pointer is stored as a light userdata upvalue so each binding
/// function can reach the World, AssetManager, Renderer, etc.
void lua_register_engine_bindings(lua_State *L, LuaHost *host);

#endif // ENGINE_CORE_SCRIPTING_LUA_BINDINGS_H
