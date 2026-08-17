/*
 * test_kernd_backup.c - Unit tests for the backup system
 *
 * Tests backup creation and pruning using temporary directories.
 */

#include "kern.h"
#include "../kernd/kernd_backup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <utime.h>

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

/* Helper to create a temporary directory */
static void setup_temp_dirs(char *data_dir, char *backup_dir, char *app_dir) {
    snprintf(data_dir, 256, "/tmp/kernd_test_data_XXXXXX");
    ASSERT(mkdtemp(data_dir) != NULL);

    snprintf(backup_dir, 256, "/tmp/kernd_test_backup_XXXXXX");
    ASSERT(mkdtemp(backup_dir) != NULL);

    snprintf(app_dir, 512, "%s/testapp", data_dir);
    ASSERT(mkdir(app_dir, 0755) == 0);

    /* Create a small file in the app directory */
    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/index.c", app_dir);
    FILE *f = fopen(file_path, "w");
    ASSERT(f != NULL);
    fprintf(f, "#include <stdio.h>\nint main() { return 0; }\n");
    fclose(f);
}

/* Helper to count .tar.gz files in a directory */
static int count_tarballs(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len >= 7 && strcmp(entry->d_name + len - 7, ".tar.gz") == 0) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

/* Helper to remove directory tree */
static void cleanup_dir(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    system(cmd);
}

/* ============================================================ */

TEST(test_backup_run_creates_tarball) {
    char data_dir[256], backup_dir[256], app_dir[512];
    setup_temp_dirs(data_dir, backup_dir, app_dir);

    int rc = kernd_backup_run("testapp", data_dir, backup_dir);
    ASSERT_EQ_INT(rc, 0);

    /* Verify tarball was created */
    char app_backup_dir[512];
    snprintf(app_backup_dir, sizeof(app_backup_dir), "%s/testapp", backup_dir);

    int count = count_tarballs(app_backup_dir);
    ASSERT(count == 1);

    cleanup_dir(data_dir);
    cleanup_dir(backup_dir);
}

TEST(test_backup_run_null_params) {
    int rc = kernd_backup_run(NULL, "/tmp", "/tmp");
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_backup_run("app", NULL, "/tmp");
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_backup_run("app", "/tmp", NULL);
    ASSERT_EQ_INT(rc, -1);
}

TEST(test_backup_run_missing_source) {
    char backup_dir[256];
    snprintf(backup_dir, sizeof(backup_dir), "/tmp/kernd_test_bk_XXXXXX");
    ASSERT(mkdtemp(backup_dir) != NULL);

    int rc = kernd_backup_run("nonexistent_app", "/tmp/no_such_dir_xyz", backup_dir);
    ASSERT_EQ_INT(rc, -1);

    cleanup_dir(backup_dir);
}

TEST(test_backup_prune_removes_old) {
    char data_dir[256], backup_dir[256], app_dir[512];
    setup_temp_dirs(data_dir, backup_dir, app_dir);

    /* Create a backup */
    int rc = kernd_backup_run("testapp", data_dir, backup_dir);
    ASSERT_EQ_INT(rc, 0);

    /* Manually age the backup file */
    char app_backup_dir[512];
    snprintf(app_backup_dir, sizeof(app_backup_dir), "%s/testapp", backup_dir);

    DIR *dir = opendir(app_backup_dir);
    ASSERT(dir != NULL);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len >= 7 && strcmp(entry->d_name + len - 7, ".tar.gz") == 0) {
            char file_path[1024];
            snprintf(file_path, sizeof(file_path), "%s/%s", app_backup_dir, entry->d_name);
            /* Set modification time to 10 days ago */
            struct utimbuf times;
            times.actime = time(NULL) - (10 * 24 * 60 * 60);
            times.modtime = time(NULL) - (10 * 24 * 60 * 60);
            utime(file_path, &times);
        }
    }
    closedir(dir);

    /* Prune backups older than 7 days */
    rc = kernd_backup_prune("testapp", backup_dir, 7);
    ASSERT_EQ_INT(rc, 0);

    /* Should have been removed */
    int count = count_tarballs(app_backup_dir);
    ASSERT_EQ_INT(count, 0);

    cleanup_dir(data_dir);
    cleanup_dir(backup_dir);
}

TEST(test_backup_prune_keeps_recent) {
    char data_dir[256], backup_dir[256], app_dir[512];
    setup_temp_dirs(data_dir, backup_dir, app_dir);

    /* Create a backup (will be fresh/recent) */
    int rc = kernd_backup_run("testapp", data_dir, backup_dir);
    ASSERT_EQ_INT(rc, 0);

    /* Prune backups older than 7 days (recent backup should survive) */
    rc = kernd_backup_prune("testapp", backup_dir, 7);
    ASSERT_EQ_INT(rc, 0);

    char app_backup_dir[512];
    snprintf(app_backup_dir, sizeof(app_backup_dir), "%s/testapp", backup_dir);
    int count = count_tarballs(app_backup_dir);
    ASSERT_EQ_INT(count, 1);

    cleanup_dir(data_dir);
    cleanup_dir(backup_dir);
}

TEST(test_backup_prune_null_params) {
    int rc = kernd_backup_prune(NULL, "/tmp", 7);
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_backup_prune("app", NULL, 7);
    ASSERT_EQ_INT(rc, -1);

    rc = kernd_backup_prune("app", "/tmp", -1);
    ASSERT_EQ_INT(rc, -1);
}

int main(void) {
    printf("test_kernd_backup:\n");

    RUN_TEST(test_backup_run_creates_tarball);
    RUN_TEST(test_backup_run_null_params);
    RUN_TEST(test_backup_run_missing_source);
    RUN_TEST(test_backup_prune_removes_old);
    RUN_TEST(test_backup_prune_keeps_recent);
    RUN_TEST(test_backup_prune_null_params);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
