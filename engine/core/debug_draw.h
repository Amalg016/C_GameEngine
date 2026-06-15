#ifndef ENGINE_CORE_DEBUG_DRAW_H
#define ENGINE_CORE_DEBUG_DRAW_H

#ifdef EDITOR_BUILD

#include <stdbool.h>
#include <stdint.h>

/// Maximum gizmo draw commands per frame.
constexpr uint32_t DebugDrawMaxCommands = 4096;

/// Default line thickness in world units.
constexpr float DebugDrawDefaultThickness = 0.02f;

typedef enum DebugDrawType {
    DEBUG_DRAW_LINE,
    DEBUG_DRAW_RECT,
    DEBUG_DRAW_CIRCLE,
} DebugDrawType;

typedef struct DebugDrawCmd {
    DebugDrawType type;
    float x1, y1;
    float x2, y2;
    float color[4];
    float thickness;
} DebugDrawCmd;

/// Begin a new frame — clears all accumulated commands.
void debug_draw_clear(void);

/// Queue a line segment from (x1, y1) to (x2, y2).
void debug_draw_line(float x1, float y1, float x2, float y2,
                     const float color[4], float thickness);

/// Queue an axis-aligned wireframe rectangle centered at (cx, cy) with width w and height h.
void debug_draw_rect(float cx, float cy, float w, float h,
                     const float color[4], float thickness);

/// Queue a wireframe circle at (cx, cy) with radius r.
void debug_draw_circle(float cx, float cy, float radius,
                       const float color[4], float thickness);

/// Get the current command buffer (read-only). Returns count via out_count.
[[nodiscard]]
const DebugDrawCmd *debug_draw_get_commands(uint32_t *out_count);

#endif // EDITOR_BUILD
#endif // ENGINE_CORE_DEBUG_DRAW_H
