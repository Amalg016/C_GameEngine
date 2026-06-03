#ifndef ENGINE_EDITOR_PANEL_TOOLBAR_H
#define ENGINE_EDITOR_PANEL_TOOLBAR_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Toolbar Panel — Play / Pause / Stop controls.
//
// Renders a thin horizontal bar with centered transport buttons.
// Also handles the Ctrl+R keyboard shortcut to toggle Play/Stop.
// ---------------------------------------------------------------------------

typedef struct Engine Engine;

/// Render the toolbar panel with Play/Pause/Stop buttons.
/// Handles Ctrl+R toggle internally.
void panel_toolbar_render(Engine *engine);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_TOOLBAR_H
