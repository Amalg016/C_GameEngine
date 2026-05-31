#ifndef ENGINE_CORE_SPRITE_META_H
#define ENGINE_CORE_SPRITE_META_H

#include "../math/engine_math.h"

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Sprite Meta — persistent sprite region definitions for a spritesheet.
//
// A `.sprite.meta` JSON file lives alongside each spritesheet texture and
// stores the named sub-regions created by the Sprite Editor panel.
//
// Format:
// {
//     "texture": "assets/images/sheet.png",
//     "texture_width": 128,
//     "texture_height": 128,
//     "sprites": [
//         { "name": "walk_0", "rect": { "x": 0, "y": 0, "w": 32, "h": 32 } },
//         ...
//     ]
// }
// ---------------------------------------------------------------------------

/// Maximum length for sprite region names.
constexpr uint32_t SpriteNameMaxLen = 64;

/// Maximum length for texture file paths.
constexpr uint32_t SpritePathMaxLen = 512;

/// A named rectangular region within a spritesheet.
typedef struct SpriteRegion {
    char name[SpriteNameMaxLen];    // user-assigned name (e.g. "walk_0")
    Rect rect;                      // pixel-space sub-region
} SpriteRegion;

/// Full sprite metadata for a single texture asset.
/// Owns the `regions` array (heap-allocated, growable).
typedef struct SpriteMeta {
    char          texture_path[SpritePathMaxLen];
    uint32_t      texture_width;
    uint32_t      texture_height;
    SpriteRegion *regions;          // heap-allocated array
    uint32_t      region_count;
    uint32_t      region_capacity;
} SpriteMeta;

/// Initialise a SpriteMeta to empty state (no allocation).
void sprite_meta_init(SpriteMeta *meta);

/// Free all resources owned by `meta` and reset to empty.
void sprite_meta_destroy(SpriteMeta *meta);

/// Load sprite metadata from a `.sprite.meta` JSON file.
/// Populates `out` (must be pre-initialised with sprite_meta_init).
/// Returns true on success.
[[nodiscard]]
bool sprite_meta_load(const char *meta_path, SpriteMeta *out);

/// Save sprite metadata to a `.sprite.meta` JSON file.
/// Returns true on success.
bool sprite_meta_save(const SpriteMeta *meta, const char *meta_path);

/// Add a named region. Grows the internal array if needed.
void sprite_meta_add_region(SpriteMeta *meta, const char *name, Rect rect);

/// Remove all regions (does not free the array — just resets count).
void sprite_meta_clear_regions(SpriteMeta *meta);

/// Generate grid-based regions with auto-generated names.
/// Clears any existing regions, then creates `cols * rows` regions
/// named "sprite_0", "sprite_1", etc.
void sprite_meta_slice_grid(SpriteMeta *meta, uint32_t cols, uint32_t rows);

/// Build the meta file path for a given texture path.
/// Writes to `out_path` (capacity `out_cap`).
/// E.g. "assets/images/sheet.png" → "assets/images/sheet.png.sprite.meta"
void sprite_meta_build_path(const char *texture_path,
                            char *out_path, uint32_t out_cap);

/// Check if a `.sprite.meta` file exists for the given texture path.
bool sprite_meta_exists(const char *texture_path);

#endif // ENGINE_CORE_SPRITE_META_H
