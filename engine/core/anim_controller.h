#ifndef ENGINE_CORE_ANIM_CONTROLLER_H
#define ENGINE_CORE_ANIM_CONTROLLER_H

#include "animation.h"

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Animation Controller — state machine for driving Animator playback.
//
// A controller defines:
//   - Parameters (float, int, bool, trigger) that game code can set at runtime
//   - States, each referencing an AnimClip by name
//   - Transitions between states, gated by conditions on parameters
//
// The Animator component evaluates transitions each frame and switches
// clips automatically when conditions are met.
// ---------------------------------------------------------------------------

constexpr uint32_t AnimCtrlMaxParams      = 16;
constexpr uint32_t AnimCtrlMaxStates      = 32;
constexpr uint32_t AnimCtrlMaxTransitions = 8;   // per state
constexpr uint32_t AnimCtrlMaxConditions  = 4;   // per transition

/// Parameter types.
typedef enum AnimParamType {
    ANIM_PARAM_FLOAT   = 0,
    ANIM_PARAM_INT     = 1,
    ANIM_PARAM_BOOL    = 2,
    ANIM_PARAM_TRIGGER = 3,
} AnimParamType;

/// A named parameter definition with a default value.
typedef struct AnimParam {
    char          name[AnimNameMaxLen];
    AnimParamType type;
    union {
        float   f;
        int32_t i;
        bool    b;
    } default_value;
} AnimParam;

/// Runtime parameter value (indexed, no name needed).
typedef struct AnimParamValue {
    union {
        float   f;
        int32_t i;
        bool    b;
    };
} AnimParamValue;

/// Comparison operators for transition conditions.
typedef enum AnimConditionOp {
    ANIM_OP_GREATER = 0,   // >
    ANIM_OP_LESS    = 1,   // <
    ANIM_OP_EQUAL   = 2,   // ==
    ANIM_OP_NOT_EQ  = 3,   // !=
} AnimConditionOp;

/// A single condition: "param[param_index] <op> threshold".
typedef struct AnimCondition {
    uint32_t        param_index;   // index into AnimController.params[]
    AnimConditionOp op;
    AnimParamValue  threshold;
} AnimCondition;

/// A transition from one state to another.
typedef struct AnimTransition {
    uint32_t      target_state;    // index into AnimController.states[]
    bool          has_exit_time;   // only evaluate when current clip finishes
    AnimCondition conditions[AnimCtrlMaxConditions];
    uint32_t      condition_count;
} AnimTransition;

/// A state in the controller state machine.
typedef struct AnimState {
    char           name[AnimNameMaxLen];
    char           clip_name[AnimNameMaxLen]; // references AnimClip by name
    float          speed;                     // playback speed multiplier
    AnimTransition transitions[AnimCtrlMaxTransitions];
    uint32_t       transition_count;
} AnimState;

/// The full controller asset — loaded from a .controller.meta JSON file.
typedef struct AnimController {
    char         anim_path[AnimPathMaxLen];    // path to the .anim.meta
    
    // --- Resolved at Cache Load Time (borrowed pointers) ---
    AnimData    *anim_data;                    // owned by cache
    AssetHandle  texture;                      // texture handle
    uint32_t     tex_width;
    uint32_t     tex_height;

    AnimParam    params[AnimCtrlMaxParams];
    uint32_t     param_count;
    AnimState    states[AnimCtrlMaxStates];
    uint32_t     state_count;
    uint32_t     default_state;               // index into states[]
} AnimController;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// Initialize an AnimController to zeroed state.
void anim_controller_init(AnimController *ctrl);

/// Load a controller from a .controller.meta JSON file.
[[nodiscard]] bool anim_controller_load(const char *path, AnimController *out);

/// Save a controller to a .controller.meta JSON file.
bool anim_controller_save(const AnimController *ctrl, const char *path);

/// Find a state index by name. Returns -1 if not found.
int32_t anim_controller_find_state(const AnimController *ctrl, const char *name);

/// Find a parameter index by name. Returns -1 if not found.
int32_t anim_controller_find_param(const AnimController *ctrl, const char *name);

/// Build the .controller.meta path from an .anim.meta path.
/// e.g. "assets/images/Run.anim.meta" → "assets/images/Run.controller.meta"
void anim_ctrl_build_meta_path(const char *anim_path, char *out, uint32_t out_cap);

/// Check if a .controller.meta file exists for the given .anim.meta path.
bool anim_ctrl_meta_exists(const char *anim_path);

#endif // ENGINE_CORE_ANIM_CONTROLLER_H
