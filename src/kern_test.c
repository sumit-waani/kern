/*
 * kern_test.c - Test framework runtime
 *
 * Provides test environment initialization and cleanup, including
 * temporary database creation for test isolation.
 */

#include "kern_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Path to the temporary test database */
static char test_db_path[256] = {0};
static int test_initialized = 0;

void kern_test_init(void)
{
    if (test_initialized) {
        return;
    }

    /* Create a unique temporary database file for this test process */
    snprintf(test_db_path, sizeof(test_db_path),
             "/tmp/kern_test_%d.db", (int)getpid());

    /* Remove any stale file from a previous run */
    unlink(test_db_path);

    test_initialized = 1;
}

void kern_test_cleanup(void)
{
    if (!test_initialized) {
        return;
    }

    /* Remove the temporary database */
    if (test_db_path[0] != '\0') {
        unlink(test_db_path);

        /* Also remove WAL and SHM files if they exist */
        char wal_path[272];
        char shm_path[272];
        snprintf(wal_path, sizeof(wal_path), "%s-wal", test_db_path);
        snprintf(shm_path, sizeof(shm_path), "%s-shm", test_db_path);
        unlink(wal_path);
        unlink(shm_path);
    }

    test_db_path[0] = '\0';
    test_initialized = 0;
}

const char *kern_test_db_path(void)
{
    if (!test_initialized) {
        return NULL;
    }
    return test_db_path;
}
