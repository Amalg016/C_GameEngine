#ifndef ENGINE_EDITOR_PANEL_GAME_VIEW_H
#define ENGINE_EDITOR_PANEL_GAME_VIEW_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Game View Panel — displays the game viewport inside the editor.
//
// In this initial implementation, the panel shows viewport dimensions and
// a placeholder.  A future enhancement will render the game scene to an
// offscreen framebuffer and display it as an ImGui image.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

typedef struct Renderer Renderer;

/// Render the game view panel.
/// `p_open` — visibility flag.
/// `renderer` — the active renderer.
/// `fb_w`, `fb_h` — current framebuffer dimensions (for display).
void panel_game_view_render(bool *p_open, Renderer *renderer, uint32_t fb_w, uint32_t fb_h);

bool panel_game_view_is_focused(void);
bool panel_game_view_is_hovered(void);
void panel_game_view_get_content_bounds(float *min_x, float *min_y, float *max_x, float *max_y);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_GAME_VIEW_H
