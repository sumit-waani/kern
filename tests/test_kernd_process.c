/*
 * test_kernd_process.c - Unit tests for the process manager
 *
 * Tests starting and stopping a simple process (/bin/sleep).
 */

#include "kern.h"
#include "../kernd/kernd_process.h"
#include "../kernd/kernd_app_registry.h"
#include "../kernd/kernd_log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
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

TEST(test_process_init) {
    kernd_process_init();
    /* After init, no process should be tracked */
    const kernd_proc_info_t *info = kernd_process_status("nonexistent");
    ASSERT(info == NULL);
}

TEST(test_process_start_stop) {
    kernd_process_init();

    /* Create a simple app that sleeps */
    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("sleeper");
    app.start_cmd = strdup("sleep 60");
    app.port = 3001;

    /* Start the process - use /tmp as data_dir, create the app subdir */
    mkdir("/tmp/sleeper", 0755);

    pid_t pid = kernd_process_start(&app, "/tmp");
    ASSERT(pid > 0);

    /* Check status */
    const kernd_proc_info_t *info = kernd_process_status("sleeper");
    ASSERT(info != NULL);
    ASSERT_EQ_INT(info->status, KERND_PROC_RUNNING);
    ASSERT(info->pid > 0);

    /* Brief pause to let process start */
    usleep(50000);

    /* Verify child is alive */
    int kill_rc = kill(pid, 0);
    ASSERT_EQ_INT(kill_rc, 0);

    /* Stop the process */
    int rc = kernd_process_stop("sleeper");
    ASSERT_EQ_INT(rc, 0);

    /* Verify status changed */
    info = kernd_process_status("sleeper");
    ASSERT(info != NULL);
    ASSERT_EQ_INT(info->status, KERND_PROC_STOPPED);

    /* Verify process is gone */
    kill_rc = kill(pid, 0);
    ASSERT(kill_rc != 0);

    free(app.name);
    free(app.start_cmd);
    rmdir("/tmp/sleeper");
}

TEST(test_process_stop_nonexistent) {
    kernd_process_init();

    int rc = kernd_process_stop("nonexistent");
    ASSERT_EQ_INT(rc, -1);
}

TEST(test_process_start_null_safety) {
    kernd_process_init();

    pid_t pid = kernd_process_start(NULL, "/tmp");
    ASSERT_EQ_INT(pid, -1);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    pid = kernd_process_start(&app, "/tmp");
    ASSERT_EQ_INT(pid, -1);
}

TEST(test_process_reap) {
    kernd_process_init();

    /* Start a short-lived process */
    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("shortlived");
    app.start_cmd = strdup("true");
    app.port = 3002;

    mkdir("/tmp/shortlived", 0755);

    pid_t pid = kernd_process_start(&app, "/tmp");
    ASSERT(pid > 0);

    /* Wait for the process to finish */
    usleep(200000); /* 200ms should be plenty for "true" to complete */

    /* Reap should detect it exited */
    kernd_process_reap();

    const kernd_proc_info_t *info = kernd_process_status("shortlived");
    ASSERT(info != NULL);
    ASSERT_EQ_INT(info->status, KERND_PROC_STOPPED);

    free(app.name);
    free(app.start_cmd);
    rmdir("/tmp/shortlived");
}

TEST(test_process_double_start) {
    kernd_process_init();

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("doublestart");
    app.start_cmd = strdup("sleep 60");
    app.port = 3003;

    mkdir("/tmp/doublestart", 0755);

    pid_t pid1 = kernd_process_start(&app, "/tmp");
    ASSERT(pid1 > 0);

    /* Starting again should return the existing PID */
    pid_t pid2 = kernd_process_start(&app, "/tmp");
    ASSERT_EQ_INT(pid1, pid2);

    /* Cleanup */
    kernd_process_stop("doublestart");

    free(app.name);
    free(app.start_cmd);
    rmdir("/tmp/doublestart");
}

int main(void) {
    printf("test_kernd_process:\n");

    RUN_TEST(test_process_init);
    RUN_TEST(test_process_start_stop);
    RUN_TEST(test_process_stop_nonexistent);
    RUN_TEST(test_process_start_null_safety);
    RUN_TEST(test_process_reap);
    RUN_TEST(test_process_double_start);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
