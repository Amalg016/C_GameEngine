#ifdef EDITOR_BUILD

#include "gizmo_system.h"
#include "../core/ecs/world.h"
#include "../core/ecs/component_pool.h"
#include "../core/ecs/hierarchy.h"
#include "../core/platformer_controller.h"
#include "../core/debug_draw.h"

void gizmo_system_draw_colliders(const World *world, const HierarchyContext *hctx) {
    if (world == nullptr || hctx == nullptr) {
        return;
    }

    ComponentId c_plat_col = platformer_collider_get_id();
    if (c_plat_col == UINT8_MAX) {
        return;
    }

    ComponentPool *collider_pool = world_get_pool(world, c_plat_col);
    if (collider_pool == nullptr || collider_pool->count == 0) {
        return;
    }

    // Green color with 0.8 opacity
    constexpr float GizmoColor[4] = { 0.0f, 1.0f, 0.0f, 0.8f };

    for (uint32_t i = 0; i < collider_pool->count; ++i) {
        PlatformerCollider *col = (PlatformerCollider *)component_pool_get_dense(collider_pool, i);
        if (!col->show_gizmo) {
            continue;
        }

        uint32_t ent_idx = component_pool_get_entity(collider_pool, i);
        Entity ent = world_entity_from_index(world, ent_idx);

        float tx = 0.0f;
        float ty = 0.0f;
        float tsx = 1.0f;
        float tsy = 1.0f;

        WorldTransform *wt = (WorldTransform *)world_get_component(world, ent, hctx->c_world_transform);
        if (wt != nullptr) {
            tx = wt->x;
            ty = wt->y;
            tsx = wt->sx;
            tsy = wt->sy;
        } else {
            LocalTransform *lt = (LocalTransform *)world_get_component(world, ent, hctx->c_local_transform);
            if (lt != nullptr) {
                tx = lt->x;
                ty = lt->y;
                tsx = lt->sx;
                tsy = lt->sy;
            }
        }

        // Center = transform position + collider offset scaled by transform scale
        float cx = tx + col->offset_x * tsx;
        float cy = ty + col->offset_y * tsy;

        // Size = collider size scaled by transform scale
        float w = col->width * tsx;
        float h = col->height * tsy;

        debug_draw_rect(cx, cy, w, h, GizmoColor, DebugDrawDefaultThickness);
    }
}

#endif // EDITOR_BUILD
