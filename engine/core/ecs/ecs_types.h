#ifndef ENGINE_CORE_ECS_TYPES_H
#define ENGINE_CORE_ECS_TYPES_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// ECS core types and constants.
//
// Entity IDs pack an index (lower 20 bits) and a generation counter
// (upper 12 bits) into a single uint32_t.  The generation detects
// use-after-destroy: when an entity slot is recycled its generation is
// bumped, invalidating any stale handles the application may still hold.
// ---------------------------------------------------------------------------

/// Opaque entity identifier.
typedef uint32_t Entity;

/// Component-type identifier returned by world_register_component().
typedef uint8_t ComponentId;

// --- Bit layout ------------------------------------------------------------

#define ECS_INDEX_BITS       20
#define ECS_GENERATION_BITS  12

#define ECS_INDEX_MASK       ((1u << ECS_INDEX_BITS) - 1)          // 0x000F_FFFF
#define ECS_GENERATION_MASK  ((1u << ECS_GENERATION_BITS) - 1)     // 0x0FFF

// --- Limits ----------------------------------------------------------------

/// Maximum number of live entities (2^20 = 1,048,576).
#define ECS_MAX_ENTITIES     (1u << ECS_INDEX_BITS)

/// Maximum number of distinct component types that can be registered.
#define ECS_MAX_COMPONENTS   64

/// Sentinel value — no valid entity will ever have this ID.
#define ENTITY_INVALID       ((Entity)0)

// --- Packing helpers -------------------------------------------------------

/// Extract the slot index from a packed entity ID.
static inline uint32_t entity_index(Entity e) {
    return e & ECS_INDEX_MASK;
}

/// Extract the generation counter from a packed entity ID.
static inline uint32_t entity_generation(Entity e) {
    return (e >> ECS_INDEX_BITS) & ECS_GENERATION_MASK;
}

/// Pack a slot index and generation into an entity ID.
static inline Entity entity_make(uint32_t index, uint32_t generation) {
    return (Entity)((generation & ECS_GENERATION_MASK) << ECS_INDEX_BITS)
         | (index & ECS_INDEX_MASK);
}

#endif // ENGINE_CORE_ECS_TYPES_H
