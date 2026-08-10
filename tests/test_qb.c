/*
 * test_qb.c - Unit tests for kern_qb (query builder)
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

TEST(test_qb_select_basic) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_qb_t *qb = kern_qb_new(db);
    ASSERT(qb != NULL);

    kern_qb_select(qb, "*");
    kern_qb_from(qb, "users");

    const char *sql = kern_qb_build(qb);
    ASSERT_EQ_STR(sql, "SELECT * FROM users");

    kern_qb_free(qb);
    kern_db_close(db);
}

TEST(test_qb_select_where_order_limit) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_select(qb, "id, name, email");
    kern_qb_from(qb, "users");
    kern_qb_where_str(qb, "status", "=", "active");
    kern_qb_where_int(qb, "age", ">", 18);
    kern_qb_order(qb, "name", "ASC");
    kern_qb_limit(qb, 10);
    kern_qb_offset(qb, 5);

    const char *sql = kern_qb_build(qb);
    ASSERT_EQ_STR(sql,
        "SELECT id, name, email FROM users WHERE status = ? AND age > ? ORDER BY name ASC LIMIT 10 OFFSET 5");

    kern_qb_free(qb);
    kern_db_close(db);
}

TEST(test_qb_insert) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_insert(qb, "posts");
    kern_qb_set_str(qb, "title", "Hello World");
    kern_qb_set_str(qb, "slug", "hello-world");
    kern_qb_set_int(qb, "author_id", 1);

    const char *sql = kern_qb_build(qb);
    ASSERT_EQ_STR(sql, "INSERT INTO posts (title, slug, author_id) VALUES (?, ?, ?)");

    kern_qb_free(qb);
    kern_db_close(db);
}

TEST(test_qb_update) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_update(qb, "users");
    kern_qb_set_str(qb, "name", "New Name");
    kern_qb_set_int(qb, "age", 31);
    kern_qb_where_int(qb, "id", "=", 5);

    const char *sql = kern_qb_build(qb);
    ASSERT_EQ_STR(sql, "UPDATE users SET name = ?, age = ? WHERE id = ?");

    kern_qb_free(qb);
    kern_db_close(db);
}

TEST(test_qb_delete) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_delete(qb, "sessions");
    kern_qb_where_str(qb, "token", "=", "abc123");

    const char *sql = kern_qb_build(qb);
    ASSERT_EQ_STR(sql, "DELETE FROM sessions WHERE token = ?");

    kern_qb_free(qb);
    kern_db_close(db);
}

TEST(test_qb_exec_insert) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, qty INTEGER)");

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_insert(qb, "items");
    kern_qb_set_str(qb, "name", "Gadget");
    kern_qb_set_int(qb, "qty", 42);
    int rc = kern_qb_exec(qb);
    ASSERT(rc >= 0);
    kern_qb_free(qb);

    /* Verify with a query */
    kern_db_result_t *res = kern_db_query(db, "SELECT name, qty FROM items");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    ASSERT_EQ_STR(kern_db_row_str(res, 0, 0), "Gadget");
    ASSERT_EQ_INT(kern_db_row_int(res, 0, 1), 42);
    kern_db_result_free(res);

    kern_db_close(db);
}

TEST(test_qb_query_select) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Alice', 30)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Bob', 20)");
    kern_db_exec(db, "INSERT INTO users (name, age) VALUES ('Charlie', 35)");

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_select(qb, "name");
    kern_qb_from(qb, "users");
    kern_qb_where_int(qb, "age", ">=", 30);
    kern_qb_order(qb, "name", "ASC");

    kern_db_result_t *res = kern_qb_query(qb);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 2);
    ASSERT_EQ_STR(kern_db_row_str(res, 0, 0), "Alice");
    ASSERT_EQ_STR(kern_db_row_str(res, 1, 0), "Charlie");
    kern_db_result_free(res);

    kern_qb_free(qb);
    kern_db_close(db);
}

TEST(test_qb_one) {
    kern_db_t *db = kern_db_open(":memory:");
    ASSERT(db != NULL);

    kern_db_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
    kern_db_exec(db, "INSERT INTO users (name) VALUES ('Alice')");
    kern_db_exec(db, "INSERT INTO users (name) VALUES ('Bob')");

    kern_qb_t *qb = kern_qb_new(db);
    kern_qb_select(qb, "name");
    kern_qb_from(qb, "users");
    kern_qb_order(qb, "name", "ASC");

    kern_db_result_t *res = kern_qb_one(qb);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_db_result_row_count(res), 1);
    ASSERT_EQ_STR(kern_db_row_str(res, 0, 0), "Alice");
    kern_db_result_free(res);

    kern_qb_free(qb);
    kern_db_close(db);
}

int main(void) {
    printf("test_qb:\n");

    RUN_TEST(test_qb_select_basic);
    RUN_TEST(test_qb_select_where_order_limit);
    RUN_TEST(test_qb_insert);
    RUN_TEST(test_qb_update);
    RUN_TEST(test_qb_delete);
    RUN_TEST(test_qb_exec_insert);
    RUN_TEST(test_qb_query_select);
    RUN_TEST(test_qb_one);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
