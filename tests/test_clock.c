#include "test_framework.h"
#include "engine/core/clock.h"

// Define a static mock time that platform_get_time will return
static double g_mock_time = 100.0;

// Linker will resolve platform_get_time to this mock instead of platform.c
double platform_get_time(void) {
    return g_mock_time;
}

static void test_clock_init_and_tick(void) {
    g_mock_time = 100.0;
    Clock clock = {};

    clock_start(&clock, 1.0 / 60.0);
    ASSERT_FLOAT_EQ(clock.start_time, 100.0, 1e-6f);
    ASSERT_FLOAT_EQ(clock.last_time, 100.0, 1e-6f);
    ASSERT_FLOAT_EQ(clock.fixed_dt, 1.0 / 60.0, 1e-6f);
    ASSERT_FLOAT_EQ(clock.delta_time, 0.0, 1e-6f);
    ASSERT_FLOAT_EQ(clock.elapsed, 0.0, 1e-6f);
    ASSERT_FLOAT_EQ(clock.accumulator, 0.0, 1e-6f);
    ASSERT(clock.frame_count == 0);

    // Advance time by 0.01s (smaller than fixed_dt)
    g_mock_time += 0.01;
    clock_tick(&clock);

    ASSERT_FLOAT_EQ(clock.delta_time, 0.01, 1e-6f);
    ASSERT_FLOAT_EQ(clock.elapsed, 0.01, 1e-6f);
    ASSERT_FLOAT_EQ(clock.accumulator, 0.01, 1e-6f);
    ASSERT(clock.frame_count == 1);
    
    // Alpha should be 0.01 / (1/60) = 0.01 * 60 = 0.6
    ASSERT_FLOAT_EQ(clock_get_alpha(&clock), 0.6, 1e-6f);
}

static void test_clock_clamping_and_guards(void) {
    g_mock_time = 200.0;
    Clock clock = {};

    clock_start(&clock, 1.0 / 60.0);

    // Test negative delta guard (time goes backwards)
    g_mock_time -= 5.0;
    clock_tick(&clock);
    ASSERT_FLOAT_EQ(clock.delta_time, 0.0, 1e-6f);
    ASSERT_FLOAT_EQ(clock.accumulator, 0.0, 1e-6f);

    // Test spiral of death clamp (time jumps by 1.0s, MAX_FRAME_DELTA is 0.25)
    g_mock_time += 1.0;
    clock_tick(&clock);
    ASSERT_FLOAT_EQ(clock.delta_time, 0.25, 1e-6f);
    ASSERT_FLOAT_EQ(clock.accumulator, 0.25, 1e-6f);
}

void test_clock_run(void) {
    printf(COLOR_BLUE COLOR_BOLD "\n--- Running Clock Tests ---" COLOR_RESET "\n");
    RUN_TEST(test_clock_init_and_tick);
    RUN_TEST(test_clock_clamping_and_guards);
}
