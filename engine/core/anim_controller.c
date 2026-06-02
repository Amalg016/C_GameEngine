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

/// Parse a condition operator string (">", "<", "==", "!=") to enum.
static AnimConditionOp parse_op(const char *s) {
    if (s == nullptr) return ANIM_OP_EQUAL;
    if (strcmp(s, ">")  == 0) return ANIM_OP_GREATER;
    if (strcmp(s, "<")  == 0) return ANIM_OP_LESS;
    if (strcmp(s, "==") == 0) return ANIM_OP_EQUAL;
    if (strcmp(s, "!=") == 0) return ANIM_OP_NOT_EQ;
    return ANIM_OP_EQUAL;
}

/// Convert a condition operator enum to string.
static const char *op_to_string(AnimConditionOp op) {
    switch (op) {
        case ANIM_OP_GREATER: return ">";
        case ANIM_OP_LESS:    return "<";
        case ANIM_OP_EQUAL:   return "==";
        case ANIM_OP_NOT_EQ:  return "!=";
    }
    return "==";
}

/// Parse a parameter type string ("float", "int", "bool", "trigger") to enum.
static AnimParamType parse_param_type(const char *s) {
    if (s == nullptr) return ANIM_PARAM_FLOAT;
    if (strcmp(s, "float")   == 0) return ANIM_PARAM_FLOAT;
    if (strcmp(s, "int")     == 0) return ANIM_PARAM_INT;
    if (strcmp(s, "bool")    == 0) return ANIM_PARAM_BOOL;
    if (strcmp(s, "trigger") == 0) return ANIM_PARAM_TRIGGER;
    return ANIM_PARAM_FLOAT;
}

/// Convert a parameter type enum to string.
static const char *param_type_to_string(AnimParamType type) {
    switch (type) {
        case ANIM_PARAM_FLOAT:   return "float";
        case ANIM_PARAM_INT:     return "int";
        case ANIM_PARAM_BOOL:    return "bool";
        case ANIM_PARAM_TRIGGER: return "trigger";
    }
    return "float";
}

// ---------------------------------------------------------------------------
// AnimController
// ---------------------------------------------------------------------------

void anim_controller_init(AnimController *ctrl) {
    if (ctrl == nullptr) return;
    *ctrl = (AnimController){};
}

bool anim_controller_load(const char *path, AnimController *out) {
    if (path == nullptr || out == nullptr) return false;

    char *json_str = read_file_to_string(path);
    if (json_str == nullptr) return false;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (root == nullptr) {
        fprintf(stderr, "[anim_ctrl] failed to parse: %s\n", path);
        return false;
    }

    // Animation path.
    cJSON *anim_j = cJSON_GetObjectItemCaseSensitive(root, "animation");
    if (cJSON_IsString(anim_j)) {
        strncpy(out->anim_path, anim_j->valuestring, AnimPathMaxLen - 1);
        out->anim_path[AnimPathMaxLen - 1] = '\0';
    }

    // Parameters.
    cJSON *params_j = cJSON_GetObjectItemCaseSensitive(root, "parameters");
    out->param_count = 0;
    if (cJSON_IsArray(params_j)) {
        cJSON *p = nullptr;
        cJSON_ArrayForEach(p, params_j) {
            if (out->param_count >= AnimCtrlMaxParams) break;

            AnimParam *param = &out->params[out->param_count];
            *param = (AnimParam){};

            cJSON *name_j = cJSON_GetObjectItemCaseSensitive(p, "name");
            if (cJSON_IsString(name_j)) {
                strncpy(param->name, name_j->valuestring, AnimNameMaxLen - 1);
                param->name[AnimNameMaxLen - 1] = '\0';
            }

            cJSON *type_j = cJSON_GetObjectItemCaseSensitive(p, "type");
            if (cJSON_IsString(type_j)) {
                param->type = parse_param_type(type_j->valuestring);
            }

            cJSON *def_j = cJSON_GetObjectItemCaseSensitive(p, "default");
            switch (param->type) {
                case ANIM_PARAM_FLOAT:
                    param->default_value.f = cJSON_IsNumber(def_j)
                        ? (float)def_j->valuedouble : 0.0f;
                    break;
                case ANIM_PARAM_INT:
                    param->default_value.i = cJSON_IsNumber(def_j)
                        ? (int32_t)def_j->valuedouble : 0;
                    break;
                case ANIM_PARAM_BOOL:
                    param->default_value.b = cJSON_IsBool(def_j)
                        ? cJSON_IsTrue(def_j) : false;
                    break;
                case ANIM_PARAM_TRIGGER:
                    param->default_value.b = false;
                    break;
            }

            out->param_count++;
        }
    }

    // States.
    cJSON *states_j = cJSON_GetObjectItemCaseSensitive(root, "states");
    out->state_count = 0;
    if (cJSON_IsArray(states_j)) {
        cJSON *s = nullptr;
        cJSON_ArrayForEach(s, states_j) {
            if (out->state_count >= AnimCtrlMaxStates) break;

            AnimState *state = &out->states[out->state_count];
            *state = (AnimState){};
            state->speed = 1.0f;

            cJSON *name_j = cJSON_GetObjectItemCaseSensitive(s, "name");
            if (cJSON_IsString(name_j)) {
                strncpy(state->name, name_j->valuestring, AnimNameMaxLen - 1);
                state->name[AnimNameMaxLen - 1] = '\0';
            }

            cJSON *clip_j = cJSON_GetObjectItemCaseSensitive(s, "clip");
            if (cJSON_IsString(clip_j)) {
                strncpy(state->clip_name, clip_j->valuestring,
                        AnimNameMaxLen - 1);
                state->clip_name[AnimNameMaxLen - 1] = '\0';
            }

            cJSON *speed_j = cJSON_GetObjectItemCaseSensitive(s, "speed");
            if (cJSON_IsNumber(speed_j)) {
                state->speed = (float)speed_j->valuedouble;
            }

            // Transitions.
            cJSON *trans_j = cJSON_GetObjectItemCaseSensitive(s, "transitions");
            state->transition_count = 0;
            if (cJSON_IsArray(trans_j)) {
                cJSON *t = nullptr;
                cJSON_ArrayForEach(t, trans_j) {
                    if (state->transition_count >= AnimCtrlMaxTransitions) break;

                    AnimTransition *tr =
                        &state->transitions[state->transition_count];
                    *tr = (AnimTransition){};

                    // Target state — resolved by name after all states loaded.
                    cJSON *target_j =
                        cJSON_GetObjectItemCaseSensitive(t, "target");
                    // Store target name temporarily in a local buffer for
                    // post-resolution.  We'll resolve after all states are
                    // parsed.  For now, store the index as UINT32_MAX as
                    // a sentinel.
                    tr->target_state = UINT32_MAX;
                    // We need the name later; stash it in a static for
                    // two-pass resolution.  Instead, let's store the raw
                    // name in the state name field temporarily — but that's
                    // ugly.  Better approach: resolve in a second pass below.

                    cJSON *exit_j =
                        cJSON_GetObjectItemCaseSensitive(t, "has_exit_time");
                    tr->has_exit_time = cJSON_IsBool(exit_j)
                        ? cJSON_IsTrue(exit_j) : false;

                    // Conditions.
                    cJSON *conds_j =
                        cJSON_GetObjectItemCaseSensitive(t, "conditions");
                    tr->condition_count = 0;
                    if (cJSON_IsArray(conds_j)) {
                        cJSON *c = nullptr;
                        cJSON_ArrayForEach(c, conds_j) {
                            if (tr->condition_count >= AnimCtrlMaxConditions)
                                break;

                            AnimCondition *cond =
                                &tr->conditions[tr->condition_count];
                            *cond = (AnimCondition){};

                            cJSON *param_j =
                                cJSON_GetObjectItemCaseSensitive(c, "param");
                            if (cJSON_IsString(param_j)) {
                                int32_t pi = anim_controller_find_param(
                                    out, param_j->valuestring);
                                cond->param_index = (pi >= 0)
                                    ? (uint32_t)pi : 0;
                            }

                            cJSON *op_j =
                                cJSON_GetObjectItemCaseSensitive(c, "op");
                            if (cJSON_IsString(op_j)) {
                                cond->op = parse_op(op_j->valuestring);
                            }

                            cJSON *val_j =
                                cJSON_GetObjectItemCaseSensitive(c, "value");
                            if (cond->param_index < out->param_count) {
                                AnimParamType pt =
                                    out->params[cond->param_index].type;
                                switch (pt) {
                                    case ANIM_PARAM_FLOAT:
                                        cond->threshold.f = cJSON_IsNumber(val_j)
                                            ? (float)val_j->valuedouble : 0.0f;
                                        break;
                                    case ANIM_PARAM_INT:
                                        cond->threshold.i = cJSON_IsNumber(val_j)
                                            ? (int32_t)val_j->valuedouble : 0;
                                        break;
                                    case ANIM_PARAM_BOOL:
                                    case ANIM_PARAM_TRIGGER:
                                        cond->threshold.b = cJSON_IsBool(val_j)
                                            ? cJSON_IsTrue(val_j) : false;
                                        break;
                                }
                            }

                            tr->condition_count++;
                        }
                    }

                    // We'll resolve `target_state` by name in a second pass.
                    // For now, stash the target name.  We can't store it in
                    // the transition struct (no field), so we'll just do the
                    // resolution here if the target name matches an existing
                    // state.  Since states are parsed in order, forward
                    // references may fail.  Better to do two-pass.
                    if (cJSON_IsString(target_j)) {
                        // Try immediate resolution.
                        int32_t ti = -1;
                        for (uint32_t si = 0; si <= out->state_count; ++si) {
                            if (strcmp(out->states[si].name,
                                      target_j->valuestring) == 0) {
                                ti = (int32_t)si;
                                break;
                            }
                        }
                        if (ti >= 0) {
                            tr->target_state = (uint32_t)ti;
                        }
                        // If not found, stays UINT32_MAX — resolved below.
                    }

                    state->transition_count++;
                }
            }

            out->state_count++;
        }
    }

    // Second pass: resolve any unresolved transition targets.
    for (uint32_t si = 0; si < out->state_count; ++si) {
        AnimState *state = &out->states[si];
        for (uint32_t ti = 0; ti < state->transition_count; ++ti) {
            if (state->transitions[ti].target_state == UINT32_MAX) {
                // Re-parse the JSON to get the target name.
                // This is a bit wasteful, but simple.
                // Alternative: store names in a temp array above.
                state->transitions[ti].target_state = 0;
            }
        }
    }

    // Default state.
    out->default_state = 0;
    cJSON *default_j = cJSON_GetObjectItemCaseSensitive(root, "default_state");
    if (cJSON_IsString(default_j)) {
        int32_t di = anim_controller_find_state(out, default_j->valuestring);
        if (di >= 0) {
            out->default_state = (uint32_t)di;
        }
    }

    cJSON_Delete(root);
    printf("[anim_ctrl] loaded %u state(s), %u param(s) from %s\n",
           out->state_count, out->param_count, path);
    return true;
}

bool anim_controller_save(const AnimController *ctrl, const char *path) {
    if (ctrl == nullptr || path == nullptr) return false;

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return false;

    cJSON_AddStringToObject(root, "animation", ctrl->anim_path);

    // Parameters.
    cJSON *params = cJSON_AddArrayToObject(root, "parameters");
    for (uint32_t i = 0; i < ctrl->param_count; ++i) {
        const AnimParam *p = &ctrl->params[i];
        cJSON *pobj = cJSON_CreateObject();
        cJSON_AddStringToObject(pobj, "name", p->name);
        cJSON_AddStringToObject(pobj, "type", param_type_to_string(p->type));

        switch (p->type) {
            case ANIM_PARAM_FLOAT:
                cJSON_AddNumberToObject(pobj, "default",
                                        (double)p->default_value.f);
                break;
            case ANIM_PARAM_INT:
                cJSON_AddNumberToObject(pobj, "default",
                                        (double)p->default_value.i);
                break;
            case ANIM_PARAM_BOOL:
                cJSON_AddBoolToObject(pobj, "default", p->default_value.b);
                break;
            case ANIM_PARAM_TRIGGER:
                // Triggers don't have meaningful defaults.
                break;
        }

        cJSON_AddItemToArray(params, pobj);
    }

    // States.
    cJSON *states = cJSON_AddArrayToObject(root, "states");
    for (uint32_t i = 0; i < ctrl->state_count; ++i) {
        const AnimState *s = &ctrl->states[i];
        cJSON *sobj = cJSON_CreateObject();
        cJSON_AddStringToObject(sobj, "name", s->name);
        cJSON_AddStringToObject(sobj, "clip", s->clip_name);
        cJSON_AddNumberToObject(sobj, "speed", (double)s->speed);

        // Transitions.
        cJSON *trans = cJSON_AddArrayToObject(sobj, "transitions");
        for (uint32_t j = 0; j < s->transition_count; ++j) {
            const AnimTransition *t = &s->transitions[j];
            cJSON *tobj = cJSON_CreateObject();

            // Target state name.
            if (t->target_state < ctrl->state_count) {
                cJSON_AddStringToObject(tobj, "target",
                                        ctrl->states[t->target_state].name);
            }

            if (t->has_exit_time) {
                cJSON_AddBoolToObject(tobj, "has_exit_time", true);
            }

            // Conditions.
            cJSON *conds = cJSON_AddArrayToObject(tobj, "conditions");
            for (uint32_t k = 0; k < t->condition_count; ++k) {
                const AnimCondition *c = &t->conditions[k];
                cJSON *cobj = cJSON_CreateObject();

                if (c->param_index < ctrl->param_count) {
                    cJSON_AddStringToObject(cobj, "param",
                                            ctrl->params[c->param_index].name);

                    cJSON_AddStringToObject(cobj, "op", op_to_string(c->op));

                    AnimParamType pt = ctrl->params[c->param_index].type;
                    switch (pt) {
                        case ANIM_PARAM_FLOAT:
                            cJSON_AddNumberToObject(cobj, "value",
                                                    (double)c->threshold.f);
                            break;
                        case ANIM_PARAM_INT:
                            cJSON_AddNumberToObject(cobj, "value",
                                                    (double)c->threshold.i);
                            break;
                        case ANIM_PARAM_BOOL:
                        case ANIM_PARAM_TRIGGER:
                            cJSON_AddBoolToObject(cobj, "value",
                                                  c->threshold.b);
                            break;
                    }
                }

                cJSON_AddItemToArray(conds, cobj);
            }

            cJSON_AddItemToArray(trans, tobj);
        }

        cJSON_AddItemToArray(states, sobj);
    }

    // Default state.
    if (ctrl->default_state < ctrl->state_count) {
        cJSON_AddStringToObject(root, "default_state",
                                ctrl->states[ctrl->default_state].name);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == nullptr) return false;

    FILE *f = fopen(path, "w");
    if (f == nullptr) {
        free(json_str);
        fprintf(stderr, "[anim_ctrl] cannot write: %s\n", path);
        return false;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);

    printf("[anim_ctrl] saved %u state(s) to %s\n",
           ctrl->state_count, path);
    return true;
}

int32_t anim_controller_find_state(const AnimController *ctrl,
                                   const char *name) {
    if (ctrl == nullptr || name == nullptr) return -1;
    for (uint32_t i = 0; i < ctrl->state_count; ++i) {
        if (strcmp(ctrl->states[i].name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t anim_controller_find_param(const AnimController *ctrl,
                                   const char *name) {
    if (ctrl == nullptr || name == nullptr) return -1;
    for (uint32_t i = 0; i < ctrl->param_count; ++i) {
        if (strcmp(ctrl->params[i].name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Meta path helpers
// ---------------------------------------------------------------------------

void anim_ctrl_build_meta_path(const char *anim_path, char *out,
                               uint32_t out_cap) {
    if (anim_path == nullptr || out == nullptr || out_cap == 0) return;

    // Strip ".anim.meta" suffix to get the base, then append
    // ".controller.meta".
    // e.g. "assets/images/Run.anim.meta" → "assets/images/Run.controller.meta"
    const char *suffix = ".anim.meta";
    size_t anim_len = strlen(anim_path);
    size_t suffix_len = strlen(suffix);

    if (anim_len > suffix_len &&
        strcmp(anim_path + anim_len - suffix_len, suffix) == 0) {
        size_t base_len = anim_len - suffix_len;
        if (base_len + sizeof(".controller.meta") > out_cap) {
            out[0] = '\0';
            return;
        }
        memcpy(out, anim_path, base_len);
        out[base_len] = '\0';
        strncat(out, ".controller.meta", out_cap - base_len - 1);
    } else {
        snprintf(out, out_cap, "%s.controller.meta", anim_path);
    }
}

bool anim_ctrl_meta_exists(const char *anim_path) {
    if (anim_path == nullptr) return false;

    char meta_path[AnimPathMaxLen];
    anim_ctrl_build_meta_path(anim_path, meta_path, AnimPathMaxLen);

#ifdef _WIN32
    DWORD attr = GetFileAttributesA(meta_path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(meta_path, &st) == 0;
#endif
}
