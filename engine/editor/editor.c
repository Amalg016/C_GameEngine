#ifdef EDITOR_BUILD

#include "editor.h"

#include "../core/engine.h"
#include "../platform/platform.h"
#include "../renderer/renderer.h"
#include "../renderer/vulkan/vulkan_renderer.h"
#include "../core/ecs/ecs.h"

#include "ui/imgui_layer.h"
#include "ui/imgui_vulkan_bridge.h"
#include "panels/panel_hierarchy.h"
#include "panels/panel_inspector.h"
#include "panels/panel_content_browser.h"
#include "panels/panel_console.h"
#include "panels/panel_game_view.h"

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
        .show_game_view        = true,
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
            igMenuItem_BoolPtr("Game View",      nullptr,
                               &editor->show_game_view, true);
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
        panel_inspector_render(&editor->show_inspector,
                                world, hctx,
                                editor->selected_entity,
                                editor->has_selection);
    }

    if (editor->show_content_browser) {
        panel_content_browser_render(&editor->show_content_browser);
    }

    if (editor->show_console) {
        panel_console_render(&editor->show_console);
    }

    if (editor->show_game_view) {
        Platform *plat = engine_get_platform(editor->engine);
        Renderer *rend = engine_get_renderer(editor->engine);
        uint32_t fb_w = 800, fb_h = 600;
        platform_get_framebuffer_size(plat, &fb_w, &fb_h);
        panel_game_view_render(&editor->show_game_view, rend, fb_w, fb_h);
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
