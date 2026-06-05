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

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr float NodeWidth      = 180.0f;
constexpr float NodeHeight     = 64.0f;
constexpr float NodeRounding   = 6.0f;
constexpr float HeaderHeight   = 22.0f;
constexpr float GridStep       = 48.0f;
constexpr float ZoomMin        = 0.3f;
constexpr float ZoomMax        = 3.0f;
constexpr float EntryRadius    = 14.0f;
constexpr float EntryOffsetX   = -80.0f;
constexpr float ArrowSize      = 8.0f;
constexpr float LinkThickness  = 2.5f;
constexpr float ParamPanelW    = 220.0f;
constexpr float InspectorPanelW = 260.0f;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

// IM_COL32 is only defined in the C++ imgui.h, not in cimgui's C header.
// Define it here for direct RGBA → ImU32 packing.
#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
    (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R)))
#endif // IM_COL32

#define COL_GRID_BG       IM_COL32(28,  28,  32,  255)
#define COL_GRID_LINE     IM_COL32(40,  40,  48,  255)
#define COL_GRID_MAJOR    IM_COL32(50,  50,  60,  255)
#define COL_NODE_BG       IM_COL32(40,  42,  54,  240)
#define COL_NODE_BORDER   IM_COL32(60,  62,  78,  255)
#define COL_NODE_SEL      IM_COL32(90, 170, 255,  255)
#define COL_HDR_DEFAULT   IM_COL32(210, 120,  40,  255)
#define COL_HDR_NORMAL    IM_COL32(55,  58,  75,  255)
#define COL_HDR_TEXT      IM_COL32(230, 230, 235,  255)
#define COL_BODY_TEXT     IM_COL32(170, 170, 180,  255)
#define COL_ENTRY_FILL    IM_COL32(60,  190, 100,  255)
#define COL_ENTRY_BORDER  IM_COL32(40,  140,  70,  255)
#define COL_LINK          IM_COL32(200, 200, 210,  200)
#define COL_LINK_SEL      IM_COL32(90, 170, 255,  255)
#define COL_LINK_ACTIVE   IM_COL32(255, 200,  60,  255)
#define COL_ARROW         IM_COL32(200, 200, 210,  230)

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
static int s_selected_trans  = -1;  // index into selected state's transitions

/// Parameter type names for combo.
static const char *s_param_type_names[] = {
    "Float", "Int", "Bool", "Trigger"
};

/// Condition operator names for combo.
static const char *s_op_names[] = { ">", "<", "==", "!=" };

// ---------------------------------------------------------------------------
// Canvas state
// ---------------------------------------------------------------------------

/// Pan offset in world-space (scroll position).
static ImVec2 s_canvas_scroll = { 0.0f, 0.0f };

/// Zoom level.
static float s_canvas_zoom = 1.0f;

/// Node dragging.
static int   s_dragging_node = -1;
static float s_drag_offset_x = 0.0f;
static float s_drag_offset_y = 0.0f;

/// Transition creation (linking mode).
static bool  s_linking_active = false;
static int   s_link_source    = -1;

/// Selected transition source state (for highlighting the curve).
static int   s_sel_trans_src  = -1;

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

/// Convert world-space position to screen-space.
static ImVec2_c canvas_to_screen(ImVec2_c origin, float wx, float wy) {
    return (ImVec2_c){
        origin.x + (wx + s_canvas_scroll.x) * s_canvas_zoom,
        origin.y + (wy + s_canvas_scroll.y) * s_canvas_zoom
    };
}

/// Convert screen-space position to world-space.
static void screen_to_canvas(ImVec2_c origin, float sx, float sy,
                              float *out_wx, float *out_wy) {
    *out_wx = (sx - origin.x) / s_canvas_zoom - s_canvas_scroll.x;
    *out_wy = (sy - origin.y) / s_canvas_zoom - s_canvas_scroll.y;
}

// ---------------------------------------------------------------------------
// Helpers (preserved from original)
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

// ---------------------------------------------------------------------------
// Internal — draw toolbar (preserved from original).
// ---------------------------------------------------------------------------

static void draw_toolbar(void) {
    igText("Controller Name:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(130.0f);
    igInputText("##ctrl_name", s_controller_name, sizeof(s_controller_name),
                0, nullptr, nullptr);

    igSameLine(0.0f, 12.0f);
    igText("Animation path:");
    igSameLine(0.0f, 4.0f);
    
    char anim_label[AnimPathMaxLen + 32];
    if (s_ctrl.anim_path[0] != '\0') {
        snprintf(anim_label, sizeof(anim_label), "%s##anim_btn", s_ctrl.anim_path);
    } else {
        snprintf(anim_label, sizeof(anim_label), "(none - drop .anim.meta/.png here)##anim_btn");
    }

    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){ 0.15f, 0.15f, 0.20f, 1.0f });
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){ 0.20f, 0.25f, 0.35f, 1.0f });
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){ 0.18f, 0.22f, 0.30f, 1.0f });
    igButton(anim_label, (ImVec2){ 250.0f, 0.0f });
    igPopStyleColor(3);

    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            if (dropped != nullptr) {
                char meta_path[AnimPathMaxLen] = {};
                if (strstr(dropped, ".anim.meta") != nullptr) {
                    strncpy(meta_path, dropped, AnimPathMaxLen - 1);
                } else if (strstr(dropped, ".png") != nullptr) {
                    anim_build_meta_path(dropped, meta_path, AnimPathMaxLen);
                }
                if (meta_path[0] != '\0') {
                    strncpy(s_ctrl.anim_path, meta_path, AnimPathMaxLen - 1);
                    s_ctrl.anim_path[AnimPathMaxLen - 1] = '\0';
                    strncpy(s_anim_path, meta_path, AnimPathMaxLen - 1);
                    s_anim_path[AnimPathMaxLen - 1] = '\0';
                    load_source_animation();
                }
            }
        }
        igEndDragDropTarget();
    }

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
    igTextDisabled("%u clips | %u states | %u params",
           s_anim_loaded ? s_anim_data.clip_count : 0, s_ctrl.state_count, s_ctrl.param_count);
}

// ---------------------------------------------------------------------------
// Internal — draw parameters panel (preserved from original).
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
// Canvas — Grid background
// ---------------------------------------------------------------------------

static void draw_canvas_grid(ImDrawList *dl, ImVec2_c origin, ImVec2_c size) {
    // Fill background.
    ImVec2_c p_max = { origin.x + size.x, origin.y + size.y };
    ImDrawList_AddRectFilled(dl, origin, p_max, COL_GRID_BG, 0.0f, 0);

    float step = GridStep * s_canvas_zoom;
    if (step < 4.0f) step = 4.0f;

    // Compute offset so grid scrolls with content.
    float off_x = fmodf(s_canvas_scroll.x * s_canvas_zoom, step);
    float off_y = fmodf(s_canvas_scroll.y * s_canvas_zoom, step);

    // Count lines for major/minor distinction.
    float start_world_x = -s_canvas_scroll.x - (origin.x) / s_canvas_zoom;
    float start_world_y = -s_canvas_scroll.y - (origin.y) / s_canvas_zoom;
    (void)start_world_x;
    (void)start_world_y;

    int ix = 0;
    for (float x = off_x; x < size.x; x += step, ++ix) {
        ImU32 col = (ix % 5 == 0) ? COL_GRID_MAJOR : COL_GRID_LINE;
        ImVec2_c p1 = { origin.x + x, origin.y };
        ImVec2_c p2 = { origin.x + x, origin.y + size.y };
        ImDrawList_AddLine(dl, p1, p2, col, 1.0f);
    }

    int iy = 0;
    for (float y = off_y; y < size.y; y += step, ++iy) {
        ImU32 col = (iy % 5 == 0) ? COL_GRID_MAJOR : COL_GRID_LINE;
        ImVec2_c p1 = { origin.x, origin.y + y };
        ImVec2_c p2 = { origin.x + size.x, origin.y + y };
        ImDrawList_AddLine(dl, p1, p2, col, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Canvas — Node rendering
// ---------------------------------------------------------------------------

static void draw_node(ImDrawList *dl, ImVec2_c origin, uint32_t idx) {
    AnimState *state = &s_ctrl.states[idx];
    float nw = NodeWidth  * s_canvas_zoom;
    float nh = NodeHeight * s_canvas_zoom;
    float hh = HeaderHeight * s_canvas_zoom;
    float rnd = NodeRounding * s_canvas_zoom;

    ImVec2_c p_min = canvas_to_screen(origin, state->editor_x, state->editor_y);
    ImVec2_c p_max = { p_min.x + nw, p_min.y + nh };
    ImVec2_c h_max = { p_min.x + nw, p_min.y + hh };

    bool is_selected = ((int)idx == s_selected_state);
    bool is_default  = (idx == s_ctrl.default_state);

    // Drop shadow.
    ImVec2_c shadow_min = { p_min.x + 3.0f * s_canvas_zoom, p_min.y + 3.0f * s_canvas_zoom };
    ImVec2_c shadow_max = { p_max.x + 3.0f * s_canvas_zoom, p_max.y + 3.0f * s_canvas_zoom };
    ImDrawList_AddRectFilled(dl, shadow_min, shadow_max,
                             IM_COL32(0, 0, 0, 60), rnd, 0);

    // Body.
    ImDrawList_AddRectFilled(dl, p_min, p_max, COL_NODE_BG, rnd, 0);

    // Header.
    ImU32 hdr_col = is_default ? COL_HDR_DEFAULT : COL_HDR_NORMAL;
    // Clip header to top-rounded corners only.
    ImDrawList_AddRectFilled(dl, p_min, h_max, hdr_col, rnd,
                             ImDrawFlags_RoundCornersTop);

    // Border.
    ImU32 border_col = is_selected ? COL_NODE_SEL : COL_NODE_BORDER;
    float border_thick = is_selected ? 2.5f * s_canvas_zoom : 1.0f * s_canvas_zoom;
    ImDrawList_AddRect(dl, p_min, p_max, border_col, rnd, border_thick, 0);

    // Header text (state name).
    float font_size = 13.0f * s_canvas_zoom;
    if (font_size < 6.0f) font_size = 6.0f;
    float text_pad = 6.0f * s_canvas_zoom;
    ImVec2_c text_pos = { p_min.x + text_pad, p_min.y + 3.0f * s_canvas_zoom };

    char label[128];
    if (is_default) {
        snprintf(label, sizeof(label), "▶ %s", state->name);
    } else {
        snprintf(label, sizeof(label), "%s", state->name);
    }
    ImDrawList_AddText_FontPtr(dl, igGetFont(), font_size, text_pos,
                               COL_HDR_TEXT, label, nullptr, 0.0f, nullptr);

    // Body text (clip name + speed).
    ImVec2_c body_pos = { p_min.x + text_pad, p_min.y + hh + 4.0f * s_canvas_zoom };
    float body_font = 11.0f * s_canvas_zoom;
    if (body_font < 5.0f) body_font = 5.0f;
    const char *clip_label = state->clip_name[0] != '\0'
        ? state->clip_name : "(no clip)";
    char body_text[128];
    snprintf(body_text, sizeof(body_text), "🎬 %s  ×%.1f", clip_label, (double)state->speed);
    ImDrawList_AddText_FontPtr(dl, igGetFont(), body_font, body_pos,
                               COL_BODY_TEXT, body_text, nullptr, 0.0f, nullptr);
}

// ---------------------------------------------------------------------------
// Canvas — Entry node (green circle → default state)
// ---------------------------------------------------------------------------

static void draw_entry_node(ImDrawList *dl, ImVec2_c origin) {
    if (s_ctrl.state_count == 0) return;

    AnimState *def = &s_ctrl.states[s_ctrl.default_state];
    float r = EntryRadius * s_canvas_zoom;

    // Position entry circle to the left of the default state.
    ImVec2_c target = canvas_to_screen(origin, def->editor_x, def->editor_y);
    float cy = target.y + (NodeHeight * s_canvas_zoom) * 0.5f;
    float cx = target.x + EntryOffsetX * s_canvas_zoom;

    ImVec2_c center = { cx, cy };
    ImDrawList_AddCircleFilled(dl, center, r, COL_ENTRY_FILL, 0);
    ImDrawList_AddCircle(dl, center, r, COL_ENTRY_BORDER, 0, 2.0f * s_canvas_zoom);

    // "Entry" text.
    float font_sz = 10.0f * s_canvas_zoom;
    if (font_sz >= 5.0f) {
        ImVec2_c txt_pos = { cx - 14.0f * s_canvas_zoom, cy - 5.0f * s_canvas_zoom };
        ImDrawList_AddText_FontPtr(dl, igGetFont(), font_sz, txt_pos,
                                   IM_COL32(255, 255, 255, 220), "Entry",
                                   nullptr, 0.0f, nullptr);
    }

    // Arrow from entry circle to default state.
    ImVec2_c arrow_start = { cx + r + 2.0f * s_canvas_zoom, cy };
    ImVec2_c arrow_end   = { target.x - 2.0f * s_canvas_zoom, cy };
    ImDrawList_AddLine(dl, arrow_start, arrow_end, COL_ENTRY_FILL,
                       2.0f * s_canvas_zoom);

    // Arrowhead.
    float as = ArrowSize * s_canvas_zoom;
    ImVec2_c t1 = { arrow_end.x, arrow_end.y };
    ImVec2_c t2 = { arrow_end.x - as, arrow_end.y - as * 0.5f };
    ImVec2_c t3 = { arrow_end.x - as, arrow_end.y + as * 0.5f };
    ImDrawList_AddTriangleFilled(dl, t1, t2, t3, COL_ENTRY_FILL);
}

// ---------------------------------------------------------------------------
// Canvas — Transition bezier curves
// ---------------------------------------------------------------------------

static void draw_transitions(ImDrawList *dl, ImVec2_c origin) {
    float nw = NodeWidth  * s_canvas_zoom;
    float nh = NodeHeight * s_canvas_zoom;

    for (uint32_t si = 0; si < s_ctrl.state_count; ++si) {
        AnimState *state = &s_ctrl.states[si];
        ImVec2_c src_screen = canvas_to_screen(origin, state->editor_x, state->editor_y);
        float src_cx = src_screen.x + nw;           // right edge
        float src_cy = src_screen.y + nh * 0.5f;    // vertical center

        for (uint32_t ti = 0; ti < state->transition_count; ++ti) {
            AnimTransition *tr = &state->transitions[ti];
            if (tr->target_state >= s_ctrl.state_count) continue;

            AnimState *target = &s_ctrl.states[tr->target_state];
            ImVec2_c dst_screen = canvas_to_screen(origin, target->editor_x,
                                                    target->editor_y);
            float dst_cx = dst_screen.x;              // left edge
            float dst_cy = dst_screen.y + nh * 0.5f;  // vertical center

            // Determine color.
            bool is_sel = ((int)si == s_sel_trans_src &&
                           (int)si == s_selected_state &&
                           (int)ti == s_selected_trans);
            ImU32 col = is_sel ? COL_LINK_SEL : COL_LINK;
            float thick = is_sel ? (LinkThickness + 1.0f) * s_canvas_zoom
                                 : LinkThickness * s_canvas_zoom;

            // Bezier control points — horizontal offset for nice curves.
            float dx = fabsf(dst_cx - src_cx) * 0.5f;
            if (dx < 50.0f * s_canvas_zoom) dx = 50.0f * s_canvas_zoom;

            ImVec2_c p1 = { src_cx, src_cy };
            ImVec2_c p2 = { src_cx + dx, src_cy };
            ImVec2_c p3 = { dst_cx - dx, dst_cy };
            ImVec2_c p4 = { dst_cx, dst_cy };

            ImDrawList_AddBezierCubic(dl, p1, p2, p3, p4, col, thick, 0);

            // Arrowhead at destination.
            float as = ArrowSize * s_canvas_zoom;
            ImVec2_c t1 = { dst_cx, dst_cy };
            ImVec2_c t2 = { dst_cx - as, dst_cy - as * 0.5f };
            ImVec2_c t3 = { dst_cx - as, dst_cy + as * 0.5f };
            ImDrawList_AddTriangleFilled(dl, t1, t2, t3, is_sel ? COL_LINK_SEL : COL_ARROW);
        }
    }
}

// ---------------------------------------------------------------------------
// Canvas — Draw the linking line (transition creation in progress)
// ---------------------------------------------------------------------------

static void draw_linking_line(ImDrawList *dl, ImVec2_c origin) {
    if (!s_linking_active || s_link_source < 0 ||
        s_link_source >= (int)s_ctrl.state_count) return;

    AnimState *src = &s_ctrl.states[s_link_source];
    float nw = NodeWidth  * s_canvas_zoom;
    float nh = NodeHeight * s_canvas_zoom;

    ImVec2_c src_screen = canvas_to_screen(origin, src->editor_x, src->editor_y);
    float sx = src_screen.x + nw;
    float sy = src_screen.y + nh * 0.5f;

    ImVec2_c mouse_pos_c = igGetMousePos();

    float dx = fabsf(mouse_pos_c.x - sx) * 0.5f;
    if (dx < 50.0f * s_canvas_zoom) dx = 50.0f * s_canvas_zoom;

    ImVec2_c p1 = { sx, sy };
    ImVec2_c p2 = { sx + dx, sy };
    ImVec2_c p3 = { mouse_pos_c.x - dx, mouse_pos_c.y };
    ImVec2_c p4 = { mouse_pos_c.x, mouse_pos_c.y };

    ImDrawList_AddBezierCubic(dl, p1, p2, p3, p4, COL_LINK_ACTIVE,
                              2.5f * s_canvas_zoom, 0);
}

// ---------------------------------------------------------------------------
// Canvas — Hit testing
// ---------------------------------------------------------------------------

/// Returns the index of the node under screen position (sx, sy), or -1.
static int hit_test_node(ImVec2_c origin, float sx, float sy) {
    float nw = NodeWidth  * s_canvas_zoom;
    float nh = NodeHeight * s_canvas_zoom;

    // Iterate in reverse so topmost (last drawn) is checked first.
    for (int i = (int)s_ctrl.state_count - 1; i >= 0; --i) {
        AnimState *st = &s_ctrl.states[i];
        ImVec2_c p = canvas_to_screen(origin, st->editor_x, st->editor_y);
        if (sx >= p.x && sx <= p.x + nw && sy >= p.y && sy <= p.y + nh) {
            return i;
        }
    }
    return -1;
}

/// Hit test transition curves. Returns source state index, sets *out_trans.
static int hit_test_transition(ImVec2_c origin, float sx, float sy,
                                int *out_trans) {
    float nw = NodeWidth  * s_canvas_zoom;
    float nh = NodeHeight * s_canvas_zoom;
    float threshold = 8.0f * s_canvas_zoom;

    for (uint32_t si = 0; si < s_ctrl.state_count; ++si) {
        AnimState *state = &s_ctrl.states[si];
        ImVec2_c src_screen = canvas_to_screen(origin, state->editor_x,
                                                state->editor_y);
        float src_cx = src_screen.x + nw;
        float src_cy = src_screen.y + nh * 0.5f;

        for (uint32_t ti = 0; ti < state->transition_count; ++ti) {
            AnimTransition *tr = &state->transitions[ti];
            if (tr->target_state >= s_ctrl.state_count) continue;

            AnimState *target = &s_ctrl.states[tr->target_state];
            ImVec2_c dst_screen = canvas_to_screen(origin, target->editor_x,
                                                    target->editor_y);
            float dst_cx = dst_screen.x;
            float dst_cy = dst_screen.y + nh * 0.5f;

            // Simple midpoint distance check (not exact bezier, but good enough).
            float mx = (src_cx + dst_cx) * 0.5f;
            float my = (src_cy + dst_cy) * 0.5f;
            float dist = sqrtf((sx - mx) * (sx - mx) + (sy - my) * (sy - my));
            if (dist < threshold + 20.0f * s_canvas_zoom) {
                // More precise: check distance to line segment approximation.
                // Sample a few points along the bezier.
                float dx_ctrl = fabsf(dst_cx - src_cx) * 0.5f;
                if (dx_ctrl < 50.0f * s_canvas_zoom) dx_ctrl = 50.0f * s_canvas_zoom;

                for (int s = 0; s <= 10; ++s) {
                    float t = (float)s / 10.0f;
                    float it = 1.0f - t;
                    // Cubic bezier evaluation.
                    float bx = it*it*it * src_cx
                             + 3.0f * it*it * t * (src_cx + dx_ctrl)
                             + 3.0f * it * t*t * (dst_cx - dx_ctrl)
                             + t*t*t * dst_cx;
                    float by = it*it*it * src_cy
                             + 3.0f * it*it * t * src_cy
                             + 3.0f * it * t*t * dst_cy
                             + t*t*t * dst_cy;
                    float d = sqrtf((sx - bx) * (sx - bx) + (sy - by) * (sy - by));
                    if (d < threshold) {
                        *out_trans = (int)ti;
                        return (int)si;
                    }
                }
            }
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Canvas — Input handling
// ---------------------------------------------------------------------------

static void handle_canvas_input(ImVec2_c origin, ImVec2_c size,
                                 bool canvas_hovered) {
    ImGuiIO *io = igGetIO_Nil();
    ImVec2_c mouse = igGetMousePos();

    // Check if mouse is within canvas bounds (for pan/zoom).
    bool in_canvas = (mouse.x >= origin.x && mouse.x <= origin.x + size.x &&
                      mouse.y >= origin.y && mouse.y <= origin.y + size.y);
    if (!in_canvas) return;

    // ---- Zoom with mouse wheel ----
    if (io->MouseWheel != 0.0f) {
        float old_zoom = s_canvas_zoom;
        s_canvas_zoom += io->MouseWheel * 0.1f;
        if (s_canvas_zoom < ZoomMin) s_canvas_zoom = ZoomMin;
        if (s_canvas_zoom > ZoomMax) s_canvas_zoom = ZoomMax;

        // Zoom toward mouse position.
        if (s_canvas_zoom != old_zoom) {
            float zoom_ratio = s_canvas_zoom / old_zoom;
            float mx_world = (mouse.x - origin.x) / old_zoom - s_canvas_scroll.x;
            float my_world = (mouse.y - origin.y) / old_zoom - s_canvas_scroll.y;
            s_canvas_scroll.x = (mouse.x - origin.x) / s_canvas_zoom - mx_world;
            s_canvas_scroll.y = (mouse.y - origin.y) / s_canvas_zoom - my_world;
            (void)zoom_ratio;
        }
    }

    // ---- Middle-mouse drag for panning ----
    if (igIsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        s_canvas_scroll.x += io->MouseDelta.x / s_canvas_zoom;
        s_canvas_scroll.y += io->MouseDelta.y / s_canvas_zoom;
    }

    // ---- Left click: node selection / dragging ----
    // Only process when canvas is hovered (not when a popup is open on top).
    if (canvas_hovered && igIsMouseClicked_Bool(ImGuiMouseButton_Left, false)) {
        int hit = hit_test_node(origin, mouse.x, mouse.y);
        if (hit >= 0) {
            // Linking mode: complete transition.
            if (s_linking_active && s_link_source != hit) {
                AnimState *src = &s_ctrl.states[s_link_source];
                if (src->transition_count < AnimCtrlMaxTransitions) {
                    AnimTransition *t = &src->transitions[src->transition_count];
                    *t = (AnimTransition){};
                    t->target_state = (uint32_t)hit;
                    src->transition_count++;
                    console_log("[ctrl_editor] Transition: %s → %s",
                                src->name, s_ctrl.states[hit].name);
                }
                s_linking_active = false;
                s_link_source = -1;
            } else {
                s_linking_active = false;
                s_link_source = -1;
            }

            s_selected_state = hit;
            s_selected_trans = -1;
            s_sel_trans_src = -1;
            s_dragging_node = hit;

            // Compute drag offset (mouse pos - node screen pos).
            ImVec2_c node_screen = canvas_to_screen(origin,
                s_ctrl.states[hit].editor_x, s_ctrl.states[hit].editor_y);
            s_drag_offset_x = mouse.x - node_screen.x;
            s_drag_offset_y = mouse.y - node_screen.y;
        } else {
            // Click on empty canvas — check transition hit.
            int trans_idx = -1;
            int src_idx = hit_test_transition(origin, mouse.x, mouse.y, &trans_idx);
            if (src_idx >= 0) {
                s_selected_state = src_idx;
                s_selected_trans = trans_idx;
                s_sel_trans_src = src_idx;
            } else {
                // Deselect.
                if (!s_linking_active) {
                    s_selected_state = -1;
                    s_selected_trans = -1;
                    s_sel_trans_src = -1;
                }
            }
            // Cancel linking if clicked on empty space.
            if (s_linking_active) {
                s_linking_active = false;
                s_link_source = -1;
            }
            s_dragging_node = -1;
        }
    }

    // ---- Node dragging ----
    if (s_dragging_node >= 0 && igIsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        float wx = 0, wy = 0;
        screen_to_canvas(origin, mouse.x - s_drag_offset_x,
                         mouse.y - s_drag_offset_y, &wx, &wy);
        s_ctrl.states[s_dragging_node].editor_x = wx;
        s_ctrl.states[s_dragging_node].editor_y = wy;
    }
    if (igIsMouseReleased_Nil(ImGuiMouseButton_Left)) {
        s_dragging_node = -1;
    }

    // ---- Right-click context menu ----
    if (canvas_hovered && igIsMouseClicked_Bool(ImGuiMouseButton_Right, false)) {
        int hit = hit_test_node(origin, mouse.x, mouse.y);
        if (hit >= 0) {
            s_selected_state = hit;
            s_selected_trans = -1;
            igOpenPopup_Str("##node_ctx", 0);
        } else {
            igOpenPopup_Str("##canvas_ctx", 0);
        }
    }

    // Node context menu.
    if (igBeginPopup("##node_ctx", 0)) {
        if (s_selected_state >= 0 && s_selected_state < (int)s_ctrl.state_count) {
            igText("%s", s_ctrl.states[s_selected_state].name);
            igSeparator();

            if (igMenuItem_Bool("Set as Default", nullptr, false, true)) {
                s_ctrl.default_state = (uint32_t)s_selected_state;
            }
            if (igMenuItem_Bool("Make Transition", nullptr, false, true)) {
                s_linking_active = true;
                s_link_source = s_selected_state;
            }
            igSeparator();
            if (igMenuItem_Bool("Delete State", nullptr, false, true)) {
                uint32_t idx = (uint32_t)s_selected_state;

                // Fix transition target indices that reference states after this one.
                for (uint32_t si = 0; si < s_ctrl.state_count; ++si) {
                    AnimState *st = &s_ctrl.states[si];
                    for (uint32_t ti = 0; ti < st->transition_count; ) {
                        if (st->transitions[ti].target_state == idx) {
                            // Remove transitions pointing to deleted state.
                            for (uint32_t j = ti; j < st->transition_count - 1; ++j) {
                                st->transitions[j] = st->transitions[j + 1];
                            }
                            st->transition_count--;
                        } else {
                            if (st->transitions[ti].target_state > idx) {
                                st->transitions[ti].target_state--;
                            }
                            ++ti;
                        }
                    }
                }

                // Remove the state.
                for (uint32_t j = idx; j < s_ctrl.state_count - 1; ++j) {
                    s_ctrl.states[j] = s_ctrl.states[j + 1];
                }
                s_ctrl.state_count--;
                if (s_ctrl.default_state >= s_ctrl.state_count && s_ctrl.state_count > 0) {
                    s_ctrl.default_state = 0;
                } else if (s_ctrl.default_state > idx) {
                    s_ctrl.default_state--;
                }
                s_selected_state = -1;
                s_selected_trans = -1;
            }
        }
        igEndPopup();
    }

    // Canvas context menu (right-click on empty space).
    if (igBeginPopup("##canvas_ctx", 0)) {
        if (s_ctrl.state_count < AnimCtrlMaxStates) {
            if (igMenuItem_Bool("Add State", nullptr, false, true)) {
                AnimState *st = &s_ctrl.states[s_ctrl.state_count];
                *st = (AnimState){};
                snprintf(st->name, AnimNameMaxLen, "state_%u", s_ctrl.state_count);
                st->speed = 1.0f;

                // Place at mouse position in world space.
                float wx = 0, wy = 0;
                screen_to_canvas(origin, mouse.x, mouse.y, &wx, &wy);
                st->editor_x = wx;
                st->editor_y = wy;

                s_selected_state = (int)s_ctrl.state_count;
                s_selected_trans = -1;
                s_ctrl.state_count++;
                console_log("[ctrl_editor] Added state: %s", st->name);
            }
        }
        igSeparator();
        if (igMenuItem_Bool("Reset View", nullptr, false, true)) {
            s_canvas_scroll = (ImVec2){ 50.0f, 50.0f };
            s_canvas_zoom = 1.0f;
        }
        igEndPopup();
    }

    // ---- Delete selected state with Delete key ----
    if (s_selected_state >= 0 && igIsKeyPressed_Bool(ImGuiKey_Delete, false)) {
        uint32_t idx = (uint32_t)s_selected_state;
        if (idx < s_ctrl.state_count) {
            // Fix transition targets.
            for (uint32_t si = 0; si < s_ctrl.state_count; ++si) {
                AnimState *st = &s_ctrl.states[si];
                for (uint32_t ti = 0; ti < st->transition_count; ) {
                    if (st->transitions[ti].target_state == idx) {
                        for (uint32_t j = ti; j < st->transition_count - 1; ++j) {
                            st->transitions[j] = st->transitions[j + 1];
                        }
                        st->transition_count--;
                    } else {
                        if (st->transitions[ti].target_state > idx) {
                            st->transitions[ti].target_state--;
                        }
                        ++ti;
                    }
                }
            }
            for (uint32_t j = idx; j < s_ctrl.state_count - 1; ++j) {
                s_ctrl.states[j] = s_ctrl.states[j + 1];
            }
            s_ctrl.state_count--;
            if (s_ctrl.default_state >= s_ctrl.state_count && s_ctrl.state_count > 0) {
                s_ctrl.default_state = 0;
            } else if (s_ctrl.default_state > idx) {
                s_ctrl.default_state--;
            }
            s_selected_state = -1;
            s_selected_trans = -1;
        }
    }
}

// ---------------------------------------------------------------------------
// Canvas — Main draw function
// ---------------------------------------------------------------------------

static void draw_node_canvas(ImVec2_c canvas_origin, ImVec2_c canvas_size,
                              bool canvas_hovered) {
    ImDrawList *dl = igGetWindowDrawList();

    // Background grid.
    draw_canvas_grid(dl, canvas_origin, canvas_size);

    // Clip drawing to the canvas area.
    ImVec2_c clip_max = { canvas_origin.x + canvas_size.x,
                          canvas_origin.y + canvas_size.y };
    ImDrawList_PushClipRect(dl, canvas_origin, clip_max, true);

    // Entry node.
    draw_entry_node(dl, canvas_origin);

    // Transition curves.
    draw_transitions(dl, canvas_origin);

    // Linking line (if creating a new transition).
    draw_linking_line(dl, canvas_origin);

    // State nodes.
    for (uint32_t i = 0; i < s_ctrl.state_count; ++i) {
        draw_node(dl, canvas_origin, i);
    }

    ImDrawList_PopClipRect(dl);

    // Input handling (must be after drawing so popup IDs exist).
    handle_canvas_input(canvas_origin, canvas_size, canvas_hovered);
}

// ---------------------------------------------------------------------------
// Inspector — State properties (right panel)
// ---------------------------------------------------------------------------

static void draw_state_inspector(void) {
    if (s_selected_state < 0 ||
        s_selected_state >= (int)s_ctrl.state_count) {
        igTextDisabled("Select a state node to inspect.");
        return;
    }

    AnimState *state = &s_ctrl.states[s_selected_state];

    // ---- State properties ----
    igTextColored((ImVec4){ 0.4f, 0.8f, 1.0f, 1.0f }, "🔷 State Properties");
    igSeparator();

    igText("Name:");
    igSetNextItemWidth(-1.0f);
    igInputText("##st_name", state->name, AnimNameMaxLen, 0, nullptr, nullptr);

    igSpacing();
    igText("Clip:");
    igSetNextItemWidth(-1.0f);
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

    igSpacing();
    igText("Speed:");
    igSetNextItemWidth(-1.0f);
    igDragFloat("##st_speed", &state->speed, 0.1f, 0.01f, 10.0f, "%.2f", 0);
    if (state->speed < 0.01f) state->speed = 0.01f;

    bool is_default = ((uint32_t)s_selected_state == s_ctrl.default_state);
    if (!is_default) {
        igSpacing();
        if (igButton("Set as Default", (ImVec2){ -1, 0 })) {
            s_ctrl.default_state = (uint32_t)s_selected_state;
        }
    } else {
        igSpacing();
        igTextColored((ImVec4){ 1.0f, 0.7f, 0.2f, 1.0f }, "★ Default State");
    }

    igSpacing();
    igSpacing();

    // ---- Transitions ----
    igTextColored((ImVec4){ 0.4f, 0.8f, 1.0f, 1.0f }, "🔗 Transitions (%u/%u)",
                  state->transition_count, AnimCtrlMaxTransitions);
    igSeparator();

    if (state->transition_count < AnimCtrlMaxTransitions) {
        if (igButton("+ Add Transition", (ImVec2){ -1, 0 })) {
            AnimTransition *t = &state->transitions[state->transition_count];
            *t = (AnimTransition){};
            s_selected_trans = (int)state->transition_count;
            s_sel_trans_src = s_selected_state;
            state->transition_count++;
        }
    }

    for (uint32_t i = 0; i < state->transition_count; ++i) {
        igPushID_Int((int)(i + 5000));
        AnimTransition *tr = &state->transitions[i];

        bool tr_selected = ((int)i == s_selected_trans &&
                            s_sel_trans_src == s_selected_state);

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
            s_sel_trans_src = s_selected_state;
        }

        // Right-click to delete transition.
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

    // ---- Selected transition details ----
    if (s_selected_trans >= 0 &&
        s_selected_trans < (int)state->transition_count &&
        s_sel_trans_src == s_selected_state) {

        igSpacing();
        igTextColored((ImVec4){ 0.4f, 0.8f, 1.0f, 1.0f }, "⚙ Transition Details");
        igSeparator();

        AnimTransition *tr = &state->transitions[s_selected_trans];

        // Target state combo.
        igText("Target:");
        igSetNextItemWidth(-1.0f);
        const char *tgt_preview = (tr->target_state < s_ctrl.state_count)
            ? s_ctrl.states[tr->target_state].name : "(none)";
        if (igBeginCombo("##tr_target", tgt_preview, 0)) {
            for (uint32_t si = 0; si < s_ctrl.state_count; ++si) {
                if ((int)si == s_selected_state) continue;
                bool sel = (si == tr->target_state);
                if (igSelectable_Bool(s_ctrl.states[si].name, sel,
                                      0, (ImVec2){ 0, 0 })) {
                    tr->target_state = si;
                }
            }
            igEndCombo();
        }

        igCheckbox("Has Exit Time", &tr->has_exit_time);

        igSpacing();
        igText("Conditions (%u/%u):", tr->condition_count,
               AnimCtrlMaxConditions);

        if (tr->condition_count < AnimCtrlMaxConditions) {
            igSameLine(0.0f, 8.0f);
            if (igButton("+ Cond", (ImVec2){ 60, 0 })) {
                AnimCondition *c = &tr->conditions[tr->condition_count];
                *c = (AnimCondition){};
                tr->condition_count++;
            }
        }

        for (uint32_t ci = 0; ci < tr->condition_count; ++ci) {
            igPushID_Int((int)(ci + 6000));
            AnimCondition *cond = &tr->conditions[ci];

            // Parameter combo.
            igSetNextItemWidth(80.0f);
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
            igSetNextItemWidth(40.0f);
            int op_idx = (int)cond->op;
            if (igCombo_Str_arr("##c_op", &op_idx, s_op_names, 4, 4)) {
                cond->op = (AnimConditionOp)op_idx;
            }

            igSameLine(0.0f, 4.0f);

            // Threshold value.
            igSetNextItemWidth(55.0f);
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
            igSameLine(0.0f, 4.0f);
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

    // ---- Empty state view (Create or Load Controller) ---------------------
    if (!s_ctrl_loaded) {
        igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){ 0.11f, 0.11f, 0.14f, 1.0f });
        igBeginChild_Str("##empty_view", (ImVec2){ 0, 0 }, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
        
        float win_w = igGetWindowWidth();
        float win_h = igGetWindowHeight();
        
        igSetCursorPosY(win_h * 0.20f);
        
        igSetCursorPosX((win_w - 300.0f) * 0.5f);
        igTextColored((ImVec4){ 0.4f, 0.8f, 1.0f, 1.0f }, "🌌 ANIMATION CONTROLLER EDITOR");
        
        igSpacing();
        igSetCursorPosX((win_w - 460.0f) * 0.5f);
        igTextDisabled("Create a state-machine based animator controller or load an existing one.");
        
        igSpacing();
        igSpacing();
        
        igSetCursorPosX((win_w - 320.0f) * 0.5f);
        igText("New Controller Name:");
        igSameLine(0.0f, 8.0f);
        igSetNextItemWidth(140.0f);
        igInputText("##empty_ctrl_name", s_controller_name, sizeof(s_controller_name), 0, nullptr, nullptr);
        
        igSpacing();
        igSpacing();
        
        igSetCursorPosX((win_w - 200.0f) * 0.5f);
        if (igButton("Create Empty Controller", (ImVec2){ 200, 35 })) {
            anim_controller_init(&s_ctrl);
            s_ctrl_loaded = true;
            s_anim_loaded = false;
            s_anim_path[0] = '\0';
            s_ctrl.anim_path[0] = '\0';
            s_selected_state = -1;
            s_selected_trans = -1;
            s_canvas_scroll = (ImVec2){ 50.0f, 50.0f };
            s_canvas_zoom = 1.0f;
            console_log("[ctrl_editor] Created empty controller: %s", s_controller_name);
        }
        
        igSpacing();
        igSpacing();
        
        // Big premium drag and drop zone
        igSetCursorPosX((win_w - 400.0f) * 0.5f);
        igBeginChild_Str("##drop_zone_box", (ImVec2){ 400, 100 }, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
        igSetCursorPosY(40.0f);
        igSetCursorPosX((400.0f - 300.0f) * 0.5f);
        igTextColored((ImVec4){ 0.5f, 0.5f, 0.7f, 1.0f }, "📥 Drop .controller.meta or .anim.meta here");
        igEndChild();
        
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
                            s_canvas_scroll = (ImVec2){ 50.0f, 50.0f };
                            s_canvas_zoom = 1.0f;
                            console_log("[ctrl_editor] Loaded dropped controller: %s", dropped);
                        }
                    } else if (strstr(dropped, ".anim.meta") != nullptr || strstr(dropped, ".png") != nullptr) {
                        anim_controller_init(&s_ctrl);
                        s_ctrl_loaded = true;
                        if (strstr(dropped, ".png") != nullptr) {
                            anim_build_meta_path(dropped, s_anim_path, sizeof(s_anim_path));
                        } else {
                            strncpy(s_anim_path, dropped, sizeof(s_anim_path) - 1);
                            s_anim_path[sizeof(s_anim_path) - 1] = '\0';
                        }
                        load_source_animation();
                        s_canvas_scroll = (ImVec2){ 50.0f, 50.0f };
                        s_canvas_zoom = 1.0f;
                    }
                }
            }
            igEndDragDropTarget();
        }
        
        igEndChild();
        igPopStyleColor(1);
        igEnd();
        return;
    }

    // ---- Menu bar ---------------------------------------------------------
    if (igBeginMenuBar()) {
        if (igBeginMenu("File", true)) {
            if (igMenuItem_Bool("Save", "Ctrl+S", false, s_ctrl_loaded)) {
                char ctrl_path[AnimPathMaxLen];
                build_controller_save_path(ctrl_path, AnimPathMaxLen);
                if (anim_controller_save(&s_ctrl, ctrl_path)) {
                    s_ctrl_loaded = true;
                    console_log("[ctrl_editor] Saved: %s", ctrl_path);
                } else {
                    console_log("[ctrl_editor] Failed to save: %s", ctrl_path);
                }
            }
            if (igMenuItem_Bool("Close Controller", nullptr, false, true)) {
                s_ctrl_loaded = false;
                s_anim_loaded = false;
                s_selected_state = -1;
                s_selected_trans = -1;
                s_linking_active = false;
                s_link_source = -1;
            }
            igEndMenu();
        }
        if (igBeginMenu("View", true)) {
            if (igMenuItem_Bool("Reset View", nullptr, false, true)) {
                s_canvas_scroll = (ImVec2){ 50.0f, 50.0f };
                s_canvas_zoom = 1.0f;
            }
            if (igMenuItem_Bool("Zoom to Fit", nullptr, false,
                                s_ctrl.state_count > 0)) {
                // Compute bounding box of all nodes.
                float min_x = 1e9f, min_y = 1e9f;
                float max_x = -1e9f, max_y = -1e9f;
                for (uint32_t i = 0; i < s_ctrl.state_count; ++i) {
                    float nx = s_ctrl.states[i].editor_x;
                    float ny = s_ctrl.states[i].editor_y;
                    if (nx < min_x) min_x = nx;
                    if (ny < min_y) min_y = ny;
                    if (nx + NodeWidth > max_x) max_x = nx + NodeWidth;
                    if (ny + NodeHeight > max_y) max_y = ny + NodeHeight;
                }
                float range_x = max_x - min_x + 100.0f;
                float range_y = max_y - min_y + 100.0f;
                float avail_w = igGetContentRegionAvail().x - ParamPanelW - InspectorPanelW - 24.0f;
                float avail_h = igGetContentRegionAvail().y;
                if (avail_w < 200.0f) avail_w = 200.0f;
                if (avail_h < 200.0f) avail_h = 200.0f;
                float zoom_x = avail_w / range_x;
                float zoom_y = avail_h / range_y;
                s_canvas_zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;
                if (s_canvas_zoom < ZoomMin) s_canvas_zoom = ZoomMin;
                if (s_canvas_zoom > ZoomMax) s_canvas_zoom = ZoomMax;
                s_canvas_scroll.x = -min_x + 50.0f / s_canvas_zoom;
                s_canvas_scroll.y = -min_y + 50.0f / s_canvas_zoom;
            }
            igEndMenu();
        }
        igEndMenuBar();
    }

    // ---- Toolbar ----------------------------------------------------------
    draw_toolbar();

    igSeparator();

    // Drop target across the whole panel window for loading clips.
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            if (dropped != nullptr) {
                char meta_path[AnimPathMaxLen] = {};
                if (strstr(dropped, ".anim.meta") != nullptr) {
                    strncpy(meta_path, dropped, AnimPathMaxLen - 1);
                } else if (strstr(dropped, ".png") != nullptr) {
                    anim_build_meta_path(dropped, meta_path, AnimPathMaxLen);
                }
                if (meta_path[0] != '\0') {
                    strncpy(s_ctrl.anim_path, meta_path, AnimPathMaxLen - 1);
                    s_ctrl.anim_path[AnimPathMaxLen - 1] = '\0';
                    strncpy(s_anim_path, meta_path, AnimPathMaxLen - 1);
                    s_anim_path[AnimPathMaxLen - 1] = '\0';
                    load_source_animation();
                }
            }
        }
        igEndDragDropTarget();
    }

    // ---- Linking mode indicator ----
    if (s_linking_active) {
        igTextColored((ImVec4){ 1.0f, 0.8f, 0.2f, 1.0f },
                      "🔗 Click a target state to create transition (ESC to cancel)");
        if (igIsKeyPressed_Bool(ImGuiKey_Escape, false)) {
            s_linking_active = false;
            s_link_source = -1;
        }
    }

    // ---- Layout: params (left) | canvas (center) | inspector (right) ------
    float avail_w = igGetContentRegionAvail().x;
    float avail_h = igGetContentRegionAvail().y;
    float canvas_w = avail_w - ParamPanelW - InspectorPanelW - 16.0f;
    if (canvas_w < 200.0f) canvas_w = avail_w - ParamPanelW - 8.0f;

    // Parameters panel (left).
    igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){ 0.12f, 0.12f, 0.16f, 1.0f });
    if (igBeginChild_Str("##ctrl_params", (ImVec2){ ParamPanelW, 0 },
                          ImGuiChildFlags_Borders, 0)) {
        draw_parameters();
    }
    igEndChild();
    igPopStyleColor(1);

    igSameLine(0.0f, 4.0f);

    // Node canvas (center) — uses InvisibleButton for input capture.
    {
        ImVec2_c canvas_pos = igGetCursorScreenPos();
        ImVec2_c canvas_sz = { canvas_w, avail_h };
        if (canvas_sz.x < 100.0f) canvas_sz.x = 100.0f;
        if (canvas_sz.y < 100.0f) canvas_sz.y = 100.0f;

        // Reserve space with an invisible button.
        igInvisibleButton("##canvas", (ImVec2){ canvas_sz.x, canvas_sz.y },
                          ImGuiButtonFlags_MouseButtonLeft |
                          ImGuiButtonFlags_MouseButtonRight |
                          ImGuiButtonFlags_MouseButtonMiddle);
        bool canvas_hovered = igIsItemHovered(0);

        // Draw the canvas.
        draw_node_canvas(canvas_pos, canvas_sz, canvas_hovered);

        // Zoom text overlay.
        ImDrawList *dl = igGetWindowDrawList();
        char zoom_txt[32];
        snprintf(zoom_txt, sizeof(zoom_txt), "%.0f%%", (double)(s_canvas_zoom * 100.0f));
        ImVec2_c zoom_pos = { canvas_pos.x + canvas_sz.x - 50.0f,
                              canvas_pos.y + canvas_sz.y - 20.0f };
        ImDrawList_AddText_Vec2(dl, zoom_pos,
                                IM_COL32(120, 120, 140, 180), zoom_txt, nullptr);
    }

    igSameLine(0.0f, 4.0f);

    // Inspector panel (right).
    igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){ 0.12f, 0.12f, 0.16f, 1.0f });
    if (canvas_w >= 200.0f) {
        if (igBeginChild_Str("##ctrl_inspector", (ImVec2){ InspectorPanelW, 0 },
                              ImGuiChildFlags_Borders, 0)) {
            draw_state_inspector();
        }
        igEndChild();
    }
    igPopStyleColor(1);

    igEnd();
}

void panel_controller_editor_open(const char *ctrl_path) {
    if (ctrl_path == nullptr) return;
    AnimController tmp = {};
    anim_controller_init(&tmp);
    if (anim_controller_load(ctrl_path, &tmp)) {
        s_ctrl = tmp;
        s_ctrl_loaded = true;
        s_selected_state = (s_ctrl.state_count > 0) ? 0 : -1;
        s_selected_trans = -1;
        strncpy(s_anim_path, tmp.anim_path, sizeof(s_anim_path) - 1);
        s_anim_path[sizeof(s_anim_path) - 1] = '\0';
        load_source_animation();
        extract_controller_name_from_path(ctrl_path);
        s_canvas_scroll = (ImVec2){ 50.0f, 50.0f };
        s_canvas_zoom = 1.0f;
        console_log("[ctrl_editor] Opened controller: %s", ctrl_path);
    }
}

void panel_controller_editor_shutdown([[maybe_unused]] Renderer *renderer) {
    s_anim_loaded  = false;
    s_ctrl_loaded  = false;
    s_selected_state = -1;
    s_selected_trans = -1;
    s_linking_active = false;
    s_link_source = -1;
    s_dragging_node = -1;
}

#endif // EDITOR_BUILD
