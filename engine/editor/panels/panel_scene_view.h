#ifndef ENGINE_EDITOR_PANEL_SCENE_VIEW_H
#define ENGINE_EDITOR_PANEL_SCENE_VIEW_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Scene View Panel — always-on editor viewport for scene editing.
//
// Displays the scene rendered to an offscreen framebuffer as an ImGui image.
// Provides query functions for focus, hover, and content bounds so that
// other editor systems (e.g. gizmos, input routing) can react accordingly.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

typedef struct Renderer Renderer;

/// Render the scene view panel (always-on editor viewport).
/// `p_open`   — visibility flag.
/// `renderer` — the active renderer.
/// `fb_w`, `fb_h` — current framebuffer dimensions (for display).
void panel_scene_view_render(bool *p_open, Renderer *renderer, uint32_t fb_w, uint32_t fb_h);

/// Returns true if the scene view panel is currently focused.
bool panel_scene_view_is_focused(void);

/// Returns true if the scene view panel is currently hovered.
bool panel_scene_view_is_hovered(void);

/// Retrieve the screen-space content bounds of the scene viewport.
void panel_scene_view_get_content_bounds(float *min_x, float *min_y, float *max_x, float *max_y);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_SCENE_VIEW_H
