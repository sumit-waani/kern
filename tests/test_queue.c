/*
 * test_queue.c - Unit tests for kern_queue
 */

#include "kern.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define ASSERT_EQ_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                (a), (b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ============================================================
 * Shared state for test handlers
 * ============================================================ */

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_processed_count = 0;
static char g_last_arg[256] = {0};
static int g_retry_attempts = 0;

static void reset_globals(void) {
    pthread_mutex_lock(&g_mutex);
    g_processed_count = 0;
    g_last_arg[0] = '\0';
    g_retry_attempts = 0;
    pthread_mutex_unlock(&g_mutex);
}

/* ============================================================
 * Test Handlers
 * ============================================================ */

static kern_queue_result_t handler_ok(const kern_job_t *job) {
    pthread_mutex_lock(&g_mutex);
    g_processed_count++;
    const void *arg = kern_job_arg(job);
    if (arg) {
        size_t sz = kern_job_arg_size(job);
        size_t copy = sz < sizeof(g_last_arg) - 1 ? sz : sizeof(g_last_arg) - 1;
        memcpy(g_last_arg, arg, copy);
        g_last_arg[copy] = '\0';
    }
    pthread_mutex_unlock(&g_mutex);
    return KERN_QUEUE_OK;
}

static kern_queue_result_t handler_fail(const kern_job_t *job) {
    (void)job;
    return KERN_QUEUE_FAIL;
}

static kern_queue_result_t handler_retry_then_ok(const kern_job_t *job) {
    int attempt = kern_job_attempt(job);
    pthread_mutex_lock(&g_mutex);
    g_retry_attempts = attempt;
    pthread_mutex_unlock(&g_mutex);

    if (attempt < 3) {
        return KERN_QUEUE_RETRY;
    }
    pthread_mutex_lock(&g_mutex);
    g_processed_count++;
    pthread_mutex_unlock(&g_mutex);
    return KERN_QUEUE_OK;
}

static kern_queue_result_t handler_always_retry(const kern_job_t *job) {
    int attempt = kern_job_attempt(job);
    pthread_mutex_lock(&g_mutex);
    g_retry_attempts = attempt;
    pthread_mutex_unlock(&g_mutex);
    return KERN_QUEUE_RETRY;
}

/* ============================================================
 * Tests
 * ============================================================ */

TEST(test_queue_new_free) {
    kern_queue_t *q = kern_queue_new(2);
    ASSERT(q != NULL);
    kern_queue_free(q);
}

TEST(test_queue_new_invalid) {
    ASSERT(kern_queue_new(0) == NULL);
    ASSERT(kern_queue_new(-1) == NULL);
}

TEST(test_queue_basic_dispatch) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(2);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "test_job", handler_ok) == 0);
    ASSERT(kern_queue_start(q) == 0);

    const char *msg = "hello";
    ASSERT(kern_queue_dispatch(q, "test_job", msg, strlen(msg) + 1) == 0);

    /* Wait for processing */
    usleep(100000);  /* 100ms */

    pthread_mutex_lock(&g_mutex);
    ASSERT_EQ_INT(g_processed_count, 1);
    ASSERT_EQ_STR(g_last_arg, "hello");
    pthread_mutex_unlock(&g_mutex);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_multiple_jobs) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(4);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "count_job", handler_ok) == 0);
    ASSERT(kern_queue_start(q) == 0);

    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "job_%d", i);
        ASSERT(kern_queue_dispatch(q, "count_job", buf, strlen(buf) + 1) == 0);
    }

    /* Wait for all jobs to process */
    usleep(200000);  /* 200ms */

    pthread_mutex_lock(&g_mutex);
    ASSERT_EQ_INT(g_processed_count, 10);
    pthread_mutex_unlock(&g_mutex);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_fail_goes_to_dead_letter) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "fail_job", handler_fail) == 0);
    ASSERT(kern_queue_start(q) == 0);

    ASSERT(kern_queue_dispatch(q, "fail_job", NULL, 0) == 0);

    usleep(100000);  /* 100ms */

    ASSERT_EQ_INT(kern_queue_failed_count(q), 1);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_retry_then_succeed) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "retry_job", handler_retry_then_ok) == 0);
    ASSERT(kern_queue_start(q) == 0);

    ASSERT(kern_queue_dispatch(q, "retry_job", NULL, 0) == 0);

    /* Wait for retries (1s + 2s backoff, but use shorter waits for test) */
    /* The handler succeeds on attempt 3, so it sleeps 1s + 2s = 3s total */
    usleep(3500000);  /* 3.5s */

    pthread_mutex_lock(&g_mutex);
    ASSERT_EQ_INT(g_processed_count, 1);
    ASSERT_EQ_INT(g_retry_attempts, 3);
    pthread_mutex_unlock(&g_mutex);

    ASSERT_EQ_INT(kern_queue_failed_count(q), 0);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_max_retries_dead_letter) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "always_retry", handler_always_retry) == 0);
    ASSERT(kern_queue_start(q) == 0);

    ASSERT(kern_queue_dispatch(q, "always_retry", NULL, 0) == 0);

    /* Max 5 attempts: backoff = 1 + 2 + 4 + 8 = 15s total.
     * Wait a bit longer to be safe. */
    usleep(16000000);  /* 16s */

    ASSERT_EQ_INT(kern_queue_failed_count(q), 1);

    pthread_mutex_lock(&g_mutex);
    ASSERT_EQ_INT(g_retry_attempts, 5);
    pthread_mutex_unlock(&g_mutex);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_failed_clear) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "fail_job", handler_fail) == 0);
    ASSERT(kern_queue_start(q) == 0);

    ASSERT(kern_queue_dispatch(q, "fail_job", NULL, 0) == 0);
    ASSERT(kern_queue_dispatch(q, "fail_job", NULL, 0) == 0);

    usleep(200000);  /* 200ms */

    ASSERT_EQ_INT(kern_queue_failed_count(q), 2);

    kern_queue_failed_clear(q);
    ASSERT_EQ_INT(kern_queue_failed_count(q), 0);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_unregistered_job_dead_letter) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_start(q) == 0);

    /* Dispatch a job with no registered handler */
    ASSERT(kern_queue_dispatch(q, "unknown_job", NULL, 0) == 0);

    usleep(100000);  /* 100ms */

    ASSERT_EQ_INT(kern_queue_failed_count(q), 1);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_null_safety) {
    /* NULL queue operations should not crash */
    kern_queue_free(NULL);
    kern_queue_stop(NULL);
    kern_queue_failed_clear(NULL);

    ASSERT(kern_queue_new(0) == NULL);
    ASSERT(kern_queue_start(NULL) == -1);
    ASSERT(kern_queue_register(NULL, "x", handler_ok) == -1);
    ASSERT(kern_queue_dispatch(NULL, "x", NULL, 0) == -1);
    ASSERT_EQ_INT(kern_queue_failed_count(NULL), 0);

    /* NULL job accessors */
    ASSERT(kern_job_name(NULL) == NULL);
    ASSERT(kern_job_arg(NULL) == NULL);
    ASSERT_EQ_INT(kern_job_arg_size(NULL), 0);
    ASSERT_EQ_INT(kern_job_attempt(NULL), 0);
}

TEST(test_queue_job_accessors) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "test_job", handler_ok) == 0);
    ASSERT(kern_queue_start(q) == 0);

    const char *data = "test_data";
    ASSERT(kern_queue_dispatch(q, "test_job", data, strlen(data) + 1) == 0);

    usleep(100000);  /* 100ms */

    /* If handler_ok ran, the arg was accessible */
    pthread_mutex_lock(&g_mutex);
    ASSERT_EQ_INT(g_processed_count, 1);
    ASSERT_EQ_STR(g_last_arg, "test_data");
    pthread_mutex_unlock(&g_mutex);

    kern_queue_stop(q);
    kern_queue_free(q);
}

TEST(test_queue_dispatch_null_arg) {
    reset_globals();

    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    ASSERT(kern_queue_register(q, "null_arg_job", handler_ok) == 0);
    ASSERT(kern_queue_start(q) == 0);

    ASSERT(kern_queue_dispatch(q, "null_arg_job", NULL, 0) == 0);

    usleep(100000);  /* 100ms */

    pthread_mutex_lock(&g_mutex);
    ASSERT_EQ_INT(g_processed_count, 1);
    pthread_mutex_unlock(&g_mutex);

    kern_queue_stop(q);
    kern_queue_free(q);
}

int main(void) {
    printf("test_queue:\n");

    RUN_TEST(test_queue_new_free);
    RUN_TEST(test_queue_new_invalid);
    RUN_TEST(test_queue_basic_dispatch);
    RUN_TEST(test_queue_multiple_jobs);
    RUN_TEST(test_queue_fail_goes_to_dead_letter);
    RUN_TEST(test_queue_retry_then_succeed);
    RUN_TEST(test_queue_max_retries_dead_letter);
    RUN_TEST(test_queue_failed_clear);
    RUN_TEST(test_queue_unregistered_job_dead_letter);
    RUN_TEST(test_queue_null_safety);
    RUN_TEST(test_queue_job_accessors);
    RUN_TEST(test_queue_dispatch_null_arg);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
