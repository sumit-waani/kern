/*
 * test_buf.c - Unit tests for kern_buf_t
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
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %d != %d (%s:%d)\n", \
                (int)(a), (int)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

TEST(test_buf_new) {
    kern_buf_t *buf = kern_buf_new(64);
    ASSERT(buf != NULL);
    ASSERT_EQ_INT(kern_buf_len(buf), 0);
    ASSERT(kern_buf_data(buf) != NULL);
    ASSERT_EQ_STR(kern_buf_data(buf), "");
    kern_buf_free(buf);
}

TEST(test_buf_write) {
    kern_buf_t *buf = kern_buf_new(16);
    ASSERT(buf != NULL);

    int rc = kern_buf_write(buf, "hello", 5);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_INT(kern_buf_len(buf), 5);
    ASSERT_EQ_STR(kern_buf_data(buf), "hello");

    rc = kern_buf_write(buf, " world", 6);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_INT(kern_buf_len(buf), 11);
    ASSERT_EQ_STR(kern_buf_data(buf), "hello world");

    kern_buf_free(buf);
}

TEST(test_buf_writes) {
    kern_buf_t *buf = kern_buf_new(16);
    ASSERT(buf != NULL);

    int rc = kern_buf_writes(buf, "hello");
    ASSERT_EQ_INT(rc, 0);
    rc = kern_buf_writes(buf, " ");
    ASSERT_EQ_INT(rc, 0);
    rc = kern_buf_writes(buf, "world");
    ASSERT_EQ_INT(rc, 0);

    ASSERT_EQ_STR(kern_buf_data(buf), "hello world");
    kern_buf_free(buf);
}

TEST(test_buf_writef) {
    kern_buf_t *buf = kern_buf_new(16);
    ASSERT(buf != NULL);

    int rc = kern_buf_writef(buf, "Hello %s, you are %d", "Alice", 30);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "Hello Alice, you are 30");

    kern_buf_free(buf);
}

TEST(test_buf_reset) {
    kern_buf_t *buf = kern_buf_new(16);
    ASSERT(buf != NULL);

    kern_buf_writes(buf, "some data here");
    ASSERT(kern_buf_len(buf) > 0);

    kern_buf_reset(buf);
    ASSERT_EQ_INT(kern_buf_len(buf), 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "");

    /* Can still write after reset */
    kern_buf_writes(buf, "new data");
    ASSERT_EQ_STR(kern_buf_data(buf), "new data");

    kern_buf_free(buf);
}

TEST(test_buf_grow) {
    kern_buf_t *buf = kern_buf_new(4);
    ASSERT(buf != NULL);

    /* Write more than initial capacity */
    for (int i = 0; i < 100; i++) {
        int rc = kern_buf_writes(buf, "abcdefghij");
        ASSERT_EQ_INT(rc, 0);
    }

    ASSERT_EQ_INT(kern_buf_len(buf), 1000);
    kern_buf_free(buf);
}

TEST(test_buf_null_safety) {
    ASSERT_EQ_INT(kern_buf_len(NULL), 0);
    ASSERT(kern_buf_data(NULL) == NULL);
    kern_buf_free(NULL); /* Should not crash */
    kern_buf_reset(NULL); /* Should not crash */
}

int main(void) {
    printf("test_buf:\n");

    RUN_TEST(test_buf_new);
    RUN_TEST(test_buf_write);
    RUN_TEST(test_buf_writes);
    RUN_TEST(test_buf_writef);
    RUN_TEST(test_buf_reset);
    RUN_TEST(test_buf_grow);
    RUN_TEST(test_buf_null_safety);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
