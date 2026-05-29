#ifdef EDITOR_BUILD

#include "panel_hierarchy.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/ecs/ecs.h"

#include <stdio.h>

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
                            World *world,
                            const HierarchyContext *hctx,
                            uint32_t *selected_entity,
                            bool *has_selection) {
    if (!igBegin("Scene Hierarchy", p_open, 0)) {
        igEnd();
        return;
    }

    if (world == nullptr || hctx == nullptr) {
        igTextDisabled("No world loaded.");
        igEnd();
        return;
    }

    // Iterate the LocalTransform pool — every entity with a transform
    // appears in the hierarchy.
    ComponentPool *lt_pool = world_get_pool(world, hctx->c_local_transform);
    if (lt_pool == nullptr || lt_pool->count == 0) {
        igTextDisabled("No entities.");
        igEnd();
        return;
    }

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

    // Click on empty space to deselect.
    if (igIsMouseClicked_Bool(ImGuiMouseButton_Left, false) &&
        igIsWindowHovered(ImGuiHoveredFlags_None) &&
        !igIsAnyItemHovered()) {
        *has_selection = false;
    }

    igEnd();
}

#endif // EDITOR_BUILD
