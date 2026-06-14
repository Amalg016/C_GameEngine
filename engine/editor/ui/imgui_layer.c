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

    // Premium, monochromatic AMOLED Black theme.
    ImVec4 *colors = style->Colors;
    colors[ImGuiCol_Text]              = (ImVec4){ 0.90f, 0.90f, 0.92f, 1.00f };
    colors[ImGuiCol_TextDisabled]      = (ImVec4){ 0.45f, 0.45f, 0.48f, 1.00f };
    colors[ImGuiCol_WindowBg]          = (ImVec4){ 0.03f, 0.03f, 0.03f, 1.00f };
    colors[ImGuiCol_ChildBg]           = (ImVec4){ 0.02f, 0.02f, 0.02f, 1.00f };
    colors[ImGuiCol_PopupBg]           = (ImVec4){ 0.05f, 0.05f, 0.05f, 0.98f };
    colors[ImGuiCol_Border]            = (ImVec4){ 0.12f, 0.12f, 0.12f, 1.00f };
    colors[ImGuiCol_BorderShadow]      = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    colors[ImGuiCol_FrameBg]           = (ImVec4){ 0.06f, 0.06f, 0.06f, 1.00f };
    colors[ImGuiCol_FrameBgHovered]    = (ImVec4){ 0.12f, 0.12f, 0.12f, 1.00f };
    colors[ImGuiCol_FrameBgActive]     = (ImVec4){ 0.18f, 0.18f, 0.18f, 1.00f };
    colors[ImGuiCol_TitleBg]           = (ImVec4){ 0.02f, 0.02f, 0.02f, 1.00f };
    colors[ImGuiCol_TitleBgActive]     = (ImVec4){ 0.04f, 0.04f, 0.04f, 1.00f };
    colors[ImGuiCol_TitleBgCollapsed]  = (ImVec4){ 0.02f, 0.02f, 0.02f, 1.00f };
    colors[ImGuiCol_MenuBarBg]         = (ImVec4){ 0.02f, 0.02f, 0.02f, 1.00f };
    colors[ImGuiCol_ScrollbarBg]       = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    colors[ImGuiCol_ScrollbarGrab]     = (ImVec4){ 0.12f, 0.12f, 0.12f, 1.00f };
    colors[ImGuiCol_ScrollbarGrabHovered] = (ImVec4){ 0.18f, 0.18f, 0.18f, 1.00f };
    colors[ImGuiCol_ScrollbarGrabActive]  = (ImVec4){ 0.24f, 0.24f, 0.24f, 1.00f };
    colors[ImGuiCol_CheckMark]         = (ImVec4){ 0.70f, 0.70f, 0.72f, 1.00f };
    colors[ImGuiCol_SliderGrab]        = (ImVec4){ 0.20f, 0.20f, 0.20f, 1.00f };
    colors[ImGuiCol_SliderGrabActive]  = (ImVec4){ 0.28f, 0.28f, 0.28f, 1.00f };
    colors[ImGuiCol_Button]            = (ImVec4){ 0.08f, 0.08f, 0.08f, 1.00f };
    colors[ImGuiCol_ButtonHovered]     = (ImVec4){ 0.15f, 0.15f, 0.15f, 1.00f };
    colors[ImGuiCol_ButtonActive]      = (ImVec4){ 0.22f, 0.22f, 0.22f, 1.00f };
    colors[ImGuiCol_Header]            = (ImVec4){ 0.08f, 0.08f, 0.08f, 1.00f };
    colors[ImGuiCol_HeaderHovered]     = (ImVec4){ 0.14f, 0.14f, 0.14f, 1.00f };
    colors[ImGuiCol_HeaderActive]      = (ImVec4){ 0.20f, 0.20f, 0.20f, 1.00f };
    colors[ImGuiCol_Separator]         = (ImVec4){ 0.12f, 0.12f, 0.12f, 1.00f };
    colors[ImGuiCol_SeparatorHovered]  = (ImVec4){ 0.24f, 0.24f, 0.24f, 1.00f };
    colors[ImGuiCol_SeparatorActive]   = (ImVec4){ 0.36f, 0.36f, 0.36f, 1.00f };
    colors[ImGuiCol_ResizeGrip]        = (ImVec4){ 0.08f, 0.08f, 0.08f, 1.00f };
    colors[ImGuiCol_ResizeGripHovered] = (ImVec4){ 0.14f, 0.14f, 0.14f, 1.00f };
    colors[ImGuiCol_ResizeGripActive]  = (ImVec4){ 0.20f, 0.20f, 0.20f, 1.00f };
    colors[ImGuiCol_Tab]               = (ImVec4){ 0.04f, 0.04f, 0.04f, 1.00f };
    colors[ImGuiCol_TabHovered]        = (ImVec4){ 0.14f, 0.14f, 0.14f, 1.00f };
    colors[ImGuiCol_TabSelected]       = (ImVec4){ 0.08f, 0.08f, 0.08f, 1.00f };
    colors[ImGuiCol_TabDimmed]         = (ImVec4){ 0.04f, 0.04f, 0.04f, 1.00f };
    colors[ImGuiCol_TabDimmedSelected] = (ImVec4){ 0.06f, 0.06f, 0.06f, 1.00f };
    colors[ImGuiCol_DockingPreview]    = (ImVec4){ 0.20f, 0.20f, 0.20f, 0.70f };
    colors[ImGuiCol_DockingEmptyBg]    = (ImVec4){ 0.02f, 0.02f, 0.02f, 1.00f };

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
