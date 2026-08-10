/*
 * test_app.c - Unit tests for kern_app (application bootstrap)
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  [RUN] %s\n", #name); \
    name(); \
    tests_passed++; \
    printf("  [PASS] %s\n", #name); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    ASSERT FAILED: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)
#define ASSERT_EQ_STR(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_EQ_INT(a, b) ASSERT((a) == (b))

/* ============================================================
 * Tests
 * ============================================================ */

TEST(test_app_new_basic) {
    kern_app_t *app = kern_app_new("test_app");
    ASSERT_NOT_NULL(app);
    ASSERT_EQ_STR(kern_app_name(app), "test_app");
    kern_app_free(app);
}

TEST(test_app_new_null_name) {
    kern_app_t *app = kern_app_new(NULL);
    ASSERT_NOT_NULL(app);
    ASSERT_EQ_STR(kern_app_name(app), "kern_app");
    kern_app_free(app);
}

TEST(test_app_set_port) {
    kern_app_t *app = kern_app_new("myapp");
    ASSERT_NOT_NULL(app);

    kern_app_listen(app, 8080);
    ASSERT_EQ_INT(kern_app_port(app), 8080);

    kern_app_listen(app, 4000);
    ASSERT_EQ_INT(kern_app_port(app), 4000);

    kern_app_free(app);
}

TEST(test_app_default_port) {
    kern_app_t *app = kern_app_new("myapp");
    ASSERT_NOT_NULL(app);
    /* Default port is 3000 (unless kern.toml overrides it) */
    int port = kern_app_port(app);
    ASSERT(port > 0);
    kern_app_free(app);
}

TEST(test_app_router) {
    kern_app_t *app = kern_app_new("myapp");
    ASSERT_NOT_NULL(app);
    ASSERT_NOT_NULL(kern_app_router(app));
    kern_app_free(app);
}

TEST(test_app_free_null) {
    /* Should not crash */
    kern_app_free(NULL);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("test_app:\n");

    RUN_TEST(test_app_new_basic);
    RUN_TEST(test_app_new_null_name);
    RUN_TEST(test_app_set_port);
    RUN_TEST(test_app_default_port);
    RUN_TEST(test_app_router);
    RUN_TEST(test_app_free_null);

    printf("\n  %d/%d tests passed\n\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
