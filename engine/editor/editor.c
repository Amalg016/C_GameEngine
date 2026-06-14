#ifdef EDITOR_BUILD

#include "editor.h"

#include "../core/engine.h"
#include "../core/asset_manager.h"
#include "../core/scripting/lua_host.h"
#include "../platform/platform.h"
#include "../renderer/renderer.h"
#include "../renderer/vulkan/vulkan_renderer.h"
#include "../core/ecs/ecs.h"
#include "../core/input.h"
#include "../core/scene_manager.h"
#include <string.h>

#include "ui/imgui_layer.h"
#include "ui/imgui_vulkan_bridge.h"
#include "panels/panel_hierarchy.h"
#include "panels/panel_inspector.h"
#include "panels/panel_content_browser.h"
#include "panels/panel_console.h"
#include "panels/panel_game_view.h"
#include "panels/panel_scene_view.h"
#include "panels/panel_sprite_editor.h"
#include "panels/panel_animation_editor.h"
#include "panels/panel_controller_editor.h"
#include "panels/panel_scene_list.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

// ---------------------------------------------------------------------------
// Editor internals
// ---------------------------------------------------------------------------

struct Editor {
    Engine *engine;

    // Panel visibility.
    bool show_hierarchy;
    bool show_inspector;
    bool show_content_browser;
    bool show_console;
    bool show_game_view;
    bool show_scene_view;
    bool show_sprite_editor;
    bool show_animation_editor;
    bool show_controller_editor;
    bool show_demo_window;
    bool show_scene_list;

    // Shared selection state (hierarchy ↔ inspector).
    uint32_t selected_entity;
    bool     has_selection;

    // Persisted scene tracking
    char    *last_scene;
};

// ---------------------------------------------------------------------------
// editor_create
// ---------------------------------------------------------------------------

Editor *editor_create(Engine *engine) {
    if (engine == nullptr) return nullptr;

    Renderer *renderer = engine_get_renderer(engine);
    Platform *platform = engine_get_platform(engine);
    if (renderer == nullptr || platform == nullptr) return nullptr;

    // Assemble Vulkan init info from renderer accessors.
    ImGuiBridgeInitInfo bridge_info = {
        .instance        = vulkan_renderer_get_instance(renderer),
        .physical_device = vulkan_renderer_get_physical_device(renderer),
        .device          = vulkan_renderer_get_device(renderer),
        .queue_family    = vulkan_renderer_get_graphics_family(renderer),
        .queue           = vulkan_renderer_get_graphics_queue(renderer),
        .render_pass     = vulkan_renderer_get_render_pass(renderer),
        .image_count     = vulkan_renderer_get_image_count(renderer),
    };

    void *glfw_window = platform_get_window_handle(platform);

    // Initialise the ImGui layer (creates context + backends).
    if (!imgui_layer_init(glfw_window, &bridge_info)) {
        fprintf(stderr, "[editor] failed to initialise ImGui layer\n");
        return nullptr;
    }

    // Allocate editor state.
    Editor *editor = calloc(1, sizeof(Editor));
    if (editor == nullptr) {
        imgui_layer_shutdown();
        return nullptr;
    }

    *editor = (Editor){
        .engine               = engine,
        .show_hierarchy        = true,
        .show_inspector        = true,
        .show_content_browser  = true,
        .show_console          = true,
        .show_game_view        = false,  // hidden until Play
        .show_scene_view       = true,   // always visible by default
        .show_sprite_editor    = false,
        .show_animation_editor = false,
        .show_controller_editor = false,
        .show_demo_window      = false,
        .show_scene_list       = true,
        .selected_entity       = 0,
        .has_selection         = false,
        .last_scene            = nullptr,
    };

    // Load last scene from meta file (.editor_meta.json)
    FILE *mf = fopen(".editor_meta.json", "rb");
    if (mf != nullptr) {
        fseek(mf, 0, SEEK_END);
        long msize = ftell(mf);
        fseek(mf, 0, SEEK_SET);
        if (msize > 0) {
            char *mbuf = malloc((size_t)msize + 1);
            if (mbuf != nullptr) {
                size_t mread = fread(mbuf, 1, (size_t)msize, mf);
                mbuf[mread] = '\0';
                cJSON *mroot = cJSON_Parse(mbuf);
                if (mroot != nullptr) {
                    cJSON *last_scene_item = cJSON_GetObjectItemCaseSensitive(mroot, "last_scene");
                    if (cJSON_IsString(last_scene_item) && last_scene_item->valuestring != nullptr) {
                        editor->last_scene = strdup(last_scene_item->valuestring);
                    }
                    cJSON_Delete(mroot);
                }
                free(mbuf);
            }
        }
        fclose(mf);
    }

    console_log("[editor] Editor created successfully");
    printf("[editor] created\n");
    return editor;
}

// ---------------------------------------------------------------------------
// editor_destroy
// ---------------------------------------------------------------------------

void editor_destroy(Editor *editor) {
    if (editor == nullptr) return;

    // Wait for GPU idle before shutting down ImGui.
    Renderer *r = engine_get_renderer(editor->engine);
    if (r != nullptr) {
        VkDevice device = vulkan_renderer_get_device(r);
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
    }

    // Shut down sprite editor resources.
    panel_sprite_editor_shutdown(r);
    panel_animation_editor_shutdown(r);
    panel_controller_editor_shutdown(r);

    imgui_layer_shutdown();
    free(editor->last_scene);
    free(editor);

    printf("[editor] destroyed\n");
}

// ---------------------------------------------------------------------------
// Dockspace + menu bar
// ---------------------------------------------------------------------------

static void editor_render_dockspace(Editor *editor) {
    // Cover the entire main viewport with a dockspace window.
    const ImGuiViewport *viewport = igGetMainViewport();

    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){ 0, 0 });
    igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    igSetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar       |
        ImGuiWindowFlags_NoDocking     |
        ImGuiWindowFlags_NoTitleBar    |
        ImGuiWindowFlags_NoCollapse    |
        ImGuiWindowFlags_NoResize      |
        ImGuiWindowFlags_NoMove        |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 0, 0 });

    igBegin("##EditorDockSpace", nullptr, window_flags);
    igPopStyleVar(3);

    // Create the dockspace.
    ImGuiID dockspace_id = igGetID_Str("EngineDockSpace");
    igDockSpace(dockspace_id, (ImVec2){ 0, 0 },
                ImGuiDockNodeFlags_None, nullptr);

    // ---- Menu bar ---------------------------------------------------------
    if (igBeginMenuBar()) {
        if (igBeginMenu("File", true)) {
            if (igMenuItem_Bool("Save Scene", "Ctrl+S", false, true)) {
                const char *scene = engine_get_current_scene(editor->engine);
                if (scene != nullptr) {
                    engine_save_scene(editor->engine, scene);
                    console_log("[editor] Scene saved: %s", scene);
                }
            }
            igSeparator();
            if (igMenuItem_Bool("Exit", "Alt+F4", false, true)) {
                // The main loop checks platform_should_close, which GLFW
                // handles via the window close button.  For a menu exit,
                // we'd need to set the close flag — omitted here for safety.
                console_log("[editor] Exit requested");
            }
            igEndMenu();
        }

        if (igBeginMenu("View", true)) {
            igMenuItem_BoolPtr("Hierarchy",      nullptr,
                               &editor->show_hierarchy, true);
            igMenuItem_BoolPtr("Inspector",      nullptr,
                               &editor->show_inspector, true);
            igMenuItem_BoolPtr("Content Browser", nullptr,
                               &editor->show_content_browser, true);
            igMenuItem_BoolPtr("Console",        nullptr,
                               &editor->show_console, true);
            igSeparator();
            igMenuItem_BoolPtr("Scene View",     nullptr,
                               &editor->show_scene_view, true);
            igMenuItem_BoolPtr("Game View",      nullptr,
                               &editor->show_game_view, true);
            igMenuItem_BoolPtr("Scene List Manager", nullptr,
                               &editor->show_scene_list, true);
            igSeparator();
            igMenuItem_BoolPtr("Sprite Editor",  nullptr,
                               &editor->show_sprite_editor, true);
            igMenuItem_BoolPtr("Animation Editor", nullptr,
                               &editor->show_animation_editor, true);
            igMenuItem_BoolPtr("Controller Editor", nullptr,
                               &editor->show_controller_editor, true);
            igSeparator();
            igMenuItem_BoolPtr("ImGui Demo",     nullptr,
                               &editor->show_demo_window, true);
            igEndMenu();
        }

        if (igBeginMenu("Scenes", true)) {
            SceneManager *sm = engine_get_scene_manager(editor->engine);
            uint32_t count = scene_manager_get_count(sm);
            int32_t current_idx = scene_manager_get_current_index(sm);

            for (uint32_t i = 0; i < count; ++i) {
                const char *path = scene_manager_get_scene_path(sm, i);
                if (path != nullptr) {
                    const char *filename = strrchr(path, '/');
                    filename = (filename != nullptr) ? filename + 1 : path;

                    char label[128];
                    snprintf(label, sizeof(label), "%d: %s", i + 1, filename);

                    bool is_selected = ((int32_t)i == current_idx);
                    if (igMenuItem_Bool(label, nullptr, is_selected, true)) {
                        engine_switch_scene(editor->engine, path);
                        console_log("[editor] Switched to scene: %s", path);
                    }
                }
            }

            if (count == 0) {
                igMenuItem_Bool("No scenes in manifest", nullptr, false, false);
            }

            igEndMenu();
        }

        // ---- Toolbar buttons centered in the main menu bar ----------------
        {
            PlayState state = engine_get_play_state(editor->engine);
            SceneManager *sm = engine_get_scene_manager(editor->engine);
            int32_t current_idx = scene_manager_get_current_index(sm);
            uint32_t scene_count = scene_manager_get_count(sm);

            constexpr float BtnSizeX = 24.0f;
            constexpr float BtnSizeY = 24.0f;
            constexpr float BtnGap   = 4.0f;
            float total_w = BtnSizeX * 5.0f + BtnGap * 4.0f;
            float win_w = igGetWindowWidth();
            float offset = (win_w - total_w) * 0.5f;

            float prev_cursor_x = igGetCursorPosX();
            if (offset > prev_cursor_x) {
                igSetCursorPosX(offset);
            }

            // Prev Scene button (⏮)
            {
                bool enabled = (current_idx > 0);
                if (enabled) {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.2f, 0.4f, 0.7f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.5f, 0.8f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.15f, 0.3f, 0.6f, 1.0f});
                } else {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.5f, 0.5f, 0.5f, 1.0f});
                }

                if (igButton("\xef\x81\x88", (ImVec2){BtnSizeX, BtnSizeY}) && enabled) {
                    engine_previous_scene(editor->engine);
                }
                if (igIsItemHovered(0)) {
                    igSetTooltip(enabled ? "Previous Scene" : "No previous scene");
                }
                igPopStyleColor(enabled ? 3 : 4);
            }

            igSameLine(0, BtnGap);

            // Play button (▶)
            {
                bool enabled = (state != PLAY_STATE_PLAYING);
                if (enabled) {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.2f, 0.7f, 0.2f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.8f, 0.3f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.15f, 0.6f, 0.15f, 1.0f});
                } else {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.5f, 0.5f, 0.5f, 1.0f});
                }

                if (igButton("\xef\x81\x8b", (ImVec2){BtnSizeX, BtnSizeY}) && enabled) {
                    engine_set_play_state(editor->engine, PLAY_STATE_PLAYING);
                }
                if (igIsItemHovered(0)) {
                    igSetTooltip(enabled ? "Play (Ctrl+R)" : "Already playing");
                }
                igPopStyleColor(enabled ? 3 : 4);
            }

            igSameLine(0, BtnGap);

            // Pause button (⏸)
            {
                bool can_pause = (state == PLAY_STATE_PLAYING || state == PLAY_STATE_PAUSED);
                if (can_pause) {
                    bool is_paused = (state == PLAY_STATE_PAUSED);
                    ImVec4 base = is_paused ? (ImVec4){0.9f, 0.8f, 0.2f, 1.0f} : (ImVec4){0.8f, 0.7f, 0.1f, 1.0f};
                    igPushStyleColor_Vec4(ImGuiCol_Button,        base);
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.9f, 0.8f, 0.2f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.7f, 0.6f, 0.05f, 1.0f});
                } else {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.5f, 0.5f, 0.5f, 1.0f});
                }

                if (igButton("\xef\x81\x8c", (ImVec2){BtnSizeX, BtnSizeY}) && can_pause) {
                    if (state == PLAY_STATE_PLAYING) {
                        engine_set_play_state(editor->engine, PLAY_STATE_PAUSED);
                    } else {
                        engine_set_play_state(editor->engine, PLAY_STATE_PLAYING);
                    }
                }
                if (igIsItemHovered(0)) {
                    igSetTooltip(can_pause ? "Pause / Resume" : "Not playing");
                }
                igPopStyleColor(can_pause ? 3 : 4);
            }

            igSameLine(0, BtnGap);

            // Stop button (⏹)
            {
                bool can_stop = (state != PLAY_STATE_EDITING);
                if (can_stop) {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.7f, 0.2f, 0.2f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.8f, 0.3f, 0.3f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.6f, 0.15f, 0.15f, 1.0f});
                } else {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.5f, 0.5f, 0.5f, 1.0f});
                }

                if (igButton("\xef\x81\x8d", (ImVec2){BtnSizeX, BtnSizeY}) && can_stop) {
                    engine_set_play_state(editor->engine, PLAY_STATE_EDITING);
                }
                if (igIsItemHovered(0)) {
                    igSetTooltip(can_stop ? "Stop (Ctrl+R)" : "Not playing");
                }
                igPopStyleColor(can_stop ? 3 : 4);
            }

            igSameLine(0, BtnGap);

            // Next Scene button (⏭)
            {
                bool enabled = (current_idx >= 0 && (uint32_t)current_idx < scene_count - 1);
                if (enabled) {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.2f, 0.4f, 0.7f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.5f, 0.8f, 1.0f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.15f, 0.3f, 0.6f, 1.0f});
                } else {
                    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.3f, 0.3f, 0.3f, 0.3f});
                    igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.5f, 0.5f, 0.5f, 1.0f});
                }

                if (igButton("\xef\x81\x91", (ImVec2){BtnSizeX, BtnSizeY}) && enabled) {
                    engine_next_scene(editor->engine);
                }
                if (igIsItemHovered(0)) {
                    igSetTooltip(enabled ? "Next Scene" : "No next scene");
                }
                igPopStyleColor(enabled ? 3 : 4);
            }

            igSetCursorPosX(prev_cursor_x);

            // Keyboard shortcut (Ctrl+R)
            ImGuiIO *io = igGetIO_Nil();
            if (!io->WantTextInput && io->KeyCtrl && igIsKeyPressed_Bool(ImGuiKey_R, false)) {
                if (state == PLAY_STATE_EDITING) {
                    engine_set_play_state(editor->engine, PLAY_STATE_PLAYING);
                } else {
                    engine_set_play_state(editor->engine, PLAY_STATE_EDITING);
                }
            }
        }

        igEndMenuBar();
    }

    igEnd();
}

// ---------------------------------------------------------------------------
// editor_begin_frame
// ---------------------------------------------------------------------------

void editor_begin_frame(Editor *editor) {
    if (editor == nullptr) return;

    imgui_layer_begin_frame();

    PlayState prev_state = engine_get_play_state(editor->engine);

    // Dockspace + menu bar.
    editor_render_dockspace(editor);

    PlayState curr_state = engine_get_play_state(editor->engine);

    // Auto-open and focus Game View when entering Play mode.
    if (prev_state == PLAY_STATE_EDITING && curr_state != PLAY_STATE_EDITING) {
        editor->show_game_view = true;
        igSetWindowFocus_Str("Game View");
    }

    // ---- Panels -----------------------------------------------------------
    World            *world = engine_get_world(editor->engine);
    HierarchyContext *hctx  = engine_get_hctx(editor->engine);

    if (editor->show_hierarchy) {
        panel_hierarchy_render(&editor->show_hierarchy,
                               world, hctx,
                               &editor->selected_entity,
                               &editor->has_selection);
    }

    if (editor->show_inspector) {
        CameraContext *cam_ctx = engine_get_cam_ctx(editor->engine);
        LuaHost       *lua     = engine_get_lua_host(editor->engine);
        AssetManager  *am      = engine_get_asset_manager(editor->engine);
        AnimCache     *acache  = engine_get_anim_cache(editor->engine);

        panel_inspector_render(&editor->show_inspector,
                                world, hctx, cam_ctx, lua, am, acache,
                                editor->selected_entity,
                                editor->has_selection);
    }

    if (editor->show_content_browser) {
        panel_content_browser_render(&editor->show_content_browser, &editor->show_controller_editor);
    }

    if (editor->show_console) {
        panel_console_render(&editor->show_console);
    }

    if (editor->show_scene_list) {
        panel_scene_list_render(&editor->show_scene_list, editor->engine);
    }

    // ---- Scene View (always-on editor viewport) ---------------------------
    if (editor->show_scene_view) {
        Platform *plat = engine_get_platform(editor->engine);
        Renderer *rend = engine_get_renderer(editor->engine);
        uint32_t fb_w = 800, fb_h = 600;
        platform_get_framebuffer_size(plat, &fb_w, &fb_h);
        if (panel_scene_view_render(&editor->show_scene_view, editor->engine, rend, fb_w, fb_h)) {
            editor->has_selection = false;
            editor->selected_entity = 0;
        }
    }

    // ---- Game View (play-mode viewport) -----------------------------------
    if (editor->show_game_view) {
        Platform *plat = engine_get_platform(editor->engine);
        Renderer *rend = engine_get_renderer(editor->engine);
        uint32_t fb_w = 800, fb_h = 600;
        platform_get_framebuffer_size(plat, &fb_w, &fb_h);
        panel_game_view_render(&editor->show_game_view, rend, fb_w, fb_h,
                               curr_state);
    }

    if (editor->show_sprite_editor) {
        AssetManager *am   = engine_get_asset_manager(editor->engine);
        Renderer     *rend = engine_get_renderer(editor->engine);
        panel_sprite_editor_render(&editor->show_sprite_editor, am, rend);
    }

    if (editor->show_animation_editor) {
        AssetManager *am   = engine_get_asset_manager(editor->engine);
        Renderer     *rend = engine_get_renderer(editor->engine);
        panel_animation_editor_render(&editor->show_animation_editor, am, rend);
    }

    if (editor->show_controller_editor) {
        AssetManager *am   = engine_get_asset_manager(editor->engine);
        Renderer     *rend = engine_get_renderer(editor->engine);
        panel_controller_editor_render(&editor->show_controller_editor,
                                       am, rend);
    }

    // ---- Input routing: game input active when either viewport focused ----
    Input *input = engine_get_input(editor->engine);
    if (input != nullptr) {
        bool viewport_focused = panel_game_view_is_focused() ||
                                panel_scene_view_is_focused();
        input_set_game_active(input, viewport_focused);
    }

    // ---- Entity picking (Scene View) --------------------------------------
    if (panel_scene_view_is_hovered() && igIsMouseClicked_Bool(0, false)) {
        float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        panel_scene_view_get_content_bounds(&min_x, &min_y, &max_x, &max_y);

        ImVec2_c mouse_pos_c = igGetMousePos();
        ImVec2 mouse_pos = { mouse_pos_c.x, mouse_pos_c.y };

        float image_w = max_x - min_x;
        float image_h = max_y - min_y;

        if (image_w > 0.0f && image_h > 0.0f) {
            float local_x = mouse_pos.x - min_x;
            float local_y = mouse_pos.y - min_y;

            if (local_x >= 0.0f && local_x < image_w && local_y >= 0.0f && local_y < image_h) {
                Renderer *rend = engine_get_renderer(editor->engine);
                uint32_t offscreen_w = 0, offscreen_h = 0;
                vulkan_renderer_get_offscreen_size(rend, &offscreen_w, &offscreen_h);

                if (offscreen_w > 0 && offscreen_h > 0) {
                    float norm_x = local_x / image_w;
                    float norm_y = local_y / image_h;

                    uint32_t pixel_x = (uint32_t)(norm_x * offscreen_w);
                    uint32_t pixel_y = (uint32_t)(norm_y * offscreen_h);

                    if (pixel_x >= offscreen_w) pixel_x = offscreen_w - 1;
                    if (pixel_y >= offscreen_h) pixel_y = offscreen_h - 1;

                    uint32_t picked_entity = vulkan_renderer_pick_entity(rend, pixel_x, pixel_y);

                    if (picked_entity != 0) {
                        editor->selected_entity = entity_index(picked_entity);
                        editor->has_selection = true;
                        console_log("[editor] Selected entity %u via Scene View picking", editor->selected_entity);
                    } else {
                        editor->has_selection = false;
                        console_log("[editor] Selection cleared");
                    }
                }
            }
        }
    }

    // ---- Entity picking (Game View) ---------------------------------------
    if (panel_game_view_is_hovered() && igIsMouseClicked_Bool(0, false)) {
        float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        panel_game_view_get_content_bounds(&min_x, &min_y, &max_x, &max_y);

        ImVec2_c mouse_pos_c = igGetMousePos();
        ImVec2 mouse_pos = { mouse_pos_c.x, mouse_pos_c.y };

        float image_w = max_x - min_x;
        float image_h = max_y - min_y;

        if (image_w > 0.0f && image_h > 0.0f) {
            float local_x = mouse_pos.x - min_x;
            float local_y = mouse_pos.y - min_y;

            if (local_x >= 0.0f && local_x < image_w && local_y >= 0.0f && local_y < image_h) {
                Renderer *rend = engine_get_renderer(editor->engine);
                uint32_t offscreen_w = 0, offscreen_h = 0;
                vulkan_renderer_get_offscreen_size(rend, &offscreen_w, &offscreen_h);

                if (offscreen_w > 0 && offscreen_h > 0) {
                    float norm_x = local_x / image_w;
                    float norm_y = local_y / image_h;

                    uint32_t pixel_x = (uint32_t)(norm_x * offscreen_w);
                    uint32_t pixel_y = (uint32_t)(norm_y * offscreen_h);

                    if (pixel_x >= offscreen_w) pixel_x = offscreen_w - 1;
                    if (pixel_y >= offscreen_h) pixel_y = offscreen_h - 1;

                    uint32_t picked_entity = vulkan_renderer_pick_entity(rend, pixel_x, pixel_y);

                    if (picked_entity != 0) {
                        editor->selected_entity = entity_index(picked_entity);
                        editor->has_selection = true;
                        console_log("[editor] Selected entity %u via GPU picking", editor->selected_entity);
                    } else {
                        editor->has_selection = false;
                        console_log("[editor] Selection cleared");
                    }
                }
            }
        }
    }

    if (editor->show_demo_window) {
        igShowDemoWindow(&editor->show_demo_window);
    }
}

// ---------------------------------------------------------------------------
// editor_end_frame
// ---------------------------------------------------------------------------

void editor_end_frame(Editor *editor) {
    if (editor == nullptr) return;

    Renderer *r = engine_get_renderer(editor->engine);
    VkCommandBuffer cmd = vulkan_renderer_get_current_command_buffer(r);

    imgui_layer_end_frame(cmd);
}

void editor_save_meta(Editor *editor, const char *scene_path) {
    if (editor == nullptr || scene_path == nullptr) return;

    // Duplicate first to prevent Use-After-Free if scene_path points to editor->last_scene
    char *new_scene = strdup(scene_path);
    if (new_scene == nullptr) return;

    free(editor->last_scene);
    editor->last_scene = new_scene;

    // Save to .editor_meta.json
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return;

    cJSON_AddStringToObject(root, "last_scene", new_scene);
    char *rendered = cJSON_Print(root);
    cJSON_Delete(root);

    if (rendered == nullptr) return;

    FILE *f = fopen(".editor_meta.json", "w");
    if (f != nullptr) {
        fputs(rendered, f);
        fclose(f);
    }
    free(rendered);
}

const char *editor_get_last_scene(const Editor *editor) {
    if (editor == nullptr) return nullptr;
    return editor->last_scene;
}

#endif // EDITOR_BUILD
