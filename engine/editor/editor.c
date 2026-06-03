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

#include "ui/imgui_layer.h"
#include "ui/imgui_vulkan_bridge.h"
#include "panels/panel_hierarchy.h"
#include "panels/panel_inspector.h"
#include "panels/panel_content_browser.h"
#include "panels/panel_console.h"
#include "panels/panel_game_view.h"
#include "panels/panel_scene_view.h"
#include "panels/panel_toolbar.h"
#include "panels/panel_sprite_editor.h"
#include "panels/panel_animation_editor.h"
#include "panels/panel_controller_editor.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <stdlib.h>

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

    // Shared selection state (hierarchy ↔ inspector).
    uint32_t selected_entity;
    bool     has_selection;
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
        .selected_entity       = 0,
        .has_selection         = false,
    };

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

    // Dockspace + menu bar.
    editor_render_dockspace(editor);

    // ---- Toolbar (Play / Pause / Stop) ------------------------------------
    PlayState prev_state = engine_get_play_state(editor->engine);
    panel_toolbar_render(editor->engine);
    PlayState curr_state = engine_get_play_state(editor->engine);

    // Auto-open Game View when entering Play mode.
    if (prev_state == PLAY_STATE_EDITING && curr_state != PLAY_STATE_EDITING) {
        editor->show_game_view = true;
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

    // ---- Scene View (always-on editor viewport) ---------------------------
    if (editor->show_scene_view) {
        Platform *plat = engine_get_platform(editor->engine);
        Renderer *rend = engine_get_renderer(editor->engine);
        uint32_t fb_w = 800, fb_h = 600;
        platform_get_framebuffer_size(plat, &fb_w, &fb_h);
        panel_scene_view_render(&editor->show_scene_view, rend, fb_w, fb_h);
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

#endif // EDITOR_BUILD
