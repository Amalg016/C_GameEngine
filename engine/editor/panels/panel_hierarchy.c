#ifdef EDITOR_BUILD

#include "panel_hierarchy.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/ecs/ecs.h"

#include <stdio.h>

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

        // Build a display label.  Check for hierarchy (parent-child).
        Hierarchy *hier = (Hierarchy *)world_get_component(
            world, ent, hctx->c_hierarchy);

        char label[64];
        if (hier != nullptr && hier->parent != ENTITY_INVALID) {
            snprintf(label, sizeof(label), "  Entity %u (child)", ent_idx);
        } else {
            snprintf(label, sizeof(label), "Entity %u", ent_idx);
        }

        // Highlight the selected entity.
        bool is_selected = (*has_selection && *selected_entity == ent_idx);

        if (igSelectable_Bool(label, is_selected, 0, (ImVec2){ 0, 0 })) {
            *selected_entity = ent_idx;
            *has_selection    = true;
        }

        // ---- Drag source: entity payload --------------------------------
        if (igBeginDragDropSource(0)) {
            igSetDragDropPayload("ENTITY", &ent_idx, sizeof(uint32_t), 0);
            igText("Entity %u", ent_idx);
            igEndDragDropSource();
        }
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
