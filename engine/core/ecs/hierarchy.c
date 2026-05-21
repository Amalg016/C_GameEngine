#include "hierarchy.h"
#include "component_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// hierarchy_init — register all hierarchy components.
// ---------------------------------------------------------------------------

HierarchyContext hierarchy_init(World *world) {
    HierarchyContext ctx = {
        .c_local_transform = world_register_component(world, sizeof(LocalTransform)),
        .c_world_transform = world_register_component(world, sizeof(WorldTransform)),
        .c_hierarchy       = world_register_component(world, sizeof(Hierarchy)),
        .c_prev_position   = world_register_component(world, sizeof(PreviousPosition)),
    };

    printf("[hierarchy] initialised (local=%u world=%u hier=%u prev=%u)\n",
           ctx.c_local_transform, ctx.c_world_transform,
           ctx.c_hierarchy, ctx.c_prev_position);

    return ctx;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Ensure an entity has a Hierarchy component, adding a default one if absent.
static Hierarchy *ensure_hierarchy(World *world, ComponentId c_hier,
                                   Entity entity) {
    Hierarchy *h = (Hierarchy *)world_get_component(world, entity, c_hier);
    if (h != nullptr) return h;

    Hierarchy init = {
        .parent       = ENTITY_INVALID,
        .first_child  = ENTITY_INVALID,
        .next_sibling = ENTITY_INVALID,
        .prev_sibling = ENTITY_INVALID,
    };
    return (Hierarchy *)world_add_component(world, entity, c_hier, &init);
}

/// Ensure an entity has a WorldTransform, adding a zeroed one if absent.
static WorldTransform *ensure_world_transform(World *world, ComponentId c_wt,
                                               Entity entity) {
    WorldTransform *wt = (WorldTransform *)world_get_component(world, entity, c_wt);
    if (wt != nullptr) return wt;

    WorldTransform init = { .x = 0, .y = 0, .sx = 1, .sy = 1 };
    return (WorldTransform *)world_add_component(world, entity, c_wt, &init);
}

/// Detach `child` from its current parent's sibling list.
/// Does NOT reset child->parent (caller does that).
static void detach_from_siblings(World *world, ComponentId c_hier,
                                  Entity child_entity, Hierarchy *child_h) {
    if (child_h->parent == ENTITY_INVALID) return;

    // Fix previous sibling's next pointer.
    if (child_h->prev_sibling != ENTITY_INVALID) {
        Hierarchy *prev = (Hierarchy *)world_get_component(
            world, child_h->prev_sibling, c_hier);
        if (prev != nullptr) prev->next_sibling = child_h->next_sibling;
    }

    // Fix next sibling's prev pointer.
    if (child_h->next_sibling != ENTITY_INVALID) {
        Hierarchy *next = (Hierarchy *)world_get_component(
            world, child_h->next_sibling, c_hier);
        if (next != nullptr) next->prev_sibling = child_h->prev_sibling;
    }

    // If we were the first child, update the parent's first_child.
    Hierarchy *parent_h = (Hierarchy *)world_get_component(
        world, child_h->parent, c_hier);
    if (parent_h != nullptr && parent_h->first_child == child_entity) {
        parent_h->first_child = child_h->next_sibling;
    }

    child_h->next_sibling = ENTITY_INVALID;
    child_h->prev_sibling = ENTITY_INVALID;
}

// ---------------------------------------------------------------------------
// hierarchy_set_parent
// ---------------------------------------------------------------------------

void hierarchy_set_parent(World *world, const HierarchyContext *ctx,
                          Entity child, Entity parent) {
    if (world == nullptr || ctx == nullptr) return;
    if (child == ENTITY_INVALID || parent == ENTITY_INVALID) return;
    if (child == parent) return;

    Hierarchy *child_h  = ensure_hierarchy(world, ctx->c_hierarchy, child);
    Hierarchy *parent_h = ensure_hierarchy(world, ctx->c_hierarchy, parent);
    if (child_h == nullptr || parent_h == nullptr) return;

    // Ensure both have WorldTransform.
    ensure_world_transform(world, ctx->c_world_transform, child);
    ensure_world_transform(world, ctx->c_world_transform, parent);

    // Already parented to this entity?
    if (child_h->parent == parent) return;

    // Detach from current parent if any.
    if (child_h->parent != ENTITY_INVALID) {
        detach_from_siblings(world, ctx->c_hierarchy, child, child_h);
    }

    // Attach to new parent: prepend to the parent's child list.
    child_h->parent       = parent;
    child_h->next_sibling = parent_h->first_child;
    child_h->prev_sibling = ENTITY_INVALID;

    // Update the old first child's prev pointer.
    if (parent_h->first_child != ENTITY_INVALID) {
        Hierarchy *old_first = (Hierarchy *)world_get_component(
            world, parent_h->first_child, ctx->c_hierarchy);
        if (old_first != nullptr) {
            old_first->prev_sibling = child;
        }
    }

    parent_h->first_child = child;
}

// ---------------------------------------------------------------------------
// hierarchy_remove_parent
// ---------------------------------------------------------------------------

void hierarchy_remove_parent(World *world, const HierarchyContext *ctx,
                             Entity child) {
    if (world == nullptr || ctx == nullptr) return;
    if (child == ENTITY_INVALID) return;

    Hierarchy *child_h = (Hierarchy *)world_get_component(
        world, child, ctx->c_hierarchy);
    if (child_h == nullptr || child_h->parent == ENTITY_INVALID) return;

    detach_from_siblings(world, ctx->c_hierarchy, child, child_h);
    child_h->parent = ENTITY_INVALID;
}

// ---------------------------------------------------------------------------
// Transform propagation — DFS
// ---------------------------------------------------------------------------

/// Recursively propagate world transforms down the tree.
static void propagate_recursive(World *world, const HierarchyContext *ctx,
                                Entity entity, const WorldTransform *parent_wt) {
    // Get this entity's local transform.
    LocalTransform *local = (LocalTransform *)world_get_component(
        world, entity, ctx->c_local_transform);
    if (local == nullptr) return;

    // Compute this entity's world transform.
    WorldTransform *wt = (WorldTransform *)world_get_component(
        world, entity, ctx->c_world_transform);
    if (wt == nullptr) return;

    if (parent_wt != nullptr) {
        // Child: compose with parent.
        wt->x  = parent_wt->x + local->x * parent_wt->sx;
        wt->y  = parent_wt->y + local->y * parent_wt->sy;
        wt->sx = parent_wt->sx * local->sx;
        wt->sy = parent_wt->sy * local->sy;
    } else {
        // Root: local IS world.
        wt->x  = local->x;
        wt->y  = local->y;
        wt->sx = local->sx;
        wt->sy = local->sy;
    }

    // Recurse into children.
    Hierarchy *h = (Hierarchy *)world_get_component(
        world, entity, ctx->c_hierarchy);
    if (h == nullptr) return;

    Entity child_ent = h->first_child;
    while (child_ent != ENTITY_INVALID) {
        propagate_recursive(world, ctx, child_ent, wt);

        Hierarchy *child_h = (Hierarchy *)world_get_component(
            world, child_ent, ctx->c_hierarchy);
        child_ent = (child_h != nullptr) ? child_h->next_sibling : ENTITY_INVALID;
    }
}

// ---------------------------------------------------------------------------
// hierarchy_update_transforms
// ---------------------------------------------------------------------------

void hierarchy_update_transforms(World *world, const HierarchyContext *ctx) {
    if (world == nullptr || ctx == nullptr) return;

    ComponentPool *lt_pool = world_get_pool(world, ctx->c_local_transform);
    if (lt_pool == nullptr || lt_pool->count == 0) return;

    // Iterate every entity that has a LocalTransform.
    // Only process roots (no Hierarchy, or parent == ENTITY_INVALID).
    for (uint32_t i = 0; i < lt_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(lt_pool, i);
        Entity ent = world_entity_from_index(world, ent_idx);

        Hierarchy *h = (Hierarchy *)world_get_component(
            world, ent, ctx->c_hierarchy);

        // Skip non-roots — they'll be visited during DFS from their root.
        if (h != nullptr && h->parent != ENTITY_INVALID) continue;

        // This is a root — ensure it has a WorldTransform and propagate.
        ensure_world_transform(world, ctx->c_world_transform, ent);
        propagate_recursive(world, ctx, ent, nullptr);
    }
}

// ---------------------------------------------------------------------------
// hierarchy_snapshot_positions
// ---------------------------------------------------------------------------

void hierarchy_snapshot_positions(World *world, const HierarchyContext *ctx) {
    if (world == nullptr || ctx == nullptr) return;

    ComponentPool *pp_pool = world_get_pool(world, ctx->c_prev_position);
    if (pp_pool == nullptr || pp_pool->count == 0) return;

    for (uint32_t i = 0; i < pp_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(pp_pool, i);
        PreviousPosition *pp = (PreviousPosition *)component_pool_get_dense(
                                    pp_pool, i);
        Entity ent = world_entity_from_index(world, ent_idx);

        WorldTransform *wt = (WorldTransform *)world_get_component(
            world, ent, ctx->c_world_transform);

        if (wt != nullptr) {
            pp->x = wt->x;
            pp->y = wt->y;
        }
    }
}
