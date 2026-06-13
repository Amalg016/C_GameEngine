#include "test_framework.h"
#include <stdio.h>

// Global test counters
int g_tests_run = 0;
int g_tests_failed = 0;

// Declarations of module run functions
void test_math_run(void);
void test_ecs_run(void);
void test_asset_manager_run(void);
void test_clock_run(void);

int main(void) {
    printf(COLOR_BOLD COLOR_CYAN "========================================\n");
    printf("     🌌 C23 GAME ENGINE UNIT TESTS\n");
    printf("========================================\n" COLOR_RESET);

    test_math_run();
    test_ecs_run();
    test_asset_manager_run();
    test_clock_run();

    printf(COLOR_BOLD COLOR_CYAN "\n========================================\n");
    printf("             TEST SUMMARY\n");
    printf("========================================\n" COLOR_RESET);

    if (g_tests_failed > 0) {
        printf(COLOR_BOLD COLOR_RED "  [FAIL] %d / %d tests failed!\n" COLOR_RESET, 
               g_tests_failed, g_tests_run);
        return 1;
    } else {
        printf(COLOR_BOLD COLOR_GREEN "  [SUCCESS] All %d tests passed successfully!\n" COLOR_RESET, 
               g_tests_run);
        return 0;
    }
}
