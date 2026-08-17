/*
 * test_kernd_proxy.c - Unit tests for the reverse proxy vhost table
 *
 * Tests the vhost table operations (add, lookup, remove) without
 * performing actual network operations.
 */

#include "kern.h"
#include "../kernd/kernd_proxy.h"

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

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %lld != %lld (%s:%d)\n", \
                (long long)(a), (long long)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ============================================================ */

TEST(test_proxy_init) {
    int rc = kernd_proxy_init();
    ASSERT_EQ_INT(rc, 0);

    /* Lookup on empty table returns -1 */
    int port = kernd_proxy_lookup("example.com");
    ASSERT_EQ_INT(port, -1);

    kernd_proxy_stop();
}

TEST(test_proxy_add_vhost) {
    kernd_proxy_init();

    int rc = kernd_proxy_add_vhost("app1.example.com", 3001);
    ASSERT_EQ_INT(rc, 0);

    rc = kernd_proxy_add_vhost("app2.example.com", 3002);
    ASSERT_EQ_INT(rc, 0);

    int port1 = kernd_proxy_lookup("app1.example.com");
    ASSERT_EQ_INT(port1, 3001);

    int port2 = kernd_proxy_lookup("app2.example.com");
    ASSERT_EQ_INT(port2, 3002);

    kernd_proxy_stop();
}

TEST(test_proxy_lookup_unknown) {
    kernd_proxy_init();

    kernd_proxy_add_vhost("known.example.com", 3001);

    int port = kernd_proxy_lookup("unknown.example.com");
    ASSERT_EQ_INT(port, -1);

    kernd_proxy_stop();
}

TEST(test_proxy_remove_vhost) {
    kernd_proxy_init();

    kernd_proxy_add_vhost("app1.example.com", 3001);
    kernd_proxy_add_vhost("app2.example.com", 3002);

    /* Remove first vhost */
    int rc = kernd_proxy_remove_vhost("app1.example.com");
    ASSERT_EQ_INT(rc, 0);

    /* Should no longer be found */
    int port = kernd_proxy_lookup("app1.example.com");
    ASSERT_EQ_INT(port, -1);

    /* Second vhost still exists */
    port = kernd_proxy_lookup("app2.example.com");
    ASSERT_EQ_INT(port, 3002);

    kernd_proxy_stop();
}

TEST(test_proxy_remove_nonexistent) {
    kernd_proxy_init();

    int rc = kernd_proxy_remove_vhost("does-not-exist.com");
    ASSERT_EQ_INT(rc, -1);

    kernd_proxy_stop();
}

TEST(test_proxy_overwrite_vhost) {
    kernd_proxy_init();

    kernd_proxy_add_vhost("app.example.com", 3001);
    int port = kernd_proxy_lookup("app.example.com");
    ASSERT_EQ_INT(port, 3001);

    /* Overwrite with new port */
    kernd_proxy_add_vhost("app.example.com", 4001);
    port = kernd_proxy_lookup("app.example.com");
    ASSERT_EQ_INT(port, 4001);

    kernd_proxy_stop();
}

TEST(test_proxy_null_safety) {
    kernd_proxy_init();

    int rc = kernd_proxy_add_vhost(NULL, 3001);
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_proxy_add_vhost("host.com", 0);
    ASSERT_EQ_INT(rc, -1);

    int port = kernd_proxy_lookup(NULL);
    ASSERT_EQ_INT(port, -1);

    rc = kernd_proxy_remove_vhost(NULL);
    ASSERT_EQ_INT(rc, -1);

    kernd_proxy_stop();
}

int main(void) {
    printf("test_kernd_proxy:\n");

    RUN_TEST(test_proxy_init);
    RUN_TEST(test_proxy_add_vhost);
    RUN_TEST(test_proxy_lookup_unknown);
    RUN_TEST(test_proxy_remove_vhost);
    RUN_TEST(test_proxy_remove_nonexistent);
    RUN_TEST(test_proxy_overwrite_vhost);
    RUN_TEST(test_proxy_null_safety);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
