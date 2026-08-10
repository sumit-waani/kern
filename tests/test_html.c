/*
 * test_html.c - Unit tests for kern_html_escape
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

TEST(test_escape_lt_gt) {
    kern_buf_t *buf = kern_buf_new(64);
    kern_html_escape(buf, "<script>alert('xss')</script>");
    ASSERT_EQ_STR(kern_buf_data(buf), "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;");
    kern_buf_free(buf);
}

TEST(test_escape_ampersand) {
    kern_buf_t *buf = kern_buf_new(64);
    kern_html_escape(buf, "foo & bar");
    ASSERT_EQ_STR(kern_buf_data(buf), "foo &amp; bar");
    kern_buf_free(buf);
}

TEST(test_escape_quotes) {
    kern_buf_t *buf = kern_buf_new(64);
    kern_html_escape(buf, "he said \"hello\"");
    ASSERT_EQ_STR(kern_buf_data(buf), "he said &quot;hello&quot;");
    kern_buf_free(buf);
}

TEST(test_escape_single_quotes) {
    kern_buf_t *buf = kern_buf_new(64);
    kern_html_escape(buf, "it's");
    ASSERT_EQ_STR(kern_buf_data(buf), "it&#39;s");
    kern_buf_free(buf);
}

TEST(test_escape_normal_text) {
    kern_buf_t *buf = kern_buf_new(64);
    kern_html_escape(buf, "Hello World 123");
    ASSERT_EQ_STR(kern_buf_data(buf), "Hello World 123");
    kern_buf_free(buf);
}

TEST(test_escape_null_input) {
    kern_buf_t *buf = kern_buf_new(64);
    int rc = kern_html_escape(buf, NULL);
    ASSERT(rc == 0);
    ASSERT_EQ_STR(kern_buf_data(buf), "");
    kern_buf_free(buf);
}

TEST(test_escape_empty_string) {
    kern_buf_t *buf = kern_buf_new(64);
    kern_html_escape(buf, "");
    ASSERT_EQ_STR(kern_buf_data(buf), "");
    kern_buf_free(buf);
}

TEST(test_escape_all_special) {
    kern_buf_t *buf = kern_buf_new(128);
    kern_html_escape(buf, "<>&\"'");
    ASSERT_EQ_STR(kern_buf_data(buf), "&lt;&gt;&amp;&quot;&#39;");
    kern_buf_free(buf);
}

int main(void) {
    printf("=== test_html ===\n");

    RUN_TEST(test_escape_lt_gt);
    RUN_TEST(test_escape_ampersand);
    RUN_TEST(test_escape_quotes);
    RUN_TEST(test_escape_single_quotes);
    RUN_TEST(test_escape_normal_text);
    RUN_TEST(test_escape_null_input);
    RUN_TEST(test_escape_empty_string);
    RUN_TEST(test_escape_all_special);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
