#ifndef ENGINE_CORE_SPRITE_H
#define ENGINE_CORE_SPRITE_H

#include "../math/engine_math.h"
#include "asset_manager.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// Sprite — a rectangular sub-region of a texture atlas.
//
// Multiple Sprites can reference the same underlying texture (AssetHandle),
// each with a different pixel-space `rect` defining which portion to render.
// The `uv_rect` holds normalised [0,1] UV coordinates computed from the
// pixel rect and the texture's dimensions.
//
// Sprites are lightweight value types — no heap allocation, no GPU state.
// The referenced texture is managed by the AssetManager.
// ---------------------------------------------------------------------------

typedef struct Sprite {
    AssetHandle texture;      // handle to the shared texture asset
    Rect        rect;         // pixel-space region (x, y, w, h)
    Rect        uv_rect;     // normalised UV rect (computed from rect + texture size)
    uint32_t    tex_width;    // source texture width  in pixels
    uint32_t    tex_height;   // source texture height in pixels
} Sprite;

/// Create a Sprite covering the full texture.
///
/// `texture`  — asset handle for the loaded texture.
/// `tex_w/h`  — texture dimensions in pixels.
[[nodiscard]]
Sprite sprite_from_texture(AssetHandle texture,
                           uint32_t    tex_w,
                           uint32_t    tex_h);

/// Create a Sprite from a specific pixel sub-region of a texture.
///
/// `texture`    — asset handle for the loaded texture.
/// `tex_w/h`    — texture dimensions in pixels.
/// `pixel_rect` — the sub-region in pixel coordinates.
[[nodiscard]]
Sprite sprite_from_sheet(AssetHandle texture,
                         uint32_t    tex_w,
                         uint32_t    tex_h,
                         Rect        pixel_rect);

/// Slice an entire spritesheet into a uniform grid of Sprites.
///
/// Divides the texture into `cols × rows` equal cells and writes up to
/// `max_sprites` Sprites into `out_sprites` (row-major order, top-left
/// to bottom-right).
///
/// Returns the number of Sprites written (min(cols*rows, max_sprites)).
///
/// `texture`     — asset handle for the loaded spritesheet texture.
/// `tex_w/h`     — texture dimensions in pixels.
/// `cols/rows`   — grid dimensions.
/// `out_sprites` — caller-owned output buffer.
/// `max_sprites` — capacity of `out_sprites`.
uint32_t sprite_slice_grid(AssetHandle texture,
                           uint32_t    tex_w,
                           uint32_t    tex_h,
                           uint32_t    cols,
                           uint32_t    rows,
                           Sprite     *out_sprites,
                           uint32_t    max_sprites);

#endif // ENGINE_CORE_SPRITE_H
