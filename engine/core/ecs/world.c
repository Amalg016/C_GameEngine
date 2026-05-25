#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr uint32_t InitialEntityCapacity = 256;
constexpr uint32_t InitialFreeCapacity   = 64;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Grow the entity arrays so that `index` is valid.
static bool grow_entities(World *world, uint32_t index) {
    if (index < world->entity_capacity) return true;

    uint32_t new_cap = world->entity_capacity == 0
                     ? InitialEntityCapacity
                     : world->entity_capacity * 2;
    if (new_cap <= index) new_cap = index + 1;
    if (new_cap > ECS_MAX_ENTITIES) new_cap = ECS_MAX_ENTITIES;

    uint32_t *new_gens = realloc(world->generations,
                                 (size_t)new_cap * sizeof(uint32_t));
    if (new_gens == nullptr) return false;

    // Zero-initialise new generation slots.
    memset(new_gens + world->entity_capacity, 0,
           (size_t)(new_cap - world->entity_capacity) * sizeof(uint32_t));

    world->generations    = new_gens;
    world->entity_capacity = new_cap;
    return true;
}

/// Push an index onto the free list.
static bool free_list_push(World *world, uint32_t index) {
    if (world->free_count >= world->free_capacity) {
        uint32_t new_cap = world->free_capacity == 0
                         ? InitialFreeCapacity
                         : world->free_capacity * 2;
        uint32_t *new_list = realloc(world->free_list,
                                     (size_t)new_cap * sizeof(uint32_t));
        if (new_list == nullptr) return false;
        world->free_list     = new_list;
        world->free_capacity = new_cap;
    }
    world->free_list[world->free_count++] = index;
    return true;
}

/// Pop an index from the free list.  Returns UINT32_MAX if empty.
static uint32_t free_list_pop(World *world) {
    if (world->free_count == 0) return UINT32_MAX;
    return world->free_list[--world->free_count];
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

World *world_create(void) {
    World *world = calloc(1, sizeof(World));
    if (world == nullptr) return nullptr;

    // Pre-allocate to guarantee zero heap allocations during runtime hot-paths.
    world->entity_capacity = InitialEntityCapacity;
    world->generations = calloc(world->entity_capacity, sizeof(uint32_t));
    if (world->generations == nullptr) {
        free(world);
        return nullptr;
    }

    world->free_capacity = InitialFreeCapacity;
    world->free_list = malloc(world->free_capacity * sizeof(uint32_t));
    if (world->free_list == nullptr) {
        free(world->generations);
        free(world);
        return nullptr;
    }

    // Index 0 is reserved — ENTITY_INVALID has index 0.
    world->next_index = 1;

    printf("[ecs] world created (pre-allocated %u entities)\n", InitialEntityCapacity);
    return world;
}

void world_destroy(World *world) {
    if (world == nullptr) return;

    // Destroy all component pools.
    for (uint8_t i = 0; i < world->registered_count; ++i) {
        component_pool_destroy(world->pools[i]);
    }

    free(world->generations);
    free(world->free_list);
    free(world);

    printf("[ecs] world destroyed\n");
}

// ---------------------------------------------------------------------------
// Component registration
// ---------------------------------------------------------------------------

ComponentId world_register_component(World *world, size_t element_size) {
    if (world == nullptr || element_size == 0) {
        fprintf(stderr, "[ecs] invalid register_component args\n");
        return UINT8_MAX;
    }

    if (world->registered_count >= ECS_MAX_COMPONENTS) {
        fprintf(stderr, "[ecs] maximum component types (%d) reached\n",
                ECS_MAX_COMPONENTS);
        return UINT8_MAX;
    }

    ComponentId id = world->registered_count;

    ComponentPool *pool = component_pool_create(element_size);
    if (pool == nullptr) {
        fprintf(stderr, "[ecs] failed to create pool for component %u\n", id);
        return UINT8_MAX;
    }

    world->pools[id]              = pool;
    world->pool_element_sizes[id] = element_size;
    world->registered_count++;

    printf("[ecs] registered component %u (size=%zu)\n", id, element_size);
    return id;
}

// ---------------------------------------------------------------------------
// Entity management
// ---------------------------------------------------------------------------

Entity world_entity_create(World *world) {
    if (world == nullptr) return ENTITY_INVALID;

    uint32_t index = free_list_pop(world);

    if (index == UINT32_MAX) {
        // No recycled slots — allocate a fresh index.
        if (world->next_index >= ECS_MAX_ENTITIES) {
            fprintf(stderr, "[ecs] entity limit reached (%u)\n",
                    ECS_MAX_ENTITIES);
            return ENTITY_INVALID;
        }
        index = world->next_index++;
    }

    if (!grow_entities(world, index)) {
        fprintf(stderr, "[ecs] failed to grow entity storage\n");
        return ENTITY_INVALID;
    }

    uint32_t gen = world->generations[index];
    return entity_make(index, gen);
}

void world_entity_destroy(World *world, Entity entity) {
    if (world == nullptr || entity == ENTITY_INVALID) return;
    if (!world_entity_alive(world, entity)) return;

    uint32_t index = entity_index(entity);

    // Remove components from ALL pools.
    for (uint8_t i = 0; i < world->registered_count; ++i) {
        component_pool_remove(world->pools[i], entity);
    }

    // Bump generation so that existing handles become stale.
    world->generations[index] = (world->generations[index] + 1)
                              & ECS_GENERATION_MASK;

    free_list_push(world, index);
}

bool world_entity_alive(const World *world, Entity entity) {
    if (world == nullptr || entity == ENTITY_INVALID) return false;

    uint32_t index = entity_index(entity);
    if (index >= world->entity_capacity) return false;

    return entity_generation(entity) == world->generations[index];
}

// ---------------------------------------------------------------------------
// Component operations
// ---------------------------------------------------------------------------

void *world_add_component(World *world, Entity entity,
                           ComponentId comp_id, const void *data) {
    if (world == nullptr || entity == ENTITY_INVALID) return nullptr;
    if (!world_entity_alive(world, entity)) return nullptr;
    if (comp_id >= world->registered_count) {
        fprintf(stderr, "[ecs] invalid component id %u\n", comp_id);
        return nullptr;
    }

    return component_pool_add(world->pools[comp_id], entity, data);
}

void world_remove_component(World *world, Entity entity,
                             ComponentId comp_id) {
    if (world == nullptr || entity == ENTITY_INVALID) return;
    if (!world_entity_alive(world, entity)) return;
    if (comp_id >= world->registered_count) return;

    component_pool_remove(world->pools[comp_id], entity);
}

void *world_get_component(const World *world, Entity entity,
                           ComponentId comp_id) {
    if (world == nullptr || entity == ENTITY_INVALID) return nullptr;
    if (!world_entity_alive(world, entity)) return nullptr;
    if (comp_id >= world->registered_count) return nullptr;

    return component_pool_get(world->pools[comp_id], entity);
}

bool world_has_component(const World *world, Entity entity,
                          ComponentId comp_id) {
    if (world == nullptr || entity == ENTITY_INVALID) return false;
    if (!world_entity_alive(world, entity)) return false;
    if (comp_id >= world->registered_count) return false;

    return component_pool_has(world->pools[comp_id], entity);
}

// ---------------------------------------------------------------------------
// Pool access
// ---------------------------------------------------------------------------

ComponentPool *world_get_pool(const World *world, ComponentId comp_id) {
    if (world == nullptr) return nullptr;
    if (comp_id >= world->registered_count) return nullptr;
    return world->pools[comp_id];
}

// ---------------------------------------------------------------------------
// world_clear — flush all entities, keep component registrations.
// ---------------------------------------------------------------------------

void world_clear(World *world) {
    if (world == nullptr) return;

    // Clear every component pool (preserves allocation + element size).
    for (uint8_t i = 0; i < world->registered_count; ++i) {
        component_pool_clear(world->pools[i]);
    }

    // Bump every generation so that any outstanding Entity handles become
    // stale.  This is cheaper than tracking which indices were live.
    for (uint32_t i = 0; i < world->entity_capacity; ++i) {
        world->generations[i] = (world->generations[i] + 1)
                              & ECS_GENERATION_MASK;
    }

    // Reset the free list — all slots are effectively reclaimed by resetting
    // next_index.  (We don't need to push them individually.)
    world->free_count = 0;
    world->next_index = 1;  // slot 0 remains reserved (ENTITY_INVALID)

    printf("[ecs] world cleared (component registrations preserved)\n");
}
