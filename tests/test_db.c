/*
 * test_db.c - Unit tests for kern_db (SQLite wrapper)
 */

#include "kern.h"

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

TEST(test_db_open_close) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);
    kern_db_close(db);
}

TEST(test_db_exec_create_table) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    int rc = kern_db_exec(db,
        "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)");
    ASSERT(rc >= 0);

    kern_db_close(db);
}

TEST(test_db_insert_and_query) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Alice', 30)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Bob', 25)");

    kern_db_result_t *res = kern_db_query(db, "SELECT name, age FROM users ORDER BY name");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 2);
    ASSERT_EQ_INT(kern_db_result_col_count(res), 2);

    ASSERT_EQ_STR(kern_db_row_str(res, 0, 0), "Alice");
    ASSERT_EQ_INT(kern_db_row_int(res, 0, 1), 30);
    ASSERT_EQ_STR(kern_db_row_str(res, 1, 0), "Bob");
    ASSERT_EQ_INT(kern_db_row_int(res, 1, 1), 25);

    kern_db_result_free(res);
    kern_db_close(db);
}

TEST(test_db_parameterized_exec) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT, price INTEGER)");

    kern_db_param_t params[2];
    params[0].type = KERN_DB_PARAM_STR;
    params[0].value.s = "Widget";
    params[1].type = KERN_DB_PARAM_INT;
    params[1].value.i = 999;

    int rc = kern_db_exec_params(db,
        "INSERT INTO items (title, price) VALUES (?, ?)", params, 2);
    ASSERT(rc >= 0);

    kern_db_result_t *res = kern_db_query(db, "SELECT title, price FROM items");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    ASSERT_EQ_STR(kern_db_row_str(res, 0, 0), "Widget");
    ASSERT_EQ_INT(kern_db_row_int(res, 0, 1), 999);

    kern_db_result_free(res);
    kern_db_close(db);
}

TEST(test_db_parameterized_query) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Alice', 30)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Bob', 25)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Charlie', 35)");

    kern_db_param_t params[1];
    params[0].type = KERN_DB_PARAM_INT;
    params[0].value.i = 28;

    kern_db_result_t *res = kern_db_query_params(db,
        "SELECT name FROM users WHERE age > ? ORDER BY name", params, 1);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 2);
    ASSERT_EQ_STR(kern_db_row_str(res, 0, 0), "Alice");
    ASSERT_EQ_STR(kern_db_row_str(res, 1, 0), "Charlie");

    kern_db_result_free(res);
    kern_db_close(db);
}

TEST(test_db_transaction_commit) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE counter (val INTEGER)");
    kern_db_exec(db, "INSERT INTO counter (val) VALUES (0)");

    kern_db_begin(db);
    kern_db_exec(db, "UPDATE counter SET val = 42");
    kern_db_commit(db);

    kern_db_result_t *res = kern_db_query(db, "SELECT val FROM counter");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_row_int(res, 0, 0), 42);

    kern_db_result_free(res);
    kern_db_close(db);
}

TEST(test_db_transaction_rollback) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE counter (val INTEGER)");
    kern_db_exec(db, "INSERT INTO counter (val) VALUES (0)");

    kern_db_begin(db);
    kern_db_exec(db, "UPDATE counter SET val = 99");
    kern_db_rollback(db);

    kern_db_result_t *res = kern_db_query(db, "SELECT val FROM counter");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_row_int(res, 0, 0), 0);

    kern_db_result_free(res);
    kern_db_close(db);
}

TEST(test_db_sql_injection_prevention) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
    kern_db_exec(db, "INSERT INTO users (name) VALUES ('Alice')");

    /* Attempt SQL injection via parameter binding - should be safe */
    kern_db_param_t params[1];
    params[0].type = KERN_DB_PARAM_STR;
    params[0].value.s = "'; DROP TABLE users; --";

    kern_db_result_t *res = kern_db_query_params(db,
        "SELECT name FROM users WHERE name = ?", params, 1);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 0);
    kern_db_result_free(res);

    /* Table should still exist */
    res = kern_db_query(db, "SELECT COUNT(*) FROM users");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_row_int(res, 0, 0), 1);
    kern_db_result_free(res);

    kern_db_close(db);
}

TEST(test_db_col_names) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE t (foo TEXT, bar INTEGER)");
    kern_db_exec(db, "INSERT INTO t (foo, bar) VALUES ('x', 1)");

    kern_db_result_t *res = kern_db_query(db, "SELECT foo, bar FROM t");
    ASSERT(res != NULL);
    ASSERT_EQ_STR(kern_db_result_col_name(res, 0), "foo");
    ASSERT_EQ_STR(kern_db_result_col_name(res, 1), "bar");

    kern_db_result_free(res);
    kern_db_close(db);
}

int main(void) {
    printf("test_db:\n");

    RUN_TEST(test_db_open_close);
    RUN_TEST(test_db_exec_create_table);
    RUN_TEST(test_db_insert_and_query);
    RUN_TEST(test_db_parameterized_exec);
    RUN_TEST(test_db_parameterized_query);
    RUN_TEST(test_db_transaction_commit);
    RUN_TEST(test_db_transaction_rollback);
    RUN_TEST(test_db_sql_injection_prevention);
    RUN_TEST(test_db_col_names);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
