#ifndef ENGINE_EDITOR_PANEL_CONSOLE_H
#define ENGINE_EDITOR_PANEL_CONSOLE_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Console Panel — ring-buffer log viewer for the editor.
//
// Provides a global console_log() function that any engine subsystem can
// call (when EDITOR_BUILD is defined) to push messages into the editor
// console.  The panel renders these messages in a scrollable window with
// auto-scroll and a clear button.
// ---------------------------------------------------------------------------

#include <stdbool.h>

/// Push a formatted message into the console ring buffer.
/// Thread-unsafe — call only from the main thread.
void console_log(const char *fmt, ...);

/// Clear all console messages.
void console_clear(void);

/// Render the console panel.
/// `p_open` — visibility flag; set to false when the user closes the panel.
void panel_console_render(bool *p_open);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_CONSOLE_H
