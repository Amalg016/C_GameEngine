#ifdef EDITOR_BUILD

#include "panel_console.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Ring buffer for log messages
// ---------------------------------------------------------------------------

constexpr uint32_t ConsoleMaxMessages = 512;
constexpr uint32_t ConsoleMaxMsgLen   = 256;

static char     s_messages[ConsoleMaxMessages][ConsoleMaxMsgLen];
static uint32_t s_count = 0;    // total messages inserted (wraps the ring)
static bool     s_auto_scroll = true;

// ---------------------------------------------------------------------------
// console_log
// ---------------------------------------------------------------------------

void console_log(const char *fmt, ...) {
    uint32_t idx = s_count % ConsoleMaxMessages;

    va_list args;
    va_start(args, fmt);
    vsnprintf(s_messages[idx], ConsoleMaxMsgLen, fmt, args);
    va_end(args);

    s_count++;
}

// ---------------------------------------------------------------------------
// console_clear
// ---------------------------------------------------------------------------

void console_clear(void) {
    s_count = 0;
}

// ---------------------------------------------------------------------------
// panel_console_render
// ---------------------------------------------------------------------------

void panel_console_render(bool *p_open) {
    if (!igBegin("Console", p_open, 0)) {
        igEnd();
        return;
    }

    // Toolbar.
    if (igButton("Clear", (ImVec2){ 0, 0 })) {
        console_clear();
    }

    igSameLine(0.0f, -1.0f);
    igCheckbox("Auto-scroll", &s_auto_scroll);

    igSeparator();

    // Scrollable log region.
    if (igBeginChild_Str("ConsoleLog", (ImVec2){ 0, 0 }, 0,
                         ImGuiWindowFlags_HorizontalScrollbar)) {
        uint32_t visible = (s_count < ConsoleMaxMessages)
                            ? s_count
                            : ConsoleMaxMessages;

        uint32_t start = (s_count <= ConsoleMaxMessages)
                          ? 0
                          : (s_count % ConsoleMaxMessages);

        for (uint32_t i = 0; i < visible; ++i) {
            uint32_t idx = (start + i) % ConsoleMaxMessages;
            igTextUnformatted(s_messages[idx], nullptr);
        }

        // Auto-scroll to bottom.
        if (s_auto_scroll && igGetScrollY() >= igGetScrollMaxY()) {
            igSetScrollHereY(1.0f);
        }
    }
    igEndChild();

    igEnd();
}

#endif // EDITOR_BUILD
