#ifndef ENGINE_CORE_PLATFORMER_CONTROLLER_H
#define ENGINE_CORE_PLATFORMER_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

// Forward declarations to keep headers clean (Rule 2)
typedef struct World World;
typedef struct Input Input;
typedef struct HierarchyContext HierarchyContext;
typedef uint8_t ComponentId;

typedef struct PlatformerController {
    // --- Customizable Physics Parameters ---
    float gravity;                  // standard gravity acceleration (units/sec^2)
    float max_fall_speed;           // max downward velocity (units/sec)
    
    float run_speed;                // horizontal max speed (units/sec)
    float run_acceleration;         // acceleration rate (units/sec^2)
    float run_deceleration;         // deceleration rate (units/sec^2)
    
    float jump_force;               // initial upward velocity (units/sec)
    float jump_cut_gravity_mult;    // gravity multiplier when jump is released early
    float coyote_time;              // grace period to jump after leaving ground (sec)
    float jump_buffer_time;         // time window to store a jump input before landing (sec)
    int32_t max_jumps;              // total allowed jumps (e.g., 2 for double jump)
    
    float dash_speed;               // speed during dash (units/sec)
    float dash_duration;            // duration of the dash (sec)
    float dash_cooldown;            // cooldown between dashes (sec)
    
    float wall_slide_speed;         // terminal velocity when sliding down walls
    float wall_jump_force_x;        // horizontal wall jump force
    float wall_jump_force_y;        // vertical wall jump force
    float wall_jump_control_lock;   // duration player horizontal control is locked during wall jump (sec)

    // --- Key Bindings ---
    int32_t key_left;
    int32_t key_right;
    int32_t key_jump;
    int32_t key_dash;

    // --- Ability Toggle Flags ---
    bool enable_double_jump;
    bool enable_wall_jump;
    bool enable_dash;

    // --- Runtime States ---
    float velocity_x;
    float velocity_y;
    bool is_grounded;
    bool is_wall_sliding;
    int32_t wall_dir;               // -1 for left wall, 1 for right wall, 0 for none
    int32_t jumps_remaining;
    
    float coyote_timer;
    float jump_buffer_timer;
    
    float dash_timer;               // active dash time remaining (0 if not dashing)
    float dash_cooldown_timer;
    bool can_dash;                  // reset when grounded/wall sliding
    float dash_dir_x;               // direction of the active dash
    
    float wall_jump_lock_timer;     // locks horizontal input temporarily
    int32_t facing_dir;             // -1 for left, 1 for right

    // Transient input queueing (prevents missed inputs in fixed update)
    bool jump_queued;
    bool dash_queued;
} PlatformerController;

typedef struct PlatformerCollider {
    float width;                    // Width of collider (local scale)
    float height;                   // Height of collider (local scale)
    float offset_x;                 // Center offset relative to local transform
    float offset_y;                 // Center offset relative to local transform
    bool is_solid;                  // Blocks platformer controllers
    bool show_gizmo;                // Show collider boundaries in the editor
} PlatformerCollider;

/// Registers PlatformerController and PlatformerCollider components.
/// Must be called during engine initialization.
void platformer_system_init(World *world);

/// Returns the ComponentId for PlatformerController (registered during platformer_system_init).
[[nodiscard]]
ComponentId platformer_controller_get_id(void);

/// Returns the ComponentId for PlatformerCollider (registered during platformer_system_init).
[[nodiscard]]
ComponentId platformer_collider_get_id(void);

/// Updates all platformer controllers.
/// Runs in the fixed update tick loop.
void platformer_system_update(World *world, const Input *input, const HierarchyContext *hctx, double dt);

#endif // ENGINE_CORE_PLATFORMER_CONTROLLER_H
