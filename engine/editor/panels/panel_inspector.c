#ifdef EDITOR_BUILD

#include "panel_inspector.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/ecs/ecs.h"
#include "../../core/asset_manager.h"
#include "../../core/sprite.h"
#include "../../core/scripting/lua_host.h"

#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// LuaHost internal accessors — needed for Sprite/Velocity/Script ComponentIds.
// ---------------------------------------------------------------------------

extern bool        lua_host_sprite_registered(LuaHost *host);
extern ComponentId lua_host_get_sprite_id(LuaHost *host);
extern bool        lua_host_velocity_registered(LuaHost *host);
extern ComponentId lua_host_get_velocity_id(LuaHost *host);
extern ComponentId lua_host_get_script_id(LuaHost *host);
extern bool        lua_host_script_registered(LuaHost *host);

// ---------------------------------------------------------------------------
// Component structs — must mirror the layouts in lua_bindings.c / scene.c.
// ---------------------------------------------------------------------------

typedef struct InspectorSprite {
    Sprite sprite;
} InspectorSprite;

typedef struct InspectorVelocity {
    float dx, dy;
} InspectorVelocity;

// ---------------------------------------------------------------------------
// Helper — check if a Lua key should be hidden from the inspector.
// We skip lifecycle methods, the entity field, and metamethods.
// ---------------------------------------------------------------------------

static bool is_hidden_lua_key(const char *key) {
    if (key == nullptr) return true;
    if (key[0] == '_') return true;  // skip __index, __newindex, etc.

    static const char *hidden_keys[] = {
        "entity",
        "on_init",
        "on_update",
        "on_fixed_update",
        "on_render",
        "on_destroy",
    };

    for (size_t i = 0; i < sizeof(hidden_keys) / sizeof(hidden_keys[0]); ++i) {
        if (strcmp(key, hidden_keys[i]) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helper — render a single Lua script instance's variables.
// ---------------------------------------------------------------------------

static void render_lua_instance_vars(lua_State *L, int instance_ref) {
    if (L == nullptr || instance_ref == LUA_NOREF || instance_ref == LUA_REFNIL)
        return;

    lua_rawgeti(L, LUA_REGISTRYINDEX, instance_ref);  // push instance table
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    // Iterate the instance table (only own keys, not inherited via __index).
    lua_pushnil(L);  // first key
    while (lua_next(L, -2) != 0) {
        // key at -2, value at -1
        if (!lua_isstring(L, -2)) {
            lua_pop(L, 1);  // pop value, keep key for next iteration
            continue;
        }

        const char *key = lua_tostring(L, -2);
        if (is_hidden_lua_key(key)) {
            lua_pop(L, 1);  // pop value
            continue;
        }

        int vtype = lua_type(L, -1);

        switch (vtype) {
            case LUA_TNUMBER: {
                float val = (float)lua_tonumber(L, -1);
                char label[128];
                snprintf(label, sizeof(label), "%s", key);
                if (igDragFloat(label, &val, 0.05f, -1e6f, 1e6f, "%.3f", 0)) {
                    // Write back to the instance table.
                    lua_pushnumber(L, (lua_Number)val);
                    lua_setfield(L, -4, key);  // instance[key] = val
                }
                break;
            }
            case LUA_TBOOLEAN: {
                bool val = lua_toboolean(L, -1);
                char label[128];
                snprintf(label, sizeof(label), "%s", key);
                if (igCheckbox(label, &val)) {
                    lua_pushboolean(L, val);
                    lua_setfield(L, -4, key);
                }
                break;
            }
            case LUA_TSTRING: {
                const char *val = lua_tostring(L, -1);
                igText("%s: \"%s\"", key, val);
                break;
            }
            case LUA_TTABLE: {
                igTextDisabled("%s: (table)", key);
                break;
            }
            default:
                // Skip functions, userdata, etc.
                break;
        }

        lua_pop(L, 1);  // pop value, keep key for next iteration
    }

    lua_pop(L, 1);  // pop instance table
}

// ---------------------------------------------------------------------------
// panel_inspector_render
// ---------------------------------------------------------------------------

void panel_inspector_render(bool *p_open,
                            World *world,
                            const HierarchyContext *hctx,
                            const CameraContext *cam_ctx,
                            LuaHost *lua_host,
                            AssetManager *am,
                            uint32_t selected_entity,
                            bool has_selection) {
    if (!igBegin("Inspector", p_open, 0)) {
        igEnd();
        return;
    }

    // ---- Drop target: accept entity payloads from hierarchy ---------------
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ENTITY", 0);
        if (payload != nullptr && payload->DataSize == sizeof(uint32_t)) {
            uint32_t dropped = *(const uint32_t *)payload->Data;
            // For now, selecting the dropped entity is the only action.
            // In a full editor, this could reparent, assign references, etc.
            igText("Dropped Entity %u", dropped);
        }
        igEndDragDropTarget();
    }

    if (!has_selection || world == nullptr || hctx == nullptr) {
        igTextDisabled("Select an entity in the Hierarchy.");
        igEnd();
        return;
    }

    Entity ent = world_entity_from_index(world, selected_entity);
    if (ent == ENTITY_INVALID || !world_entity_alive(world, ent)) {
        igTextDisabled("Entity %u is not alive.", selected_entity);
        igEnd();
        return;
    }

    igText("Entity %u", selected_entity);
    igSeparator();

    // ---- LocalTransform ---------------------------------------------------
    LocalTransform *lt = (LocalTransform *)world_get_component(
        world, ent, hctx->c_local_transform);

    if (lt != nullptr) {
        if (igCollapsingHeader_BoolPtr("Local Transform", nullptr,
                                       ImGuiTreeNodeFlags_DefaultOpen)) {
            float pos[2] = { lt->x, lt->y };
            if (igDragFloat2("Position", pos, 0.05f, -100.0f, 100.0f, "%.2f", 0)) {
                lt->x = pos[0];
                lt->y = pos[1];
            }
            float scale[2] = { lt->sx, lt->sy };
            if (igDragFloat2("Scale", scale, 0.01f, 0.01f, 100.0f, "%.2f", 0)) {
                lt->sx = scale[0];
                lt->sy = scale[1];
            }
        }
    }

    // ---- WorldTransform (read-only) ---------------------------------------
    WorldTransform *wt = (WorldTransform *)world_get_component(
        world, ent, hctx->c_world_transform);

    if (wt != nullptr) {
        if (igCollapsingHeader_BoolPtr("World Transform", nullptr, 0)) {
            igBeginDisabled(true);
            float wpos[2] = { wt->x, wt->y };
            float wscale[2] = { wt->sx, wt->sy };
            igDragFloat2("World Pos", wpos, 0.0f, 0.0f, 0.0f, "%.2f", 0);
            igDragFloat2("World Scale", wscale, 0.0f, 0.0f, 0.0f, "%.2f", 0);
            igEndDisabled();
        }
    }

    // ---- Camera -----------------------------------------------------------
    if (cam_ctx != nullptr) {
        Camera *cam = (Camera *)world_get_component(
            world, ent, cam_ctx->c_camera);

        if (cam != nullptr) {
            if (igCollapsingHeader_BoolPtr("Camera", nullptr,
                                           ImGuiTreeNodeFlags_DefaultOpen)) {
                // Projection type selector.
                const char *proj_items[] = { "Orthographic", "Perspective" };
                int proj_current = (int)cam->projection;
                if (igCombo_Str_arr("Projection", &proj_current,
                                    proj_items, 2, 0)) {
                    cam->projection = (CameraProjection)proj_current;
                }

                if (cam->projection == CAMERA_ORTHOGRAPHIC) {
                    igDragFloat("Ortho Size", &cam->ortho_size,
                                0.1f, 0.1f, 100.0f, "%.2f", 0);
                } else {
                    float fov_deg = cam->fov * 180.0f / 3.14159265f;
                    if (igDragFloat("FOV (deg)", &fov_deg,
                                    0.5f, 1.0f, 179.0f, "%.1f", 0)) {
                        cam->fov = fov_deg * 3.14159265f / 180.0f;
                    }
                }

                igDragFloat("Near Plane", &cam->near_plane,
                            0.01f, -100.0f, 100.0f, "%.3f", 0);
                igDragFloat("Far Plane", &cam->far_plane,
                            0.01f, -100.0f, 1000.0f, "%.3f", 0);

                igCheckbox("Is Active", &cam->is_active);
            }
        }
    }

    // ---- Sprite -----------------------------------------------------------
    if (lua_host != nullptr && lua_host_sprite_registered(lua_host)) {
        ComponentId c_sprite = lua_host_get_sprite_id(lua_host);
        InspectorSprite *spr = (InspectorSprite *)world_get_component(
            world, ent, c_sprite);

        if (spr != nullptr) {
            if (igCollapsingHeader_BoolPtr("Sprite", nullptr,
                                           ImGuiTreeNodeFlags_DefaultOpen)) {
                // Display texture path (read-only).
                const char *tex_path = nullptr;
                if (am != nullptr) {
                    tex_path = asset_manager_get_path(am, spr->sprite.texture);
                }
                if (tex_path != nullptr) {
                    igText("Texture: %s", tex_path);
                } else {
                    igTextDisabled("Texture: (none)");
                }

                // Texture dimensions.
                igText("Tex Size: %ux%u", spr->sprite.tex_width,
                       spr->sprite.tex_height);

                // Pixel rect.
                igText("Pixel Rect: (%.0f, %.0f) %.0fx%.0f",
                       spr->sprite.rect.x, spr->sprite.rect.y,
                       spr->sprite.rect.w, spr->sprite.rect.h);

                // UV rect (read-only).
                igBeginDisabled(true);
                float uv[4] = {
                    spr->sprite.uv_rect.x, spr->sprite.uv_rect.y,
                    spr->sprite.uv_rect.w, spr->sprite.uv_rect.h
                };
                igDragFloat4("UV Rect", uv, 0.0f, 0.0f, 0.0f, "%.3f", 0);
                igEndDisabled();

                igBeginDisabled(true);
                uint32_t handle_val = (uint32_t)spr->sprite.texture;
                igDragScalar("Handle", ImGuiDataType_U32,
                             &handle_val, 0.0f, nullptr, nullptr, "%u", 0);
                igEndDisabled();
            }
        }
    }

    // ---- Velocity ---------------------------------------------------------
    if (lua_host != nullptr && lua_host_velocity_registered(lua_host)) {
        ComponentId c_vel = lua_host_get_velocity_id(lua_host);
        InspectorVelocity *vel = (InspectorVelocity *)world_get_component(
            world, ent, c_vel);

        if (vel != nullptr) {
            if (igCollapsingHeader_BoolPtr("Velocity", nullptr,
                                           ImGuiTreeNodeFlags_DefaultOpen)) {
                float v[2] = { vel->dx, vel->dy };
                if (igDragFloat2("Velocity", v, 0.1f, -100.0f, 100.0f, "%.2f", 0)) {
                    vel->dx = v[0];
                    vel->dy = v[1];
                }
            }
        }
    }

    // ---- Script Component (Lua) -------------------------------------------
    if (lua_host != nullptr && lua_host_script_registered(lua_host)) {
        ComponentId c_script = lua_host_get_script_id(lua_host);
        ScriptComponent *sc = (ScriptComponent *)world_get_component(
            world, ent, c_script);

        if (sc != nullptr && sc->count > 0) {
            lua_State *L = lua_host_get_state(lua_host);

            for (uint8_t s = 0; s < sc->count; ++s) {
                ScriptSlot *slot = &sc->slots[s];

                // Extract basename from the script path for a cleaner header.
                const char *basename = slot->path;
                const char *slash = strrchr(slot->path, '/');
                if (slash != nullptr) basename = slash + 1;

                // Use a unique header label per script slot.
                char header_label[320];
                snprintf(header_label, sizeof(header_label),
                         "Script: %s###script_%u", basename, (unsigned)s);

                if (igCollapsingHeader_BoolPtr(header_label, nullptr,
                                               ImGuiTreeNodeFlags_DefaultOpen)) {
                    igTextDisabled("Path: %s", slot->path);

                    igSeparator();

                    // Enumerate per-instance variables from the Lua table.
                    if (L != nullptr && slot->instance_ref != LUA_NOREF) {
                        render_lua_instance_vars(L, slot->instance_ref);
                    } else {
                        igTextDisabled("(no instance data)");
                    }
                }
            }
        }
    }

    // ---- Hierarchy info ---------------------------------------------------
    Hierarchy *hier = (Hierarchy *)world_get_component(
        world, ent, hctx->c_hierarchy);

    if (hier != nullptr) {
        if (igCollapsingHeader_BoolPtr("Hierarchy", nullptr, 0)) {
            if (hier->parent != ENTITY_INVALID) {
                igText("Parent: Entity %u", entity_index(hier->parent));
            } else {
                igTextDisabled("Root entity (no parent)");
            }

            if (hier->first_child != ENTITY_INVALID) {
                igText("First Child: Entity %u",
                       entity_index(hier->first_child));
            } else {
                igTextDisabled("No children");
            }
        }
    }

    // ---- PreviousPosition (read-only) -------------------------------------
    PreviousPosition *pp = (PreviousPosition *)world_get_component(
        world, ent, hctx->c_prev_position);

    if (pp != nullptr) {
        if (igCollapsingHeader_BoolPtr("Previous Position", nullptr, 0)) {
            igBeginDisabled(true);
            float ppos[2] = { pp->x, pp->y };
            igDragFloat2("Prev Pos", ppos, 0.0f, 0.0f, 0.0f, "%.2f", 0);
            igEndDisabled();
        }
    }

    igEnd();
}

#endif // EDITOR_BUILD
