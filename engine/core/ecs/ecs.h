#ifndef ENGINE_CORE_ECS_H
#define ENGINE_CORE_ECS_H

// ---------------------------------------------------------------------------
// ECS umbrella header — include this single file from application code.
//
// Provides the full ECS API plus convenience macros for type-safe,
// boilerplate-free component access.
// ---------------------------------------------------------------------------

#include "ecs_types.h"
#include "component_pool.h"
#include "world.h"
#include "hierarchy.h"
#include "camera.h"

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------

/// Register a component type by its struct name.
/// Usage:  ComponentId c_pos = ECS_REGISTER(world, Position);
#define ECS_REGISTER(world, Type) \
    world_register_component((world), sizeof(Type))

/// Add a component with a compound literal initialiser.
/// Usage:  ECS_ADD(world, entity, c_pos, Position, { .x = 0, .y = 0 });
/// Returns a typed pointer to the stored component.
#define ECS_ADD(world, entity, comp_id, Type, ...) \
    ((Type *)world_add_component((world), (entity), (comp_id), \
                                  &(Type)__VA_ARGS__))

/// Get a typed pointer to a component on an entity.
/// Returns nullptr if the entity doesn't have this component.
/// Usage:  Position *p = ECS_GET(world, entity, c_pos, Position);
#define ECS_GET(world, entity, comp_id, Type) \
    ((Type *)world_get_component((world), (entity), (comp_id)))

// ---------------------------------------------------------------------------
// Iteration macros
//
// These iterate the dense array of a component pool, giving you direct
// access to each component and its entity index.  For multi-component
// queries the convention is to iterate the smallest pool and look up
// the other components via world_get_component().
// ---------------------------------------------------------------------------

/// Iterate all components of a single type.
///
/// Usage:
///   ECS_EACH(world, c_pos, Position, pos, ent_idx) {
///       printf("entity %u at (%.1f, %.1f)\n", ent_idx, pos->x, pos->y);
///   }
///
/// `var`     — pointer variable name for the current component (Type *)
/// `ent_var` — variable name for the current entity index (uint32_t)
#define ECS_EACH(world, comp_id, Type, var, ent_var)                        \
    for (uint32_t _ecs_i_ = 0,                                             \
                  _ecs_n_ = world_get_pool((world), (comp_id))              \
                            ? world_get_pool((world), (comp_id))->count     \
                            : 0;                                            \
         _ecs_i_ < _ecs_n_; ++_ecs_i_)                                     \
        for (Type *var = (Type *)component_pool_get_dense(                  \
                             world_get_pool((world), (comp_id)), _ecs_i_);  \
             var != nullptr; var = nullptr)                                 \
            for (uint32_t ent_var = component_pool_get_entity(              \
                             world_get_pool((world), (comp_id)), _ecs_i_);  \
                 var != nullptr; var = nullptr)

/// Iterate entities that have BOTH of two component types.
///
/// Iterates the first pool's dense array and looks up the second component
/// for each entity.  Skips entities that lack the second component.
///
/// Usage:
///   ECS_EACH2(world, c_pos, Position, pos, c_vel, Velocity, vel, ent_idx) {
///       pos->x += vel->dx * dt;
///   }
#define ECS_EACH2(world, id1, T1, v1, id2, T2, v2, ent_var)                \
    for (uint32_t _ecs2_i_ = 0,                                            \
                  _ecs2_n_ = world_get_pool((world), (id1))                 \
                             ? world_get_pool((world), (id1))->count        \
                             : 0;                                           \
         _ecs2_i_ < _ecs2_n_; ++_ecs2_i_)                                  \
        for (uint32_t ent_var = component_pool_get_entity(                  \
                 world_get_pool((world), (id1)), _ecs2_i_);                 \
             ent_var != UINT32_MAX; ent_var = UINT32_MAX)                   \
            for (T1 *v1 = (T1 *)component_pool_get_dense(                  \
                     world_get_pool((world), (id1)), _ecs2_i_);             \
                 v1 != nullptr; v1 = nullptr)                               \
                for (T2 *v2 = (T2 *)component_pool_get(                    \
                         world_get_pool((world), (id2)),                    \
                         entity_make(ent_var, 0));                          \
                     v2 != nullptr; v2 = nullptr)

#endif // ENGINE_CORE_ECS_H
