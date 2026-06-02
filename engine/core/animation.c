#include "animation.h"
#include "anim_controller.h"

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
    if (anim == nullptr || anim->controller == nullptr || anim->controller->anim_data == nullptr || clip_name == nullptr) {
        return false;
    }

    int32_t idx = anim_data_find_clip(anim->controller->anim_data, clip_name);
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

// ---------------------------------------------------------------------------
// Controller transition evaluation
// ---------------------------------------------------------------------------

/// Evaluate a single condition against current runtime parameters.
static bool eval_condition(const AnimCondition *cond,
                           const AnimParamValue *params,
                           const AnimController *ctrl) {
    if (cond->param_index >= ctrl->param_count) return false;

    AnimParamType pt = ctrl->params[cond->param_index].type;
    const AnimParamValue *val = &params[cond->param_index];

    switch (pt) {
        case ANIM_PARAM_FLOAT:
            switch (cond->op) {
                case ANIM_OP_GREATER: return val->f >  cond->threshold.f;
                case ANIM_OP_LESS:    return val->f <  cond->threshold.f;
                case ANIM_OP_EQUAL:   return val->f == cond->threshold.f;
                case ANIM_OP_NOT_EQ:  return val->f != cond->threshold.f;
            }
            break;
        case ANIM_PARAM_INT:
            switch (cond->op) {
                case ANIM_OP_GREATER: return val->i >  cond->threshold.i;
                case ANIM_OP_LESS:    return val->i <  cond->threshold.i;
                case ANIM_OP_EQUAL:   return val->i == cond->threshold.i;
                case ANIM_OP_NOT_EQ:  return val->i != cond->threshold.i;
            }
            break;
        case ANIM_PARAM_BOOL:
        case ANIM_PARAM_TRIGGER:
            switch (cond->op) {
                case ANIM_OP_EQUAL:   return val->b == cond->threshold.b;
                case ANIM_OP_NOT_EQ:  return val->b != cond->threshold.b;
                default:              return val->b == cond->threshold.b;
            }
            break;
    }
    return false;
}

/// Evaluate all outgoing transitions from the current state.
/// Returns the target state index if a transition fires, or -1 if none.
/// Consumes trigger parameters that contributed to a firing transition.
static int32_t eval_transitions(Animator *anim) {
    const AnimController *ctrl = anim->controller;
    if (ctrl == nullptr || anim->current_state >= ctrl->state_count) return -1;

    const AnimState *state = &ctrl->states[anim->current_state];

    for (uint32_t i = 0; i < state->transition_count; ++i) {
        const AnimTransition *tr = &state->transitions[i];

        // Exit-time transitions only fire when the current clip has finished.
        if (tr->has_exit_time && !anim->finished) continue;

        // All conditions must be true (AND logic).
        bool all_met = true;
        for (uint32_t c = 0; c < tr->condition_count; ++c) {
            if (!eval_condition(&tr->conditions[c], anim->params, ctrl)) {
                all_met = false;
                break;
            }
        }

        if (all_met && tr->target_state < ctrl->state_count) {
            // Consume any trigger parameters used in this transition.
            for (uint32_t c = 0; c < tr->condition_count; ++c) {
                uint32_t pi = tr->conditions[c].param_index;
                if (pi < ctrl->param_count &&
                    ctrl->params[pi].type == ANIM_PARAM_TRIGGER) {
                    anim->params[pi].b = false;
                }
            }
            return (int32_t)tr->target_state;
        }
    }

    return -1;
}

void animator_update(Animator *anim, float dt, Sprite *out_sprite) {
    if (anim == nullptr || anim->controller == nullptr || anim->controller->anim_data == nullptr || !anim->playing) return;

    // --- Controller: evaluate transitions before advancing ----------------
    int32_t next = eval_transitions(anim);
    if (next >= 0 && (uint32_t)next != anim->current_state) {
        anim->current_state = (uint32_t)next;

        // Switch to the clip referenced by the new state.
        const AnimState *ns =
            &anim->controller->states[anim->current_state];
        int32_t clip_idx =
            anim_data_find_clip(anim->controller->anim_data, ns->clip_name);
        if (clip_idx >= 0) {
            anim->current_clip  = (uint32_t)clip_idx;
            anim->current_frame = 0;
            anim->elapsed       = 0.0f;
            anim->finished      = false;
            anim->playing       = true;
        }
    }

    // --- Advance playback ------------------------------------------------
    const AnimClip *clip = &anim->controller->anim_data->clips[anim->current_clip];
    if (clip->frame_count == 0 || clip->fps <= 0.0f) return;

    // Apply speed multiplier from controller state (if present).
    float speed = anim->controller->states[anim->current_state].speed;
    if (speed <= 0.0f) speed = 1.0f;

    float frame_duration = 1.0f / (clip->fps * speed);

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
        *out_sprite = sprite_from_sheet(anim->controller->texture,
                                        anim->controller->tex_width,
                                        anim->controller->tex_height,
                                        frame->rect);
    }
}

// ---------------------------------------------------------------------------
// Parameter accessors
// ---------------------------------------------------------------------------

void animator_set_float(Animator *anim, const char *name, float value) {
    if (anim == nullptr || anim->controller == nullptr) return;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && anim->controller->params[idx].type == ANIM_PARAM_FLOAT) {
        anim->params[idx].f = value;
    }
}

void animator_set_int(Animator *anim, const char *name, int32_t value) {
    if (anim == nullptr || anim->controller == nullptr) return;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && anim->controller->params[idx].type == ANIM_PARAM_INT) {
        anim->params[idx].i = value;
    }
}

void animator_set_bool(Animator *anim, const char *name, bool value) {
    if (anim == nullptr || anim->controller == nullptr) return;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && anim->controller->params[idx].type == ANIM_PARAM_BOOL) {
        anim->params[idx].b = value;
    }
}

void animator_set_trigger(Animator *anim, const char *name) {
    if (anim == nullptr || anim->controller == nullptr) return;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && anim->controller->params[idx].type == ANIM_PARAM_TRIGGER) {
        anim->params[idx].b = true;
    }
}

float animator_get_float(const Animator *anim, const char *name) {
    if (anim == nullptr || anim->controller == nullptr) return 0.0f;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && anim->controller->params[idx].type == ANIM_PARAM_FLOAT) {
        return anim->params[idx].f;
    }
    return 0.0f;
}

int32_t animator_get_int(const Animator *anim, const char *name) {
    if (anim == nullptr || anim->controller == nullptr) return 0;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && anim->controller->params[idx].type == ANIM_PARAM_INT) {
        return anim->params[idx].i;
    }
    return 0;
}

bool animator_get_bool(const Animator *anim, const char *name) {
    if (anim == nullptr || anim->controller == nullptr) return false;
    int32_t idx = anim_controller_find_param(anim->controller, name);
    if (idx >= 0 && (anim->controller->params[idx].type == ANIM_PARAM_BOOL ||
                     anim->controller->params[idx].type == ANIM_PARAM_TRIGGER)) {
        return anim->params[idx].b;
    }
    return false;
}

void animator_reset_params(Animator *anim) {
    if (anim == nullptr || anim->controller == nullptr) return;
    for (uint32_t i = 0; i < anim->controller->param_count; ++i) {
        const AnimParam *p = &anim->controller->params[i];
        switch (p->type) {
            case ANIM_PARAM_FLOAT:
                anim->params[i].f = p->default_value.f;
                break;
            case ANIM_PARAM_INT:
                anim->params[i].i = p->default_value.i;
                break;
            case ANIM_PARAM_BOOL:
                anim->params[i].b = p->default_value.b;
                break;
            case ANIM_PARAM_TRIGGER:
                anim->params[i].b = false;
                break;
        }
    }
}

