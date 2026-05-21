#include "component_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define POOL_INITIAL_CAPACITY  64
#define POOL_INITIAL_SPARSE    256

/// Sentinel value in the sparse array — means "no component for this entity".
#define SPARSE_EMPTY  UINT32_MAX

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Grow the dense arrays to at least `min_cap` slots.
static bool grow_dense(ComponentPool *pool, uint32_t min_cap) {
    uint32_t new_cap = pool->capacity == 0 ? POOL_INITIAL_CAPACITY
                                           : pool->capacity * 2;
    if (new_cap < min_cap) new_cap = min_cap;

    void *new_data = realloc(pool->dense_data, (size_t)new_cap * pool->element_size);
    if (new_data == nullptr) return false;
    pool->dense_data = new_data;

    uint32_t *new_ids = realloc(pool->dense_ids, (size_t)new_cap * sizeof(uint32_t));
    if (new_ids == nullptr) return false;
    pool->dense_ids = new_ids;

    pool->capacity = new_cap;
    return true;
}

/// Grow the sparse array so that `entity_idx` is a valid index.
static bool grow_sparse(ComponentPool *pool, uint32_t entity_idx) {
    if (entity_idx < pool->sparse_size) return true;

    uint32_t new_size = pool->sparse_size == 0 ? POOL_INITIAL_SPARSE
                                               : pool->sparse_size * 2;
    if (new_size <= entity_idx) new_size = entity_idx + 1;

    uint32_t *new_sparse = realloc(pool->sparse,
                                   (size_t)new_size * sizeof(uint32_t));
    if (new_sparse == nullptr) return false;

    // Initialise new slots to SPARSE_EMPTY.
    for (uint32_t i = pool->sparse_size; i < new_size; ++i) {
        new_sparse[i] = SPARSE_EMPTY;
    }

    pool->sparse      = new_sparse;
    pool->sparse_size = new_size;
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ComponentPool *component_pool_create(size_t element_size) {
    if (element_size == 0) {
        fprintf(stderr, "[ecs] component_pool_create: element_size must be > 0\n");
        return nullptr;
    }

    ComponentPool *pool = calloc(1, sizeof(ComponentPool));
    if (pool == nullptr) return nullptr;

    pool->element_size = element_size;
    return pool;
}

void component_pool_destroy(ComponentPool *pool) {
    if (pool == nullptr) return;
    free(pool->dense_data);
    free(pool->dense_ids);
    free(pool->sparse);
    free(pool);
}

// ---------------------------------------------------------------------------
// Per-entity operations
// ---------------------------------------------------------------------------

void *component_pool_add(ComponentPool *pool, Entity entity,
                          const void *data) {
    if (pool == nullptr) return nullptr;

    uint32_t idx = entity_index(entity);

    // Already present?
    if (component_pool_has(pool, entity)) {
        fprintf(stderr,
                "[ecs] component_pool_add: entity %u already has component\n",
                idx);
        return nullptr;
    }

    // Ensure we have room.
    if (!grow_sparse(pool, idx))   return nullptr;
    if (pool->count >= pool->capacity) {
        if (!grow_dense(pool, pool->count + 1)) return nullptr;
    }

    // Append to dense arrays.
    uint32_t dense_idx = pool->count;
    pool->dense_ids[dense_idx] = idx;

    void *dest = (char *)pool->dense_data
               + (size_t)dense_idx * pool->element_size;
    if (data != nullptr) {
        memcpy(dest, data, pool->element_size);
    } else {
        memset(dest, 0, pool->element_size);
    }

    // Link sparse → dense.
    pool->sparse[idx] = dense_idx;
    pool->count++;

    return dest;
}

void component_pool_remove(ComponentPool *pool, Entity entity) {
    if (pool == nullptr) return;
    if (!component_pool_has(pool, entity)) return;

    uint32_t idx       = entity_index(entity);
    uint32_t dense_idx = pool->sparse[idx];
    uint32_t last      = pool->count - 1;

    if (dense_idx != last) {
        // Swap the last element into the hole.
        uint32_t last_entity_idx = pool->dense_ids[last];

        memcpy((char *)pool->dense_data + (size_t)dense_idx * pool->element_size,
               (char *)pool->dense_data + (size_t)last      * pool->element_size,
               pool->element_size);

        pool->dense_ids[dense_idx] = last_entity_idx;
        pool->sparse[last_entity_idx] = dense_idx;
    }

    pool->sparse[idx] = SPARSE_EMPTY;
    pool->count--;
}

void *component_pool_get(const ComponentPool *pool, Entity entity) {
    if (pool == nullptr) return nullptr;
    if (!component_pool_has(pool, entity)) return nullptr;

    uint32_t idx       = entity_index(entity);
    uint32_t dense_idx = pool->sparse[idx];
    return (char *)pool->dense_data + (size_t)dense_idx * pool->element_size;
}

bool component_pool_has(const ComponentPool *pool, Entity entity) {
    if (pool == nullptr) return false;

    uint32_t idx = entity_index(entity);
    if (idx >= pool->sparse_size) return false;
    if (pool->sparse[idx] == SPARSE_EMPTY) return false;

    // Sanity: the dense slot must point back to this entity.
    uint32_t dense_idx = pool->sparse[idx];
    return dense_idx < pool->count && pool->dense_ids[dense_idx] == idx;
}

void component_pool_clear(ComponentPool *pool) {
    if (pool == nullptr) return;

    pool->count = 0;

    // Reset the entire sparse array to "no component".
    for (uint32_t i = 0; i < pool->sparse_size; ++i) {
        pool->sparse[i] = SPARSE_EMPTY;
    }
}
