#ifdef EDITOR_BUILD

#include "panel_hierarchy.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/ecs/ecs.h"
#include "../../core/engine.h"
#include "../../core/sprite.h"
#include "../../core/asset_manager.h"

#include <stdio.h>
#include <string.h>

extern void        lua_host_set_sprite_id(LuaHost *host, ComponentId id);
extern bool        lua_host_sprite_registered(LuaHost *host);
extern ComponentId lua_host_get_sprite_id(LuaHost *host);

static Entity create_primitive_entity(Engine *engine, const char *primitive_type) {
    World *world = engine_get_world(engine);
    HierarchyContext *hctx = engine_get_hctx(engine);
    LuaHost *lua = engine_get_lua_host(engine);
    AssetManager *am = engine_get_asset_manager(engine);

    Entity ent = world_entity_create(world);
    if (ent == ENTITY_INVALID) return ENTITY_INVALID;

    // Add LocalTransform, WorldTransform, PreviousPosition
    LocalTransform lt_val = { .x = 0.0f, .y = 0.0f, .sx = 1.0f, .sy = 1.0f };
    world_add_component(world, ent, hctx->c_local_transform, &lt_val);

    WorldTransform wt_val = { .x = 0.0f, .y = 0.0f, .sx = 1.0f, .sy = 1.0f };
    world_add_component(world, ent, hctx->c_world_transform, &wt_val);

    PreviousPosition pp_val = { .x = 0.0f, .y = 0.0f };
    world_add_component(world, ent, hctx->c_prev_position, &pp_val);

    if (strcmp(primitive_type, "Box") == 0 || strcmp(primitive_type, "Circle") == 0) {
        ComponentId c_sprite = (lua != nullptr && lua_host_sprite_registered(lua))
            ? lua_host_get_sprite_id(lua) : UINT8_MAX;
        if (c_sprite == UINT8_MAX) {
            c_sprite = world_register_component(world, sizeof(Sprite));
            if (lua != nullptr) {
                lua_host_set_sprite_id(lua, c_sprite);
            }
        }

        Sprite spr_val = {};
        if (strcmp(primitive_type, "Box") == 0) {
            spr_val.texture = ASSET_HANDLE_INVALID;
        } else {
            AssetHandle h = asset_manager_load_texture(am, "assets/images/circle.png");
            if (h != ASSET_HANDLE_INVALID) {
                uint32_t tw = 0, th = 0;
                asset_manager_get_texture_size(am, h, &tw, &th);
                spr_val = sprite_from_texture(h, tw, th);
            } else {
                spr_val.texture = ASSET_HANDLE_INVALID;
            }
        }
        world_add_component(world, ent, c_sprite, &spr_val);
    }
    return ent;
}

// ---------------------------------------------------------------------------
// draw_entity_node
// ---------------------------------------------------------------------------
static void draw_entity_node(World *world,
                             const HierarchyContext *hctx,
                             Entity ent,
                             uint32_t ent_idx,
                             uint32_t *selected_entity,
                             bool *has_selection) {
    Hierarchy *hier = (Hierarchy *)world_get_component(world, ent, hctx->c_hierarchy);
    bool has_children = (hier != nullptr && hier->first_child != ENTITY_INVALID);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (*has_selection && *selected_entity == ent_idx) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    char label[64];
    snprintf(label, sizeof(label), "Entity %u", ent_idx);

    bool node_open = igTreeNodeEx_Ptr((const void *)(uintptr_t)ent_idx, flags, "%s", label);

    if (igIsItemClicked(ImGuiMouseButton_Left)) {
        *selected_entity = ent_idx;
        *has_selection = true;
    }

    // ---- Drag source: entity payload --------------------------------
    if (igBeginDragDropSource(0)) {
        igSetDragDropPayload("ENTITY", &ent_idx, sizeof(uint32_t), 0);
        igText("Entity %u", ent_idx);
        igEndDragDropSource();
    }

    if (node_open) {
        if (has_children) {
            Entity child = hier->first_child;
            while (child != ENTITY_INVALID) {
                uint32_t child_idx = entity_index(child);
                draw_entity_node(world, hctx, child, child_idx, selected_entity, has_selection);

                Hierarchy *child_hier = (Hierarchy *)world_get_component(world, child, hctx->c_hierarchy);
                if (child_hier == nullptr) break;
                child = child_hier->next_sibling;
            }
        }
        igTreePop();
    }
}

// ---------------------------------------------------------------------------
// panel_hierarchy_render
// ---------------------------------------------------------------------------
void panel_hierarchy_render(bool *p_open,
                            Engine *engine,
                            uint32_t *selected_entity,
                            bool *has_selection) {
    if (!igBegin("Scene Hierarchy", p_open, 0)) {
        igEnd();
        return;
    }

    if (engine == nullptr) {
        igTextDisabled("No engine loaded.");
        igEnd();
        return;
    }

    World            *world = engine_get_world(engine);
    HierarchyContext *hctx  = engine_get_hctx(engine);

    if (world == nullptr || hctx == nullptr) {
        igTextDisabled("No world loaded.");
        igEnd();
        return;
    }

    // Iterate the LocalTransform pool — every entity with a transform
    // appears in the hierarchy.
    ComponentPool *lt_pool = world_get_pool(world, hctx->c_local_transform);
    if (lt_pool == nullptr || lt_pool->count == 0) {
        igTextDisabled("No entities. Right-click to create.");
    } else {
        for (uint32_t i = 0; i < lt_pool->count; ++i) {
            uint32_t ent_idx = component_pool_get_entity(lt_pool, i);
            Entity   ent     = world_entity_from_index(world, ent_idx);

            if (ent == ENTITY_INVALID) continue;

            // Only draw root entities at the top level of the hierarchy tree.
            // Child entities are rendered recursively inside their parents.
            Hierarchy *hier = (Hierarchy *)world_get_component(world, ent, hctx->c_hierarchy);
            if (hier != nullptr && hier->parent != ENTITY_INVALID) {
                continue;
            }

            draw_entity_node(world, hctx, ent, ent_idx, selected_entity, has_selection);
        }
    }

    // Click on empty space to deselect.
    if (igIsMouseClicked_Bool(ImGuiMouseButton_Left, false) &&
        igIsWindowHovered(ImGuiHoveredFlags_None) &&
        !igIsAnyItemHovered()) {
        *has_selection = false;
    }

    // Right-click context menu
    if (igBeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
        if (igBeginMenu("Create Entity", true)) {
            if (igMenuItem_Bool("Empty Entity", nullptr, false, true)) {
                Entity ent = create_primitive_entity(engine, "Empty");
                if (ent != ENTITY_INVALID) {
                    *selected_entity = entity_index(ent);
                    *has_selection = true;
                }
            }
            igSeparator();
            if (igMenuItem_Bool("Box Sprite", nullptr, false, true)) {
                Entity ent = create_primitive_entity(engine, "Box");
                if (ent != ENTITY_INVALID) {
                    *selected_entity = entity_index(ent);
                    *has_selection = true;
                }
            }
            if (igMenuItem_Bool("Circle Sprite", nullptr, false, true)) {
                Entity ent = create_primitive_entity(engine, "Circle");
                if (ent != ENTITY_INVALID) {
                    *selected_entity = entity_index(ent);
                    *has_selection = true;
                }
            }
            igEndMenu();
        }

        if (*has_selection) {
            igSeparator();
            char delete_label[64];
            snprintf(delete_label, sizeof(delete_label), "Delete Entity %u", *selected_entity);
            if (igMenuItem_Bool(delete_label, nullptr, false, true)) {
                Entity ent = world_entity_from_index(world, *selected_entity);
                if (ent != ENTITY_INVALID) {
                    world_entity_destroy(world, ent);
                    *has_selection = false;
                    *selected_entity = 0;
                }
            }
        }

        igEndPopup();
    }

    igEnd();
}

#endif // EDITOR_BUILD
