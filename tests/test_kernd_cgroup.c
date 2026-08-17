/*
 * test_kernd_cgroup.c - Unit tests for cgroup v2 management
 *
 * Tests graceful fallback behavior when cgroups are not available,
 * and verifies parameter validation.
 */

#include "kern.h"
#include "../kernd/kernd_cgroup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %lld != %lld (%s:%d)\n", \
                (long long)(a), (long long)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ============================================================ */

TEST(test_cgroup_init_graceful) {
    /*
     * kernd_cgroup_init() should either succeed (if we have permissions)
     * or return -1 gracefully (if not root / no cgroup2 fs).
     * Either way it should not crash.
     */
    int rc = kernd_cgroup_init();
    /* rc is either 0 (available) or -1 (not available) */
    ASSERT(rc == 0 || rc == -1);
}

TEST(test_cgroup_available_matches_init) {
    int rc = kernd_cgroup_init();
    bool avail = kernd_cgroup_available();

    if (rc == 0) {
        ASSERT(avail == true);
    } else {
        ASSERT(avail == false);
    }
}

TEST(test_cgroup_create_when_unavailable) {
    /*
     * If cgroups are not available, create should return -1.
     * If they ARE available (running as root in container), test passes too.
     */
    kernd_cgroup_init();

    if (!kernd_cgroup_available()) {
        int rc = kernd_cgroup_create("test_app", 50, 256, 100);
        ASSERT_EQ_INT(rc, -1);
    }
    /* If available, we skip this test assertion - just verify no crash */
}

TEST(test_cgroup_assign_when_unavailable) {
    kernd_cgroup_init();

    if (!kernd_cgroup_available()) {
        int rc = kernd_cgroup_assign("test_app", 12345);
        ASSERT_EQ_INT(rc, -1);
    }
}

TEST(test_cgroup_destroy_when_unavailable) {
    kernd_cgroup_init();

    if (!kernd_cgroup_available()) {
        int rc = kernd_cgroup_destroy("test_app");
        ASSERT_EQ_INT(rc, -1);
    }
}

TEST(test_cgroup_null_params) {
    kernd_cgroup_init();

    int rc = kernd_cgroup_create(NULL, 50, 256, 100);
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_cgroup_assign(NULL, 1234);
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_cgroup_destroy(NULL);
    ASSERT_EQ_INT(rc, -1);
}

TEST(test_cgroup_invalid_limits) {
    kernd_cgroup_init();

    /* Even if cgroups are available, invalid limits should fail */
    if (kernd_cgroup_available()) {
        int rc = kernd_cgroup_create("bad_app", 0, 256, 100);
        ASSERT_EQ_INT(rc, -1);

        rc = kernd_cgroup_create("bad_app", 101, 256, 100);
        ASSERT_EQ_INT(rc, -1);

        rc = kernd_cgroup_create("bad_app", 50, 0, 100);
        ASSERT_EQ_INT(rc, -1);

        rc = kernd_cgroup_create("bad_app", 50, 256, 0);
        ASSERT_EQ_INT(rc, -1);
    }
}

TEST(test_cgroup_assign_invalid_pid) {
    kernd_cgroup_init();

    if (kernd_cgroup_available()) {
        int rc = kernd_cgroup_assign("test_app", 0);
        ASSERT_EQ_INT(rc, -1);

        rc = kernd_cgroup_assign("test_app", -1);
        ASSERT_EQ_INT(rc, -1);
    }
}

int main(void) {
    printf("test_kernd_cgroup:\n");

    RUN_TEST(test_cgroup_init_graceful);
    RUN_TEST(test_cgroup_available_matches_init);
    RUN_TEST(test_cgroup_create_when_unavailable);
    RUN_TEST(test_cgroup_assign_when_unavailable);
    RUN_TEST(test_cgroup_destroy_when_unavailable);
    RUN_TEST(test_cgroup_null_params);
    RUN_TEST(test_cgroup_invalid_limits);
    RUN_TEST(test_cgroup_assign_invalid_pid);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
