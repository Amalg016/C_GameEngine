#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// ImGui Layer — implementation
//
// Uses the cimgui C API (ig* prefix) for all core ImGui operations.
// Delegates platform/renderer backend work to the C++ bridge.
// ---------------------------------------------------------------------------

#include "imgui_layer.h"
#include "imgui_vulkan_bridge.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static bool s_initialized = false;

// ---------------------------------------------------------------------------
// imgui_layer_init
// ---------------------------------------------------------------------------

bool imgui_layer_init(void *glfw_window, const ImGuiBridgeInitInfo *info) {
    if (s_initialized) return true;
    if (glfw_window == nullptr || info == nullptr) return false;

    // ---- Create ImGui context ---------------------------------------------
    igCreateContext(nullptr);

    ImGuiIO *io = igGetIO_Nil();

    // ---- Load Fonts -------------------------------------------------------
    ImFontAtlas *fonts = io->Fonts;
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin & Latin Supplement
        0x0100, 0x024F, // Latin Extended-A & B
        0xF000, 0xF8FF, // Private Use Area (Nerd Font icons)
        0
    };
    ImFont *main_font = ImFontAtlas_AddFontFromFileTTF(fonts, "engine/editor/fonts/JetBrainsMonoNerdFont-Regular.ttf", 15.0f, nullptr, ranges);
    if (main_font == nullptr) {
        fprintf(stderr, "[imgui_layer] Warning: failed to load JetBrainsMonoNerdFont-Regular.ttf, using default font\n");
    }

    // Enable docking.
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Enable keyboard navigation.
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // ---- Style ------------------------------------------------------------
    igStyleColorsDark(nullptr);

    ImGuiStyle *style = igGetStyle();
    style->WindowRounding    = 4.0f;
    style->FrameRounding     = 2.0f;
    style->GrabRounding      = 2.0f;
    style->ScrollbarRounding = 4.0f;
    style->TabRounding       = 4.0f;
    style->WindowBorderSize  = 1.0f;
    style->FrameBorderSize   = 0.0f;
    style->PopupBorderSize   = 1.0f;

    // Slightly customised dark palette for a game-engine editor feel.
    ImVec4 *colors = style->Colors;
    colors[ImGuiCol_WindowBg]          = (ImVec4){ 0.10f, 0.10f, 0.12f, 1.00f };
    colors[ImGuiCol_TitleBg]           = (ImVec4){ 0.08f, 0.08f, 0.10f, 1.00f };
    colors[ImGuiCol_TitleBgActive]     = (ImVec4){ 0.14f, 0.14f, 0.18f, 1.00f };
    colors[ImGuiCol_Tab]               = (ImVec4){ 0.14f, 0.14f, 0.18f, 1.00f };
    colors[ImGuiCol_TabSelected]       = (ImVec4){ 0.24f, 0.24f, 0.32f, 1.00f };
    colors[ImGuiCol_TabHovered]        = (ImVec4){ 0.32f, 0.32f, 0.42f, 1.00f };
    colors[ImGuiCol_FrameBg]           = (ImVec4){ 0.16f, 0.16f, 0.20f, 1.00f };
    colors[ImGuiCol_FrameBgHovered]    = (ImVec4){ 0.22f, 0.22f, 0.28f, 1.00f };
    colors[ImGuiCol_FrameBgActive]     = (ImVec4){ 0.28f, 0.28f, 0.36f, 1.00f };
    colors[ImGuiCol_Header]            = (ImVec4){ 0.20f, 0.20f, 0.26f, 1.00f };
    colors[ImGuiCol_HeaderHovered]     = (ImVec4){ 0.28f, 0.28f, 0.36f, 1.00f };
    colors[ImGuiCol_HeaderActive]      = (ImVec4){ 0.34f, 0.34f, 0.44f, 1.00f };
    colors[ImGuiCol_Button]            = (ImVec4){ 0.20f, 0.20f, 0.26f, 1.00f };
    colors[ImGuiCol_ButtonHovered]     = (ImVec4){ 0.30f, 0.30f, 0.40f, 1.00f };
    colors[ImGuiCol_ButtonActive]      = (ImVec4){ 0.36f, 0.36f, 0.48f, 1.00f };
    colors[ImGuiCol_DockingPreview]    = (ImVec4){ 0.26f, 0.59f, 0.98f, 0.70f };
    colors[ImGuiCol_DockingEmptyBg]    = (ImVec4){ 0.08f, 0.08f, 0.08f, 1.00f };
    colors[ImGuiCol_MenuBarBg]         = (ImVec4){ 0.12f, 0.12f, 0.14f, 1.00f };
    colors[ImGuiCol_PopupBg]           = (ImVec4){ 0.10f, 0.10f, 0.12f, 0.94f };
    colors[ImGuiCol_ScrollbarBg]       = (ImVec4){ 0.08f, 0.08f, 0.10f, 0.60f };
    colors[ImGuiCol_SeparatorHovered]  = (ImVec4){ 0.26f, 0.59f, 0.98f, 0.78f };
    colors[ImGuiCol_SeparatorActive]   = (ImVec4){ 0.26f, 0.59f, 0.98f, 1.00f };
    colors[ImGuiCol_ResizeGrip]        = (ImVec4){ 0.26f, 0.59f, 0.98f, 0.20f };
    colors[ImGuiCol_ResizeGripHovered] = (ImVec4){ 0.26f, 0.59f, 0.98f, 0.67f };
    colors[ImGuiCol_ResizeGripActive]  = (ImVec4){ 0.26f, 0.59f, 0.98f, 0.95f };

    // ---- Initialise backends via the C++ bridge ---------------------------
    if (!imgui_bridge_init(glfw_window, info)) {
        fprintf(stderr, "[imgui_layer] backend initialisation failed\n");
        igDestroyContext(nullptr);
        return false;
    }

    s_initialized = true;
    printf("[imgui_layer] initialised (docking enabled)\n");
    return true;
}

// ---------------------------------------------------------------------------
// imgui_layer_shutdown
// ---------------------------------------------------------------------------

void imgui_layer_shutdown(void) {
    if (!s_initialized) return;

    imgui_bridge_shutdown();
    igDestroyContext(nullptr);

    s_initialized = false;
    printf("[imgui_layer] shut down\n");
}

// ---------------------------------------------------------------------------
// imgui_layer_begin_frame
// ---------------------------------------------------------------------------

void imgui_layer_begin_frame(void) {
    if (!s_initialized) return;

    imgui_bridge_new_frame();
    igNewFrame();
}

// ---------------------------------------------------------------------------
// imgui_layer_end_frame
// ---------------------------------------------------------------------------

void imgui_layer_end_frame(VkCommandBuffer cmd) {
    if (!s_initialized) return;

    igRender();
    ImDrawData *draw_data = igGetDrawData();
    imgui_bridge_render(draw_data, cmd);
}

#endif // EDITOR_BUILD
