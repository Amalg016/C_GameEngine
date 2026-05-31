#ifndef ENGINE_CORE_ANIMATION_H
#define ENGINE_CORE_ANIMATION_H

#include "../math/engine_math.h"
#include "asset_manager.h"
#include "sprite.h"

#include <stdint.h>
#include <stdbool.h>

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

typedef struct Animator {
    char        anim_path[AnimPathMaxLen];
    AssetHandle texture;
    uint32_t    tex_width;
    uint32_t    tex_height;
    AnimData   *anim_data;      // borrowed pointer (owned by AnimCache)
    uint32_t    current_clip;
    uint32_t    current_frame;
    float       elapsed;
    bool        playing;
    bool        finished;
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
void animator_update(Animator *anim, float dt, Sprite *out_sprite);

#endif // ENGINE_CORE_ANIMATION_H
