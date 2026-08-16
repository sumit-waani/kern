/*
 * test_rate_limit.c - Unit tests for kern_rate_limit (token bucket)
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
    long long a_ = (long long)(a); \
    long long b_ = (long long)(b); \
    if (a_ != b_) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %lld != %lld (%s:%d)\n", \
                a_, b_, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ============================================================
 * Tests for basic creation and destruction
 * ============================================================ */

TEST(test_create_and_free) {
    kern_rate_limiter_t *rl = kern_rate_limiter_new(10.0, 1.0);
    ASSERT(rl != NULL);
    kern_rate_limiter_free(rl);
}

TEST(test_free_null_safe) {
    kern_rate_limiter_free(NULL); /* should not crash */
}

/* ============================================================
 * Tests for token consumption
 * ============================================================ */

TEST(test_allows_up_to_max_tokens) {
    kern_rate_limiter_t *rl = kern_rate_limiter_new(5.0, 1.0);
    ASSERT(rl != NULL);

    /* Should allow 5 requests (max_tokens = 5) */
    for (int i = 0; i < 5; i++) {
        ASSERT(kern_rate_limiter_check(rl, "192.168.1.1") == true);
    }

    /* 6th request should be denied */
    ASSERT(kern_rate_limiter_check(rl, "192.168.1.1") == false);

    kern_rate_limiter_free(rl);
}

TEST(test_different_keys_independent) {
    kern_rate_limiter_t *rl = kern_rate_limiter_new(2.0, 1.0);
    ASSERT(rl != NULL);

    /* Exhaust tokens for key A */
    ASSERT(kern_rate_limiter_check(rl, "keyA") == true);
    ASSERT(kern_rate_limiter_check(rl, "keyA") == true);
    ASSERT(kern_rate_limiter_check(rl, "keyA") == false);

    /* Key B should still have tokens */
    ASSERT(kern_rate_limiter_check(rl, "keyB") == true);
    ASSERT(kern_rate_limiter_check(rl, "keyB") == true);
    ASSERT(kern_rate_limiter_check(rl, "keyB") == false);

    kern_rate_limiter_free(rl);
}

TEST(test_null_safety) {
    /* NULL limiter */
    ASSERT(kern_rate_limiter_check(NULL, "key") == false);

    /* NULL key */
    kern_rate_limiter_t *rl = kern_rate_limiter_new(5.0, 1.0);
    ASSERT(kern_rate_limiter_check(rl, NULL) == false);
    kern_rate_limiter_free(rl);
}

/* ============================================================
 * Tests for token refill
 * ============================================================ */

TEST(test_tokens_refill_over_time) {
    /* High refill rate for quick test: 100 tokens/sec */
    kern_rate_limiter_t *rl = kern_rate_limiter_new(2.0, 100.0);
    ASSERT(rl != NULL);

    /* Exhaust tokens */
    ASSERT(kern_rate_limiter_check(rl, "ip1") == true);
    ASSERT(kern_rate_limiter_check(rl, "ip1") == true);
    ASSERT(kern_rate_limiter_check(rl, "ip1") == false);

    /* Wait 50ms - should refill ~5 tokens (capped at 2) */
    struct timespec ts = {0, 50000000}; /* 50ms */
    nanosleep(&ts, NULL);

    /* Should be allowed again */
    ASSERT(kern_rate_limiter_check(rl, "ip1") == true);

    kern_rate_limiter_free(rl);
}

/* ============================================================
 * Tests for kern_rate_limit_check (429 response)
 * ============================================================ */

TEST(test_rate_limit_check_allows) {
    kern_rate_limiter_t *rl = kern_rate_limiter_new(3.0, 1.0);
    ASSERT(rl != NULL);

    /* First request should be allowed (returns NULL) */
    kern_response_t *res = kern_rate_limit_check(rl, "client1");
    ASSERT(res == NULL);

    kern_rate_limiter_free(rl);
}

TEST(test_rate_limit_check_returns_429) {
    kern_rate_limiter_t *rl = kern_rate_limiter_new(2.0, 1.0);
    ASSERT(rl != NULL);

    /* Exhaust tokens */
    kern_response_t *res = kern_rate_limit_check(rl, "client1");
    ASSERT(res == NULL);
    res = kern_rate_limit_check(rl, "client1");
    ASSERT(res == NULL);

    /* Third request should be limited */
    res = kern_rate_limit_check(rl, "client1");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 429);
    kern_response_free(res);

    kern_rate_limiter_free(rl);
}

TEST(test_rate_limit_check_null_safety) {
    /* NULL limiter returns NULL (allowed - safe default) */
    kern_response_t *res = kern_rate_limit_check(NULL, "key");
    ASSERT(res == NULL);

    /* NULL key returns NULL (allowed - safe default) */
    kern_rate_limiter_t *rl = kern_rate_limiter_new(5.0, 1.0);
    res = kern_rate_limit_check(rl, NULL);
    ASSERT(res == NULL);
    kern_rate_limiter_free(rl);
}

/* ============================================================
 * Tests for cleanup
 * ============================================================ */

TEST(test_cleanup_null_safe) {
    kern_rate_limiter_cleanup(NULL); /* should not crash */
}

TEST(test_cleanup_empty) {
    kern_rate_limiter_t *rl = kern_rate_limiter_new(5.0, 1.0);
    kern_rate_limiter_cleanup(rl); /* should not crash */
    kern_rate_limiter_free(rl);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("test_rate_limit:\n");

    RUN_TEST(test_create_and_free);
    RUN_TEST(test_free_null_safe);
    RUN_TEST(test_allows_up_to_max_tokens);
    RUN_TEST(test_different_keys_independent);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_tokens_refill_over_time);
    RUN_TEST(test_rate_limit_check_allows);
    RUN_TEST(test_rate_limit_check_returns_429);
    RUN_TEST(test_rate_limit_check_null_safety);
    RUN_TEST(test_cleanup_null_safe);
    RUN_TEST(test_cleanup_empty);

    printf("  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
