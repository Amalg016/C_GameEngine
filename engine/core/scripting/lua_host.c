#include "lua_host.h"
#include "lua_bindings.h"

#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>

#include <stdio.h>
#include <stdlib.h>

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

    // Lazy-registered component IDs for Lua-created component types.
    ComponentId       c_velocity;
    bool              velocity_registered;
    ComponentId       c_sprite;
    bool              sprite_registered;
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
                         Renderer         *renderer) {
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
// Per-frame hooks
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
