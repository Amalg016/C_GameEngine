#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Color codes for premium CLI interface
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

extern int g_tests_run;
extern int g_tests_failed;

#define ASSERT_MSG(cond, msg, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, COLOR_RED "      [FAIL] %s:%d: " msg COLOR_RESET "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
            g_tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT(cond) ASSERT_MSG(cond, "Assertion failed: %s", #cond)

#define ASSERT_FLOAT_EQ(a, b, epsilon) \
    ASSERT_MSG(fabsf((float)(a) - (float)(b)) <= (float)(epsilon), "Expected %f to be close to %f (diff: %f)", (double)(a), (double)(b), (double)fabsf((float)(a) - (float)(b)))

#define ASSERT_STR_EQ(a, b) \
    ASSERT_MSG(((a) != nullptr && (b) != nullptr && strcmp((a), (b)) == 0), "Expected string '%s' to equal '%s'", (a) ? (a) : "nullptr", (b) ? (b) : "nullptr")

#define RUN_TEST(test_func) \
    do { \
        printf(COLOR_CYAN "  [RUN]  " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "...\n", #test_func); \
        int failed_before = g_tests_failed; \
        g_tests_run++; \
        test_func(); \
        if (g_tests_failed == failed_before) { \
            printf(COLOR_GREEN "  [PASS] " COLOR_RESET "%s\n", #test_func); \
        } \
    } while (0)

#endif // TEST_FRAMEWORK_H
