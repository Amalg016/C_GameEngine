#ifndef ENGINE_CORE_ECS_COMPONENT_POOL_H
#define ENGINE_CORE_ECS_COMPONENT_POOL_H

#include "ecs_types.h"

#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// ComponentPool — sparse-set storage for a single component type.
//
// Each registered component type gets its own pool.  The pool keeps a
// tightly packed "dense" array of component data plus a parallel array of
// entity IDs.  A "sparse" array maps entity indices to dense slots,
// giving O(1) add / remove / lookup.
//
// Removal is a swap-and-pop on the dense arrays, so iteration over live
// components is always a linear scan with no gaps — ideal for cache lines.
// ---------------------------------------------------------------------------

typedef struct ComponentPool {
    void      *dense_data;    // packed component array  (count * element_size)
    uint32_t  *dense_ids;     // entity index at each dense slot
    uint32_t  *sparse;        // entity index → dense index  (UINT32_MAX = absent)
    uint32_t   count;         // number of live components
    uint32_t   capacity;      // allocated dense slots
    uint32_t   sparse_size;   // allocated sparse slots (≥ max entity index seen)
    size_t     element_size;  // sizeof(component type)
} ComponentPool;

// --- Lifecycle -------------------------------------------------------------

/// Create a pool for components of `element_size` bytes.
/// Returns nullptr on allocation failure.
ComponentPool *component_pool_create(size_t element_size);

/// Free all memory owned by the pool.
void component_pool_destroy(ComponentPool *pool);

// --- Per-entity operations -------------------------------------------------

/// Add a component to `entity`.  `data` is copied into the pool.
/// Returns a pointer to the stored copy, or nullptr if the entity already
/// has this component or allocation fails.
void *component_pool_add(ComponentPool *pool, Entity entity, const void *data);

/// Remove the component from `entity`.  No-op if absent.
void component_pool_remove(ComponentPool *pool, Entity entity);

/// Look up the component for `entity`.  Returns nullptr if absent.
void *component_pool_get(const ComponentPool *pool, Entity entity);

/// Returns true if `entity` has a component in this pool.
bool component_pool_has(const ComponentPool *pool, Entity entity);

/// Remove all components from the pool, resetting it to empty.
/// The pool's element_size and allocated memory are preserved.
void component_pool_clear(ComponentPool *pool);

// --- Dense-array access (for iteration) ------------------------------------

/// Get a pointer to the component at dense index `i`.
/// `i` must be in [0, pool->count).
static inline void *component_pool_get_dense(const ComponentPool *pool,
                                             uint32_t i) {
    return (char *)pool->dense_data + (size_t)i * pool->element_size;
}

/// Get the entity index stored at dense slot `i`.
static inline uint32_t component_pool_get_entity(const ComponentPool *pool,
                                                  uint32_t i) {
    return pool->dense_ids[i];
}

#endif // ENGINE_CORE_ECS_COMPONENT_POOL_H
