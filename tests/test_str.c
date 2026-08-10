/*
 * test_str.c - Unit tests for kern string utilities
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

TEST(test_str_from_cstr) {
    kern_str_t s = kern_str("hello");
    ASSERT_EQ_INT(s.len, 5);
    ASSERT(memcmp(s.data, "hello", 5) == 0);

    kern_str_t empty = kern_str("");
    ASSERT_EQ_INT(empty.len, 0);

    kern_str_t null_str = kern_str(NULL);
    ASSERT(null_str.data == NULL);
    ASSERT_EQ_INT(null_str.len, 0);
}

TEST(test_str_dup) {
    kern_str_t s = kern_str("hello world");
    char *copy = kern_str_dup(s);
    ASSERT(copy != NULL);
    ASSERT_EQ_STR(copy, "hello world");
    /* Ensure it's a separate allocation */
    ASSERT(copy != s.data);
    free(copy);

    /* Null data */
    kern_str_t null_str = {NULL, 0};
    ASSERT(kern_str_dup(null_str) == NULL);
}

TEST(test_str_eq) {
    kern_str_t a = kern_str("hello");
    kern_str_t b = kern_str("hello");
    kern_str_t c = kern_str("world");
    kern_str_t d = kern_str("hell");

    ASSERT(kern_str_eq(a, b));
    ASSERT(!kern_str_eq(a, c));
    ASSERT(!kern_str_eq(a, d));

    kern_str_t empty1 = kern_str("");
    kern_str_t empty2 = kern_str("");
    ASSERT(kern_str_eq(empty1, empty2));
}

TEST(test_str_starts_with) {
    kern_str_t s = kern_str("/api/users");
    kern_str_t prefix1 = kern_str("/api");
    kern_str_t prefix2 = kern_str("/api/users");
    kern_str_t prefix3 = kern_str("/other");
    kern_str_t empty = kern_str("");

    ASSERT(kern_str_starts_with(s, prefix1));
    ASSERT(kern_str_starts_with(s, prefix2));
    ASSERT(!kern_str_starts_with(s, prefix3));
    ASSERT(kern_str_starts_with(s, empty));  /* Empty prefix always matches */
}

TEST(test_str_split) {
    kern_str_t s = kern_str("one/two/three");
    kern_str_t parts[10];
    size_t count = 0;

    kern_str_split(s, '/', parts, 10, &count);
    ASSERT_EQ_INT(count, 3);
    ASSERT(kern_str_eq(parts[0], kern_str("one")));
    ASSERT(kern_str_eq(parts[1], kern_str("two")));
    ASSERT(kern_str_eq(parts[2], kern_str("three")));

    /* Split with limited parts */
    kern_str_split(s, '/', parts, 2, &count);
    ASSERT_EQ_INT(count, 2);
    ASSERT(kern_str_eq(parts[0], kern_str("one")));
    ASSERT(kern_str_eq(parts[1], kern_str("two")));
}

TEST(test_str_split_empty_parts) {
    kern_str_t s = kern_str("a,,b");
    kern_str_t parts[10];
    size_t count = 0;

    kern_str_split(s, ',', parts, 10, &count);
    ASSERT_EQ_INT(count, 3);
    ASSERT(kern_str_eq(parts[0], kern_str("a")));
    ASSERT_EQ_INT(parts[1].len, 0);
    ASSERT(kern_str_eq(parts[2], kern_str("b")));
}

TEST(test_str_trim) {
    kern_str_t s1 = kern_str("  hello  ");
    kern_str_t trimmed = kern_str_trim(s1);
    ASSERT(kern_str_eq(trimmed, kern_str("hello")));

    kern_str_t s2 = kern_str("\t\n  spaces \r\n ");
    trimmed = kern_str_trim(s2);
    ASSERT(kern_str_eq(trimmed, kern_str("spaces")));

    kern_str_t s3 = kern_str("no-trim");
    trimmed = kern_str_trim(s3);
    ASSERT(kern_str_eq(trimmed, kern_str("no-trim")));

    kern_str_t s4 = kern_str("   ");
    trimmed = kern_str_trim(s4);
    ASSERT_EQ_INT(trimmed.len, 0);
}

TEST(test_url_encode) {
    kern_buf_t *buf = kern_buf_new(64);
    ASSERT(buf != NULL);

    kern_str_t s = kern_str("hello world&foo=bar");
    int rc = kern_url_encode(s, buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "hello%20world%26foo%3Dbar");

    kern_buf_reset(buf);

    /* Unreserved characters should pass through */
    kern_str_t s2 = kern_str("ABCabc123-_.~");
    rc = kern_url_encode(s2, buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "ABCabc123-_.~");

    kern_buf_free(buf);
}

TEST(test_url_decode) {
    kern_buf_t *buf = kern_buf_new(64);
    ASSERT(buf != NULL);

    kern_str_t s = kern_str("hello%20world%26foo%3Dbar");
    int rc = kern_url_decode(s, buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "hello world&foo=bar");

    kern_buf_reset(buf);

    /* Plus sign decodes to space */
    kern_str_t s2 = kern_str("hello+world");
    rc = kern_url_decode(s2, buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "hello world");

    kern_buf_free(buf);
}

int main(void) {
    printf("test_str:\n");

    RUN_TEST(test_str_from_cstr);
    RUN_TEST(test_str_dup);
    RUN_TEST(test_str_eq);
    RUN_TEST(test_str_starts_with);
    RUN_TEST(test_str_split);
    RUN_TEST(test_str_split_empty_parts);
    RUN_TEST(test_str_trim);
    RUN_TEST(test_url_encode);
    RUN_TEST(test_url_decode);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
