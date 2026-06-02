#ifdef EDITOR_BUILD

#include "panel_inspector.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/ecs/ecs.h"
#include "../../core/asset_manager.h"
#include "../../core/sprite.h"
#include "../../core/sprite_meta.h"
#include "../../core/animation.h"
#include "../../core/anim_controller.h"
#include "../../core/anim_cache.h"
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
extern void        lua_host_set_sprite_id(LuaHost *host, ComponentId id);
extern bool        lua_host_velocity_registered(LuaHost *host);
extern ComponentId lua_host_get_velocity_id(LuaHost *host);
extern void        lua_host_set_velocity_id(LuaHost *host, ComponentId id);
extern ComponentId lua_host_get_script_id(LuaHost *host);
extern bool        lua_host_script_registered(LuaHost *host);
extern bool        lua_host_animator_registered(LuaHost *host);
extern ComponentId lua_host_get_animator_id(LuaHost *host);
extern void        lua_host_set_animator_id(LuaHost *host, ComponentId id);

// ---------------------------------------------------------------------------
// Component structs — must mirror the layouts in lua_bindings.c / scene.c.
// ---------------------------------------------------------------------------

typedef struct InspectorSprite {
    Sprite sprite;
} InspectorSprite;

typedef struct InspectorVelocity {
    float dx, dy;
} InspectorVelocity;

typedef struct InspectorAnimator {
    Animator animator;
} InspectorAnimator;

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
                            AnimCache *acache,
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
        bool header_open = igCollapsingHeader_BoolPtr("Local Transform", nullptr,
                                                      ImGuiTreeNodeFlags_DefaultOpen);
        if (igBeginPopupContextItem("TransformHeaderContext", ImGuiPopupFlags_MouseButtonRight)) {
            if (igMenuItem_Bool("Remove Component", nullptr, false, true)) {
                world_remove_component(world, ent, hctx->c_local_transform);
                world_remove_component(world, ent, hctx->c_world_transform);
                world_remove_component(world, ent, hctx->c_prev_position);
            }
            igEndPopup();
        }
        if (header_open) {
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
            bool header_open = igCollapsingHeader_BoolPtr("Camera", nullptr,
                                                          ImGuiTreeNodeFlags_DefaultOpen);
            if (igBeginPopupContextItem("CameraHeaderContext", ImGuiPopupFlags_MouseButtonRight)) {
                if (igMenuItem_Bool("Remove Component", nullptr, false, true)) {
                    world_remove_component(world, ent, cam_ctx->c_camera);
                }
                igEndPopup();
            }
            if (header_open) {
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
            bool header_open = igCollapsingHeader_BoolPtr(
                "Sprite", nullptr, ImGuiTreeNodeFlags_DefaultOpen);

            if (igBeginPopupContextItem("SpriteHeaderContext", ImGuiPopupFlags_MouseButtonRight)) {
                if (igMenuItem_Bool("Remove Component", nullptr, false, true)) {
                    if (spr->sprite.texture != ASSET_HANDLE_INVALID && am != nullptr) {
                        asset_manager_release(am, spr->sprite.texture);
                    }
                    world_remove_component(world, ent, c_sprite);
                }
                igEndPopup();
            }

            // ---- Drop target on the header (works even when collapsed) ----
            if (igBeginDragDropTarget()) {
                const ImGuiPayload *rp =
                    igAcceptDragDropPayload(SPRITE_REGION_DRAG_TYPE, 0);
                if (rp != nullptr &&
                    rp->DataSize == sizeof(SpriteDragPayload)) {
                    const SpriteDragPayload *drop =
                        (const SpriteDragPayload *)rp->Data;
                    // Load texture (cache-hit = refcount++ only, no Vulkan).
                    // NOTE: we intentionally do NOT release the old handle
                    // here to avoid vkDeviceWaitIdle inside the render pass.
                    AssetHandle h =
                        asset_manager_load_texture(am, drop->texture_path);
                    if (h != ASSET_HANDLE_INVALID) {
                        uint32_t tw = 0, th = 0;
                        asset_manager_get_texture_size(am, h, &tw, &th);
                        spr->sprite = sprite_from_sheet(h, tw, th,
                                                        drop->rect);
                    }
                }
                const ImGuiPayload *ap =
                    igAcceptDragDropPayload("ASSET_PATH", 0);
                if (ap != nullptr && am != nullptr) {
                    const char *path = (const char *)ap->Data;
                    if (path != nullptr) {
                        AssetHandle h =
                            asset_manager_load_texture(am, path);
                        if (h != ASSET_HANDLE_INVALID) {
                            uint32_t tw = 0, th = 0;
                            asset_manager_get_texture_size(am, h, &tw, &th);
                            spr->sprite = sprite_from_texture(h, tw, th);
                        }
                    }
                }
                igEndDragDropTarget();
            }

            if (header_open) {
                // ---- Texture field (interactive — accepts drops) -----------
                const char *tex_path = nullptr;
                if (am != nullptr) {
                    tex_path = asset_manager_get_path(am, spr->sprite.texture);
                }

                igText("Texture");
                igSameLine(0.0f, 8.0f);

                // Styled button showing the current texture path.
                // Acts as the main drop target for sprites and textures.
                char tex_label[256];
                if (tex_path != nullptr) {
                    snprintf(tex_label, sizeof(tex_label),
                             "%s##sprite_tex", tex_path);
                } else {
                    snprintf(tex_label, sizeof(tex_label),
                             "(none — drop texture here)##sprite_tex");
                }

                igPushStyleColor_Vec4(ImGuiCol_Button,
                    (ImVec4){ 0.15f, 0.15f, 0.20f, 1.0f });
                igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                    (ImVec4){ 0.20f, 0.25f, 0.35f, 1.0f });
                igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                    (ImVec4){ 0.18f, 0.22f, 0.30f, 1.0f });
                igButton(tex_label, (ImVec2){ -1.0f, 0.0f });
                igPopStyleColor(3);

                // Drop target on the texture button.
                if (igBeginDragDropTarget()) {
                    const ImGuiPayload *rp =
                        igAcceptDragDropPayload(SPRITE_REGION_DRAG_TYPE, 0);
                    if (rp != nullptr &&
                        rp->DataSize == sizeof(SpriteDragPayload)) {
                        const SpriteDragPayload *drop =
                            (const SpriteDragPayload *)rp->Data;
                        AssetHandle h =
                            asset_manager_load_texture(am,
                                                      drop->texture_path);
                        if (h != ASSET_HANDLE_INVALID) {
                            uint32_t tw = 0, th = 0;
                            asset_manager_get_texture_size(am, h, &tw, &th);
                            spr->sprite = sprite_from_sheet(h, tw, th,
                                                            drop->rect);
                        }
                    }
                    const ImGuiPayload *ap =
                        igAcceptDragDropPayload("ASSET_PATH", 0);
                    if (ap != nullptr && am != nullptr) {
                        const char *path = (const char *)ap->Data;
                        if (path != nullptr) {
                            AssetHandle h =
                                asset_manager_load_texture(am, path);
                            if (h != ASSET_HANDLE_INVALID) {
                                uint32_t tw = 0, th = 0;
                                asset_manager_get_texture_size(am, h,
                                                              &tw, &th);
                                spr->sprite = sprite_from_texture(h, tw, th);
                            }
                        }
                    }
                    igEndDragDropTarget();
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
            }
        }
    }

    // ---- Velocity ---------------------------------------------------------
    if (lua_host != nullptr && lua_host_velocity_registered(lua_host)) {
        ComponentId c_vel = lua_host_get_velocity_id(lua_host);
        InspectorVelocity *vel = (InspectorVelocity *)world_get_component(
            world, ent, c_vel);

        if (vel != nullptr) {
            bool header_open = igCollapsingHeader_BoolPtr("Velocity", nullptr,
                                                          ImGuiTreeNodeFlags_DefaultOpen);
            if (igBeginPopupContextItem("VelocityHeaderContext", ImGuiPopupFlags_MouseButtonRight)) {
                if (igMenuItem_Bool("Remove Component", nullptr, false, true)) {
                    world_remove_component(world, ent, c_vel);
                }
                igEndPopup();
            }
            if (header_open) {
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
                if (basename[0] == '\0') basename = "(empty)";
                const char *slash = strrchr(slot->path, '/');
                if (slash != nullptr) basename = slash + 1;

                // Use a unique header label per script slot.
                char header_label[320];
                snprintf(header_label, sizeof(header_label),
                         "Script: %s###script_%u", basename, (unsigned)s);

                bool header_open = igCollapsingHeader_BoolPtr(header_label, nullptr,
                                                              ImGuiTreeNodeFlags_DefaultOpen);
                if (igBeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
                    if (igMenuItem_Bool("Remove Script", nullptr, false, true)) {
                        if (slot->path[0] != '\0') {
                            lua_host_detach_script(lua_host, ent, slot->path);
                        } else {
                            // Empty slot: compact array directly.
                            for (uint8_t j = s; j < sc->count - 1; ++j) {
                                sc->slots[j] = sc->slots[j + 1];
                            }
                            sc->count--;
                        }
                    }
                    igEndPopup();
                }

                if (header_open) {
                    if (slot->path[0] == '\0') {
                        igText("Drop a .lua script here:");
                        
                        // Styled drop target zone.
                        igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){ 0.15f, 0.15f, 0.20f, 1.0f });
                        igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){ 0.20f, 0.25f, 0.35f, 1.0f });
                        igButton("(drop .lua script file here)##script_drop", (ImVec2){ -1.0f, 40.0f });
                        igPopStyleColor(2);

                        if (igBeginDragDropTarget()) {
                            const ImGuiPayload *ap =
                                igAcceptDragDropPayload("ASSET_PATH", 0);
                            if (ap != nullptr) {
                                const char *path = (const char *)ap->Data;
                                if (path != nullptr && strstr(path, ".lua") != nullptr) {
                                    // Remove the empty slot first since attach_script will append a new one.
                                    for (uint8_t j = s; j < sc->count - 1; ++j) {
                                        sc->slots[j] = sc->slots[j + 1];
                                    }
                                    sc->count--;
                                    lua_host_attach_script(lua_host, ent, path);
                                }
                            }
                            igEndDragDropTarget();
                        }
                    } else {
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
    }

    // ---- Animator ---------------------------------------------------------
    if (lua_host != nullptr && lua_host_animator_registered(lua_host)) {
        ComponentId c_anim = lua_host_get_animator_id(lua_host);
        InspectorAnimator *ia = (InspectorAnimator *)world_get_component(
            world, ent, c_anim);

        if (ia != nullptr) {
            bool header_open = igCollapsingHeader_BoolPtr("Animator", nullptr,
                                                          ImGuiTreeNodeFlags_DefaultOpen);

            if (igBeginDragDropTarget()) {
                const ImGuiPayload *ap = igAcceptDragDropPayload("ASSET_PATH", 0);
                if (ap != nullptr && acache != nullptr) {
                    const char *path = (const char *)ap->Data;
                    if (path != nullptr) {
                        char ctrl_path[AnimPathMaxLen] = {};
                        if (strstr(path, ".controller.meta") != nullptr) {
                            strncpy(ctrl_path, path, AnimPathMaxLen - 1);
                        } else if (strstr(path, ".anim.meta") != nullptr) {
                            anim_ctrl_build_meta_path(path, ctrl_path, AnimPathMaxLen);
                        } else if (strstr(path, ".png") != nullptr) {
                            char anim_meta[AnimPathMaxLen] = {};
                            anim_build_meta_path(path, anim_meta, AnimPathMaxLen);
                            anim_ctrl_build_meta_path(anim_meta, ctrl_path, AnimPathMaxLen);
                        }
                        if (ctrl_path[0] != '\0') {
                            AnimController *ctrl = anim_cache_load_controller(acache, ctrl_path, am);
                            if (ctrl != nullptr) {
                                strncpy(ia->animator.controller_path, ctrl_path, AnimPathMaxLen - 1);
                                ia->animator.controller = ctrl;
                                animator_reset_params(&ia->animator);
                                ia->animator.current_state = ctrl->default_state;
                                ia->animator.playing = true;
                            }
                        }
                    }
                }
                igEndDragDropTarget();
            }

            if (igBeginPopupContextItem("AnimatorHeaderContext", ImGuiPopupFlags_MouseButtonRight)) {
                if (igMenuItem_Bool("Remove Component", nullptr, false, true)) {
                    world_remove_component(world, ent, c_anim);
                }
                igEndPopup();
            }
            if (header_open) {
                // Controller file path.
                igText("Controller");
                igSameLine(0.0f, 8.0f);

                char ctrl_label[AnimPathMaxLen + 32];
                if (ia->animator.controller_path[0] != '\0') {
                    snprintf(ctrl_label, sizeof(ctrl_label), "%s##ctrl_path_btn", ia->animator.controller_path);
                } else {
                    snprintf(ctrl_label, sizeof(ctrl_label), "(none — drop .controller.meta here)##ctrl_path_btn");
                }

                igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){ 0.15f, 0.15f, 0.20f, 1.0f });
                igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){ 0.20f, 0.25f, 0.35f, 1.0f });
                igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){ 0.18f, 0.22f, 0.30f, 1.0f });
                igButton(ctrl_label, (ImVec2){ -1.0f, 0.0f });
                igPopStyleColor(3);

                if (igBeginDragDropTarget()) {
                    const ImGuiPayload *ap = igAcceptDragDropPayload("ASSET_PATH", 0);
                    if (ap != nullptr && acache != nullptr) {
                        const char *path = (const char *)ap->Data;
                        if (path != nullptr) {
                            char ctrl_path[AnimPathMaxLen] = {};
                            if (strstr(path, ".controller.meta") != nullptr) {
                                strncpy(ctrl_path, path, AnimPathMaxLen - 1);
                            } else if (strstr(path, ".anim.meta") != nullptr) {
                                anim_ctrl_build_meta_path(path, ctrl_path, AnimPathMaxLen);
                            } else if (strstr(path, ".png") != nullptr) {
                                char anim_meta[AnimPathMaxLen] = {};
                                anim_build_meta_path(path, anim_meta, AnimPathMaxLen);
                                anim_ctrl_build_meta_path(anim_meta, ctrl_path, AnimPathMaxLen);
                            }
                            if (ctrl_path[0] != '\0') {
                                AnimController *ctrl = anim_cache_load_controller(acache, ctrl_path, am);
                                if (ctrl != nullptr) {
                                    strncpy(ia->animator.controller_path, ctrl_path, AnimPathMaxLen - 1);
                                    ia->animator.controller = ctrl;
                                    animator_reset_params(&ia->animator);
                                    ia->animator.current_state = ctrl->default_state;
                                    ia->animator.playing = true;
                                }
                            }
                        }
                    }
                    igEndDragDropTarget();
                }

                // Current state (if controller attached).
                if (ia->animator.controller != nullptr) {
                    const AnimController *ctrl = ia->animator.controller;
                    if (ia->animator.current_state < ctrl->state_count) {
                        igText("State:");
                        igSameLine(0.0f, 4.0f);
                        igTextColored((ImVec4){ 0.4f, 0.8f, 1.0f, 1.0f },
                                      "%s",
                                      ctrl->states[ia->animator.current_state].name);
                    }
                }

                // Clip selector and status (if controller and anim_data are loaded).
                if (ia->animator.controller != nullptr &&
                    ia->animator.controller->anim_data != nullptr &&
                    ia->animator.controller->anim_data->clip_count > 0) {
                    AnimData *ad = ia->animator.controller->anim_data;

                    // Current clip name for preview label.
                    const char *current_name = "(none)";
                    if (ia->animator.current_clip < ad->clip_count) {
                        current_name = ad->clips[ia->animator.current_clip].name;
                    }

                    igText("Clip:");
                    igSameLine(0.0f, 4.0f);
                    igSetNextItemWidth(150.0f);
                    if (igBeginCombo("##anim_clip", current_name, 0)) {
                        for (uint32_t c = 0; c < ad->clip_count; ++c) {
                            bool is_sel = (c == ia->animator.current_clip);
                            if (igSelectable_Bool(ad->clips[c].name, is_sel,
                                                  0, (ImVec2){ 0, 0 })) {
                                animator_play(&ia->animator, ad->clips[c].name);
                            }
                        }
                        igEndCombo();
                    }

                    // Play / Stop buttons.
                    igSameLine(0.0f, 12.0f);
                    if (ia->animator.playing) {
                        if (igButton("Stop##anim", (ImVec2){ 50, 0 })) {
                            animator_stop(&ia->animator);
                        }
                    } else {
                        if (igButton("Play##anim", (ImVec2){ 50, 0 })) {
                            ia->animator.playing = true;
                        }
                    }

                    // Status.
                    if (ia->animator.current_clip < ad->clip_count) {
                        AnimClip *clip = &ad->clips[ia->animator.current_clip];
                        igText("Frame: %u / %u  |  FPS: %.0f  |  %s",
                               ia->animator.current_frame,
                               clip->frame_count,
                               clip->fps,
                               ia->animator.playing ? "Playing"
                                                    : (ia->animator.finished ? "Finished" : "Stopped"));
                    }
                } else {
                    igTextDisabled("No animation data loaded.");
                }

                // Runtime parameters (if controller attached).
                if (ia->animator.controller != nullptr &&
                    ia->animator.controller->param_count > 0) {
                    igSeparator();
                    igText("Parameters:");
                    const AnimController *ctrl = ia->animator.controller;
                    for (uint32_t pi = 0; pi < ctrl->param_count; ++pi) {
                        igPushID_Int((int)(pi + 8000));
                        const AnimParam *p = &ctrl->params[pi];

                        switch (p->type) {
                            case ANIM_PARAM_FLOAT:
                                igSetNextItemWidth(100.0f);
                                igDragFloat(p->name,
                                            &ia->animator.params[pi].f,
                                            0.1f, -9999.0f, 9999.0f,
                                            "%.2f", 0);
                                break;
                            case ANIM_PARAM_INT: {
                                int val = (int)ia->animator.params[pi].i;
                                igSetNextItemWidth(100.0f);
                                if (igDragInt(p->name, &val, 1.0f,
                                              -9999, 9999, "%d", 0)) {
                                    ia->animator.params[pi].i = (int32_t)val;
                                }
                                break;
                            }
                            case ANIM_PARAM_BOOL:
                                igCheckbox(p->name,
                                           &ia->animator.params[pi].b);
                                break;
                            case ANIM_PARAM_TRIGGER: {
                                char btn_label[80];
                                snprintf(btn_label, sizeof(btn_label),
                                         "%s##trig", p->name);
                                if (igButton(btn_label, (ImVec2){ 0, 0 })) {
                                    ia->animator.params[pi].b = true;
                                }
                                igSameLine(0.0f, 4.0f);
                                igTextDisabled("%s",
                                    ia->animator.params[pi].b
                                        ? "(set)" : "(idle)");
                                break;
                            }
                        }
                        igPopID();
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

    // ---- Right-Click Context Menu to Add Components -----------------------
    if (igBeginPopupContextWindow("InspectorContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
        igTextDisabled("Add Component");
        igSeparator();

        bool has_transform = world_has_component(world, ent, hctx->c_local_transform);
        bool has_camera = cam_ctx != nullptr && world_has_component(world, ent, cam_ctx->c_camera);
        
        bool has_sprite = false;
        ComponentId c_sprite = (lua_host != nullptr && lua_host_sprite_registered(lua_host))
            ? lua_host_get_sprite_id(lua_host) : UINT8_MAX;
        if (c_sprite != UINT8_MAX) {
            has_sprite = world_has_component(world, ent, c_sprite);
        }

        bool has_velocity = false;
        ComponentId c_velocity = (lua_host != nullptr && lua_host_velocity_registered(lua_host))
            ? lua_host_get_velocity_id(lua_host) : UINT8_MAX;
        if (c_velocity != UINT8_MAX) {
            has_velocity = world_has_component(world, ent, c_velocity);
        }

        bool has_animator = false;
        ComponentId c_animator = (lua_host != nullptr && lua_host_animator_registered(lua_host))
            ? lua_host_get_animator_id(lua_host) : UINT8_MAX;
        if (c_animator != UINT8_MAX) {
            has_animator = world_has_component(world, ent, c_animator);
        }

        ComponentId c_script = (lua_host != nullptr && lua_host_script_registered(lua_host))
            ? lua_host_get_script_id(lua_host) : UINT8_MAX;

        if (igMenuItem_Bool("Transform", nullptr, false, !has_transform)) {
            LocalTransform lt_val = { .x = 0.0f, .y = 0.0f, .sx = 1.0f, .sy = 1.0f };
            world_add_component(world, ent, hctx->c_local_transform, &lt_val);

            WorldTransform wt_val = { .x = 0.0f, .y = 0.0f, .sx = 1.0f, .sy = 1.0f };
            world_add_component(world, ent, hctx->c_world_transform, &wt_val);

            PreviousPosition pp_val = { .x = 0.0f, .y = 0.0f };
            world_add_component(world, ent, hctx->c_prev_position, &pp_val);
        }

        if (cam_ctx != nullptr) {
            if (igMenuItem_Bool("Camera", nullptr, false, !has_camera)) {
                Camera cam_val = camera_default_ortho(5.0f);
                world_add_component(world, ent, cam_ctx->c_camera, &cam_val);
            }
        }

        if (lua_host != nullptr) {
            if (igMenuItem_Bool("Sprite", nullptr, false, !has_sprite)) {
                if (c_sprite == UINT8_MAX) {
                    c_sprite = world_register_component(world, sizeof(InspectorSprite));
                    lua_host_set_sprite_id(lua_host, c_sprite);
                }
                InspectorSprite spr_val = {};
                spr_val.sprite.texture = ASSET_HANDLE_INVALID;
                world_add_component(world, ent, c_sprite, &spr_val);
            }

            if (igMenuItem_Bool("Velocity", nullptr, false, !has_velocity)) {
                if (c_velocity == UINT8_MAX) {
                    c_velocity = world_register_component(world, sizeof(InspectorVelocity));
                    lua_host_set_velocity_id(lua_host, c_velocity);
                }
                InspectorVelocity vel_val = { .dx = 0.0f, .dy = 0.0f };
                world_add_component(world, ent, c_velocity, &vel_val);
            }

            if (igMenuItem_Bool("Animator", nullptr, false, !has_animator)) {
                if (c_animator == UINT8_MAX) {
                    c_animator = world_register_component(world, sizeof(InspectorAnimator));
                    lua_host_set_animator_id(lua_host, c_animator);
                }
                InspectorAnimator anim_val = {};
                animator_init(&anim_val.animator);
                world_add_component(world, ent, c_animator, &anim_val);
            }

            if (igMenuItem_Bool("Script Slot", nullptr, false, true)) {
                if (c_script == UINT8_MAX) {
                    c_script = lua_host_get_script_id(lua_host);
                }
                ScriptComponent *sc = (ScriptComponent *)world_get_component(world, ent, c_script);
                if (sc == nullptr) {
                    ScriptComponent sc_val = {};
                    sc_val.count = 0;
                    world_add_component(world, ent, c_script, &sc_val);
                    sc = (ScriptComponent *)world_get_component(world, ent, c_script);
                }
                if (sc != nullptr && sc->count < MAX_SCRIPTS_PER_ENTITY) {
                    ScriptSlot *slot = &sc->slots[sc->count];
                    *slot = (ScriptSlot){};
                    slot->path[0] = '\0';
                    slot->instance_ref = LUA_NOREF;
                    sc->count++;
                }
            }
        }

        igEndPopup();
    }

    igEnd();
}

#endif // EDITOR_BUILD
