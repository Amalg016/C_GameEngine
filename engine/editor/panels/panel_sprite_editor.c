#ifdef EDITOR_BUILD

#include "panel_sprite_editor.h"

#include "../../core/asset_manager.h"
#include "../../core/sprite_meta.h"
#include "../../renderer/renderer.h"
#include "panel_console.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Panel state (module-level statics)
// ---------------------------------------------------------------------------

/// Path input buffer.
static char s_texture_path[512] = {};

/// Loaded texture state.
static AssetHandle s_loaded_handle  = ASSET_HANDLE_INVALID;
static void       *s_imgui_tex_id  = nullptr;
static uint32_t    s_tex_width     = 0;
static uint32_t    s_tex_height    = 0;
static bool        s_texture_loaded = false;

/// Sprite meta data (current session).
static SpriteMeta s_meta = {};
static bool       s_meta_inited = false;

/// Slicing parameters.
static int s_grid_cols = 4;
static int s_grid_rows = 4;

/// Selected region index (-1 = none).
static int s_selected_region = -1;

/// Zoom level for the texture preview.
static float s_zoom = 1.0f;

/// Cached renderer/am pointers for open helper.
static Renderer     *s_cached_renderer = nullptr;
static AssetManager *s_cached_am       = nullptr;

// ---------------------------------------------------------------------------
// Internal — load a texture into the panel.
// ---------------------------------------------------------------------------

static void load_texture(AssetManager *am, Renderer *renderer) {
    // Release previous texture if any.
    if (s_imgui_tex_id != nullptr) {
        renderer_unregister_imgui_texture(renderer, s_imgui_tex_id);
        s_imgui_tex_id = nullptr;
    }
    if (s_loaded_handle != ASSET_HANDLE_INVALID) {
        asset_manager_release(am, s_loaded_handle);
        s_loaded_handle = ASSET_HANDLE_INVALID;
    }

    // Reset meta.
    if (s_meta_inited) {
        sprite_meta_destroy(&s_meta);
    }
    sprite_meta_init(&s_meta);
    s_meta_inited = true;
    s_selected_region = -1;

    // Load the texture via asset manager.
    s_loaded_handle = asset_manager_load_texture(am, s_texture_path);
    if (s_loaded_handle == ASSET_HANDLE_INVALID) {
        s_texture_loaded = false;
        console_log("[sprite_editor] Failed to load: %s", s_texture_path);
        return;
    }

    // Query dimensions.
    asset_manager_get_texture_size(am, s_loaded_handle, &s_tex_width, &s_tex_height);

    // Register with ImGui.
    void *gpu_data = asset_manager_get_data(am, s_loaded_handle);
    s_imgui_tex_id = renderer_register_imgui_texture(renderer, gpu_data);

    if (s_imgui_tex_id == nullptr) {
        console_log("[sprite_editor] Failed to register texture with ImGui");
        s_texture_loaded = false;
        return;
    }

    // Set up meta.
    strncpy(s_meta.texture_path, s_texture_path, SpritePathMaxLen - 1);
    s_meta.texture_path[SpritePathMaxLen - 1] = '\0';
    s_meta.texture_width  = s_tex_width;
    s_meta.texture_height = s_tex_height;

    // Try loading existing meta file.
    char meta_path[SpritePathMaxLen];
    sprite_meta_build_path(s_texture_path, meta_path, SpritePathMaxLen);
    if (sprite_meta_exists(s_texture_path)) {
        if (sprite_meta_load(meta_path, &s_meta)) {
            console_log("[sprite_editor] Loaded meta: %s (%u regions)",
                        meta_path, s_meta.region_count);
        } else {
            console_log("[sprite_editor] Failed to load meta: %s", meta_path);
        }
    }

    s_texture_loaded = true;
    s_zoom = 1.0f;
    console_log("[sprite_editor] Loaded: %s (%ux%u)",
                s_texture_path, s_tex_width, s_tex_height);
}

// ---------------------------------------------------------------------------
// Internal — draw the toolbar (import bar + slicing controls).
// ---------------------------------------------------------------------------

static void draw_toolbar(AssetManager *am, Renderer *renderer) {
    // ---- Import bar -------------------------------------------------------
    igText("Texture:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(300.0f);
    igInputText("##tex_path", s_texture_path, sizeof(s_texture_path), 0,
                nullptr, nullptr);

    // Drop target on the text input field.
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            strncpy(s_texture_path, dropped, sizeof(s_texture_path) - 1);
            s_texture_path[sizeof(s_texture_path) - 1] = '\0';
            load_texture(am, renderer);
        }
        igEndDragDropTarget();
    }

    igSameLine(0.0f, 4.0f);
    if (igButton("Load", (ImVec2){ 60, 0 })) {
        load_texture(am, renderer);
    }

    // ---- Drop target for asset paths from content browser -----------------
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            strncpy(s_texture_path, dropped, sizeof(s_texture_path) - 1);
            s_texture_path[sizeof(s_texture_path) - 1] = '\0';
            load_texture(am, renderer);
        }
        igEndDragDropTarget();
    }

    if (!s_texture_loaded) return;

    igSameLine(0.0f, 16.0f);
    igText("%ux%u", s_tex_width, s_tex_height);

    igSeparator();

    // ---- Slicing controls -------------------------------------------------
    igText("Grid:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(80.0f);
    igInputInt("Cols", &s_grid_cols, 1, 1, 0);
    igSameLine(0.0f, 8.0f);
    igSetNextItemWidth(80.0f);
    igInputInt("Rows", &s_grid_rows, 1, 1, 0);

    if (s_grid_cols < 1) s_grid_cols = 1;
    if (s_grid_rows < 1) s_grid_rows = 1;
    if (s_grid_cols > 64) s_grid_cols = 64;
    if (s_grid_rows > 64) s_grid_rows = 64;

    igSameLine(0.0f, 8.0f);
    if (igButton("Slice", (ImVec2){ 60, 0 })) {
        sprite_meta_slice_grid(&s_meta, (uint32_t)s_grid_cols,
                               (uint32_t)s_grid_rows);
        s_selected_region = -1;
        console_log("[sprite_editor] Sliced into %u regions",
                    s_meta.region_count);
    }

    igSameLine(0.0f, 8.0f);
    if (igButton("Save Meta", (ImVec2){ 90, 0 })) {
        char meta_path[SpritePathMaxLen];
        sprite_meta_build_path(s_texture_path, meta_path, SpritePathMaxLen);
        if (sprite_meta_save(&s_meta, meta_path)) {
            console_log("[sprite_editor] Saved: %s", meta_path);
        } else {
            console_log("[sprite_editor] Failed to save: %s", meta_path);
        }
    }

    igSameLine(0.0f, 16.0f);
    igSetNextItemWidth(100.0f);
    igSliderFloat("Zoom", &s_zoom, 0.25f, 8.0f, "%.2fx", 0);
}

// ---------------------------------------------------------------------------
// Internal — draw the texture preview with grid overlay.
// ---------------------------------------------------------------------------

static void draw_preview(void) {
    if (!s_texture_loaded || s_imgui_tex_id == nullptr) {
        igTextDisabled("No texture loaded. Enter a path above or drag a file"
                       " from the Content Browser.");
        return;
    }

    // Compute display size.
    float disp_w = (float)s_tex_width  * s_zoom;
    float disp_h = (float)s_tex_height * s_zoom;

    // Scrollable child region for the preview.
    ImVec2 child_size = { 0, 0 }; // fill available space
    if (igBeginChild_Str("##preview", child_size,
                         ImGuiChildFlags_Borders,
                         ImGuiWindowFlags_HorizontalScrollbar)) {

        ImVec2 cursor = igGetCursorScreenPos();
        ImDrawList *draw_list = igGetWindowDrawList();

        // Draw the texture.
        ImTextureRef_c tex_ref = {
            ._TexData = nullptr,
            ._TexID   = (ImTextureID)s_imgui_tex_id,
        };
        igImage(tex_ref, (ImVec2){ disp_w, disp_h },
                (ImVec2){ 0, 0 }, (ImVec2){ 1, 1 });

        // ---- Grid overlay -------------------------------------------------
        float cell_w = disp_w / (float)s_grid_cols;
        float cell_h = disp_h / (float)s_grid_rows;

        ImU32 grid_color     = igGetColorU32_Vec4(
            (ImVec4){ 0.2f, 0.8f, 0.2f, 0.5f });
        ImU32 selected_color = igGetColorU32_Vec4(
            (ImVec4){ 1.0f, 0.85f, 0.0f, 0.7f });
        ImU32 hover_color    = igGetColorU32_Vec4(
            (ImVec4){ 0.4f, 0.6f, 1.0f, 0.4f });

        // Draw vertical grid lines.
        for (int c = 0; c <= s_grid_cols; ++c) {
            float x = cursor.x + (float)c * cell_w;
            ImDrawList_AddLine(draw_list,
                               (ImVec2){ x, cursor.y },
                               (ImVec2){ x, cursor.y + disp_h },
                               grid_color, 1.0f);
        }

        // Draw horizontal grid lines.
        for (int r = 0; r <= s_grid_rows; ++r) {
            float y = cursor.y + (float)r * cell_h;
            ImDrawList_AddLine(draw_list,
                               (ImVec2){ cursor.x, y },
                               (ImVec2){ cursor.x + disp_w, y },
                               grid_color, 1.0f);
        }

        // ---- Region highlights --------------------------------------------
        // If we have meta regions, draw them.
        for (uint32_t i = 0; i < s_meta.region_count; ++i) {
            const SpriteRegion *reg = &s_meta.regions[i];

            // Convert pixel rect to screen coordinates.
            float rx = cursor.x + reg->rect.x * s_zoom;
            float ry = cursor.y + reg->rect.y * s_zoom;
            float rw = reg->rect.w * s_zoom;
            float rh = reg->rect.h * s_zoom;

            ImVec2 p_min = { rx, ry };
            ImVec2 p_max = { rx + rw, ry + rh };

            if ((int)i == s_selected_region) {
                // Filled highlight for selected region.
                ImDrawList_AddRectFilled(draw_list, p_min, p_max,
                                         selected_color, 0.0f, 0);
                ImDrawList_AddRect(draw_list, p_min, p_max,
                                   igGetColorU32_Vec4(
                                       (ImVec4){ 1.0f, 0.85f, 0.0f, 1.0f }),
                                   0.0f, 0, 2.0f);
            }
        }

        // ---- Click detection — select cell --------------------------------
        if (igIsItemHovered(0) && igIsMouseClicked_Bool(0, false)) {
            ImVec2_c mouse_c = igGetMousePos();
            float local_x = mouse_c.x - cursor.x;
            float local_y = mouse_c.y - cursor.y;

            if (local_x >= 0.0f && local_x < disp_w &&
                local_y >= 0.0f && local_y < disp_h) {
                // Find which pixel coordinate this maps to.
                float px = local_x / s_zoom;
                float py = local_y / s_zoom;

                // Search meta regions for a hit.
                for (uint32_t i = 0; i < s_meta.region_count; ++i) {
                    const SpriteRegion *reg = &s_meta.regions[i];
                    if (px >= reg->rect.x && px < reg->rect.x + reg->rect.w &&
                        py >= reg->rect.y && py < reg->rect.y + reg->rect.h) {
                        s_selected_region = (int)i;
                        break;
                    }
                }
            }
        }

        // ---- Hover highlight ----------------------------------------------
        if (igIsItemHovered(0)) {
            ImVec2_c mouse_c = igGetMousePos();
            float local_x = mouse_c.x - cursor.x;
            float local_y = mouse_c.y - cursor.y;

            if (local_x >= 0.0f && local_x < disp_w &&
                local_y >= 0.0f && local_y < disp_h) {
                for (uint32_t i = 0; i < s_meta.region_count; ++i) {
                    const SpriteRegion *reg = &s_meta.regions[i];
                    float rx = reg->rect.x * s_zoom;
                    float ry = reg->rect.y * s_zoom;
                    float rw = reg->rect.w * s_zoom;
                    float rh = reg->rect.h * s_zoom;

                    if (local_x >= rx && local_x < rx + rw &&
                        local_y >= ry && local_y < ry + rh &&
                        (int)i != s_selected_region) {
                        ImVec2 hp_min = { cursor.x + rx, cursor.y + ry };
                        ImVec2 hp_max = { cursor.x + rx + rw,
                                          cursor.y + ry + rh };
                        ImDrawList_AddRectFilled(draw_list, hp_min, hp_max,
                                                 hover_color, 0.0f, 0);
                        break;
                    }
                }
            }
        }
    }
    igEndChild();
}

// ---------------------------------------------------------------------------
// Internal — draw the region list sidebar.
// ---------------------------------------------------------------------------

static void draw_region_list(void) {
    if (!s_texture_loaded || s_meta.region_count == 0) {
        igTextDisabled("No sprite regions defined.\n"
                       "Set grid cols/rows and click Slice.");
        return;
    }

    igText("Sprite Regions (%u):", s_meta.region_count);
    igSeparator();

    for (uint32_t i = 0; i < s_meta.region_count; ++i) {
        SpriteRegion *reg = &s_meta.regions[i];

        igPushID_Int((int)i);

        // Selectable row.
        bool is_selected = ((int)i == s_selected_region);
        if (igSelectable_Bool(reg->name, is_selected, 0,
                              (ImVec2){ 0, 0 })) {
            s_selected_region = (int)i;
        }

        // Show details inline when selected.
        if (is_selected) {
            igIndent(16.0f);

            // Editable name.
            char name_label[80];
            snprintf(name_label, sizeof(name_label), "Name##%u", i);
            igSetNextItemWidth(150.0f);
            igInputText(name_label, reg->name, SpriteNameMaxLen, 0,
                        nullptr, nullptr);

            // Rect display (read-only).
            igText("  x:%.0f  y:%.0f  w:%.0f  h:%.0f",
                   reg->rect.x, reg->rect.y, reg->rect.w, reg->rect.h);

            igUnindent(16.0f);
        }

        igPopID();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void panel_sprite_editor_render(bool *p_open,
                                AssetManager *am,
                                Renderer *renderer) {
    s_cached_am       = am;
    s_cached_renderer = renderer;

    if (!igBegin("Sprite Editor", p_open, ImGuiWindowFlags_MenuBar)) {
        igEnd();
        return;
    }

    // ---- Menu bar ---------------------------------------------------------
    if (igBeginMenuBar()) {
        if (igBeginMenu("File", true)) {
            if (igMenuItem_Bool("Save Meta", "Ctrl+S", false,
                                s_texture_loaded)) {
                char meta_path[SpritePathMaxLen];
                sprite_meta_build_path(s_texture_path, meta_path,
                                       SpritePathMaxLen);
                if (sprite_meta_save(&s_meta, meta_path)) {
                    console_log("[sprite_editor] Saved: %s", meta_path);
                }
            }
            igEndMenu();
        }
        igEndMenuBar();
    }

    // ---- Toolbar ----------------------------------------------------------
    draw_toolbar(am, renderer);

    igSeparator();

    // ---- Layout: preview (left) + region list (right) ---------------------
    float avail_w = igGetContentRegionAvail().x;
    float sidebar_w = 220.0f;
    float preview_w = avail_w - sidebar_w - 8.0f;
    if (preview_w < 200.0f) preview_w = avail_w; // collapse sidebar if too narrow

    // Preview area.
    if (igBeginChild_Str("##preview_area", (ImVec2){ preview_w, 0 },
                         0, 0)) {
        draw_preview();
    }
    igEndChild();

    // Sidebar — only show if there's room.
    if (preview_w < avail_w) {
        igSameLine(0.0f, 8.0f);
        if (igBeginChild_Str("##region_list", (ImVec2){ sidebar_w, 0 },
                             ImGuiChildFlags_Borders, 0)) {
            draw_region_list();
        }
        igEndChild();
    }

    igEnd();
}

void panel_sprite_editor_open(const char *texture_path,
                              AssetManager *am,
                              Renderer *renderer) {
    if (texture_path == nullptr) return;

    strncpy(s_texture_path, texture_path, sizeof(s_texture_path) - 1);
    s_texture_path[sizeof(s_texture_path) - 1] = '\0';

    load_texture(am, renderer);
}

void panel_sprite_editor_shutdown(Renderer *renderer) {
    if (s_imgui_tex_id != nullptr && renderer != nullptr) {
        renderer_unregister_imgui_texture(renderer, s_imgui_tex_id);
        s_imgui_tex_id = nullptr;
    }
    if (s_meta_inited) {
        sprite_meta_destroy(&s_meta);
        s_meta_inited = false;
    }
    s_loaded_handle  = ASSET_HANDLE_INVALID;
    s_texture_loaded = false;
}

#endif // EDITOR_BUILD
