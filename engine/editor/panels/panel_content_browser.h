#ifndef ENGINE_EDITOR_PANEL_CONTENT_BROWSER_H
#define ENGINE_EDITOR_PANEL_CONTENT_BROWSER_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Content Browser Panel — lists files in the assets directory.
//
// Supports:
//   - Directory navigation (double-click to enter, back button)
//   - Drag source for asset paths (payload type: "ASSET_PATH")
// ---------------------------------------------------------------------------

#include <stdbool.h>

/// Render the content browser panel.
/// `p_open` — visibility flag.
void panel_content_browser_render(bool *p_open);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_CONTENT_BROWSER_H
