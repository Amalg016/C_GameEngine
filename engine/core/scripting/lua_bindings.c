#include "lua_bindings.h"
#include "lua_host.h"

#include "../ecs/ecs.h"
#include "../asset_manager.h"
#include "../input.h"
#include "../../renderer/renderer.h"

#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// LuaHost struct — defined in lua_host.c, forward-declared here.
// We need access to the subsystem pointers stored in the host.
// ---------------------------------------------------------------------------

// The LuaHost struct layout is private to lua_host.c.  We access its fields
// through a small set of internal accessors defined at the bottom of this
// file.  To avoid exposing the struct in the public header, we declare the
// accessors here as extern and implement them in lua_host.c.

extern World            *lua_host_get_world(LuaHost *host);
extern HierarchyContext *lua_host_get_hctx(LuaHost *host);
extern CameraContext    *lua_host_get_cam_ctx(LuaHost *host);
extern AssetManager     *lua_host_get_asset_manager(LuaHost *host);
extern Renderer         *lua_host_get_renderer(LuaHost *host);
extern ComponentId       lua_host_get_velocity_id(LuaHost *host);
extern void              lua_host_set_velocity_id(LuaHost *host, ComponentId id);
extern ComponentId       lua_host_get_sprite_id(LuaHost *host);
extern void              lua_host_set_sprite_id(LuaHost *host, ComponentId id);
extern bool              lua_host_velocity_registered(LuaHost *host);
extern bool              lua_host_sprite_registered(LuaHost *host);
extern Input            *lua_host_get_input(LuaHost *host);

// ---------------------------------------------------------------------------
// Application-level Sprite component.  Mirrors the definition in main.c.
// ---------------------------------------------------------------------------
typedef struct LuaSprite {
    AssetHandle texture;
} LuaSprite;

// ---------------------------------------------------------------------------
// Helper — retrieve the LuaHost* from the first upvalue.
// ---------------------------------------------------------------------------

static LuaHost *get_host(lua_State *L) {
    return (LuaHost *)lua_touserdata(L, lua_upvalueindex(1));
}

// ---------------------------------------------------------------------------
// engine.log(msg)
// ---------------------------------------------------------------------------

static int l_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    printf("%s\n", msg);
    return 0;
}

// ---------------------------------------------------------------------------
// engine.create_entity() → entity_id (integer)
// ---------------------------------------------------------------------------

static int l_create_entity(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    Entity e = world_entity_create(world);
    if (e == ENTITY_INVALID) {
        return luaL_error(L, "engine.create_entity: failed to create entity");
    }
    lua_pushinteger(L, (lua_Integer)e);
    return 1;
}

// ---------------------------------------------------------------------------
// engine.destroy_entity(entity_id)
// ---------------------------------------------------------------------------

static int l_destroy_entity(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    Entity e = (Entity)luaL_checkinteger(L, 1);

    // 1. Detach all scripts attached to this entity first.
    // This executes on_destroy() and cleanly releases Lua registry references.
    lua_host_detach_script(host, e, nullptr);

    // 2. Release Sprite component's texture reference if it exists.
    if (lua_host_sprite_registered(host)) {
        ComponentId c_sprite = lua_host_get_sprite_id(host);
        LuaSprite *spr = (LuaSprite *)world_get_component(world, e, c_sprite);
        if (spr != nullptr && spr->texture != ASSET_HANDLE_INVALID) {
            AssetManager *am = lua_host_get_asset_manager(host);
            asset_manager_release(am, spr->texture);
        }
    }

    // 3. Destroy the entity in the ECS world.
    world_entity_destroy(world, e);
    return 0;
}

// ---------------------------------------------------------------------------
// engine.set_transform(entity_id, x, y, sx, sy)
//
// Adds or updates a LocalTransform + WorldTransform on the entity.
// ---------------------------------------------------------------------------

static int l_set_transform(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    HierarchyContext *hctx = lua_host_get_hctx(host);

    Entity e = (Entity)luaL_checkinteger(L, 1);
    float x  = (float)luaL_checknumber(L, 2);
    float y  = (float)luaL_checknumber(L, 3);
    float sx = (float)luaL_checknumber(L, 4);
    float sy = (float)luaL_checknumber(L, 5);

    // Add or update LocalTransform.
    LocalTransform *lt = (LocalTransform *)world_get_component(
        world, e, hctx->c_local_transform);
    if (lt != nullptr) {
        lt->x = x;  lt->y = y;  lt->sx = sx;  lt->sy = sy;
    } else {
        LocalTransform new_lt = { .x = x, .y = y, .sx = sx, .sy = sy };
        world_add_component(world, e, hctx->c_local_transform, &new_lt);
    }

    // Ensure WorldTransform exists.
    if (!world_has_component(world, e, hctx->c_world_transform)) {
        WorldTransform wt = { .x = x, .y = y, .sx = sx, .sy = sy };
        world_add_component(world, e, hctx->c_world_transform, &wt);
    }

    // Ensure PreviousPosition exists.
    if (!world_has_component(world, e, hctx->c_prev_position)) {
        PreviousPosition pp = { .x = x, .y = y };
        world_add_component(world, e, hctx->c_prev_position, &pp);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// engine.get_transform(entity_id) → x, y, sx, sy
// ---------------------------------------------------------------------------

static int l_get_transform(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    HierarchyContext *hctx = lua_host_get_hctx(host);

    Entity e = (Entity)luaL_checkinteger(L, 1);
    LocalTransform *lt = (LocalTransform *)world_get_component(
        world, e, hctx->c_local_transform);
    if (lt == nullptr) {
        return luaL_error(L, "engine.get_transform: entity %u has no LocalTransform",
                          (unsigned)e);
    }

    lua_pushnumber(L, lt->x);
    lua_pushnumber(L, lt->y);
    lua_pushnumber(L, lt->sx);
    lua_pushnumber(L, lt->sy);
    return 4;
}

// ---------------------------------------------------------------------------
// engine.set_parent(child_id, parent_id)
// ---------------------------------------------------------------------------

static int l_set_parent(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    HierarchyContext *hctx = lua_host_get_hctx(host);

    Entity child  = (Entity)luaL_checkinteger(L, 1);
    Entity parent = (Entity)luaL_checkinteger(L, 2);
    hierarchy_set_parent(world, hctx, child, parent);
    return 0;
}

// ---------------------------------------------------------------------------
// engine.load_texture(path) → asset_handle (integer)
// ---------------------------------------------------------------------------

static int l_load_texture(lua_State *L) {
    LuaHost *host = get_host(L);
    AssetManager *am = lua_host_get_asset_manager(host);

    const char *path = luaL_checkstring(L, 1);
    AssetHandle h = asset_manager_load_texture(am, path);
    if (h == ASSET_HANDLE_INVALID) {
        return luaL_error(L, "engine.load_texture: failed to load '%s'", path);
    }
    lua_pushinteger(L, (lua_Integer)h);
    return 1;
}

// ---------------------------------------------------------------------------
// engine.release_texture(asset_handle)
// ---------------------------------------------------------------------------
static int l_release_texture(lua_State *L) {
    LuaHost *host = get_host(L);
    AssetManager *am = lua_host_get_asset_manager(host);

    AssetHandle h = (AssetHandle)luaL_checkinteger(L, 1);
    if (h != ASSET_HANDLE_INVALID) {
        asset_manager_release(am, h);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// engine.set_sprite(entity_id, asset_handle)
//
// The Sprite component type is registered lazily on first use so we don't
// require the app to pre-register it.
// ---------------------------------------------------------------------------

/// Application-level Sprite component (defined at the top of this file).

static int l_set_sprite(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);

    Entity e        = (Entity)luaL_checkinteger(L, 1);
    AssetHandle tex = (AssetHandle)luaL_checkinteger(L, 2);

    // Lazy-register the Sprite component on first use.
    if (!lua_host_sprite_registered(host)) {
        ComponentId id = world_register_component(world, sizeof(LuaSprite));
        lua_host_set_sprite_id(host, id);
    }

    ComponentId c_sprite = lua_host_get_sprite_id(host);
    LuaSprite spr = { .texture = tex };

    LuaSprite *existing = (LuaSprite *)world_get_component(world, e, c_sprite);
    if (existing != nullptr) {
        existing->texture = tex;
    } else {
        world_add_component(world, e, c_sprite, &spr);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// engine.set_velocity(entity_id, dx, dy)
// ---------------------------------------------------------------------------

typedef struct LuaVelocity {
    float dx, dy;
} LuaVelocity;

static int l_set_velocity(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);

    Entity e   = (Entity)luaL_checkinteger(L, 1);
    float dx   = (float)luaL_checknumber(L, 2);
    float dy   = (float)luaL_checknumber(L, 3);

    // Lazy-register.
    if (!lua_host_velocity_registered(host)) {
        ComponentId id = world_register_component(world, sizeof(LuaVelocity));
        lua_host_set_velocity_id(host, id);
    }

    ComponentId c_vel = lua_host_get_velocity_id(host);
    LuaVelocity vel = { .dx = dx, .dy = dy };

    LuaVelocity *existing = (LuaVelocity *)world_get_component(world, e, c_vel);
    if (existing != nullptr) {
        existing->dx = dx;
        existing->dy = dy;
    } else {
        world_add_component(world, e, c_vel, &vel);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// engine.set_camera_ortho(entity_id, ortho_size)
// ---------------------------------------------------------------------------

static int l_set_camera_ortho(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    CameraContext *cam_ctx = lua_host_get_cam_ctx(host);

    Entity e       = (Entity)luaL_checkinteger(L, 1);
    float size     = (float)luaL_checknumber(L, 2);

    Camera cam = camera_default_ortho(size);
    Camera *existing = (Camera *)world_get_component(world, e, cam_ctx->c_camera);
    if (existing != nullptr) {
        *existing = cam;
    } else {
        world_add_component(world, e, cam_ctx->c_camera, &cam);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// engine.set_camera_perspective(entity_id, fov_degrees)
// ---------------------------------------------------------------------------

static int l_set_camera_perspective(lua_State *L) {
    LuaHost *host = get_host(L);
    World *world = lua_host_get_world(host);
    CameraContext *cam_ctx = lua_host_get_cam_ctx(host);

    Entity e     = (Entity)luaL_checkinteger(L, 1);
    float fov_deg = (float)luaL_checknumber(L, 2);
    float fov_rad = fov_deg * 3.14159265f / 180.0f;

    Camera cam = camera_default_perspective(fov_rad);
    Camera *existing = (Camera *)world_get_component(world, e, cam_ctx->c_camera);
    if (existing != nullptr) {
        *existing = cam;
    } else {
        world_add_component(world, e, cam_ctx->c_camera, &cam);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// engine.add_script(entity_id, path)
// ---------------------------------------------------------------------------

static int l_add_script(lua_State *L) {
    LuaHost *host = get_host(L);
    Entity e      = (Entity)luaL_checkinteger(L, 1);
    const char *path = luaL_checkstring(L, 2);

    if (!lua_host_attach_script(host, e, path)) {
        return luaL_error(L, "engine.add_script: failed to attach '%s'", path);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// engine.remove_script(entity_id, path)
// ---------------------------------------------------------------------------

static int l_remove_script(lua_State *L) {
    LuaHost *host = get_host(L);
    Entity e      = (Entity)luaL_checkinteger(L, 1);
    const char *path = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;

    lua_host_detach_script(host, e, path);
    return 0;
}

// ---------------------------------------------------------------------------
// Input bindings
// ---------------------------------------------------------------------------

/// engine.is_key_down(keycode) → boolean
static int l_is_key_down(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input != nullptr && input_is_game_active(input) && input_key_down(input, key));
    return 1;
}

/// engine.is_key_pressed(keycode) → boolean
static int l_is_key_pressed(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input != nullptr && input_is_game_active(input) && input_key_pressed(input, key));
    return 1;
}

/// engine.is_key_released(keycode) → boolean
static int l_is_key_released(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input != nullptr && input_is_game_active(input) && input_key_released(input, key));
    return 1;
}

/// engine.is_button_down(button) → boolean
static int l_is_button_down(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input != nullptr && input_button_down(input, btn));
    return 1;
}

/// engine.is_button_pressed(button) → boolean
static int l_is_button_pressed(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input != nullptr && input_button_pressed(input, btn));
    return 1;
}

/// engine.get_mouse_pos() → x, y
static int l_get_mouse_pos(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    if (input == nullptr) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
    } else {
        lua_pushnumber(L, input->mouse_x);
        lua_pushnumber(L, input->mouse_y);
    }
    return 2;
}

/// engine.get_mouse_delta() → dx, dy
static int l_get_mouse_delta(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    if (input == nullptr) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
    } else {
        lua_pushnumber(L, input->mouse_dx);
        lua_pushnumber(L, input->mouse_dy);
    }
    return 2;
}

/// engine.get_scroll_delta() → dx, dy
static int l_get_scroll_delta(lua_State *L) {
    Input *input = lua_host_get_input(get_host(L));
    if (input == nullptr) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
    } else {
        lua_pushnumber(L, input->scroll_dx);
        lua_pushnumber(L, input->scroll_dy);
    }
    return 2;
}

// ---------------------------------------------------------------------------
// Registration table
// ---------------------------------------------------------------------------

static const luaL_Reg engine_funcs[] = {
    { "log",                    l_log                   },
    { "create_entity",          l_create_entity         },
    { "destroy_entity",         l_destroy_entity        },
    { "set_transform",          l_set_transform         },
    { "get_transform",          l_get_transform         },
    { "set_parent",             l_set_parent            },
    { "load_texture",           l_load_texture          },
    { "release_texture",        l_release_texture       },
    { "set_sprite",             l_set_sprite            },
    { "set_velocity",           l_set_velocity          },
    { "set_camera_ortho",       l_set_camera_ortho      },
    { "set_camera_perspective", l_set_camera_perspective },
    { "add_script",             l_add_script            },
    { "remove_script",          l_remove_script         },
    { "is_key_down",            l_is_key_down           },
    { "is_key_pressed",         l_is_key_pressed        },
    { "is_key_released",        l_is_key_released       },
    { "is_button_down",         l_is_button_down        },
    { "is_button_pressed",      l_is_button_pressed     },
    { "get_mouse_pos",          l_get_mouse_pos         },
    { "get_mouse_delta",        l_get_mouse_delta       },
    { "get_scroll_delta",       l_get_scroll_delta      },
    { nullptr,                  nullptr                 },
};

// ---------------------------------------------------------------------------
// lua_register_engine_bindings — push the `engine` global table.
// ---------------------------------------------------------------------------

void lua_register_engine_bindings(lua_State *L, LuaHost *host) {
    lua_newtable(L);  // the `engine` table

    // Register each function with `host` as an upvalue.
    for (const luaL_Reg *fn = engine_funcs; fn->name != nullptr; ++fn) {
        lua_pushlightuserdata(L, host);          // upvalue 1
        lua_pushcclosure(L, fn->func, 1);        // closure(fn, host)
        lua_setfield(L, -2, fn->name);           // engine[name] = closure
    }

    // Register key constants as engine.key.*
    // Allows Lua scripts to write:  engine.is_key_down(engine.key.W)
    lua_newtable(L);  // engine.key
    struct { const char *name; int value; } key_constants[] = {
        { "SPACE",      32  }, { "APOSTROPHE", 39  },
        { "COMMA",      44  }, { "MINUS",      45  },
        { "PERIOD",     46  }, { "SLASH",      47  },
        { "_0", 48 }, { "_1", 49 }, { "_2", 50 }, { "_3", 51 }, { "_4", 52 },
        { "_5", 53 }, { "_6", 54 }, { "_7", 55 }, { "_8", 56 }, { "_9", 57 },
        { "A", 65 }, { "B", 66 }, { "C", 67 }, { "D", 68 }, { "E", 69 },
        { "F", 70 }, { "G", 71 }, { "H", 72 }, { "I", 73 }, { "J", 74 },
        { "K", 75 }, { "L", 76 }, { "M", 77 }, { "N", 78 }, { "O", 79 },
        { "P", 80 }, { "Q", 81 }, { "R", 82 }, { "S", 83 }, { "T", 84 },
        { "U", 85 }, { "V", 86 }, { "W", 87 }, { "X", 88 }, { "Y", 89 },
        { "Z", 90 },
        { "ESCAPE",     256 }, { "ENTER",      257 }, { "TAB",        258 },
        { "BACKSPACE",  259 }, { "INSERT",     260 }, { "DELETE",     261 },
        { "RIGHT",      262 }, { "LEFT",       263 }, { "DOWN",       264 },
        { "UP",         265 }, { "PAGE_UP",    266 }, { "PAGE_DOWN",  267 },
        { "HOME",       268 }, { "END",        269 }, { "CAPS_LOCK",  280 },
        { "F1",  290 }, { "F2",  291 }, { "F3",  292 }, { "F4",  293 },
        { "F5",  294 }, { "F6",  295 }, { "F7",  296 }, { "F8",  297 },
        { "F9",  298 }, { "F10", 299 }, { "F11", 300 }, { "F12", 301 },
        { "LEFT_SHIFT",    340 }, { "LEFT_CONTROL",  341 },
        { "LEFT_ALT",      342 }, { "LEFT_SUPER",    343 },
        { "RIGHT_SHIFT",   344 }, { "RIGHT_CONTROL", 345 },
        { "RIGHT_ALT",     346 }, { "RIGHT_SUPER",   347 },
    };
    for (size_t i = 0; i < sizeof(key_constants) / sizeof(key_constants[0]); ++i) {
        lua_pushinteger(L, key_constants[i].value);
        lua_setfield(L, -2, key_constants[i].name);
    }
    lua_setfield(L, -2, "key");  // engine.key = {...}

    // Mouse button constants as engine.mouse.*
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "LEFT");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "RIGHT");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "MIDDLE");
    lua_setfield(L, -2, "mouse");  // engine.mouse = {...}

    lua_setglobal(L, "engine");
}
