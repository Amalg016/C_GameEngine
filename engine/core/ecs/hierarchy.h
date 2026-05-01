#ifndef ENGINE_CORE_ECS_HIERARCHY_H
#define ENGINE_CORE_ECS_HIERARCHY_H

#include "ecs_types.h"
#include "world.h"

// ---------------------------------------------------------------------------
// Transform Hierarchy (Scene Graph)
//
// Provides parent-child entity relationships with hierarchical 2D transform
// propagation.  Local transforms are defined relative to a parent; a system
// computes absolute world transforms by traversing the tree top-down.
//
// Usage:
//   HierarchyContext hctx = hierarchy_init(world);   // registers components
//
//   Entity parent = world_entity_create(world);
//   Entity child  = world_entity_create(world);
//   // ... add LocalTransform to both ...
//   hierarchy_set_parent(world, &hctx, child, parent);
//
//   // Each frame (fixed update):
//   hierarchy_snapshot_positions(world, &hctx);   // before physics
//   // ... run physics / movement ...
//   hierarchy_update_transforms(world, &hctx);    // after physics
//
//   // Rendering uses WorldTransform + PreviousPosition for interpolation.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

/// Local-space transform — position and scale relative to the parent.
/// Root entities (no parent) treat this as world-space.
typedef struct LocalTransform {
    float x, y;      // position offset from parent
    float sx, sy;     // scale (1.0 = same as parent)
} LocalTransform;

/// World-space transform — computed by hierarchy_update_transforms().
/// Read-only from the application's perspective.
typedef struct WorldTransform {
    float x, y;      // absolute position in NDC
    float sx, sy;    // absolute scale
} WorldTransform;

/// Previous world position — snapshot taken before physics for render
/// interpolation.  Entities that need smooth motion should have this.
typedef struct PreviousPosition {
    float x, y;
} PreviousPosition;

/// Parent-child relationship — intrusive doubly-linked sibling list.
///
/// A parent's children form a ring:
///   parent.first_child → child_A → child_B → ... → ENTITY_INVALID
///
/// Each child knows its parent and its neighbours in the sibling list.
typedef struct Hierarchy {
    Entity parent;          // ENTITY_INVALID = root
    Entity first_child;     // ENTITY_INVALID = no children (leaf)
    Entity next_sibling;    // ENTITY_INVALID = last in list
    Entity prev_sibling;    // ENTITY_INVALID = first in list
} Hierarchy;

// ---------------------------------------------------------------------------
// Context — holds the ComponentIds registered by hierarchy_init().
// ---------------------------------------------------------------------------

typedef struct HierarchyContext {
    ComponentId c_local_transform;
    ComponentId c_world_transform;
    ComponentId c_hierarchy;
    ComponentId c_prev_position;
} HierarchyContext;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// Register all hierarchy-related components with the world.
/// Call once at startup before creating any entities.
/// Returns a context containing the assigned ComponentIds.
HierarchyContext hierarchy_init(World *world);

/// Attach `child` as a child of `parent`.
/// Both entities receive a Hierarchy component if they don't already have one.
/// The child also receives a WorldTransform if it doesn't have one.
/// If `child` already has a parent, it is detached first.
void hierarchy_set_parent(World *world, const HierarchyContext *ctx,
                          Entity child, Entity parent);

/// Detach `child` from its parent, making it a root entity.
/// No-op if the child has no parent.
void hierarchy_remove_parent(World *world, const HierarchyContext *ctx,
                             Entity child);

/// Propagate LocalTransform down the hierarchy tree, computing WorldTransform
/// for every entity.
///
/// Call after physics / movement, before rendering.
///
/// For root entities (no Hierarchy, or parent == ENTITY_INVALID):
///   WorldTransform = LocalTransform
///
/// For children:
///   world.x  = parent_world.x  + local.x * parent_world.sx
///   world.y  = parent_world.y  + local.y * parent_world.sy
///   world.sx = parent_world.sx * local.sx
///   world.sy = parent_world.sy * local.sy
void hierarchy_update_transforms(World *world, const HierarchyContext *ctx);

/// Snapshot current WorldTransform positions into PreviousPosition.
/// Call BEFORE physics / movement each fixed step so the render system
/// can interpolate between prev and current for smooth motion.
void hierarchy_snapshot_positions(World *world, const HierarchyContext *ctx);

#endif // ENGINE_CORE_ECS_HIERARCHY_H
