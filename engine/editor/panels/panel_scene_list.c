#ifdef EDITOR_BUILD

#include "panel_scene_list.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "../../core/engine.h"
#include "../../core/scene_manager.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// panel_scene_list_render
// ---------------------------------------------------------------------------
void panel_scene_list_render(bool *p_open, Engine *engine) {
    if (!igBegin("Scene List Manager", p_open, 0)) {
        igEnd();
        return;
    }

    if (engine == nullptr) {
        igTextDisabled("No engine instance.");
        igEnd();
        return;
    }

    SceneManager *sm = engine_get_scene_manager(engine);
    if (sm == nullptr) {
        igTextDisabled("Scene Manager not initialised.");
        igEnd();
        return;
    }

    uint32_t count = scene_manager_get_count(sm);
    int32_t current_idx = scene_manager_get_current_index(sm);

    igText("Registered Scenes: %u", count);
    igSpacing();

    // Table view of scenes
    if (igBeginTable("SceneListTable", 4, 
                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, 
                     (ImVec2){0, 0}, 0.0f)) {
        
        igTableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 40.0f, 0);
        igTableSetupColumn("Scene Path", ImGuiTableColumnFlags_None, 0.0f, 0);
        igTableSetupColumn("Reorder", ImGuiTableColumnFlags_WidthFixed, 60.0f, 0);
        igTableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 30.0f, 0);
        igTableHeadersRow();

        bool modified = false;

        for (uint32_t i = 0; i < count; ++i) {
            igTableNextRow(0, 0.0f);

            // Column 1: Order / Active Indicator
            igTableSetColumnIndex(0);
            char idx_str[16];
            if ((int32_t)i == current_idx) {
                snprintf(idx_str, sizeof(idx_str), "* %u", i + 1);
            } else {
                snprintf(idx_str, sizeof(idx_str), "  %u", i + 1);
            }
            igText("%s", idx_str);

            // Column 2: Scene Path (Click to Switch)
            igTableSetColumnIndex(1);
            const char *path = scene_manager_get_scene_path(sm, i);
            if (path != nullptr) {
                bool is_active = ((int32_t)i == current_idx);
                if (is_active) {
                    igTextColored((ImVec4){0.3f, 0.8f, 0.3f, 1.0f}, "%s (Active)", path);
                } else {
                    if (igSelectable_Bool(path, false, 0, (ImVec2){0, 0})) {
                        engine_switch_scene(engine, path);
                    }
                    if (igIsItemHovered(0)) {
                        igSetTooltip("Click to switch to this scene");
                    }
                }
            }

            // Column 3: Reorder buttons
            igTableSetColumnIndex(2);
            igPushID_Int(i);
            
            bool can_up = (i > 0);
            if (!can_up) {
                igBeginDisabled(true);
            }
            if (igButton("^", (ImVec2){24, 18})) {
                scene_manager_swap_scenes(sm, i, i - 1);
                modified = true;
            }
            if (!can_up) {
                igEndDisabled();
            }
            
            igSameLine(0, 4);

            bool can_down = (i < count - 1);
            if (!can_down) {
                igBeginDisabled(true);
            }
            if (igButton("v", (ImVec2){24, 18})) {
                scene_manager_swap_scenes(sm, i, i + 1);
                modified = true;
            }
            if (!can_down) {
                igEndDisabled();
            }
            igPopID();

            // Column 4: Delete button
            igTableSetColumnIndex(3);
            igPushID_Int(i + 10000);
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.6f, 0.15f, 0.15f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.8f, 0.25f, 0.25f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.5f, 0.1f, 0.1f, 1.0f});
            
            if (igButton("X", (ImVec2){24, 18})) {
                scene_manager_remove_scene(sm, i);
                modified = true;
            }
            
            igPopStyleColor(3);
            igPopID();
        }

        igEndTable();
        
        if (modified) {
            engine_save_scene_manifest(engine, "assets/scenes/scene_manifest.json");
        }
    }

    igSeparator();
    igSpacing();
    igText("Add Scene to Manifest");

    // Add Current Scene button
    const char *current_path = engine_get_current_scene(engine);
    bool has_current = (current_path != nullptr);
    if (!has_current) {
        igBeginDisabled(true);
    }
    if (igButton("Add Current Scene", (ImVec2){0, 0}) && has_current) {
        int32_t idx = scene_manager_find_scene(sm, current_path);
        if (idx == -1) {
            scene_manager_add_scene(sm, current_path);
            engine_save_scene_manifest(engine, "assets/scenes/scene_manifest.json");
        }
    }
    if (!has_current) {
        igEndDisabled();
    }

    igSameLine(0, 10);

    // Manual input scene path
    static char add_buf[256] = {};
    igSetNextItemWidth(250.0f);
    igInputText("##add_scene_path", add_buf, sizeof(add_buf), 0, nullptr, nullptr);
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr && payload->Data != nullptr) {
            const char *path = (const char *)payload->Data;
            if (strstr(path, ".json") != nullptr) {
                strncpy(add_buf, path, sizeof(add_buf) - 1);
                add_buf[sizeof(add_buf) - 1] = '\0';
            }
        }
        igEndDragDropTarget();
    }
    igSameLine(0, 4);
    if (igButton("Add Path", (ImVec2){0, 0})) {
        if (strlen(add_buf) > 0) {
            int32_t idx = scene_manager_find_scene(sm, add_buf);
            if (idx == -1) {
                scene_manager_add_scene(sm, add_buf);
                engine_save_scene_manifest(engine, "assets/scenes/scene_manifest.json");
                add_buf[0] = '\0';
            }
        }
    }

    igEnd();
}

#endif // EDITOR_BUILD
