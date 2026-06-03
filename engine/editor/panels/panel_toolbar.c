#ifdef EDITOR_BUILD

#include "panel_toolbar.h"
#include "../../core/engine.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Style helpers
// ---------------------------------------------------------------------------

/// Push button colours (button, hovered, active) for a tinted button.
static void push_button_color(ImVec4 base, ImVec4 hovered, ImVec4 active) {
    igPushStyleColor_Vec4(ImGuiCol_Button,        base);
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  hovered);
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   active);
}

/// Push a greyed-out disabled style (button + text).
static void push_disabled_style(void) {
    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.3f, 0.3f, 0.3f, 0.5f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.3f, 0.3f, 0.5f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.3f, 0.3f, 0.3f, 0.5f});
    igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.5f, 0.5f, 0.5f, 1.0f});
}

/// Pop disabled style (4 colours).
static void pop_disabled_style(void) {
    igPopStyleColor(4);
}

// ---------------------------------------------------------------------------
// panel_toolbar_render
// ---------------------------------------------------------------------------

void panel_toolbar_render(Engine *engine) {
    if (engine == nullptr) return;

    PlayState state = engine_get_play_state(engine);

    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,  (ImVec2){0, 4});
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing,    (ImVec2){4, 0});

    igBegin("##Toolbar", nullptr,
            ImGuiWindowFlags_NoDecoration   |
            ImGuiWindowFlags_NoScrollbar    |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoTitleBar);

    igPopStyleVar(3);

    // Button dimensions.
    constexpr float BtnSize = 36.0f;
    constexpr float BtnGap  = 8.0f;

    // Centre the three buttons horizontally.
    float total_w = BtnSize * 3.0f + BtnGap * 2.0f;
    float avail   = igGetContentRegionAvail().x;
    float offset  = (avail - total_w) * 0.5f;
    if (offset > 0.0f) {
        igSetCursorPosX(igGetCursorPosX() + offset);
    }

    // Vertically centre buttons in the toolbar height.
    float toolbar_h = igGetContentRegionAvail().y;
    float btn_y = (toolbar_h - BtnSize) * 0.5f;
    if (btn_y > 0.0f) {
        igSetCursorPosY(igGetCursorPosY() + btn_y);
    }

    // ---- Play button (\u25b6) ------------------------------------------------
    {
        bool enabled = (state != PLAY_STATE_PLAYING);
        if (enabled) {
            push_button_color(
                (ImVec4){0.2f, 0.7f, 0.2f, 1.0f},
                (ImVec4){0.3f, 0.8f, 0.3f, 1.0f},
                (ImVec4){0.15f, 0.6f, 0.15f, 1.0f});
        } else {
            push_disabled_style();
        }

        if (igButton("\xe2\x96\xb6", (ImVec2){BtnSize, BtnSize}) && enabled) {
            engine_set_play_state(engine, PLAY_STATE_PLAYING);
            state = PLAY_STATE_PLAYING;
        }
        if (igIsItemHovered(0)) {
            igSetTooltip(enabled ? "Play (Ctrl+R)" : "Already playing");
        }

        if (enabled) {
            igPopStyleColor(3);
        } else {
            pop_disabled_style();
        }
    }

    igSameLine(0, BtnGap);

    // ---- Pause button (\u23f8) -----------------------------------------------
    {
        bool can_pause = (state == PLAY_STATE_PLAYING || state == PLAY_STATE_PAUSED);
        if (can_pause) {
            bool is_paused = (state == PLAY_STATE_PAUSED);
            ImVec4 base = is_paused
                ? (ImVec4){0.9f, 0.8f, 0.2f, 1.0f}   // brighter when active
                : (ImVec4){0.8f, 0.7f, 0.1f, 1.0f};
            push_button_color(
                base,
                (ImVec4){0.9f, 0.8f, 0.2f, 1.0f},
                (ImVec4){0.7f, 0.6f, 0.05f, 1.0f});
        } else {
            push_disabled_style();
        }

        if (igButton("\xe2\x8f\xb8", (ImVec2){BtnSize, BtnSize}) && can_pause) {
            if (state == PLAY_STATE_PLAYING) {
                engine_set_play_state(engine, PLAY_STATE_PAUSED);
                state = PLAY_STATE_PAUSED;
            } else {
                engine_set_play_state(engine, PLAY_STATE_PLAYING);
                state = PLAY_STATE_PLAYING;
            }
        }
        if (igIsItemHovered(0)) {
            igSetTooltip(can_pause ? "Pause / Resume" : "Not playing");
        }

        if (can_pause) {
            igPopStyleColor(3);
        } else {
            pop_disabled_style();
        }
    }

    igSameLine(0, BtnGap);

    // ---- Stop button (\u23f9) ------------------------------------------------
    {
        bool can_stop = (state != PLAY_STATE_EDITING);
        if (can_stop) {
            push_button_color(
                (ImVec4){0.7f, 0.2f, 0.2f, 1.0f},
                (ImVec4){0.8f, 0.3f, 0.3f, 1.0f},
                (ImVec4){0.6f, 0.15f, 0.15f, 1.0f});
        } else {
            push_disabled_style();
        }

        if (igButton("\xe2\x8f\xb9", (ImVec2){BtnSize, BtnSize}) && can_stop) {
            engine_set_play_state(engine, PLAY_STATE_EDITING);
            state = PLAY_STATE_EDITING;
        }
        if (igIsItemHovered(0)) {
            igSetTooltip(can_stop ? "Stop (Ctrl+R)" : "Not playing");
        }

        if (can_stop) {
            igPopStyleColor(3);
        } else {
            pop_disabled_style();
        }
    }

    // ---- Status label (right side) ----------------------------------------
    {
        const char *label = nullptr;
        ImVec4 color = {};

        switch (state) {
        case PLAY_STATE_PLAYING:
            label = "\xe2\x96\xb6 Playing";
            color = (ImVec4){0.3f, 0.85f, 0.4f, 1.0f};
            break;
        case PLAY_STATE_PAUSED:
            label = "\xe2\x8f\xb8 Paused";
            color = (ImVec4){0.9f, 0.8f, 0.2f, 1.0f};
            break;
        case PLAY_STATE_EDITING:
        default:
            label = "Edit Mode";
            color = (ImVec4){0.5f, 0.5f, 0.5f, 0.6f};
            break;
        }

        igSameLine(0, 24.0f);
        igPushStyleColor_Vec4(ImGuiCol_Text, color);
        igTextUnformatted(label, nullptr);
        igPopStyleColor(1);
    }

    // ---- Ctrl+R shortcut --------------------------------------------------
    {
        ImGuiIO *io = igGetIO_Nil();
        if (!io->WantTextInput && io->KeyCtrl &&
            igIsKeyPressed_Bool(ImGuiKey_R, false)) {
            if (state == PLAY_STATE_EDITING) {
                engine_set_play_state(engine, PLAY_STATE_PLAYING);
            } else {
                engine_set_play_state(engine, PLAY_STATE_EDITING);
            }
        }
    }

    igEnd();
}

#endif // EDITOR_BUILD
