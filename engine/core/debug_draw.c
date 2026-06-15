#ifdef EDITOR_BUILD

#include "debug_draw.h"
#include <math.h>
#include <string.h>

static DebugDrawCmd s_commands[DebugDrawMaxCommands];
static uint32_t s_command_count = 0;

void debug_draw_clear(void) {
    s_command_count = 0;
}

void debug_draw_line(float x1, float y1, float x2, float y2,
                     const float color[4], float thickness) {
    if (s_command_count >= DebugDrawMaxCommands) {
        return;
    }

    DebugDrawCmd cmd = {
        .type = DEBUG_DRAW_LINE,
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .thickness = thickness
    };
    if (color != nullptr) {
        memcpy(cmd.color, color, sizeof(cmd.color));
    } else {
        cmd.color[0] = 1.0f;
        cmd.color[1] = 1.0f;
        cmd.color[2] = 1.0f;
        cmd.color[3] = 1.0f;
    }

    s_commands[s_command_count++] = cmd;
}

void debug_draw_rect(float cx, float cy, float w, float h,
                     const float color[4], float thickness) {
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    float x_min = cx - hw;
    float x_max = cx + hw;
    float y_min = cy - hh;
    float y_max = cy + hh;

    // Top edge
    debug_draw_line(x_min, y_min, x_max, y_min, color, thickness);
    // Right edge
    debug_draw_line(x_max, y_min, x_max, y_max, color, thickness);
    // Bottom edge
    debug_draw_line(x_max, y_max, x_min, y_max, color, thickness);
    // Left edge
    debug_draw_line(x_min, y_max, x_min, y_min, color, thickness);
}

void debug_draw_circle(float cx, float cy, float radius,
                       const float color[4], float thickness) {
    constexpr int Segments = 32;
    constexpr float TwoPi = 6.28318530718f;

    float prev_x = cx + radius;
    float prev_y = cy;

    for (int i = 1; i <= Segments; ++i) {
        float theta = ((float)i / (float)Segments) * TwoPi;
        float curr_x = cx + radius * cosf(theta);
        float curr_y = cy + radius * sinf(theta);

        debug_draw_line(prev_x, prev_y, curr_x, curr_y, color, thickness);

        prev_x = curr_x;
        prev_y = curr_y;
    }
}

const DebugDrawCmd *debug_draw_get_commands(uint32_t *out_count) {
    if (out_count != nullptr) {
        *out_count = s_command_count;
    }
    return s_commands;
}

#endif // EDITOR_BUILD

#ifndef EDITOR_BUILD
// Prevent empty translation unit warning
typedef int debug_draw_dummy_t;
#endif

