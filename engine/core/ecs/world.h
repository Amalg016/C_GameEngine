#ifndef ENGINE_CORE_ECS_WORLD_H
#define ENGINE_CORE_ECS_WORLD_H

#include "ecs_types.h"
#include "component_pool.h"

#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// World — the top-level ECS container.
//
// Owns all entities and component pools.  Typical usage:
//
//   World *world = world_create();
//
//   ComponentId c_pos = world_register_component(world, sizeof(Position));
//   ComponentId c_vel = world_register_component(world, sizeof(Velocity));
//
//   Entity e = world_entity_create(world);
//   world_add_component(world, e, c_pos, &(Position){.x=0, .y=0});
//   world_add_component(world, e, c_vel, &(Velocity){.dx=1, .dy=2});
//
//   // iterate all entities with Position + Velocity:
//   ComponentPool *pool = world_get_pool(world, c_vel);
//   for (uint32_t i = 0; i < pool->count; ++i) {
//       Entity ent = entity_make(component_pool_get_entity(pool, i),
//                                /* generation check needed */);
//       Position *p = world_get_component(world, ent, c_pos);
//       Velocity *v = component_pool_get_dense(pool, i);
//       if (p) { p->x += v->dx; p->y += v->dy; }
//   }
//
//   world_destroy(world);
// ---------------------------------------------------------------------------

typedef struct World {
    // --- Entity management -------------------------------------------------
    uint32_t  *generations;      // generation counter per entity index
    uint32_t  *free_list;        // stack of recycled entity indices
    uint32_t   free_count;       // number of entries in free_list
    uint32_t   free_capacity;    // allocated free_list slots
    uint32_t   entity_capacity;  // allocated generation slots
    uint32_t   next_index;       // next fresh index (if free_list empty)

    // --- Component storage -------------------------------------------------
    ComponentPool *pools[ECS_MAX_COMPONENTS];          // one pool per type
    size_t         pool_element_sizes[ECS_MAX_COMPONENTS]; // recorded sizes
    uint8_t        registered_count;                   // types registered so far
} World;

// --- Lifecycle -------------------------------------------------------------

/// Create an empty world.  Returns nullptr on failure.
World *world_create(void);

/// Destroy the world and free all entity / component memory.
void world_destroy(World *world);

// --- Component registration -----------------------------------------------

/// Register a new component type with the given element size.
/// Must be called before any entity uses this component.
/// Returns the assigned ComponentId, or UINT8_MAX on failure.
ComponentId world_register_component(World *world, size_t element_size);

// --- Entity management -----------------------------------------------------

/// Create a new entity.  Returns ENTITY_INVALID on failure.
Entity world_entity_create(World *world);

/// Destroy an entity, removing all its components and recycling its index.
void world_entity_destroy(World *world, Entity entity);

/// Returns true if `entity` is still alive (generation matches).
bool world_entity_alive(const World *world, Entity entity);

// --- Component operations --------------------------------------------------

/// Attach a component to an entity.  `data` is copied into the pool.
/// Pass nullptr for `data` to zero-initialise the component.
/// Returns a pointer to the stored component, or nullptr on failure.
void *world_add_component(World *world, Entity entity,
                           ComponentId comp_id, const void *data);

/// Remove a component from an entity.  No-op if absent.
void world_remove_component(World *world, Entity entity,
                             ComponentId comp_id);

/// Look up a component on an entity.  Returns nullptr if absent.
void *world_get_component(const World *world, Entity entity,
                           ComponentId comp_id);

/// Check whether an entity has a specific component.
bool world_has_component(const World *world, Entity entity,
                          ComponentId comp_id);

// --- Pool access (for iteration) -------------------------------------------

/// Get the raw component pool for a registered component type.
/// Returns nullptr if `comp_id` is out of range or unregistered.
ComponentPool *world_get_pool(const World *world, ComponentId comp_id);

/// Destroy all entities and clear all component pool contents.
/// Component type registrations are preserved (pools remain allocated
/// and their element sizes unchanged).  After this call, the world has
/// zero live entities.  Existing Entity handles are invalidated.
void world_clear(World *world);

#endif // ENGINE_CORE_ECS_WORLD_H
