/*
 * test_fmt.c - Unit tests for kern_fmt (code formatter)
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

/* ============================================================
 * C Formatter Tests
 * ============================================================ */

TEST(test_fmt_c_normalizes_indentation)
{
    const char *input =
        "int main(void) {\n"
        "  int x = 1;\n"
        "    int y = 2;\n"
        "}\n";

    const char *expected =
        "int main(void) {\n"
        "    int x = 1;\n"
        "    int y = 2;\n"
        "}\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_c(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_c_trims_trailing_whitespace)
{
    const char *input =
        "int x = 1;   \n"
        "int y = 2;\t\n"
        "int z = 3;  \t  \n";

    const char *expected =
        "int x = 1;\n"
        "int y = 2;\n"
        "int z = 3;\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_c(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_c_handles_braces)
{
    const char *input =
        "void foo(void)\n"
        "{\n"
        "if (x)\n"
        "{\n"
        "bar();\n"
        "}\n"
        "}\n";

    const char *expected =
        "void foo(void)\n"
        "{\n"
        "    if (x)\n"
        "    {\n"
        "        bar();\n"
        "    }\n"
        "}\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_c(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_c_empty_lines)
{
    const char *input =
        "int a;\n"
        "\n"
        "int b;\n";

    const char *expected =
        "int a;\n"
        "\n"
        "int b;\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_c(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_c_null_safety)
{
    kern_buf_t *out = kern_buf_new(64);
    ASSERT(out != NULL);

    ASSERT_EQ_INT(kern_fmt_c(NULL, out), -1);
    ASSERT_EQ_INT(kern_fmt_c("int x;", NULL), -1);

    kern_buf_free(out);
}

/* ============================================================
 * KHTML Formatter Tests
 * ============================================================ */

TEST(test_fmt_khtml_normalizes_indentation)
{
    const char *input =
        "html\n"
        "    head\n"
        "        title Hello\n"
        "    body\n"
        "        h1 World\n";

    /* 4 spaces -> level 2, 8 spaces -> level 4 */
    const char *expected =
        "html\n"
        "  head\n"
        "    title Hello\n"
        "  body\n"
        "    h1 World\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_khtml(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_khtml_trims_trailing_whitespace)
{
    const char *input =
        "html   \n"
        "  head\t\t\n"
        "    title Test   \n";

    const char *expected =
        "html\n"
        "  head\n"
        "    title Test\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_khtml(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_khtml_preserves_empty_lines)
{
    const char *input =
        "html\n"
        "\n"
        "  body\n";

    const char *expected =
        "html\n"
        "\n"
        "  body\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_khtml(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_khtml_tab_indentation)
{
    const char *input =
        "html\n"
        "\thead\n"
        "\t\ttitle Hello\n";

    /* Tab = 4 spaces, so level 2 and level 4 */
    const char *expected =
        "html\n"
        "  head\n"
        "    title Hello\n";

    kern_buf_t *out = kern_buf_new(256);
    ASSERT(out != NULL);

    int rc = kern_fmt_khtml(input, out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_STR(kern_buf_data(out), expected);

    kern_buf_free(out);
}

TEST(test_fmt_khtml_null_safety)
{
    kern_buf_t *out = kern_buf_new(64);
    ASSERT(out != NULL);

    ASSERT_EQ_INT(kern_fmt_khtml(NULL, out), -1);
    ASSERT_EQ_INT(kern_fmt_khtml("html\n", NULL), -1);

    kern_buf_free(out);
}

/* ============================================================
 * Check Mode Tests
 * ============================================================ */

TEST(test_fmt_check_detects_unformatted)
{
    /* Create a temp file with unformatted content */
    const char *path = "/tmp/test_kern_fmt_check.c";
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fprintf(f, "int main(void) {\n");
    fprintf(f, "  int x = 1;   \n");  /* wrong indent + trailing ws */
    fprintf(f, "}\n");
    fclose(f);

    int result = kern_fmt_check(path);
    ASSERT_EQ_INT(result, 1); /* needs formatting */

    /* Clean up */
    remove(path);
}

TEST(test_fmt_check_accepts_formatted)
{
    /* Create a temp file with properly formatted content */
    const char *path = "/tmp/test_kern_fmt_check_ok.c";
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fprintf(f, "int main(void) {\n");
    fprintf(f, "    int x = 1;\n");
    fprintf(f, "}\n");
    fclose(f);

    int result = kern_fmt_check(path);
    ASSERT_EQ_INT(result, 0); /* already formatted */

    /* Clean up */
    remove(path);
}

TEST(test_fmt_file_formats_in_place)
{
    /* Create a temp file with unformatted content */
    const char *path = "/tmp/test_kern_fmt_file.c";
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fprintf(f, "void foo(void) {\n");
    fprintf(f, "  return;   \n");
    fprintf(f, "}\n");
    fclose(f);

    /* Format it */
    int rc = kern_fmt_file(path);
    ASSERT_EQ_INT(rc, 0);

    /* Check it is now formatted */
    int result = kern_fmt_check(path);
    ASSERT_EQ_INT(result, 0);

    /* Clean up */
    remove(path);
}

int main(void)
{
    printf("test_fmt:\n");

    /* C formatter tests */
    RUN_TEST(test_fmt_c_normalizes_indentation);
    RUN_TEST(test_fmt_c_trims_trailing_whitespace);
    RUN_TEST(test_fmt_c_handles_braces);
    RUN_TEST(test_fmt_c_empty_lines);
    RUN_TEST(test_fmt_c_null_safety);

    /* KHTML formatter tests */
    RUN_TEST(test_fmt_khtml_normalizes_indentation);
    RUN_TEST(test_fmt_khtml_trims_trailing_whitespace);
    RUN_TEST(test_fmt_khtml_preserves_empty_lines);
    RUN_TEST(test_fmt_khtml_tab_indentation);
    RUN_TEST(test_fmt_khtml_null_safety);

    /* Check mode tests */
    RUN_TEST(test_fmt_check_detects_unformatted);
    RUN_TEST(test_fmt_check_accepts_formatted);
    RUN_TEST(test_fmt_file_formats_in_place);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
