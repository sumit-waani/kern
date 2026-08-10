/*
 * test_test_framework.c - Tests for the kern test framework itself
 *
 * Verifies that assertion macros work correctly and that the
 * test lifecycle (init/cleanup) functions properly.
 */

#include "kern_test.h"

#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

/* Use inline macros for this test since we are testing the framework itself
 * and cannot use KERN_TEST_MAIN_BEGIN/END (which relies on the counters
 * being declared inside main). We test the individual components. */

#define RUN(name) do { \
    tests_run++; \
    printf("  [RUN] %s\n", #name); \
    name(); \
    tests_passed++; \
    printf("  [PASS] %s\n", #name); \
} while (0)

/* ============================================================
 * Test: ASSERT_TRUE / ASSERT_FALSE
 * ============================================================ */
TEST(test_assert_true_false)
{
    ASSERT_TRUE(1);
    ASSERT_TRUE(42);
    ASSERT_TRUE(1 == 1);
    ASSERT_FALSE(0);
    ASSERT_FALSE(1 == 2);
}

/* ============================================================
 * Test: ASSERT_EQ_STR
 * ============================================================ */
TEST(test_assert_eq_str)
{
    ASSERT_EQ_STR("hello", "hello");
    ASSERT_EQ_STR("", "");
    ASSERT_EQ_STR("kern", "kern");

    /* Test with variables */
    const char *a = "test";
    const char *b = "test";
    ASSERT_EQ_STR(a, b);
}

/* ============================================================
 * Test: ASSERT_EQ_INT
 * ============================================================ */
TEST(test_assert_eq_int)
{
    ASSERT_EQ_INT(0, 0);
    ASSERT_EQ_INT(42, 42);
    ASSERT_EQ_INT(-1, -1);
    ASSERT_EQ_INT(1 + 1, 2);

    int x = 100;
    ASSERT_EQ_INT(x, 100);
}

/* ============================================================
 * Test: ASSERT_EQ_PTR
 * ============================================================ */
TEST(test_assert_eq_ptr)
{
    int x = 0;
    int *p = &x;
    ASSERT_EQ_PTR(p, &x);
    ASSERT_EQ_PTR(NULL, NULL);
}

/* ============================================================
 * Test: ASSERT_NULL / ASSERT_NOT_NULL
 * ============================================================ */
TEST(test_assert_null_not_null)
{
    ASSERT_NULL(NULL);
    int x = 0;
    ASSERT_NOT_NULL(&x);

    const char *s = "hello";
    ASSERT_NOT_NULL(s);
}

/* ============================================================
 * Test: kern_test_init / kern_test_cleanup lifecycle
 * ============================================================ */
TEST(test_lifecycle)
{
    /* kern_test_init was already called, so db_path should be set */
    const char *path = kern_test_db_path();
    ASSERT_NOT_NULL(path);

    /* Path should contain our PID */
    char expected_fragment[64];
    snprintf(expected_fragment, sizeof(expected_fragment), "%d", (int)getpid());
    ASSERT_NOT_NULL(strstr(path, expected_fragment));

    /* Path should start with /tmp/ */
    ASSERT_EQ_INT(strncmp(path, "/tmp/", 5), 0);
}

/* ============================================================
 * Test: kern_test_init is idempotent
 * ============================================================ */
TEST(test_init_idempotent)
{
    const char *path1 = kern_test_db_path();
    ASSERT_NOT_NULL(path1);

    /* Calling init again should not change anything */
    kern_test_init();
    const char *path2 = kern_test_db_path();
    ASSERT_NOT_NULL(path2);
    ASSERT_EQ_STR(path1, path2);
}

/* ============================================================
 * Test: kern_test_cleanup and re-init
 * ============================================================ */
TEST(test_cleanup_and_reinit)
{
    /* Get current path */
    const char *path = kern_test_db_path();
    ASSERT_NOT_NULL(path);

    /* Create a file at that path to verify cleanup removes it */
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "test");
        fclose(f);
    }

    /* Cleanup should remove it */
    kern_test_cleanup();
    ASSERT_NULL(kern_test_db_path());

    struct stat st;
    ASSERT_EQ_INT(stat(path, &st), -1);

    /* Re-init for remaining tests */
    kern_test_init();
    ASSERT_NOT_NULL(kern_test_db_path());
}

/* ============================================================
 * Test: Test summary macro
 * ============================================================ */
TEST(test_summary_output)
{
    /* Just verify it compiles and runs without crashing */
    /* Redirect to /dev/null would be complex, just call it */
    kern_test_summary(5, 5);
    kern_test_summary(10, 8);
}

int main(void)
{
    printf("test_test_framework:\n");

    kern_test_init();

    RUN(test_assert_true_false);
    RUN(test_assert_eq_str);
    RUN(test_assert_eq_int);
    RUN(test_assert_eq_ptr);
    RUN(test_assert_null_not_null);
    RUN(test_lifecycle);
    RUN(test_init_idempotent);
    RUN(test_cleanup_and_reinit);
    RUN(test_summary_output);

    kern_test_cleanup();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
