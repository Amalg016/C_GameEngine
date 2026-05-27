#ifdef EDITOR_BUILD

#include "panel_inspector.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/ecs/ecs.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// panel_inspector_render
// ---------------------------------------------------------------------------

void panel_inspector_render(bool *p_open,
                            World *world,
                            const HierarchyContext *hctx,
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

    igEnd();
}

#endif // EDITOR_BUILD
