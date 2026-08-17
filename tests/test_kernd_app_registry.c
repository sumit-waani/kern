/*
 * test_kernd_app_registry.c - Unit tests for the app registry CRUD
 *
 * Tests against an in-memory SQLite database.
 */

#include "kern.h"
#include "../kernd/kernd_app_registry.h"
#include "../kernd/kernd_log.h"

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

/* ============================================================ */

TEST(test_registry_init) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    int rc = kernd_registry_init(db);
    ASSERT_EQ_INT(rc, 0);

    /* Calling init again should be idempotent */
    rc = kernd_registry_init(db);
    ASSERT_EQ_INT(rc, 0);

    kern_db_close(db);
}

TEST(test_registry_init_null) {
    int rc = kernd_registry_init(NULL);
    ASSERT_EQ_INT(rc, -1);
}

TEST(test_app_add) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("myapp");
    app.repo_url = strdup("https://github.com/user/myapp.git");
    app.branch = strdup("main");
    app.build_cmd = strdup("make");
    app.start_cmd = strdup("./build/myapp");
    app.port = 0; /* auto-assign */

    int rc = kernd_app_add(db, &app);
    ASSERT_EQ_INT(rc, 0);

    /* Port should have been auto-assigned */
    ASSERT_EQ_INT(app.port, 3001);

    /* Status should be set */
    ASSERT(app.status != NULL);
    ASSERT_EQ_STR(app.status, "created");

    /* Timestamps should be set */
    ASSERT(app.created_at != NULL);
    ASSERT(app.updated_at != NULL);

    free(app.name);
    free(app.repo_url);
    free(app.branch);
    free(app.build_cmd);
    free(app.start_cmd);
    free(app.domain);
    free(app.status);
    free(app.created_at);
    free(app.updated_at);
    kern_db_close(db);
}

TEST(test_app_add_auto_port_increment) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    /* Add first app */
    kernd_app_t app1;
    memset(&app1, 0, sizeof(app1));
    app1.name = strdup("app1");
    app1.repo_url = strdup("https://github.com/user/app1.git");
    app1.start_cmd = strdup("./app1");
    kernd_app_add(db, &app1);
    ASSERT_EQ_INT(app1.port, 3001);

    /* Add second app */
    kernd_app_t app2;
    memset(&app2, 0, sizeof(app2));
    app2.name = strdup("app2");
    app2.repo_url = strdup("https://github.com/user/app2.git");
    app2.start_cmd = strdup("./app2");
    kernd_app_add(db, &app2);
    ASSERT_EQ_INT(app2.port, 3002);

    free(app1.name); free(app1.repo_url); free(app1.branch);
    free(app1.build_cmd); free(app1.start_cmd); free(app1.domain);
    free(app1.status); free(app1.created_at); free(app1.updated_at);
    free(app2.name); free(app2.repo_url); free(app2.branch);
    free(app2.build_cmd); free(app2.start_cmd); free(app2.domain);
    free(app2.status); free(app2.created_at); free(app2.updated_at);
    kern_db_close(db);
}

TEST(test_app_find_by_name) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("findme");
    app.repo_url = strdup("https://github.com/user/findme.git");
    app.branch = strdup("develop");
    app.build_cmd = strdup("cargo build");
    app.start_cmd = strdup("./target/release/findme");
    app.domain = strdup("findme.example.com");
    kernd_app_add(db, &app);

    /* Find the app */
    kernd_app_t *found = kernd_app_find_by_name(db, "findme");
    ASSERT(found != NULL);
    ASSERT_EQ_STR(found->name, "findme");
    ASSERT_EQ_STR(found->repo_url, "https://github.com/user/findme.git");
    ASSERT_EQ_STR(found->branch, "develop");
    ASSERT_EQ_STR(found->build_cmd, "cargo build");
    ASSERT_EQ_STR(found->start_cmd, "./target/release/findme");
    ASSERT_EQ_STR(found->domain, "findme.example.com");
    ASSERT_EQ_INT(found->port, 3001);
    ASSERT_EQ_STR(found->status, "created");
    kernd_app_free(found);

    /* Not found */
    found = kernd_app_find_by_name(db, "nonexistent");
    ASSERT(found == NULL);

    free(app.name); free(app.repo_url); free(app.branch);
    free(app.build_cmd); free(app.start_cmd); free(app.domain);
    free(app.status); free(app.created_at); free(app.updated_at);
    kern_db_close(db);
}

TEST(test_app_list) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    /* Empty list */
    kern_db_result_t *res = kernd_app_list(db);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 0);
    kern_db_result_free(res);

    /* Add two apps */
    kernd_app_t a1;
    memset(&a1, 0, sizeof(a1));
    a1.name = strdup("alpha");
    a1.repo_url = strdup("https://github.com/user/alpha.git");
    a1.start_cmd = strdup("./alpha");
    kernd_app_add(db, &a1);

    kernd_app_t a2;
    memset(&a2, 0, sizeof(a2));
    a2.name = strdup("beta");
    a2.repo_url = strdup("https://github.com/user/beta.git");
    a2.start_cmd = strdup("./beta");
    kernd_app_add(db, &a2);

    res = kernd_app_list(db);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 2);

    /* Ordered by name */
    ASSERT_EQ_STR(kern_db_row_str(res, 0, 1), "alpha");
    ASSERT_EQ_STR(kern_db_row_str(res, 1, 1), "beta");
    kern_db_result_free(res);

    free(a1.name); free(a1.repo_url); free(a1.branch);
    free(a1.build_cmd); free(a1.start_cmd); free(a1.domain);
    free(a1.status); free(a1.created_at); free(a1.updated_at);
    free(a2.name); free(a2.repo_url); free(a2.branch);
    free(a2.build_cmd); free(a2.start_cmd); free(a2.domain);
    free(a2.status); free(a2.created_at); free(a2.updated_at);
    kern_db_close(db);
}

TEST(test_app_update_status) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("statusapp");
    app.repo_url = strdup("https://github.com/user/statusapp.git");
    app.start_cmd = strdup("./statusapp");
    kernd_app_add(db, &app);

    /* Update status */
    int rc = kernd_app_update_status(db, "statusapp", "running");
    ASSERT_EQ_INT(rc, 0);

    /* Verify */
    kernd_app_t *found = kernd_app_find_by_name(db, "statusapp");
    ASSERT(found != NULL);
    ASSERT_EQ_STR(found->status, "running");
    kernd_app_free(found);

    free(app.name); free(app.repo_url); free(app.branch);
    free(app.build_cmd); free(app.start_cmd); free(app.domain);
    free(app.status); free(app.created_at); free(app.updated_at);
    kern_db_close(db);
}

TEST(test_app_remove) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("removeapp");
    app.repo_url = strdup("https://github.com/user/removeapp.git");
    app.start_cmd = strdup("./removeapp");
    kernd_app_add(db, &app);

    /* Verify it exists */
    kernd_app_t *found = kernd_app_find_by_name(db, "removeapp");
    ASSERT(found != NULL);
    kernd_app_free(found);

    /* Remove it */
    int rc = kernd_app_remove(db, "removeapp");
    ASSERT_EQ_INT(rc, 0);

    /* Verify it's gone */
    found = kernd_app_find_by_name(db, "removeapp");
    ASSERT(found == NULL);

    free(app.name); free(app.repo_url); free(app.branch);
    free(app.build_cmd); free(app.start_cmd); free(app.domain);
    free(app.status); free(app.created_at); free(app.updated_at);
    kern_db_close(db);
}

TEST(test_app_add_duplicate_name) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup("dup");
    app.repo_url = strdup("https://github.com/user/dup.git");
    app.start_cmd = strdup("./dup");
    int rc = kernd_app_add(db, &app);
    ASSERT_EQ_INT(rc, 0);

    /* Second add with same name should fail */
    kernd_app_t app2;
    memset(&app2, 0, sizeof(app2));
    app2.name = strdup("dup");
    app2.repo_url = strdup("https://github.com/user/dup2.git");
    app2.start_cmd = strdup("./dup2");
    rc = kernd_app_add(db, &app2);
    ASSERT_EQ_INT(rc, -1);

    free(app.name); free(app.repo_url); free(app.branch);
    free(app.build_cmd); free(app.start_cmd); free(app.domain);
    free(app.status); free(app.created_at); free(app.updated_at);
    free(app2.name); free(app2.repo_url); free(app2.branch);
    free(app2.build_cmd); free(app2.start_cmd); free(app2.domain);
    free(app2.status); free(app2.created_at); free(app2.updated_at);
    kern_db_close(db);
}

TEST(test_app_add_null_safety) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kernd_registry_init(db);

    ASSERT_EQ_INT(kernd_app_add(NULL, NULL), -1);
    ASSERT_EQ_INT(kernd_app_add(db, NULL), -1);

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    /* Missing required fields */
    ASSERT_EQ_INT(kernd_app_add(db, &app), -1);

    kern_db_close(db);
}

int main(void) {
    printf("test_kernd_app_registry:\n");

    RUN_TEST(test_registry_init);
    RUN_TEST(test_registry_init_null);
    RUN_TEST(test_app_add);
    RUN_TEST(test_app_add_auto_port_increment);
    RUN_TEST(test_app_find_by_name);
    RUN_TEST(test_app_list);
    RUN_TEST(test_app_update_status);
    RUN_TEST(test_app_remove);
    RUN_TEST(test_app_add_duplicate_name);
    RUN_TEST(test_app_add_null_safety);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
