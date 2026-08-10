/*
 * test_migrate.c - Unit tests for kern_migrate (migration runner)
 */

#include "kern.h"

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

#define ASSERT_EQ_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                (a), (b), __FILE__, __LINE__); \
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

static char tmp_dir[256];

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fputs(content, f);
    fclose(f);
}

static void setup_migrations(void) {
    /* Create a temp directory for migrations */
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/kern_migrate_test_XXXXXX");
    char *result = mkdtemp(tmp_dir);
    ASSERT(result != NULL);

    /* Create single-file migration */
    char path[512];
    snprintf(path, sizeof(path), "%s/001_create_users.sql", tmp_dir);
    write_file(path, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT);");

    /* Create directory-style migration */
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/002_create_posts", tmp_dir);
    mkdir(dir_path, 0755);

    snprintf(path, sizeof(path), "%s/up.sql", dir_path);
    write_file(path, "CREATE TABLE posts (id INTEGER PRIMARY KEY, title TEXT, user_id INTEGER);");

    snprintf(path, sizeof(path), "%s/down.sql", dir_path);
    write_file(path, "DROP TABLE posts;");
}

static void cleanup_dir(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    (void)system(cmd);
}

TEST(test_migrate_init) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    int rc = kern_migrate_init(db);
    ASSERT(rc >= 0);

    /* Verify the table was created */
    kern_db_result_t *res = kern_db_query(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='kern_migrations'");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    kern_db_result_free(res);

    kern_db_close(db);
}

TEST(test_migrate_up) {
    setup_migrations();

    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    int applied = kern_migrate_up(db, tmp_dir);
    ASSERT_EQ_INT(applied, 2);

    /* Verify tables were created */
    kern_db_result_t *res = kern_db_query(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='users'");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    kern_db_result_free(res);

    res = kern_db_query(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='posts'");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    kern_db_result_free(res);

    /* Running again should apply 0 new migrations */
    applied = kern_migrate_up(db, tmp_dir);
    ASSERT_EQ_INT(applied, 0);

    kern_db_close(db);
    cleanup_dir(tmp_dir);
}

TEST(test_migrate_status) {
    setup_migrations();

    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    /* Before running migrations, all should be pending */
    kern_db_result_t *status = kern_migrate_status(db, tmp_dir);
    ASSERT(status != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(status), 2);
    ASSERT_EQ_STR(kern_db_row_str(status, 0, 1), "pending");
    ASSERT_EQ_STR(kern_db_row_str(status, 1, 1), "pending");
    kern_db_result_free(status);

    /* Run migrations */
    kern_migrate_up(db, tmp_dir);

    /* After running, all should be applied */
    status = kern_migrate_status(db, tmp_dir);
    ASSERT(status != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(status), 2);
    ASSERT_EQ_STR(kern_db_row_str(status, 0, 1), "applied");
    ASSERT_EQ_STR(kern_db_row_str(status, 1, 1), "applied");
    kern_db_result_free(status);

    kern_db_close(db);
    cleanup_dir(tmp_dir);
}

TEST(test_migrate_down) {
    setup_migrations();

    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    /* Apply all migrations */
    kern_migrate_up(db, tmp_dir);

    /* Roll back the last one (002_create_posts) */
    int rc = kern_migrate_down(db, tmp_dir);
    ASSERT_EQ_INT(rc, 1);

    /* posts table should be gone */
    kern_db_result_t *res = kern_db_query(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='posts'");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 0);
    kern_db_result_free(res);

    /* users table should still exist */
    res = kern_db_query(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='users'");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    kern_db_result_free(res);

    kern_db_close(db);
    cleanup_dir(tmp_dir);
}

int main(void) {
    printf("test_migrate:\n");

    RUN_TEST(test_migrate_init);
    RUN_TEST(test_migrate_up);
    RUN_TEST(test_migrate_status);
    RUN_TEST(test_migrate_down);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
