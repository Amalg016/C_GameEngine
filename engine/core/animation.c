#include "animation.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

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
// AnimData
// ---------------------------------------------------------------------------

void anim_data_init(AnimData *data) {
    if (data == nullptr) return;
    *data = (AnimData){};
}

bool anim_data_load(const char *path, AnimData *out) {
    if (path == nullptr || out == nullptr) return false;

    char *json_str = read_file_to_string(path);
    if (json_str == nullptr) return false;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (root == nullptr) {
        fprintf(stderr, "[animation] failed to parse: %s\n", path);
        return false;
    }

    // Texture path.
    cJSON *tex = cJSON_GetObjectItemCaseSensitive(root, "texture");
    if (cJSON_IsString(tex)) {
        strncpy(out->texture_path, tex->valuestring, AnimPathMaxLen - 1);
        out->texture_path[AnimPathMaxLen - 1] = '\0';
    }

    // Clips array.
    cJSON *clips = cJSON_GetObjectItemCaseSensitive(root, "clips");
    out->clip_count = 0;

    if (cJSON_IsArray(clips)) {
        cJSON *clip_item = nullptr;
        cJSON_ArrayForEach(clip_item, clips) {
            if (out->clip_count >= AnimMaxClips) break;

            AnimClip *clip = &out->clips[out->clip_count];
            *clip = (AnimClip){};

            // Name.
            cJSON *name_j = cJSON_GetObjectItemCaseSensitive(clip_item, "name");
            if (cJSON_IsString(name_j)) {
                strncpy(clip->name, name_j->valuestring, AnimNameMaxLen - 1);
                clip->name[AnimNameMaxLen - 1] = '\0';
            }

            // FPS.
            cJSON *fps_j = cJSON_GetObjectItemCaseSensitive(clip_item, "fps");
            if (cJSON_IsNumber(fps_j)) {
                clip->fps = (float)fps_j->valuedouble;
            }

            // Loop.
            cJSON *loop_j = cJSON_GetObjectItemCaseSensitive(clip_item, "loop");
            if (cJSON_IsBool(loop_j)) {
                clip->loop = cJSON_IsTrue(loop_j);
            }

            // Frames.
            cJSON *frames_j = cJSON_GetObjectItemCaseSensitive(clip_item, "frames");
            clip->frame_count = 0;

            if (cJSON_IsArray(frames_j)) {
                cJSON *frame_item = nullptr;
                cJSON_ArrayForEach(frame_item, frames_j) {
                    if (clip->frame_count >= AnimMaxFrames) break;

                    AnimFrame *frame = &clip->frames[clip->frame_count];
                    frame->rect = (Rect){
                        .x = (float)cJSON_GetNumberValue(
                            cJSON_GetObjectItemCaseSensitive(frame_item, "x")),
                        .y = (float)cJSON_GetNumberValue(
                            cJSON_GetObjectItemCaseSensitive(frame_item, "y")),
                        .w = (float)cJSON_GetNumberValue(
                            cJSON_GetObjectItemCaseSensitive(frame_item, "w")),
                        .h = (float)cJSON_GetNumberValue(
                            cJSON_GetObjectItemCaseSensitive(frame_item, "h")),
                    };

                    clip->frame_count++;
                }
            }

            out->clip_count++;
        }
    }

    cJSON_Delete(root);
    printf("[animation] loaded %u clip(s) from %s\n", out->clip_count, path);
    return true;
}

bool anim_data_save(const AnimData *data, const char *path) {
    if (data == nullptr || path == nullptr) return false;

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return false;

    cJSON_AddStringToObject(root, "texture", data->texture_path);

    cJSON *clips = cJSON_AddArrayToObject(root, "clips");

    for (uint32_t i = 0; i < data->clip_count; ++i) {
        const AnimClip *clip = &data->clips[i];

        cJSON *clip_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(clip_obj, "name", clip->name);
        cJSON_AddNumberToObject(clip_obj, "fps", (double)clip->fps);
        cJSON_AddBoolToObject(clip_obj, "loop", clip->loop);

        cJSON *frames = cJSON_AddArrayToObject(clip_obj, "frames");

        for (uint32_t j = 0; j < clip->frame_count; ++j) {
            const AnimFrame *frame = &clip->frames[j];

            cJSON *frame_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(frame_obj, "x", (double)frame->rect.x);
            cJSON_AddNumberToObject(frame_obj, "y", (double)frame->rect.y);
            cJSON_AddNumberToObject(frame_obj, "w", (double)frame->rect.w);
            cJSON_AddNumberToObject(frame_obj, "h", (double)frame->rect.h);

            cJSON_AddItemToArray(frames, frame_obj);
        }

        cJSON_AddItemToArray(clips, clip_obj);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == nullptr) return false;

    FILE *f = fopen(path, "w");
    if (f == nullptr) {
        free(json_str);
        fprintf(stderr, "[animation] cannot write: %s\n", path);
        return false;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);

    printf("[animation] saved %u clip(s) to %s\n", data->clip_count, path);
    return true;
}

int32_t anim_data_find_clip(const AnimData *data, const char *name) {
    if (data == nullptr || name == nullptr) return -1;

    for (uint32_t i = 0; i < data->clip_count; ++i) {
        if (strcmp(data->clips[i].name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Meta path helpers
// ---------------------------------------------------------------------------

void anim_build_meta_path(const char *texture_path, char *out, uint32_t out_cap) {
    if (texture_path == nullptr || out == nullptr || out_cap == 0) return;

    // Strip the file extension to produce e.g. "assets/images/sheet.anim.meta"
    // from "assets/images/sheet.png".
    const char *dot = strrchr(texture_path, '.');
    if (dot != nullptr) {
        size_t base_len = (size_t)(dot - texture_path);
        if (base_len + sizeof(".anim.meta") > out_cap) {
            out[0] = '\0';
            return;
        }
        memcpy(out, texture_path, base_len);
        out[base_len] = '\0';
        strncat(out, ".anim.meta", out_cap - base_len - 1);
    } else {
        snprintf(out, out_cap, "%s.anim.meta", texture_path);
    }
}

bool anim_meta_exists(const char *texture_path) {
    if (texture_path == nullptr) return false;

    char meta_path[AnimPathMaxLen];
    anim_build_meta_path(texture_path, meta_path, AnimPathMaxLen);

#ifdef _WIN32
    DWORD attr = GetFileAttributesA(meta_path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(meta_path, &st) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Animator
// ---------------------------------------------------------------------------

void animator_init(Animator *anim) {
    if (anim == nullptr) return;
    *anim = (Animator){};
}

bool animator_play(Animator *anim, const char *clip_name) {
    if (anim == nullptr || anim->anim_data == nullptr || clip_name == nullptr) {
        return false;
    }

    int32_t idx = anim_data_find_clip(anim->anim_data, clip_name);
    if (idx < 0) return false;

    anim->current_clip  = (uint32_t)idx;
    anim->current_frame = 0;
    anim->elapsed       = 0.0f;
    anim->playing       = true;
    anim->finished      = false;
    return true;
}

void animator_stop(Animator *anim) {
    if (anim == nullptr) return;
    anim->playing  = false;
    anim->finished = false;
}

void animator_update(Animator *anim, float dt, Sprite *out_sprite) {
    if (anim == nullptr || anim->anim_data == nullptr || !anim->playing) return;

    const AnimClip *clip = &anim->anim_data->clips[anim->current_clip];
    if (clip->frame_count == 0 || clip->fps <= 0.0f) return;

    float frame_duration = 1.0f / clip->fps;

    anim->elapsed += dt;

    // Advance frames based on elapsed time.
    while (anim->elapsed >= frame_duration) {
        anim->elapsed -= frame_duration;
        anim->current_frame++;

        if (anim->current_frame >= clip->frame_count) {
            if (clip->loop) {
                anim->current_frame = 0;
            } else {
                anim->current_frame = clip->frame_count - 1;
                anim->playing  = false;
                anim->finished = true;
                anim->elapsed  = 0.0f;
                break;
            }
        }
    }

    // Write current frame to output sprite.
    const AnimFrame *frame = &clip->frames[anim->current_frame];

    if (out_sprite != nullptr) {
        *out_sprite = sprite_from_sheet(anim->texture,
                                        anim->tex_width,
                                        anim->tex_height,
                                        frame->rect);
    }
}
