/*
 * kern_test.h - Public test framework header for kern projects
 *
 * Provides macros for defining tests, running them, and asserting conditions.
 * Include this header in test files to get a complete test framework.
 * Each test file should have its own main() using KERN_TEST_MAIN_BEGIN/END.
 */

#ifndef KERN_TEST_H
#define KERN_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Test Runtime API (implemented in kern_test.c)
 * ============================================================ */

/**
 * Initialize the test environment.
 * Creates a temporary SQLite database for test isolation.
 * Call once at the start of the test process.
 */
void kern_test_init(void);

/**
 * Clean up the test environment.
 * Removes the temporary database file.
 * Call once at the end of the test process.
 */
void kern_test_cleanup(void);

/**
 * Get the path to the test-specific temporary database.
 * Returns NULL if kern_test_init() has not been called.
 */
const char *kern_test_db_path(void);

/* ============================================================
 * Test Definition Macros
 * ============================================================ */

/**
 * TEST(name) - Define a test function.
 * Usage: TEST(my_test) { ... }
 */
#define TEST(name) static void name(void)

/**
 * RUN_TEST(name) - Run a test function and track pass/fail.
 * Increments counters and prints status. On assertion failure
 * the test calls exit(1), so if we return the test passed.
 */
#define RUN_TEST(name) do { \
    kern_tests_run_++; \
    printf("  [RUN] %s\n", #name); \
    name(); \
    kern_tests_passed_++; \
    printf("  [PASS] %s\n", #name); \
} while (0)

/* ============================================================
 * Assertion Macros
 * ============================================================ */

/**
 * ASSERT(cond) - Assert that a condition is true.
 */
#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    ASSERT FAILED: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_TRUE(cond) - Assert condition is true (alias for ASSERT).
 */
#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    ASSERT_TRUE FAILED: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_FALSE(cond) - Assert condition is false.
 */
#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        fprintf(stderr, "    ASSERT_FALSE FAILED: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_EQ_STR(a, b) - Assert two strings are equal.
 */
#define ASSERT_EQ_STR(a, b) do { \
    const char *a_ = (a); \
    const char *b_ = (b); \
    if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                a_ ? a_ : "(null)", b_ ? b_ : "(null)", \
                __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_EQ_INT(a, b) - Assert two integers are equal.
 */
#define ASSERT_EQ_INT(a, b) do { \
    long long a_ = (long long)(a); \
    long long b_ = (long long)(b); \
    if (a_ != b_) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %lld != %lld (%s:%d)\n", \
                a_, b_, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_EQ_PTR(a, b) - Assert two pointers are equal.
 */
#define ASSERT_EQ_PTR(a, b) do { \
    const void *a_ = (const void *)(a); \
    const void *b_ = (const void *)(b); \
    if (a_ != b_) { \
        fprintf(stderr, "    ASSERT_EQ_PTR FAILED: %p != %p (%s:%d)\n", \
                a_, b_, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_NULL(ptr) - Assert pointer is NULL.
 */
#define ASSERT_NULL(ptr) do { \
    const void *p_ = (const void *)(ptr); \
    if (p_ != NULL) { \
        fprintf(stderr, "    ASSERT_NULL FAILED: %p is not NULL (%s:%d)\n", \
                p_, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/**
 * ASSERT_NOT_NULL(ptr) - Assert pointer is not NULL.
 */
#define ASSERT_NOT_NULL(ptr) do { \
    const void *p_ = (const void *)(ptr); \
    if (p_ == NULL) { \
        fprintf(stderr, "    ASSERT_NOT_NULL FAILED: pointer is NULL (%s:%d)\n", \
                __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ============================================================
 * Test Main Wrappers
 * ============================================================ */

/**
 * KERN_TEST_MAIN_BEGIN - Start a test main function.
 * Declares counters and initializes the test environment.
 * Usage:
 *   KERN_TEST_MAIN_BEGIN
 *     RUN_TEST(test_foo);
 *     RUN_TEST(test_bar);
 *   KERN_TEST_MAIN_END
 */
#define KERN_TEST_MAIN_BEGIN \
    int main(void) { \
        int kern_tests_run_ = 0; \
        int kern_tests_passed_ = 0; \
        kern_test_init();

/**
 * KERN_TEST_MAIN_END - End the test main function.
 * Prints summary and returns appropriate exit code.
 */
#define KERN_TEST_MAIN_END \
        kern_test_cleanup(); \
        printf("\n  %d/%d tests passed\n", kern_tests_passed_, kern_tests_run_); \
        return kern_tests_passed_ == kern_tests_run_ ? 0 : 1; \
    }

/**
 * kern_test_summary - Print test summary (standalone helper macro).
 * For use without KERN_TEST_MAIN_BEGIN/END.
 */
#define kern_test_summary(run, passed) \
    printf("\n  %d/%d tests passed\n", (passed), (run))

#ifdef __cplusplus
}
#endif

#endif /* KERN_TEST_H */
