/*
 * test_scheduler.c - Unit tests for kern_scheduler and cron parsing
 */

#include "kern.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
 * Cron Expression Matching Tests
 * ============================================================ */

TEST(test_cron_every_minute) {
    /* "* * * * *" should match any time */
    struct tm t = {0};
    t.tm_min = 30;
    t.tm_hour = 14;
    t.tm_mday = 15;
    t.tm_mon = 5;  /* June (0-based) */
    t.tm_wday = 3; /* Wednesday */
    ASSERT(kern_cron_matches("* * * * *", &t) == true);

    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_wday = 0;
    ASSERT(kern_cron_matches("* * * * *", &t) == true);
}

TEST(test_cron_specific_minute) {
    struct tm t = {0};
    t.tm_min = 30;
    t.tm_hour = 12;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_wday = 1;

    ASSERT(kern_cron_matches("30 * * * *", &t) == true);

    t.tm_min = 29;
    ASSERT(kern_cron_matches("30 * * * *", &t) == false);

    t.tm_min = 31;
    ASSERT(kern_cron_matches("30 * * * *", &t) == false);
}

TEST(test_cron_specific_hour) {
    struct tm t = {0};
    t.tm_min = 0;
    t.tm_hour = 3;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_wday = 1;

    ASSERT(kern_cron_matches("0 3 * * *", &t) == true);

    t.tm_hour = 4;
    ASSERT(kern_cron_matches("0 3 * * *", &t) == false);

    t.tm_hour = 3;
    t.tm_min = 1;
    ASSERT(kern_cron_matches("0 3 * * *", &t) == false);
}

TEST(test_cron_range) {
    struct tm t = {0};
    t.tm_hour = 12;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_wday = 1;

    /* Minutes 1-5 */
    t.tm_min = 1;
    ASSERT(kern_cron_matches("1-5 * * * *", &t) == true);

    t.tm_min = 3;
    ASSERT(kern_cron_matches("1-5 * * * *", &t) == true);

    t.tm_min = 5;
    ASSERT(kern_cron_matches("1-5 * * * *", &t) == true);

    t.tm_min = 0;
    ASSERT(kern_cron_matches("1-5 * * * *", &t) == false);

    t.tm_min = 6;
    ASSERT(kern_cron_matches("1-5 * * * *", &t) == false);
}

TEST(test_cron_step) {
    struct tm t = {0};
    t.tm_hour = 12;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_wday = 1;

    /* Every 15 minutes: *//*15 */
    t.tm_min = 0;
    ASSERT(kern_cron_matches("*/15 * * * *", &t) == true);

    t.tm_min = 15;
    ASSERT(kern_cron_matches("*/15 * * * *", &t) == true);

    t.tm_min = 30;
    ASSERT(kern_cron_matches("*/15 * * * *", &t) == true);

    t.tm_min = 45;
    ASSERT(kern_cron_matches("*/15 * * * *", &t) == true);

    t.tm_min = 10;
    ASSERT(kern_cron_matches("*/15 * * * *", &t) == false);

    t.tm_min = 14;
    ASSERT(kern_cron_matches("*/15 * * * *", &t) == false);
}

TEST(test_cron_range_with_step) {
    struct tm t = {0};
    t.tm_hour = 12;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_wday = 1;

    /* 1-30/5: minutes 1,6,11,16,21,26 */
    t.tm_min = 1;
    ASSERT(kern_cron_matches("1-30/5 * * * *", &t) == true);

    t.tm_min = 6;
    ASSERT(kern_cron_matches("1-30/5 * * * *", &t) == true);

    t.tm_min = 11;
    ASSERT(kern_cron_matches("1-30/5 * * * *", &t) == true);

    t.tm_min = 26;
    ASSERT(kern_cron_matches("1-30/5 * * * *", &t) == true);

    t.tm_min = 2;
    ASSERT(kern_cron_matches("1-30/5 * * * *", &t) == false);

    t.tm_min = 31;
    ASSERT(kern_cron_matches("1-30/5 * * * *", &t) == false);
}

TEST(test_cron_day_of_week) {
    struct tm t = {0};
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon = 0;

    /* Monday = 1 */
    t.tm_wday = 1;
    ASSERT(kern_cron_matches("* * * * 1", &t) == true);

    t.tm_wday = 2;
    ASSERT(kern_cron_matches("* * * * 1", &t) == false);

    /* Sunday = 0 */
    t.tm_wday = 0;
    ASSERT(kern_cron_matches("* * * * 0", &t) == true);

    /* Saturday = 6 */
    t.tm_wday = 6;
    ASSERT(kern_cron_matches("* * * * 6", &t) == true);
}

TEST(test_cron_day_of_month) {
    struct tm t = {0};
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mon = 0;
    t.tm_wday = 1;

    t.tm_mday = 15;
    ASSERT(kern_cron_matches("* * 15 * *", &t) == true);

    t.tm_mday = 16;
    ASSERT(kern_cron_matches("* * 15 * *", &t) == false);
}

TEST(test_cron_month) {
    struct tm t = {0};
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_wday = 1;

    /* January = tm_mon 0, cron month 1 */
    t.tm_mon = 0;
    ASSERT(kern_cron_matches("* * * 1 *", &t) == true);

    t.tm_mon = 1;
    ASSERT(kern_cron_matches("* * * 1 *", &t) == false);

    /* December = tm_mon 11, cron month 12 */
    t.tm_mon = 11;
    ASSERT(kern_cron_matches("* * * 12 *", &t) == true);
}

TEST(test_cron_combined) {
    /* "0 3 * * *" - every day at 3:00 AM */
    struct tm t = {0};
    t.tm_min = 0;
    t.tm_hour = 3;
    t.tm_mday = 15;
    t.tm_mon = 5;
    t.tm_wday = 2;
    ASSERT(kern_cron_matches("0 3 * * *", &t) == true);

    /* "30 9 1 * *" - 1st of every month at 9:30 */
    t.tm_min = 30;
    t.tm_hour = 9;
    t.tm_mday = 1;
    t.tm_mon = 3;
    t.tm_wday = 4;
    ASSERT(kern_cron_matches("30 9 1 * *", &t) == true);

    t.tm_mday = 2;
    ASSERT(kern_cron_matches("30 9 1 * *", &t) == false);
}

TEST(test_cron_null_safety) {
    struct tm t = {0};
    ASSERT(kern_cron_matches(NULL, &t) == false);
    ASSERT(kern_cron_matches("* * * * *", NULL) == false);
    ASSERT(kern_cron_matches(NULL, NULL) == false);
}

TEST(test_cron_invalid) {
    struct tm t = {0};
    /* Too few fields */
    ASSERT(kern_cron_matches("* * *", &t) == false);
    /* Empty string */
    ASSERT(kern_cron_matches("", &t) == false);
}

/* ============================================================
 * Scheduler Creation and Lifecycle Tests
 * ============================================================ */

TEST(test_scheduler_new_free) {
    kern_queue_t *q = kern_queue_new(1);
    ASSERT(q != NULL);

    kern_scheduler_t *sched = kern_scheduler_new(q);
    ASSERT(sched != NULL);

    kern_scheduler_free(sched);
    kern_queue_free(q);
}

TEST(test_scheduler_new_null_queue) {
    ASSERT(kern_scheduler_new(NULL) == NULL);
}

TEST(test_scheduler_add) {
    kern_queue_t *q = kern_queue_new(1);
    kern_scheduler_t *sched = kern_scheduler_new(q);
    ASSERT(sched != NULL);

    ASSERT(kern_scheduler_add(sched, "* * * * *", "test_job") == 0);
    ASSERT(kern_scheduler_add(sched, "0 3 * * *", "daily_job") == 0);
    ASSERT(kern_scheduler_add(sched, "*/15 * * * *", "frequent_job") == 0);

    kern_scheduler_free(sched);
    kern_queue_free(q);
}

TEST(test_scheduler_add_null_safety) {
    kern_queue_t *q = kern_queue_new(1);
    kern_scheduler_t *sched = kern_scheduler_new(q);

    ASSERT(kern_scheduler_add(NULL, "* * * * *", "job") == -1);
    ASSERT(kern_scheduler_add(sched, NULL, "job") == -1);
    ASSERT(kern_scheduler_add(sched, "* * * * *", NULL) == -1);

    kern_scheduler_free(sched);
    kern_queue_free(q);
}

TEST(test_scheduler_start_stop) {
    kern_queue_t *q = kern_queue_new(1);
    kern_scheduler_t *sched = kern_scheduler_new(q);
    ASSERT(sched != NULL);

    ASSERT(kern_scheduler_add(sched, "* * * * *", "test_job") == 0);
    ASSERT(kern_scheduler_start(sched) == 0);

    /* Should not be able to start twice */
    ASSERT(kern_scheduler_start(sched) == -1);

    kern_scheduler_stop(sched);
    kern_scheduler_free(sched);
    kern_queue_free(q);
}

TEST(test_scheduler_free_while_running) {
    kern_queue_t *q = kern_queue_new(1);
    kern_scheduler_t *sched = kern_scheduler_new(q);
    ASSERT(sched != NULL);

    ASSERT(kern_scheduler_start(sched) == 0);

    /* Free should stop and clean up */
    kern_scheduler_free(sched);
    kern_queue_free(q);
}

TEST(test_scheduler_null_safety) {
    kern_scheduler_free(NULL);
    kern_scheduler_stop(NULL);
    ASSERT(kern_scheduler_start(NULL) == -1);
}

int main(void) {
    printf("test_scheduler:\n");

    RUN_TEST(test_cron_every_minute);
    RUN_TEST(test_cron_specific_minute);
    RUN_TEST(test_cron_specific_hour);
    RUN_TEST(test_cron_range);
    RUN_TEST(test_cron_step);
    RUN_TEST(test_cron_range_with_step);
    RUN_TEST(test_cron_day_of_week);
    RUN_TEST(test_cron_day_of_month);
    RUN_TEST(test_cron_month);
    RUN_TEST(test_cron_combined);
    RUN_TEST(test_cron_null_safety);
    RUN_TEST(test_cron_invalid);
    RUN_TEST(test_scheduler_new_free);
    RUN_TEST(test_scheduler_new_null_queue);
    RUN_TEST(test_scheduler_add);
    RUN_TEST(test_scheduler_add_null_safety);
    RUN_TEST(test_scheduler_start_stop);
    RUN_TEST(test_scheduler_free_while_running);
    RUN_TEST(test_scheduler_null_safety);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
