#include "clock.h"
#include "../platform/platform.h"

#include <string.h>

// Maximum frame delta (seconds).  If a frame takes longer than this
// (e.g. debugger pause, OS stall), we clamp to avoid the accumulator
// spiralling out of control and issuing dozens of fixed updates.
static const double MAX_FRAME_DELTA = 0.25;

// ---------------------------------------------------------------------------
// clock_start
// ---------------------------------------------------------------------------

void clock_start(Clock *clock, double fixed_dt) {
    if (clock == nullptr) return;

    memset(clock, 0, sizeof(*clock));

    double now        = platform_get_time();
    clock->start_time = now;
    clock->last_time  = now;
    clock->fixed_dt   = fixed_dt > 0.0 ? fixed_dt : (1.0 / 60.0);
}

// ---------------------------------------------------------------------------
// clock_tick — call once per frame, before the fixed-update drain.
// ---------------------------------------------------------------------------

void clock_tick(Clock *clock) {
    if (clock == nullptr) return;

    double now   = platform_get_time();
    double delta = now - clock->last_time;
    clock->last_time = now;

    // Clamp to prevent spiral of death.
    if (delta > MAX_FRAME_DELTA) {
        delta = MAX_FRAME_DELTA;
    }
    // Guard against negative deltas (shouldn't happen, but be safe).
    if (delta < 0.0) {
        delta = 0.0;
    }

    clock->delta_time   = delta;
    clock->elapsed      = now - clock->start_time;
    clock->accumulator += delta;
    clock->frame_count++;
}

// ---------------------------------------------------------------------------
// clock_get_alpha — interpolation factor for rendering.
// ---------------------------------------------------------------------------

double clock_get_alpha(const Clock *clock) {
    if (clock == nullptr || clock->fixed_dt <= 0.0) return 0.0;
    return clock->accumulator / clock->fixed_dt;
}
