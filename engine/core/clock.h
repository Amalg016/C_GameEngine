#ifndef ENGINE_CORE_CLOCK_H
#define ENGINE_CORE_CLOCK_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Clock — lightweight frame timing with fixed-timestep accumulator.
//
// Call clock_start() once before the loop, then clock_tick() every frame.
// The accumulator tracks leftover time for fixed-step draining.
// ---------------------------------------------------------------------------

typedef struct Clock {
    double   start_time;    // platform time when clock_start() was called
    double   last_time;     // platform time at the previous tick
    double   delta_time;    // seconds since the last tick (variable, per-frame)
    double   elapsed;       // total seconds since clock_start()
    double   accumulator;   // time waiting to be consumed by fixed updates
    double   fixed_dt;      // the fixed timestep interval (e.g. 1/60)
    uint64_t frame_count;   // total frames ticked
} Clock;

/// Initialise the clock and snapshot the current time.
/// `fixed_dt` is the fixed update interval in seconds (e.g. 1.0/60.0).
void clock_start(Clock *clock, double fixed_dt);

/// Advance the clock by one frame.  Computes delta_time, adds to accumulator.
/// Frame delta is clamped to 0.25s to prevent the "spiral of death".
void clock_tick(Clock *clock);

/// Returns the interpolation factor (0.0 – 1.0) for rendering between
/// the last two fixed steps: accumulator / fixed_dt.
double clock_get_alpha(const Clock *clock);

#endif // ENGINE_CORE_CLOCK_H
