#ifdef EDITOR_BUILD

#include "panel_controller_editor.h"

#include "../../core/asset_manager.h"
#include "../../core/animation.h"
#include "../../core/anim_controller.h"
#include "../../core/anim_cache.h"
#include "../../renderer/renderer.h"
#include "panel_console.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Panel state (module-level statics)
// ---------------------------------------------------------------------------

/// Source .anim.meta path — the controller references this.
static char s_anim_path[AnimPathMaxLen] = {};

/// Custom name for the controller asset.
static char s_controller_name[AnimNameMaxLen] = "NewController";

/// Loaded animation data for clip name reference.
static AnimData s_anim_data = {};
static bool     s_anim_loaded = false;

/// Controller being edited.
static AnimController s_ctrl = {};
static bool           s_ctrl_loaded = false;

/// Selected indices.
static int s_selected_state  = -1;
static int s_selected_trans  = -1;

/// Parameter type names for combo.
static const char *s_param_type_names[] = {
    "Float", "Int", "Bool", "Trigger"
};

/// Condition operator names for combo.
static const char *s_op_names[] = { ">", "<", "==", "!=" };

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void extract_controller_name_from_path(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *start = (slash != nullptr) ? slash + 1 : path;
    const char *dot = strstr(start, ".controller.meta");
    if (dot != nullptr) {
        size_t len = (size_t)(dot - start);
        if (len >= AnimNameMaxLen) len = AnimNameMaxLen - 1;
        memcpy(s_controller_name, start, len);
        s_controller_name[len] = '\0';
    } else {
        const char *anim_dot = strstr(start, ".anim.meta");
        if (anim_dot != nullptr) {
            size_t len = (size_t)(anim_dot - start);
            if (len >= AnimNameMaxLen) len = AnimNameMaxLen - 1;
            memcpy(s_controller_name, start, len);
            s_controller_name[len] = '\0';
        } else {
            strncpy(s_controller_name, start, AnimNameMaxLen - 1);
            s_controller_name[AnimNameMaxLen - 1] = '\0';
        }
    }
}

static void build_controller_save_path(char *out_path, uint32_t max_len) {
    const char *slash = strrchr(s_anim_path, '/');
    if (slash != nullptr) {
        size_t dir_len = (size_t)(slash - s_anim_path) + 1;
        if (dir_len + strlen(s_controller_name) + strlen(".controller.meta") < max_len) {
            memcpy(out_path, s_anim_path, dir_len);
            out_path[dir_len] = '\0';
            strcat(out_path, s_controller_name);
            strcat(out_path, ".controller.meta");
            return;
        }
    }
    snprintf(out_path, max_len, "assets/images/%s.controller.meta", s_controller_name);
}

// ---------------------------------------------------------------------------
// Internal — load animation data for clip palette.
// ---------------------------------------------------------------------------

static void load_source_animation(void) {
    s_anim_loaded = false;
    anim_data_init(&s_anim_data);

    if (s_anim_path[0] == '\0') return;

    if (anim_data_load(s_anim_path, &s_anim_data)) {
        s_anim_loaded = true;
        console_log("[ctrl_editor] Loaded animation: %s (%u clips)",
                    s_anim_path, s_anim_data.clip_count);
        extract_controller_name_from_path(s_anim_path);
    } else {
        console_log("[ctrl_editor] Failed to load animation: %s",
                    s_anim_path);
    }

    // Sync controller's anim path.
    strncpy(s_ctrl.anim_path, s_anim_path, AnimPathMaxLen - 1);
    s_ctrl.anim_path[AnimPathMaxLen - 1] = '\0';
}

/// Try loading an existing .controller.meta for the current anim path.
static void try_load_controller(void) {
    if (s_anim_path[0] == '\0') return;

    char ctrl_path[AnimPathMaxLen];
    build_controller_save_path(ctrl_path, AnimPathMaxLen);

    AnimController tmp = {};
    anim_controller_init(&tmp);
    if (anim_controller_load(ctrl_path, &tmp)) {
        s_ctrl = tmp;
        s_ctrl_loaded = true;
        s_selected_state = (s_ctrl.state_count > 0) ? 0 : -1;
        s_selected_trans = -1;
        extract_controller_name_from_path(ctrl_path);
        console_log("[ctrl_editor] Loaded controller: %s", ctrl_path);
    }
}

// ---------------------------------------------------------------------------
// Internal — draw toolbar.
// ---------------------------------------------------------------------------

static void draw_toolbar(void) {
    igText("Animation:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(200.0f);
    igInputText("##ctrl_anim_path", s_anim_path, sizeof(s_anim_path),
                0, nullptr, nullptr);

    // Drop target for drag-and-drop animation or controller files.
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            if (dropped != nullptr) {
                if (strstr(dropped, ".controller.meta") != nullptr) {
                    AnimController tmp = {};
                    anim_controller_init(&tmp);
                    if (anim_controller_load(dropped, &tmp)) {
                        s_ctrl = tmp;
                        s_ctrl_loaded = true;
                        s_selected_state = (s_ctrl.state_count > 0) ? 0 : -1;
                        s_selected_trans = -1;
                        strncpy(s_anim_path, tmp.anim_path, sizeof(s_anim_path) - 1);
                        s_anim_path[sizeof(s_anim_path) - 1] = '\0';
                        load_source_animation();
                        extract_controller_name_from_path(dropped);
                        console_log("[ctrl_editor] Loaded dropped controller: %s", dropped);
                    }
                } else if (strstr(dropped, ".png") != nullptr) {
                    anim_build_meta_path(dropped, s_anim_path, sizeof(s_anim_path));
                    load_source_animation();
                    try_load_controller();
                } else {
                    strncpy(s_anim_path, dropped, sizeof(s_anim_path) - 1);
                    s_anim_path[sizeof(s_anim_path) - 1] = '\0';
                    load_source_animation();
                    try_load_controller();
                }
            }
        }
        igEndDragDropTarget();
    }

    igSameLine(0.0f, 4.0f);
    if (igButton("Load##ctrl_load", (ImVec2){ 50, 0 })) {
        load_source_animation();
        try_load_controller();
    }

    igSameLine(0.0f, 8.0f);
    igText("Controller Name:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(140.0f);
    igInputText("##ctrl_name", s_controller_name, sizeof(s_controller_name),
                0, nullptr, nullptr);

    if (!s_anim_loaded) return;

    igSameLine(0.0f, 12.0f);
    if (igButton("Save .controller.meta", (ImVec2){ 170, 0 })) {
        char ctrl_path[AnimPathMaxLen];
        build_controller_save_path(ctrl_path, AnimPathMaxLen);
        if (anim_controller_save(&s_ctrl, ctrl_path)) {
            s_ctrl_loaded = true;
            console_log("[ctrl_editor] Saved: %s", ctrl_path);
        } else {
            console_log("[ctrl_editor] Failed to save: %s", ctrl_path);
        }
    }

    igSameLine(0.0f, 8.0f);
    igText("%u clips  |  %u states  |  %u params",
           s_anim_data.clip_count, s_ctrl.state_count, s_ctrl.param_count);
}

// ---------------------------------------------------------------------------
// Internal — draw parameters panel.
// ---------------------------------------------------------------------------

static void draw_parameters(void) {
    igText("Parameters");
    igSeparator();

    // Add parameter button.
    if (s_ctrl.param_count < AnimCtrlMaxParams) {
        if (igButton("+ Add Parameter", (ImVec2){ -1, 0 })) {
            AnimParam *p = &s_ctrl.params[s_ctrl.param_count];
            *p = (AnimParam){};
            snprintf(p->name, AnimNameMaxLen, "param_%u", s_ctrl.param_count);
            p->type = ANIM_PARAM_FLOAT;
            s_ctrl.param_count++;
        }
    }

    igSeparator();

    for (uint32_t i = 0; i < s_ctrl.param_count; ++i) {
        igPushID_Int((int)i);
        AnimParam *p = &s_ctrl.params[i];

        // Type indicator.
        const char *type_label = "";
        switch (p->type) {
            case ANIM_PARAM_FLOAT:   type_label = "[F]"; break;
            case ANIM_PARAM_INT:     type_label = "[I]"; break;
            case ANIM_PARAM_BOOL:    type_label = "[B]"; break;
            case ANIM_PARAM_TRIGGER: type_label = "[T]"; break;
        }
        igTextDisabled("%s", type_label);
        igSameLine(0.0f, 4.0f);

        // Name.
        igSetNextItemWidth(100.0f);
        igInputText("##p_name", p->name, AnimNameMaxLen, 0, nullptr, nullptr);

        igSameLine(0.0f, 4.0f);

        // Type combo.
        igSetNextItemWidth(75.0f);
        int type_idx = (int)p->type;
        if (igCombo_Str_arr("##p_type", &type_idx, s_param_type_names, 4, 4)) {
            p->type = (AnimParamType)type_idx;
        }

        igSameLine(0.0f, 4.0f);

        // Default value.
        igSetNextItemWidth(60.0f);
        switch (p->type) {
            case ANIM_PARAM_FLOAT:
                igDragFloat("##p_def", &p->default_value.f, 0.1f,
                            -9999.0f, 9999.0f, "%.2f", 0);
                break;
            case ANIM_PARAM_INT: {
                int val = (int)p->default_value.i;
                if (igDragInt("##p_def", &val, 1.0f, -9999, 9999, "%d", 0)) {
                    p->default_value.i = (int32_t)val;
                }
                break;
            }
            case ANIM_PARAM_BOOL:
                igCheckbox("##p_def", &p->default_value.b);
                break;
            case ANIM_PARAM_TRIGGER:
                igTextDisabled("(trigger)");
                break;
        }

        // Delete button.
        igSameLine(0.0f, 8.0f);
        if (igButton("X##p_del", (ImVec2){ 20, 0 })) {
            for (uint32_t j = i; j < s_ctrl.param_count - 1; ++j) {
                s_ctrl.params[j] = s_ctrl.params[j + 1];
            }
            s_ctrl.param_count--;
            igPopID();
            break;
        }

        igPopID();
    }
}

// ---------------------------------------------------------------------------
// Internal — draw state list.
// ---------------------------------------------------------------------------

static void draw_state_list(void) {
    igText("States");
    igSeparator();

    // Add state button.
    if (s_ctrl.state_count < AnimCtrlMaxStates) {
        if (igButton("+ Add State", (ImVec2){ -1, 0 })) {
            AnimState *s = &s_ctrl.states[s_ctrl.state_count];
            *s = (AnimState){};
            snprintf(s->name, AnimNameMaxLen, "state_%u", s_ctrl.state_count);
            s->speed = 1.0f;
            s_selected_state = (int)s_ctrl.state_count;
            s_selected_trans = -1;
            s_ctrl.state_count++;
        }
    }

    igSeparator();

    for (uint32_t i = 0; i < s_ctrl.state_count; ++i) {
        igPushID_Int((int)(i + 1000));

        bool is_default = (i == s_ctrl.default_state);
        bool is_selected = ((int)i == s_selected_state);

        // Label: state name + (default) indicator.
        char label[128];
        snprintf(label, sizeof(label), "%s%s (%u trans)",
                 s_ctrl.states[i].name,
                 is_default ? " [DEFAULT]" : "",
                 s_ctrl.states[i].transition_count);

        if (igSelectable_Bool(label, is_selected, 0, (ImVec2){ 0, 0 })) {
            s_selected_state = (int)i;
            s_selected_trans = -1;
        }

        // Right-click context menu.
        if (igBeginPopupContextItem("##state_ctx",
                                     ImGuiPopupFlags_MouseButtonRight)) {
            if (igMenuItem_Bool("Set as Default", nullptr, false, true)) {
                s_ctrl.default_state = i;
            }
            if (igMenuItem_Bool("Delete State", nullptr, false, true)) {
                for (uint32_t j = i; j < s_ctrl.state_count - 1; ++j) {
                    s_ctrl.states[j] = s_ctrl.states[j + 1];
                }
                s_ctrl.state_count--;
                if (s_ctrl.default_state >= s_ctrl.state_count &&
                    s_ctrl.state_count > 0) {
                    s_ctrl.default_state = 0;
                }
                if (s_selected_state >= (int)s_ctrl.state_count) {
                    s_selected_state = (int)s_ctrl.state_count - 1;
                }
                igEndPopup();
                igPopID();
                break;
            }
            igEndPopup();
        }

        igPopID();
    }
}

// ---------------------------------------------------------------------------
// Internal — draw state editor (selected state properties + transitions).
// ---------------------------------------------------------------------------

static void draw_state_editor(void) {
    if (s_selected_state < 0 ||
        s_selected_state >= (int)s_ctrl.state_count) {
        igTextDisabled("Select or create a state from the list.");
        return;
    }

    AnimState *state = &s_ctrl.states[s_selected_state];

    // ---- State properties -------------------------------------------------
    igText("State:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(140.0f);
    igInputText("##st_name", state->name, AnimNameMaxLen, 0,
                nullptr, nullptr);

    igSameLine(0.0f, 12.0f);
    igText("Clip:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(120.0f);

    // Clip selector dropdown (from loaded AnimData clips).
    const char *clip_preview = state->clip_name[0] != '\0'
        ? state->clip_name : "(none)";
    if (igBeginCombo("##st_clip", clip_preview, 0)) {
        if (s_anim_loaded) {
            for (uint32_t c = 0; c < s_anim_data.clip_count; ++c) {
                bool sel = (strcmp(state->clip_name,
                                  s_anim_data.clips[c].name) == 0);
                if (igSelectable_Bool(s_anim_data.clips[c].name, sel,
                                      0, (ImVec2){ 0, 0 })) {
                    strncpy(state->clip_name, s_anim_data.clips[c].name,
                            AnimNameMaxLen - 1);
                    state->clip_name[AnimNameMaxLen - 1] = '\0';
                }
            }
        } else {
            igTextDisabled("No animation data loaded.");
        }
        igEndCombo();
    }

    igSameLine(0.0f, 12.0f);
    igSetNextItemWidth(80.0f);
    igDragFloat("Speed", &state->speed, 0.1f, 0.01f, 10.0f, "%.2f", 0);
    if (state->speed < 0.01f) state->speed = 0.01f;

    igSeparator();

    // ---- Transitions ------------------------------------------------------
    igText("Transitions (%u / %u):", state->transition_count,
           AnimCtrlMaxTransitions);

    if (state->transition_count < AnimCtrlMaxTransitions) {
        igSameLine(0.0f, 8.0f);
        if (igButton("+ Add Transition", (ImVec2){ 130, 0 })) {
            AnimTransition *t =
                &state->transitions[state->transition_count];
            *t = (AnimTransition){};
            s_selected_trans = (int)state->transition_count;
            state->transition_count++;
        }
    }

    igSeparator();

    for (uint32_t i = 0; i < state->transition_count; ++i) {
        igPushID_Int((int)(i + 2000));
        AnimTransition *tr = &state->transitions[i];

        bool tr_selected = ((int)i == s_selected_trans);

        // Target state name.
        const char *target_name = "(invalid)";
        if (tr->target_state < s_ctrl.state_count) {
            target_name = s_ctrl.states[tr->target_state].name;
        }

        char tr_label[128];
        snprintf(tr_label, sizeof(tr_label), "→ %s (%u cond%s)%s",
                 target_name, tr->condition_count,
                 tr->condition_count != 1 ? "s" : "",
                 tr->has_exit_time ? " [exit]" : "");

        if (igSelectable_Bool(tr_label, tr_selected, 0, (ImVec2){ 0, 0 })) {
            s_selected_trans = (int)i;
        }

        // Right-click to delete.
        if (igBeginPopupContextItem("##trans_ctx",
                                     ImGuiPopupFlags_MouseButtonRight)) {
            if (igMenuItem_Bool("Delete Transition", nullptr, false, true)) {
                for (uint32_t j = i; j < state->transition_count - 1; ++j) {
                    state->transitions[j] = state->transitions[j + 1];
                }
                state->transition_count--;
                if (s_selected_trans >= (int)state->transition_count) {
                    s_selected_trans = (int)state->transition_count - 1;
                }
                igEndPopup();
                igPopID();
                break;
            }
            igEndPopup();
        }

        igPopID();
    }

    // ---- Selected transition details --------------------------------------
    if (s_selected_trans >= 0 &&
        s_selected_trans < (int)state->transition_count) {

        igSeparator();
        AnimTransition *tr = &state->transitions[s_selected_trans];

        igText("Transition Details:");

        // Target state combo.
        igText("Target:");
        igSameLine(0.0f, 4.0f);
        igSetNextItemWidth(120.0f);
        const char *tgt_preview = (tr->target_state < s_ctrl.state_count)
            ? s_ctrl.states[tr->target_state].name : "(none)";
        if (igBeginCombo("##tr_target", tgt_preview, 0)) {
            for (uint32_t si = 0; si < s_ctrl.state_count; ++si) {
                if ((int)si == s_selected_state) continue; // skip self
                bool sel = (si == tr->target_state);
                if (igSelectable_Bool(s_ctrl.states[si].name, sel,
                                      0, (ImVec2){ 0, 0 })) {
                    tr->target_state = si;
                }
            }
            igEndCombo();
        }

        igSameLine(0.0f, 12.0f);
        igCheckbox("Has Exit Time", &tr->has_exit_time);

        // Conditions.
        igText("Conditions (%u / %u):", tr->condition_count,
               AnimCtrlMaxConditions);

        if (tr->condition_count < AnimCtrlMaxConditions) {
            igSameLine(0.0f, 8.0f);
            if (igButton("+ Condition", (ImVec2){ 100, 0 })) {
                AnimCondition *c = &tr->conditions[tr->condition_count];
                *c = (AnimCondition){};
                tr->condition_count++;
            }
        }

        for (uint32_t ci = 0; ci < tr->condition_count; ++ci) {
            igPushID_Int((int)(ci + 3000));
            AnimCondition *cond = &tr->conditions[ci];

            // Parameter combo.
            igSetNextItemWidth(100.0f);
            const char *param_preview = (cond->param_index < s_ctrl.param_count)
                ? s_ctrl.params[cond->param_index].name : "(none)";
            if (igBeginCombo("##c_param", param_preview, 0)) {
                for (uint32_t pi = 0; pi < s_ctrl.param_count; ++pi) {
                    bool sel = (pi == cond->param_index);
                    if (igSelectable_Bool(s_ctrl.params[pi].name, sel,
                                          0, (ImVec2){ 0, 0 })) {
                        cond->param_index = pi;
                    }
                }
                igEndCombo();
            }

            igSameLine(0.0f, 4.0f);

            // Operator combo.
            igSetNextItemWidth(50.0f);
            int op_idx = (int)cond->op;
            if (igCombo_Str_arr("##c_op", &op_idx, s_op_names, 4, 4)) {
                cond->op = (AnimConditionOp)op_idx;
            }

            igSameLine(0.0f, 4.0f);

            // Threshold value — type depends on referenced parameter.
            igSetNextItemWidth(80.0f);
            if (cond->param_index < s_ctrl.param_count) {
                AnimParamType pt = s_ctrl.params[cond->param_index].type;
                switch (pt) {
                    case ANIM_PARAM_FLOAT:
                        igDragFloat("##c_val", &cond->threshold.f, 0.1f,
                                    -9999.0f, 9999.0f, "%.2f", 0);
                        break;
                    case ANIM_PARAM_INT: {
                        int val = (int)cond->threshold.i;
                        if (igDragInt("##c_val", &val, 1.0f,
                                      -9999, 9999, "%d", 0)) {
                            cond->threshold.i = (int32_t)val;
                        }
                        break;
                    }
                    case ANIM_PARAM_BOOL:
                    case ANIM_PARAM_TRIGGER:
                        igCheckbox("##c_val", &cond->threshold.b);
                        break;
                }
            }

            // Delete condition.
            igSameLine(0.0f, 8.0f);
            if (igButton("X##c_del", (ImVec2){ 20, 0 })) {
                for (uint32_t j = ci; j < tr->condition_count - 1; ++j) {
                    tr->conditions[j] = tr->conditions[j + 1];
                }
                tr->condition_count--;
                igPopID();
                break;
            }

            igPopID();
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void panel_controller_editor_render(bool *p_open,
                                    [[maybe_unused]] AssetManager *am,
                                    [[maybe_unused]] Renderer *renderer) {
    if (!igBegin("Controller Editor", p_open, ImGuiWindowFlags_MenuBar)) {
        igEnd();
        return;
    }

    // ---- Menu bar ---------------------------------------------------------
    if (igBeginMenuBar()) {
        if (igBeginMenu("File", true)) {
            if (igMenuItem_Bool("Save", "Ctrl+S", false,
                                s_anim_loaded)) {
                char ctrl_path[AnimPathMaxLen];
                anim_ctrl_build_meta_path(s_anim_path, ctrl_path,
                                          AnimPathMaxLen);
                if (anim_controller_save(&s_ctrl, ctrl_path)) {
                    s_ctrl_loaded = true;
                    console_log("[ctrl_editor] Saved: %s", ctrl_path);
                }
            }
            igEndMenu();
        }
        igEndMenuBar();
    }

    // ---- Toolbar ----------------------------------------------------------
    draw_toolbar();

    if (!s_anim_loaded) {
        igTextDisabled("Load an .anim.meta file to begin authoring a "
                       "controller.");
        igEnd();
        return;
    }

    igSeparator();

    // ---- Layout: params (left) | states (center) | editor (right) ---------
    float avail_w = igGetContentRegionAvail().x;
    float param_w  = 220.0f;
    float state_w  = 200.0f;
    float editor_w = avail_w - param_w - state_w - 16.0f;
    if (editor_w < 200.0f) editor_w = avail_w;

    // Parameters panel.
    if (igBeginChild_Str("##ctrl_params", (ImVec2){ param_w, 0 },
                          ImGuiChildFlags_Borders, 0)) {
        draw_parameters();
    }
    igEndChild();

    igSameLine(0.0f, 8.0f);

    // State list.
    if (igBeginChild_Str("##ctrl_states", (ImVec2){ state_w, 0 },
                          ImGuiChildFlags_Borders, 0)) {
        draw_state_list();
    }
    igEndChild();

    igSameLine(0.0f, 8.0f);

    // State editor.
    if (igBeginChild_Str("##ctrl_state_editor", (ImVec2){ editor_w, 0 },
                          ImGuiChildFlags_Borders, 0)) {
        draw_state_editor();
    }
    igEndChild();

    igEnd();
}

void panel_controller_editor_shutdown([[maybe_unused]] Renderer *renderer) {
    s_anim_loaded  = false;
    s_ctrl_loaded  = false;
    s_selected_state = -1;
    s_selected_trans = -1;
}

#endif // EDITOR_BUILD
