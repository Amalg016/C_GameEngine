#ifdef EDITOR_BUILD

#include "panel_scene_view.h"
#include "../../renderer/vulkan/vulkan_renderer.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <string.h>
#include "../../core/engine.h"

/// Returns true if `name` ends with `suffix`.
static bool has_suffix(const char *name, const char *suffix) {
    size_t name_len   = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) return false;
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static bool s_focused = false;
static bool s_hovered = false;
static float s_min_x = 0.0f;
static float s_min_y = 0.0f;
static float s_max_x = 0.0f;
static float s_max_y = 0.0f;

bool panel_scene_view_is_focused(void) {
    return s_focused;
}

bool panel_scene_view_is_hovered(void) {
    return s_hovered;
}

void panel_scene_view_get_content_bounds(float *min_x, float *min_y, float *max_x, float *max_y) {
    if (min_x) *min_x = s_min_x;
    if (min_y) *min_y = s_min_y;
    if (max_x) *max_x = s_max_x;
    if (max_y) *max_y = s_max_y;
}

// ---------------------------------------------------------------------------
// panel_scene_view_render
// ---------------------------------------------------------------------------

bool panel_scene_view_render(bool *p_open, Engine *engine, Renderer *renderer, uint32_t fb_w, uint32_t fb_h) {
    bool scene_switched = false;
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 0, 0 });

    if (!igBegin("Scene View", p_open, ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse)) {
        s_focused = false;
        s_hovered = false;
        igEnd();
        igPopStyleVar(1);
        return false;
    }

    igPopStyleVar(1);

    s_focused = igIsWindowFocused(0);
    s_hovered = igIsWindowHovered(0);

    // Get the available content region for the scene viewport.
    ImVec2 viewport_size = igGetContentRegionAvail();

    ImVec2 cursor_pos = igGetCursorScreenPos();
    s_min_x = cursor_pos.x;
    s_min_y = cursor_pos.y;
    s_max_x = cursor_pos.x + viewport_size.x;
    s_max_y = cursor_pos.y + viewport_size.y;
    ImDrawList *draw_list = igGetWindowDrawList();

    // Render the actual scene viewport texture from Vulkan offscreen pass.
    void *tex_id = vulkan_renderer_get_editor_viewport_texture(renderer);

    if (tex_id != nullptr) {
        ImTextureRef_c tex_ref = { ._TexData = nullptr, ._TexID = (ImTextureID)tex_id };
        igImage(tex_ref, viewport_size, (ImVec2){0, 0}, (ImVec2){1, 1});
    } else {
        // Fallback: draw placeholder background rect.
        ImVec2 p_max = {
            cursor_pos.x + viewport_size.x,
            cursor_pos.y + viewport_size.y,
        };
        ImDrawList_AddRectFilled(draw_list, cursor_pos, p_max,
                                 igGetColorU32_Vec4((ImVec4){0.02f, 0.02f, 0.04f, 1.0f}),
                                 0.0f, 0);

        // Centered info text.
        char info[128];
        snprintf(info, sizeof(info), "Scene Viewport  %ux%u",
                 (unsigned)fb_w, (unsigned)fb_h);

        ImVec2 text_size = igCalcTextSize(info, nullptr, false, -1.0f);

        ImVec2 text_pos = {
            cursor_pos.x + (viewport_size.x - text_size.x) * 0.5f,
            cursor_pos.y + (viewport_size.y - text_size.y) * 0.5f,
        };
        ImDrawList_AddText_Vec2(draw_list, text_pos,
                                igGetColorU32_Vec4((ImVec4){0.5f, 0.5f, 0.5f, 1.0f}),
                                info, nullptr);
    }

    // Display viewport dimensions in the top-left corner (green).
    char size_text[64];
    snprintf(size_text, sizeof(size_text), "%.0f x %.0f",
             viewport_size.x, viewport_size.y);

    ImVec2 corner = {
        cursor_pos.x + 8.0f,
        cursor_pos.y + 8.0f,
    };
    ImDrawList_AddText_Vec2(draw_list, corner,
                            igGetColorU32_Vec4((ImVec4){0.3f, 0.8f, 0.4f, 1.0f}),
                            size_text, nullptr);

    // "Scene" badge in the top-right corner.
    {
        constexpr float pad_x = 6.0f;
        constexpr float pad_y = 3.0f;
        constexpr float margin = 8.0f;

        const char *label = "Scene";
        ImVec2 label_size = igCalcTextSize(label, nullptr, false, -1.0f);

        ImVec2 bg_max = {
            cursor_pos.x + viewport_size.x - margin,
            cursor_pos.y + margin + label_size.y + pad_y * 2.0f,
        };
        ImVec2 bg_min = {
            bg_max.x - label_size.x - pad_x * 2.0f,
            cursor_pos.y + margin,
        };

        ImDrawList_AddRectFilled(draw_list, bg_min, bg_max,
                                 igGetColorU32_Vec4((ImVec4){0.1f, 0.4f, 0.7f, 0.7f}),
                                 4.0f, 0);

        ImVec2 label_pos = {
            bg_min.x + pad_x,
            bg_min.y + pad_y,
        };
        ImDrawList_AddText_Vec2(draw_list, label_pos,
                                igGetColorU32_Vec4((ImVec4){1.0f, 1.0f, 1.0f, 1.0f}),
                                label, nullptr);
    }

    // ---- Drop target: accept scene (.json) payload ------------------------
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *ap = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (ap != nullptr) {
            const char *path = (const char *)ap->Data;
            if (path != nullptr && has_suffix(path, ".json")) {
                if (engine_switch_scene(engine, path)) {
                    printf("[editor] Loaded dropped scene: %s\n", path);
                    scene_switched = true;
                } else {
                    fprintf(stderr, "[editor] Failed to load dropped scene: %s\n", path);
                }
            }
        }
        igEndDragDropTarget();
    }

    igEnd();
    return scene_switched;
}

#endif // EDITOR_BUILD
