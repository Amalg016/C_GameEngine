#include "lua_host.h"
#include "lua_bindings.h"
#include "../input.h"

#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// LuaHost — owns the lua_State and pointers to engine subsystems.
// ---------------------------------------------------------------------------

struct LuaHost {
    lua_State        *L;
    World            *world;
    HierarchyContext *hctx;
    CameraContext    *cam_ctx;
    AssetManager     *am;
    Renderer         *renderer;
    Input            *input;

    // Lazy-registered component IDs for Lua-created component types.
    ComponentId       c_velocity;
    bool              velocity_registered;
    ComponentId       c_sprite;
    bool              sprite_registered;
    ComponentId       c_script;
    bool              script_registered;
};

// ---------------------------------------------------------------------------
// Internal accessors — used by lua_bindings.c to reach host fields without
// exposing the struct definition in a public header.
// ---------------------------------------------------------------------------

World            *lua_host_get_world(LuaHost *h)          { return h->world; }
HierarchyContext *lua_host_get_hctx(LuaHost *h)           { return h->hctx; }
CameraContext    *lua_host_get_cam_ctx(LuaHost *h)         { return h->cam_ctx; }
AssetManager     *lua_host_get_asset_manager(LuaHost *h)   { return h->am; }
Renderer         *lua_host_get_renderer(LuaHost *h)        { return h->renderer; }
Input            *lua_host_get_input(LuaHost *h)            { return h->input; }

ComponentId lua_host_get_velocity_id(LuaHost *h)           { return h->c_velocity; }
void        lua_host_set_velocity_id(LuaHost *h, ComponentId id) {
    h->c_velocity = id;
    h->velocity_registered = true;
}
bool        lua_host_velocity_registered(LuaHost *h)       { return h->velocity_registered; }

ComponentId lua_host_get_sprite_id(LuaHost *h)             { return h->c_sprite; }
void        lua_host_set_sprite_id(LuaHost *h, ComponentId id) {
    h->c_sprite = id;
    h->sprite_registered = true;
}
bool        lua_host_sprite_registered(LuaHost *h)         { return h->sprite_registered; }

ComponentId lua_host_get_script_id(LuaHost *h)             { return h->c_script; }
bool        lua_host_script_registered(LuaHost *h)         { return h->script_registered; }

// ---------------------------------------------------------------------------
// Module cache — a Lua table stored in the registry, keyed by script path.
// ---------------------------------------------------------------------------

// Unique address used as the registry key for the module cache table.
static const char MODULE_CACHE_KEY = 'M';

/// Push the module cache table onto the Lua stack.  Creates it on first call.
static void push_module_cache(lua_State *L) {
    lua_pushlightuserdata(L, (void *)&MODULE_CACHE_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);  // pop nil
        lua_newtable(L);
        lua_pushlightuserdata(L, (void *)&MODULE_CACHE_KEY);
        lua_pushvalue(L, -2);  // dup table
        lua_rawset(L, LUA_REGISTRYINDEX);
        // table remains on stack
    }
}

/// Load a script module — returns it from cache, or dofile()s it and caches.
/// On success, the module table is on top of the Lua stack.  Returns true.
/// On failure, nothing is pushed.  Returns false.
static bool load_module(lua_State *L, const char *path) {
    push_module_cache(L);  // stack: cache

    lua_getfield(L, -1, path);
    if (!lua_isnil(L, -1)) {
        // Cache hit — module is on top.
        lua_remove(L, -2);  // remove cache table
        return true;
    }
    lua_pop(L, 1);  // pop nil

    // Cache miss — load the file.
    if (luaL_dofile(L, path) != LUA_OK) {
        fprintf(stderr, "[script] error loading '%s': %s\n",
                path, lua_tostring(L, -1));
        lua_pop(L, 2);  // pop error + cache
        return false;
    }

    // The script must return a table.
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "[script] '%s' did not return a table\n", path);
        lua_pop(L, 2);  // pop result + cache
        return false;
    }

    // Cache it: cache[path] = module
    lua_pushvalue(L, -1);               // dup module
    lua_setfield(L, -3, path);          // cache[path] = module
    lua_remove(L, -2);                  // remove cache table

    printf("[script] loaded module: %s\n", path);
    return true;
}

/// Create a per-entity instance table that inherits from a module.
/// Pushes the instance onto the Lua stack and returns its registry ref.
///
/// Instance layout:
///   instance = { entity = <entity_id> }
///   metatable = { __index = module }
static int create_instance(lua_State *L, Entity entity) {
    // Stack top is the module table.
    lua_newtable(L);  // instance

    // instance.entity = entity_id
    lua_pushinteger(L, (lua_Integer)entity);
    lua_setfield(L, -2, "entity");

    // metatable = { __index = module }
    lua_newtable(L);                // metatable
    lua_pushvalue(L, -3);           // push module
    lua_setfield(L, -2, "__index"); // metatable.__index = module
    lua_setmetatable(L, -2);        // setmetatable(instance, metatable)

    // Store in registry and return the ref.
    lua_pushvalue(L, -1);  // dup instance
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_remove(L, -2);  // remove module from below instance
    // instance is now on top of stack
    lua_pop(L, 1);       // pop instance — we have the ref

    return ref;
}

/// Call a method on a script instance.  No arguments beyond `self`.
static void call_instance_void(lua_State *L, int instance_ref,
                               const char *method) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, instance_ref);  // push instance
    lua_getfield(L, -1, method);                       // push method
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);  // pop non-function + instance
        return;
    }
    lua_pushvalue(L, -2);  // push instance as `self`
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "[script] error in %s(): %s\n",
                method, lua_tostring(L, -1));
        lua_pop(L, 1);  // pop error
    }
    lua_pop(L, 1);  // pop instance
}

/// Call a method on a script instance with one numeric argument.
static void call_instance_double(lua_State *L, int instance_ref,
                                 const char *method, double arg) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, instance_ref);  // push instance
    lua_getfield(L, -1, method);                       // push method
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);  // pop non-function + instance
        return;
    }
    lua_pushvalue(L, -2);    // push instance as `self`
    lua_pushnumber(L, arg);  // push dt/alpha
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        fprintf(stderr, "[script] error in %s(): %s\n",
                method, lua_tostring(L, -1));
        lua_pop(L, 1);  // pop error
    }
    lua_pop(L, 1);  // pop instance
}

// ---------------------------------------------------------------------------
// Ensure script component is registered.
// ---------------------------------------------------------------------------

static ComponentId ensure_script_component(LuaHost *host) {
    if (host->script_registered) return host->c_script;
    host->c_script = world_register_component(host->world, sizeof(ScriptComponent));
    host->script_registered = true;
    printf("[script] registered ScriptComponent (id=%u)\n", host->c_script);
    return host->c_script;
}

// ---------------------------------------------------------------------------
// Helper — call a global Lua function by name (no args / with numeric args).
// Errors are caught and logged; they never propagate to the caller.
// ---------------------------------------------------------------------------

static void call_lua_void(lua_State *L, const char *fn_name) {
    lua_getglobal(L, fn_name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);  // not defined — silently skip
        return;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "[lua] error in %s(): %s\n",
                fn_name, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void call_lua_double(lua_State *L, const char *fn_name, double arg) {
    lua_getglobal(L, fn_name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushnumber(L, arg);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "[lua] error in %s(): %s\n",
                fn_name, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LuaHost *lua_host_create(World            *world,
                         HierarchyContext *hctx,
                         CameraContext    *cam_ctx,
                         AssetManager     *am,
                         Renderer         *renderer,
                         Input            *input) {
    if (world == nullptr) {
        fprintf(stderr, "[lua] null world\n");
        return nullptr;
    }

    LuaHost *host = calloc(1, sizeof(LuaHost));
    if (host == nullptr) {
        fprintf(stderr, "[lua] failed to allocate LuaHost\n");
        return nullptr;
    }

    host->world    = world;
    host->hctx     = hctx;
    host->cam_ctx  = cam_ctx;
    host->am       = am;
    host->renderer = renderer;
    host->input    = input;

    // Open Lua VM.
    host->L = luaL_newstate();
    if (host->L == nullptr) {
        fprintf(stderr, "[lua] failed to create lua_State\n");
        free(host);
        return nullptr;
    }

    luaL_openlibs(host->L);

    // Register C bindings.
    lua_register_engine_bindings(host->L, host);

    printf("[lua] host created (Lua %s)\n", LUA_VERSION);
    return host;
}

bool lua_host_load_script(LuaHost *host, const char *path) {
    if (host == nullptr || path == nullptr) return false;

    printf("[lua] loading script: %s\n", path);

    if (luaL_dofile(host->L, path) != LUA_OK) {
        fprintf(stderr, "[lua] error loading '%s': %s\n",
                path, lua_tostring(host->L, -1));
        lua_pop(host->L, 1);
        return false;
    }

    printf("[lua] script loaded successfully\n");
    return true;
}

void lua_host_destroy(LuaHost *host) {
    if (host == nullptr) return;

    if (host->L != nullptr) {
        lua_close(host->L);
    }

    printf("[lua] host destroyed\n");
    free(host);
}

// ---------------------------------------------------------------------------
// Per-frame hooks (global Lua functions)
// ---------------------------------------------------------------------------

void lua_host_on_init(LuaHost *host) {
    if (host == nullptr) return;
    call_lua_void(host->L, "on_init");
}

void lua_host_on_fixed_update(LuaHost *host, double dt) {
    if (host == nullptr) return;
    call_lua_double(host->L, "on_fixed_update", dt);
}

void lua_host_on_update(LuaHost *host, double dt) {
    if (host == nullptr) return;
    call_lua_double(host->L, "on_update", dt);
}

void lua_host_on_render(LuaHost *host, double alpha) {
    if (host == nullptr) return;
    call_lua_double(host->L, "on_render", alpha);
}

// ---------------------------------------------------------------------------
// Script Component System
// ---------------------------------------------------------------------------

bool lua_host_attach_script(LuaHost *host, Entity entity,
                            const char *script_path) {
    if (host == nullptr || entity == ENTITY_INVALID || script_path == nullptr)
        return false;

    ComponentId c_script = ensure_script_component(host);
    lua_State *L = host->L;

    // Load or retrieve the cached module.
    if (!load_module(L, script_path)) return false;
    // Stack: module

    // Create per-entity instance inheriting from the module.
    int ref = create_instance(L, entity);
    // Stack is clean (instance stored in registry).

    // Get or create the ScriptComponent on the entity.
    ScriptComponent *sc = (ScriptComponent *)world_get_component(
        host->world, entity, c_script);

    if (sc == nullptr) {
        // First script on this entity — add the component.
        ScriptComponent new_sc = {0};
        world_add_component(host->world, entity, c_script, &new_sc);
        sc = (ScriptComponent *)world_get_component(
            host->world, entity, c_script);
        if (sc == nullptr) {
            fprintf(stderr, "[script] failed to add ScriptComponent\n");
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            return false;
        }
    }

    // Check slot limit.
    if (sc->count >= MAX_SCRIPTS_PER_ENTITY) {
        fprintf(stderr, "[script] entity %u: max scripts (%d) reached\n",
                (unsigned)entity, MAX_SCRIPTS_PER_ENTITY);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return false;
    }

    // Fill the next slot.
    ScriptSlot *slot = &sc->slots[sc->count];
    strncpy(slot->path, script_path, SCRIPT_PATH_MAX - 1);
    slot->path[SCRIPT_PATH_MAX - 1] = '\0';
    slot->instance_ref = ref;
    slot->initialized  = false;
    sc->count++;

    printf("[script] attached '%s' to entity %u (slot %u)\n",
           script_path, (unsigned)entity, sc->count - 1);
    return true;
}

void lua_host_detach_script(LuaHost *host, Entity entity,
                            const char *script_path) {
    if (host == nullptr || entity == ENTITY_INVALID) return;

    if (!host->script_registered) return;

    ScriptComponent *sc = (ScriptComponent *)world_get_component(
        host->world, entity, host->c_script);
    if (sc == nullptr || sc->count == 0) return;

    lua_State *L = host->L;

    if (script_path == nullptr) {
        // Detach ALL scripts.
        for (uint8_t i = 0; i < sc->count; ++i) {
            if (sc->slots[i].instance_ref != LUA_NOREF) {
                call_instance_void(L, sc->slots[i].instance_ref, "on_destroy");
                luaL_unref(L, LUA_REGISTRYINDEX, sc->slots[i].instance_ref);
            }
        }
        sc->count = 0;
        printf("[script] detached all scripts from entity %u\n",
               (unsigned)entity);
        return;
    }

    // Detach a specific script by path.
    for (uint8_t i = 0; i < sc->count; ++i) {
        if (strcmp(sc->slots[i].path, script_path) != 0) continue;

        // Call on_destroy.
        if (sc->slots[i].instance_ref != LUA_NOREF) {
            call_instance_void(L, sc->slots[i].instance_ref, "on_destroy");
            luaL_unref(L, LUA_REGISTRYINDEX, sc->slots[i].instance_ref);
        }

        // Compact: shift remaining slots down.
        for (uint8_t j = i; j < sc->count - 1; ++j) {
            sc->slots[j] = sc->slots[j + 1];
        }
        sc->count--;

        printf("[script] detached '%s' from entity %u\n",
               script_path, (unsigned)entity);
        return;
    }
}

void lua_host_scripts_init(LuaHost *host) {
    if (host == nullptr || !host->script_registered) return;

    ComponentPool *pool = world_get_pool(host->world, host->c_script);
    if (pool == nullptr) return;

    lua_State *L = host->L;

    for (uint32_t i = 0; i < pool->count; ++i) {
        ScriptComponent *sc = (ScriptComponent *)component_pool_get_dense(pool, i);
        for (uint8_t s = 0; s < sc->count; ++s) {
            if (sc->slots[s].initialized) continue;
            if (sc->slots[s].instance_ref == LUA_NOREF) continue;

            call_instance_void(L, sc->slots[s].instance_ref, "on_init");
            sc->slots[s].initialized = true;
        }
    }
}

void lua_host_scripts_update(LuaHost *host, double dt) {
    if (host == nullptr || !host->script_registered) return;

    ComponentPool *pool = world_get_pool(host->world, host->c_script);
    if (pool == nullptr) return;

    lua_State *L = host->L;

    for (uint32_t i = 0; i < pool->count; ++i) {
        ScriptComponent *sc = (ScriptComponent *)component_pool_get_dense(pool, i);
        for (uint8_t s = 0; s < sc->count; ++s) {
            if (sc->slots[s].instance_ref == LUA_NOREF) continue;
            call_instance_double(L, sc->slots[s].instance_ref, "on_update", dt);
        }
    }
}

void lua_host_scripts_fixed_update(LuaHost *host, double dt) {
    if (host == nullptr || !host->script_registered) return;

    ComponentPool *pool = world_get_pool(host->world, host->c_script);
    if (pool == nullptr) return;

    lua_State *L = host->L;

    for (uint32_t i = 0; i < pool->count; ++i) {
        ScriptComponent *sc = (ScriptComponent *)component_pool_get_dense(pool, i);
        for (uint8_t s = 0; s < sc->count; ++s) {
            if (sc->slots[s].instance_ref == LUA_NOREF) continue;
            call_instance_double(L, sc->slots[s].instance_ref,
                                 "on_fixed_update", dt);
        }
    }
}

void lua_host_scripts_clear(LuaHost *host) {
    if (host == nullptr || !host->script_registered) return;

    ComponentPool *pool = world_get_pool(host->world, host->c_script);
    if (pool == nullptr || pool->count == 0) return;

    lua_State *L = host->L;
    uint32_t total = 0;

    for (uint32_t i = 0; i < pool->count; ++i) {
        ScriptComponent *sc = (ScriptComponent *)component_pool_get_dense(pool, i);
        for (uint8_t s = 0; s < sc->count; ++s) {
            if (sc->slots[s].instance_ref != LUA_NOREF) {
                call_instance_void(L, sc->slots[s].instance_ref, "on_destroy");
                luaL_unref(L, LUA_REGISTRYINDEX, sc->slots[s].instance_ref);
                sc->slots[s].instance_ref = LUA_NOREF;
                total++;
            }
        }
        sc->count = 0;
    }

    // Also clear the module cache so re-loading a scene gets fresh modules.
    lua_pushlightuserdata(L, (void *)&MODULE_CACHE_KEY);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    printf("[script] cleared %u script instance(s)\n", total);
}
