#include "sprite.h"

// ---------------------------------------------------------------------------
// Internal helper — compute normalised UV rect from pixel rect + texture size.
// ---------------------------------------------------------------------------

static Rect compute_uv_rect(Rect pixel_rect, uint32_t tex_w, uint32_t tex_h) {
    float fw = (float)tex_w;
    float fh = (float)tex_h;

    if (fw < 1.0f) fw = 1.0f;
    if (fh < 1.0f) fh = 1.0f;

    return (Rect){
        .x = pixel_rect.x / fw,
        .y = pixel_rect.y / fh,
        .w = pixel_rect.w / fw,
        .h = pixel_rect.h / fh,
    };
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Sprite sprite_from_texture(AssetHandle texture,
                           uint32_t    tex_w,
                           uint32_t    tex_h) {
    Rect pixel_rect = { .x = 0.0f, .y = 0.0f,
                        .w = (float)tex_w, .h = (float)tex_h };

    return (Sprite){
        .texture    = texture,
        .rect       = pixel_rect,
        .uv_rect    = { .x = 0.0f, .y = 0.0f, .w = 1.0f, .h = 1.0f },
        .tex_width  = tex_w,
        .tex_height = tex_h,
    };
}

Sprite sprite_from_sheet(AssetHandle texture,
                         uint32_t    tex_w,
                         uint32_t    tex_h,
                         Rect        pixel_rect) {
    return (Sprite){
        .texture    = texture,
        .rect       = pixel_rect,
        .uv_rect    = compute_uv_rect(pixel_rect, tex_w, tex_h),
        .tex_width  = tex_w,
        .tex_height = tex_h,
    };
}

uint32_t sprite_slice_grid(AssetHandle texture,
                           uint32_t    tex_w,
                           uint32_t    tex_h,
                           uint32_t    cols,
                           uint32_t    rows,
                           Sprite     *out_sprites,
                           uint32_t    max_sprites) {
    if (out_sprites == nullptr || cols == 0 || rows == 0) return 0;

    float cell_w = (float)tex_w / (float)cols;
    float cell_h = (float)tex_h / (float)rows;

    uint32_t count = 0;

    for (uint32_t r = 0; r < rows && count < max_sprites; ++r) {
        for (uint32_t c = 0; c < cols && count < max_sprites; ++c) {
            Rect pixel_rect = {
                .x = (float)c * cell_w,
                .y = (float)r * cell_h,
                .w = cell_w,
                .h = cell_h,
            };

            out_sprites[count] = (Sprite){
                .texture    = texture,
                .rect       = pixel_rect,
                .uv_rect    = compute_uv_rect(pixel_rect, tex_w, tex_h),
                .tex_width  = tex_w,
                .tex_height = tex_h,
            };

            ++count;
        }
    }

    return count;
}
