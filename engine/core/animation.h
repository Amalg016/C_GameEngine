#ifndef ENGINE_CORE_ANIMATION_H
#define ENGINE_CORE_ANIMATION_H

#include "../math/engine_math.h"
#include "asset_manager.h"
#include "sprite.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declaration — full definition in anim_controller.h.
typedef struct AnimController AnimController;

/// Runtime parameter value — defined here so Animator can embed an array.
typedef struct AnimParamValue {
    union {
        float   f;
        int32_t i;
        bool    b;
    };
} AnimParamValue;

constexpr uint32_t AnimMaxFrames = 64;
constexpr uint32_t AnimMaxClips = 16;
constexpr uint32_t AnimNameMaxLen = 64;
constexpr uint32_t AnimPathMaxLen = 256;

typedef struct AnimFrame {
    Rect rect;
} AnimFrame;

typedef struct AnimClip {
    char      name[AnimNameMaxLen];
    float     fps;
    bool      loop;
    AnimFrame frames[AnimMaxFrames];
    uint32_t  frame_count;
} AnimClip;

typedef struct AnimData {
    char      texture_path[AnimPathMaxLen];
    AnimClip  clips[AnimMaxClips];
    uint32_t  clip_count;
} AnimData;

constexpr uint32_t AnimCtrlMaxParamsInline = 16;

typedef struct Animator {
    // --- Animation data (always present) ---
    char        anim_path[AnimPathMaxLen];
    AssetHandle texture;
    uint32_t    tex_width;
    uint32_t    tex_height;
    AnimData   *anim_data;       // borrowed pointer (owned by AnimCache)

    // --- Playback state ---
    uint32_t    current_clip;
    uint32_t    current_frame;
    float       elapsed;
    bool        playing;
    bool        finished;

    // --- Controller (optional — nullptr for direct clip playback) ---
    char             controller_path[AnimPathMaxLen];
    AnimController  *controller;         // borrowed (owned by AnimCache)
    uint32_t         current_state;      // index into controller->states[]
    AnimParamValue   params[AnimCtrlMaxParamsInline]; // runtime param values
} Animator;

/// Initialize AnimData to zeroed state.
void anim_data_init(AnimData *data);

/// Load animation data from a .anim.meta JSON file.
[[nodiscard]] bool anim_data_load(const char *path, AnimData *out);

/// Save animation data to a .anim.meta JSON file.
bool anim_data_save(const AnimData *data, const char *path);

/// Find a clip index by name. Returns -1 if not found.
int32_t anim_data_find_clip(const AnimData *data, const char *name);

/// Build the .anim.meta path from a texture path.
void anim_build_meta_path(const char *texture_path, char *out, uint32_t out_cap);

/// Check if an .anim.meta file exists for the given texture path.
bool anim_meta_exists(const char *texture_path);

/// Initialize an Animator to default state.
void animator_init(Animator *anim);

/// Start playing a clip by name. Returns true if found.
bool animator_play(Animator *anim, const char *clip_name);

/// Stop playback.
void animator_stop(Animator *anim);

/// Advance the animation by dt seconds. Writes the current frame
/// to out_sprite (if non-null). Call this every frame.
/// If a controller is attached, evaluates transitions before advancing.
void animator_update(Animator *anim, float dt, Sprite *out_sprite);

// ---------------------------------------------------------------------------
// Parameter accessors — set/get runtime parameter values by name.
// These only work when a controller is attached (controller != nullptr).
// ---------------------------------------------------------------------------

/// Set a float parameter by name.
void animator_set_float(Animator *anim, const char *name, float value);

/// Set an int parameter by name.
void animator_set_int(Animator *anim, const char *name, int32_t value);

/// Set a bool parameter by name.
void animator_set_bool(Animator *anim, const char *name, bool value);

/// Set a trigger parameter by name (sets to true; consumed on transition).
void animator_set_trigger(Animator *anim, const char *name);

/// Get a float parameter by name. Returns 0.0f if not found.
float animator_get_float(const Animator *anim, const char *name);

/// Get an int parameter by name. Returns 0 if not found.
int32_t animator_get_int(const Animator *anim, const char *name);

/// Get a bool parameter by name. Returns false if not found.
bool animator_get_bool(const Animator *anim, const char *name);

/// Initialize runtime params from controller defaults.
void animator_reset_params(Animator *anim);

#endif // ENGINE_CORE_ANIMATION_H
