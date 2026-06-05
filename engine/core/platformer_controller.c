#include "platformer_controller.h"
#include "ecs/ecs.h"
#include "ecs/hierarchy.h"
#include "input.h"

#include <math.h>
#include <stdio.h>

static ComponentId s_controller_id = UINT8_MAX;
static ComponentId s_collider_id = UINT8_MAX;

typedef struct AABB {
    float min_x, max_x;
    float min_y, max_y;
} AABB;

static AABB get_aabb(const LocalTransform *lt, const PlatformerCollider *col) {
    float cx = lt->x + col->offset_x * lt->sx;
    float cy = lt->y + col->offset_y * lt->sy;
    float hw = col->width * lt->sx * 0.5f;
    float hh = col->height * lt->sy * 0.5f;
    return (AABB){
        .min_x = cx - hw,
        .max_x = cx + hw,
        .min_y = cy - hh,
        .max_y = cy + hh
    };
}

static bool aabb_overlap(AABB a, AABB b) {
    return a.min_x < b.max_x && a.max_x > b.min_x &&
           a.min_y < b.max_y && a.max_y > b.min_y;
}

void platformer_system_init(World *world) {
    if (world == nullptr) return;
    if (s_controller_id == UINT8_MAX) {
        s_controller_id = world_register_component(world, sizeof(PlatformerController));
    }
    if (s_collider_id == UINT8_MAX) {
        s_collider_id = world_register_component(world, sizeof(PlatformerCollider));
    }
}

ComponentId platformer_controller_get_id(void) {
    return s_controller_id;
}

ComponentId platformer_collider_get_id(void) {
    return s_collider_id;
}

void platformer_system_update(World *world, const Input *input, const HierarchyContext *hctx, double dt) {
    if (world == nullptr || hctx == nullptr) return;

    // Check if input is active. If not, we don't process keyboard events.
    bool game_active = input_is_game_active(input);

    ComponentPool *ctrl_pool = world_get_pool(world, s_controller_id);
    if (ctrl_pool == nullptr) return;

    float fdt = (float)dt;

    // We iterate through all entities with a PlatformerController component.
    for (uint32_t i = 0; i < ctrl_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(ctrl_pool, i);
        PlatformerController *ctrl = (PlatformerController *)component_pool_get_dense(ctrl_pool, i);
        Entity ent = world_entity_from_index(world, ent_idx);

        LocalTransform *lt = (LocalTransform *)world_get_component(world, ent, hctx->c_local_transform);
        if (lt == nullptr) continue;

        PlatformerCollider *pcol = (PlatformerCollider *)world_get_component(world, ent, s_collider_id);

        // Update Timers
        ctrl->coyote_timer -= fdt;
        ctrl->jump_buffer_timer -= fdt;
        ctrl->dash_cooldown_timer -= fdt;
        ctrl->wall_jump_lock_timer -= fdt;

        // 1. Dash Logic
        if (ctrl->dash_timer > 0.0f) {
            ctrl->dash_timer -= fdt;
            if (ctrl->dash_timer <= 0.0f) {
                ctrl->dash_timer = 0.0f;
                ctrl->velocity_x = 0.0f;
            }
        }

        bool dash_pressed = ctrl->dash_queued;
        ctrl->dash_queued = false;
        if (ctrl->enable_dash && dash_pressed && ctrl->can_dash && ctrl->dash_cooldown_timer <= 0.0f && ctrl->dash_timer <= 0.0f) {
            ctrl->dash_timer = ctrl->dash_duration;
            ctrl->dash_cooldown_timer = ctrl->dash_cooldown;
            ctrl->can_dash = false;
            ctrl->dash_dir_x = (float)ctrl->facing_dir;
            ctrl->velocity_y = 0.0f;
            ctrl->velocity_x = ctrl->dash_dir_x * ctrl->dash_speed;
        }

        // If currently dashing, override standard physics and inputs
        if (ctrl->dash_timer > 0.0f) {
            ctrl->velocity_y = 0.0f;
            ctrl->velocity_x = ctrl->dash_dir_x * ctrl->dash_speed;
        } else {
            // 2. Horizontal Movement
            float move_input = 0.0f;
            if (game_active) {
                if (input_key_down(input, ctrl->key_left)) move_input -= 1.0f;
                if (input_key_down(input, ctrl->key_right)) move_input += 1.0f;
            }

            if (move_input != 0.0f) {
                ctrl->facing_dir = (move_input > 0.0f) ? 1 : -1;
            }

            if (ctrl->wall_jump_lock_timer <= 0.0f) {
                float target_speed = move_input * ctrl->run_speed;
                float accel = (move_input != 0.0f) ? ctrl->run_acceleration : ctrl->run_deceleration;
                
                // Snappy approach to target speed (Hollow Knight style acceleration)
                if (ctrl->velocity_x < target_speed) {
                    ctrl->velocity_x += accel * fdt;
                    if (ctrl->velocity_x > target_speed) ctrl->velocity_x = target_speed;
                } else if (ctrl->velocity_x > target_speed) {
                    ctrl->velocity_x -= accel * fdt;
                    if (ctrl->velocity_x < target_speed) ctrl->velocity_x = target_speed;
                }
            }

            // 3. Vertical Physics (Gravity)
            if (ctrl->is_wall_sliding) {
                // Apply sliding gravity clamp
                ctrl->velocity_y += ctrl->gravity * fdt;
                if (ctrl->velocity_y > ctrl->wall_slide_speed) {
                    ctrl->velocity_y = ctrl->wall_slide_speed;
                }
            } else {
                // Variable Jump Height: Apply extra gravity when jump button is released early
                bool jump_held = game_active && input_key_down(input, ctrl->key_jump);
                if (ctrl->velocity_y < 0.0f && !jump_held) {
                    ctrl->velocity_y += ctrl->gravity * ctrl->jump_cut_gravity_mult * fdt;
                } else {
                    ctrl->velocity_y += ctrl->gravity * fdt;
                }

                // Terminal velocity clamp
                if (ctrl->velocity_y > ctrl->max_fall_speed) {
                    ctrl->velocity_y = ctrl->max_fall_speed;
                }
            }

            // 4. Jump Inputs & Buffering
            if (ctrl->jump_queued) {
                ctrl->jump_queued = false;
                ctrl->jump_buffer_timer = ctrl->jump_buffer_time;
            }

            if (ctrl->jump_buffer_timer > 0.0f) {
                if (ctrl->is_grounded || ctrl->coyote_timer > 0.0f) {
                    // Normal jump
                    ctrl->velocity_y = -ctrl->jump_force;
                    ctrl->jumps_remaining = ctrl->max_jumps - 1;
                    ctrl->is_grounded = false;
                    ctrl->coyote_timer = 0.0f;
                    ctrl->jump_buffer_timer = 0.0f;
                } else if (ctrl->enable_wall_jump && (ctrl->is_wall_sliding || ctrl->wall_dir != 0)) {
                    // Wall jump
                    int32_t jump_dir = (ctrl->is_wall_sliding) ? -ctrl->wall_dir : -ctrl->wall_dir;
                    ctrl->velocity_x = (float)jump_dir * ctrl->wall_jump_force_x;
                    ctrl->velocity_y = -ctrl->wall_jump_force_y;
                    ctrl->wall_jump_lock_timer = ctrl->wall_jump_control_lock;
                    ctrl->facing_dir = jump_dir;
                    ctrl->jumps_remaining = ctrl->max_jumps - 1;
                    ctrl->jump_buffer_timer = 0.0f;
                    ctrl->is_wall_sliding = false;
                } else if (ctrl->enable_double_jump && ctrl->jumps_remaining > 0) {
                    // Double/Air jump
                    ctrl->velocity_y = -ctrl->jump_force;
                    ctrl->jumps_remaining--;
                    ctrl->jump_buffer_timer = 0.0f;
                }
            }
        }

        // 5. Collision Resolution Sweep
        if (pcol != nullptr) {
            ComponentPool *collider_pool = world_get_pool(world, s_collider_id);
            
            // X Sweep
            lt->x += ctrl->velocity_x * fdt;
            AABB player_aabb = get_aabb(lt, pcol);

            if (collider_pool != nullptr) {
                for (uint32_t j = 0; j < collider_pool->count; ++j) {
                    uint32_t other_idx = component_pool_get_entity(collider_pool, j);
                    if (other_idx == ent_idx) continue; // skip self

                    PlatformerCollider *other_col = (PlatformerCollider *)component_pool_get_dense(collider_pool, j);
                    if (!other_col->is_solid) continue;

                    Entity other_ent = world_entity_from_index(world, other_idx);
                    LocalTransform *other_lt = (LocalTransform *)world_get_component(world, other_ent, hctx->c_local_transform);
                    if (other_lt == nullptr) continue;

                    AABB other_aabb = get_aabb(other_lt, other_col);
                    if (aabb_overlap(player_aabb, other_aabb)) {
                        // Overlap resolution
                        if (ctrl->velocity_x > 0.0f) {
                            lt->x -= (player_aabb.max_x - other_aabb.min_x);
                        } else if (ctrl->velocity_x < 0.0f) {
                            lt->x += (other_aabb.max_x - player_aabb.min_x);
                        }
                        ctrl->velocity_x = 0.0f;
                        player_aabb = get_aabb(lt, pcol); // recalculate
                    }
                }
            }

            // Y Sweep
            lt->y += ctrl->velocity_y * fdt;
            player_aabb = get_aabb(lt, pcol);
            bool landed = false;
            bool hit_ceiling = false;

            if (collider_pool != nullptr) {
                for (uint32_t j = 0; j < collider_pool->count; ++j) {
                    uint32_t other_idx = component_pool_get_entity(collider_pool, j);
                    if (other_idx == ent_idx) continue;

                    PlatformerCollider *other_col = (PlatformerCollider *)component_pool_get_dense(collider_pool, j);
                    if (!other_col->is_solid) continue;

                    Entity other_ent = world_entity_from_index(world, other_idx);
                    LocalTransform *other_lt = (LocalTransform *)world_get_component(world, other_ent, hctx->c_local_transform);
                    if (other_lt == nullptr) continue;

                    AABB other_aabb = get_aabb(other_lt, other_col);
                    if (aabb_overlap(player_aabb, other_aabb)) {
                        if (ctrl->velocity_y > 0.0f) {
                            lt->y -= (player_aabb.max_y - other_aabb.min_y);
                            landed = true;
                        } else if (ctrl->velocity_y < 0.0f) {
                            lt->y += (other_aabb.max_y - player_aabb.min_y);
                            hit_ceiling = true;
                        }
                        ctrl->velocity_y = 0.0f;
                        player_aabb = get_aabb(lt, pcol); // recalculate
                    }
                }
            }

            // Update Grounded State
            if (landed) {
                ctrl->is_grounded = true;
                ctrl->jumps_remaining = ctrl->max_jumps;
                ctrl->coyote_timer = ctrl->coyote_time;
                ctrl->can_dash = true;
            } else {
                ctrl->is_grounded = false;
                if (hit_ceiling) {
                    ctrl->velocity_y = 0.0f;
                }
            }

            // 6. Wall Slide & Contacts Detection
            if (!ctrl->is_grounded) {
                bool wall_left = false;
                bool wall_right = false;

                // Check Left: Shift AABB slightly left
                AABB check_left = player_aabb;
                check_left.min_x -= 0.02f;
                check_left.max_x -= 0.02f;

                // Check Right: Shift AABB slightly right
                AABB check_right = player_aabb;
                check_right.min_x += 0.02f;
                check_right.max_x += 0.02f;

                if (collider_pool != nullptr) {
                    for (uint32_t j = 0; j < collider_pool->count; ++j) {
                        uint32_t other_idx = component_pool_get_entity(collider_pool, j);
                        if (other_idx == ent_idx) continue;

                        PlatformerCollider *other_col = (PlatformerCollider *)component_pool_get_dense(collider_pool, j);
                        if (!other_col->is_solid) continue;

                        Entity other_ent = world_entity_from_index(world, other_idx);
                        LocalTransform *other_lt = (LocalTransform *)world_get_component(world, other_ent, hctx->c_local_transform);
                        if (other_lt == nullptr) continue;

                        AABB other_aabb = get_aabb(other_lt, other_col);
                        if (aabb_overlap(check_left, other_aabb)) {
                            wall_left = true;
                        }
                        if (aabb_overlap(check_right, other_aabb)) {
                            wall_right = true;
                        }
                    }
                }

                ctrl->wall_dir = 0;
                if (wall_left) ctrl->wall_dir = -1;
                else if (wall_right) ctrl->wall_dir = 1;

                // Slide down logic
                if (ctrl->enable_wall_jump && ctrl->wall_dir != 0 && ctrl->velocity_y > 0.0f) {
                    float move_input = 0.0f;
                    if (game_active) {
                        if (input_key_down(input, ctrl->key_left)) move_input -= 1.0f;
                        if (input_key_down(input, ctrl->key_right)) move_input += 1.0f;
                    }

                    // Must hold direction against the wall to slide
                    if ((ctrl->wall_dir == -1 && move_input < 0.0f) || (ctrl->wall_dir == 1 && move_input > 0.0f)) {
                        ctrl->is_wall_sliding = true;
                        ctrl->can_dash = true; // reset dash on wall slide (very standard/forgiving)
                    } else {
                        ctrl->is_wall_sliding = false;
                    }
                } else {
                    ctrl->is_wall_sliding = false;
                }
            } else {
                ctrl->wall_dir = 0;
                ctrl->is_wall_sliding = false;
            }
        } else {
            // Fallback if no collider component: simple movement along velocities
            lt->x += ctrl->velocity_x * fdt;
            lt->y += ctrl->velocity_y * fdt;
            ctrl->is_grounded = (lt->y >= 0.0f); // fake ground
            if (ctrl->is_grounded) {
                lt->y = 0.0f;
                ctrl->velocity_y = 0.0f;
                ctrl->jumps_remaining = ctrl->max_jumps;
                ctrl->coyote_timer = ctrl->coyote_time;
                ctrl->can_dash = true;
            }
            ctrl->wall_dir = 0;
            ctrl->is_wall_sliding = false;
        }
    }
}
