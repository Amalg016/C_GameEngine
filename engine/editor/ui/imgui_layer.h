#ifndef ENGINE_EDITOR_IMGUI_LAYER_H
#define ENGINE_EDITOR_IMGUI_LAYER_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// ImGui Layer — high-level lifecycle for the Dear ImGui integration.
//
// This module owns the ImGui context and orchestrates frame begin / end.
// It talks to the C++ backend bridge (imgui_vulkan_bridge) for platform-
// specific initialisation, and uses the cimgui C API for all core ImGui
// operations (context creation, configuration, rendering).
//
// All functions are no-ops if called without a prior successful init.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <vulkan/vulkan.h>

#include "imgui_vulkan_bridge.h"   // ImGuiBridgeInitInfo

/// Initialise the ImGui context and Vulkan/GLFW backends.
///
/// Creates the ImGui context, enables docking, sets up the dark theme,
/// and initialises the rendering backends via the C++ bridge.
///
/// `glfw_window` — raw GLFWwindow* (passed as void* to avoid GLFW header).
/// `info`        — Vulkan handles required by the backend.
///
/// Must be called ONCE after the renderer is fully initialised.
/// Returns true on success.
[[nodiscard]]
bool imgui_layer_init(void *glfw_window, const ImGuiBridgeInitInfo *info);

/// Shut down ImGui — destroys backends and the ImGui context.
/// Safe to call even if init was never called or already shut down.
void imgui_layer_shutdown(void);

/// Begin a new ImGui frame.
/// Calls backend NewFrame + igNewFrame.
/// After this call, you may issue ig* draw commands (igBegin, igText, etc.).
void imgui_layer_begin_frame(void);

/// End the ImGui frame and record draw commands into the Vulkan command buffer.
/// Calls igRender + ImGui_ImplVulkan_RenderDrawData.
/// `cmd` must be a recording command buffer with an active render pass.
void imgui_layer_end_frame(VkCommandBuffer cmd);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_IMGUI_LAYER_H
