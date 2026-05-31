#include "sprite_meta.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Default initial capacity for the regions array.
constexpr uint32_t DefaultRegionCapacity = 16;

/// Ensure the regions array has room for at least one more entry.
static void ensure_capacity(SpriteMeta *meta) {
    if (meta->region_count < meta->region_capacity) return;

    uint32_t new_cap = meta->region_capacity == 0
                       ? DefaultRegionCapacity
                       : meta->region_capacity * 2;

    SpriteRegion *new_arr = realloc(meta->regions,
                                    new_cap * sizeof(SpriteRegion));
    if (new_arr == nullptr) {
        fprintf(stderr, "[sprite_meta] realloc failed\n");
        return;
    }

    meta->regions         = new_arr;
    meta->region_capacity = new_cap;
}

/// Read an entire file into a heap-allocated string (null-terminated).
/// Caller owns the returned buffer.
static char *read_file_to_string(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == nullptr) return nullptr;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return nullptr; }

    char *buf = malloc((size_t)len + 1);
    if (buf == nullptr) { fclose(f); return nullptr; }

    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void sprite_meta_init(SpriteMeta *meta) {
    if (meta == nullptr) return;
    *meta = (SpriteMeta){};
}

void sprite_meta_destroy(SpriteMeta *meta) {
    if (meta == nullptr) return;
    free(meta->regions);
    *meta = (SpriteMeta){};
}

bool sprite_meta_load(const char *meta_path, SpriteMeta *out) {
    if (meta_path == nullptr || out == nullptr) return false;

    char *json_str = read_file_to_string(meta_path);
    if (json_str == nullptr) return false;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (root == nullptr) {
        fprintf(stderr, "[sprite_meta] failed to parse: %s\n", meta_path);
        return false;
    }

    // Texture path.
    cJSON *tex = cJSON_GetObjectItemCaseSensitive(root, "texture");
    if (cJSON_IsString(tex)) {
        strncpy(out->texture_path, tex->valuestring, SpritePathMaxLen - 1);
        out->texture_path[SpritePathMaxLen - 1] = '\0';
    }

    // Texture dimensions.
    cJSON *tw = cJSON_GetObjectItemCaseSensitive(root, "texture_width");
    cJSON *th = cJSON_GetObjectItemCaseSensitive(root, "texture_height");
    if (cJSON_IsNumber(tw)) out->texture_width  = (uint32_t)tw->valuedouble;
    if (cJSON_IsNumber(th)) out->texture_height = (uint32_t)th->valuedouble;

    // Sprite regions.
    cJSON *sprites = cJSON_GetObjectItemCaseSensitive(root, "sprites");
    if (cJSON_IsArray(sprites)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, sprites) {
            cJSON *name_j = cJSON_GetObjectItemCaseSensitive(item, "name");
            cJSON *rect_j = cJSON_GetObjectItemCaseSensitive(item, "rect");

            if (!cJSON_IsString(name_j) || !cJSON_IsObject(rect_j)) continue;

            Rect r = {
                .x = (float)cJSON_GetNumberValue(
                    cJSON_GetObjectItemCaseSensitive(rect_j, "x")),
                .y = (float)cJSON_GetNumberValue(
                    cJSON_GetObjectItemCaseSensitive(rect_j, "y")),
                .w = (float)cJSON_GetNumberValue(
                    cJSON_GetObjectItemCaseSensitive(rect_j, "w")),
                .h = (float)cJSON_GetNumberValue(
                    cJSON_GetObjectItemCaseSensitive(rect_j, "h")),
            };

            sprite_meta_add_region(out, name_j->valuestring, r);
        }
    }

    cJSON_Delete(root);
    printf("[sprite_meta] loaded %u region(s) from %s\n",
           out->region_count, meta_path);
    return true;
}

bool sprite_meta_save(const SpriteMeta *meta, const char *meta_path) {
    if (meta == nullptr || meta_path == nullptr) return false;

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return false;

    cJSON_AddStringToObject(root, "texture", meta->texture_path);
    cJSON_AddNumberToObject(root, "texture_width",  (double)meta->texture_width);
    cJSON_AddNumberToObject(root, "texture_height", (double)meta->texture_height);

    cJSON *sprites = cJSON_AddArrayToObject(root, "sprites");

    for (uint32_t i = 0; i < meta->region_count; ++i) {
        const SpriteRegion *reg = &meta->regions[i];

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", reg->name);

        cJSON *rect_obj = cJSON_AddObjectToObject(item, "rect");
        cJSON_AddNumberToObject(rect_obj, "x", (double)reg->rect.x);
        cJSON_AddNumberToObject(rect_obj, "y", (double)reg->rect.y);
        cJSON_AddNumberToObject(rect_obj, "w", (double)reg->rect.w);
        cJSON_AddNumberToObject(rect_obj, "h", (double)reg->rect.h);

        cJSON_AddItemToArray(sprites, item);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == nullptr) return false;

    FILE *f = fopen(meta_path, "w");
    if (f == nullptr) {
        free(json_str);
        fprintf(stderr, "[sprite_meta] cannot write: %s\n", meta_path);
        return false;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);

    printf("[sprite_meta] saved %u region(s) to %s\n",
           meta->region_count, meta_path);
    return true;
}

void sprite_meta_add_region(SpriteMeta *meta, const char *name, Rect r) {
    if (meta == nullptr) return;

    ensure_capacity(meta);
    if (meta->region_count >= meta->region_capacity) return; // alloc failed

    SpriteRegion *reg = &meta->regions[meta->region_count];
    strncpy(reg->name, name != nullptr ? name : "unnamed",
            SpriteNameMaxLen - 1);
    reg->name[SpriteNameMaxLen - 1] = '\0';
    reg->rect = r;

    meta->region_count++;
}

void sprite_meta_clear_regions(SpriteMeta *meta) {
    if (meta == nullptr) return;
    meta->region_count = 0;
}

void sprite_meta_slice_grid(SpriteMeta *meta, uint32_t cols, uint32_t rows) {
    if (meta == nullptr || cols == 0 || rows == 0) return;
    if (meta->texture_width == 0 || meta->texture_height == 0) return;

    sprite_meta_clear_regions(meta);

    float cell_w = (float)meta->texture_width  / (float)cols;
    float cell_h = (float)meta->texture_height / (float)rows;

    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            char name[SpriteNameMaxLen];
            snprintf(name, SpriteNameMaxLen, "sprite_%u", r * cols + c);

            Rect pixel_rect = {
                .x = (float)c * cell_w,
                .y = (float)r * cell_h,
                .w = cell_w,
                .h = cell_h,
            };

            sprite_meta_add_region(meta, name, pixel_rect);
        }
    }
}

void sprite_meta_build_path(const char *texture_path,
                            char *out_path, uint32_t out_cap) {
    if (texture_path == nullptr || out_path == nullptr || out_cap == 0) return;
    snprintf(out_path, out_cap, "%s.sprite.meta", texture_path);
}

bool sprite_meta_exists(const char *texture_path) {
    if (texture_path == nullptr) return false;

    char meta_path[SpritePathMaxLen];
    sprite_meta_build_path(texture_path, meta_path, SpritePathMaxLen);

    struct stat st;
    return stat(meta_path, &st) == 0;
}
