/*
 * test_tpl_integration.c - End-to-end template compilation test
 *
 * Tests the full pipeline: parse .khtml -> generate C code -> verify output
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

#define ASSERT_CONTAINS(haystack, needle) do { \
    if (!strstr((haystack), (needle))) { \
        fprintf(stderr, "    ASSERT_CONTAINS FAILED: could not find \"%s\" in output (%s:%d)\n", \
                (needle), __FILE__, __LINE__); \
        fprintf(stderr, "    Output was:\n%s\n", (haystack)); \
        exit(1); \
    } \
} while (0)

TEST(test_compile_string_simple) {
    const char *src = "h1 Hello World";
    char *code = NULL;

    int rc = kern_tpl_compile_string(src, "kern_render_simple", &code);
    ASSERT(rc == 0);
    ASSERT(code != NULL);

    /* Verify the generated code has proper structure */
    ASSERT_CONTAINS(code, "#include \"kern.h\"");
    ASSERT_CONTAINS(code, "void kern_render_simple(kern_buf_t *buf, kern_dict_t *vars)");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<h1\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"Hello World\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"</h1>\")");

    free(code);
}

TEST(test_compile_string_with_interpolation) {
    const char *src =
        "doctype html\n"
        "html(lang=\"en\")\n"
        "  head\n"
        "    title #{title}\n"
        "  body\n"
        "    h1 #{title}\n";

    char *code = NULL;
    int rc = kern_tpl_compile_string(src, "kern_render_page", &code);
    ASSERT(rc == 0);
    ASSERT(code != NULL);

    ASSERT_CONTAINS(code, "<!DOCTYPE html>");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<html\")");
    ASSERT_CONTAINS(code, " lang=\\\"en\\\"");
    ASSERT_CONTAINS(code, "kern_html_escape(buf, (const char *)(title))");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"</html>\")");

    free(code);
}

TEST(test_compile_string_with_conditionals) {
    const char *src =
        "- if (show)\n"
        "  p Visible\n"
        "- else\n"
        "  p Hidden\n";

    char *code = NULL;
    int rc = kern_tpl_compile_string(src, "kern_render_cond", &code);
    ASSERT(rc == 0);
    ASSERT(code != NULL);

    ASSERT_CONTAINS(code, "if (show) {");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"Visible\")");
    ASSERT_CONTAINS(code, "} else {");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"Hidden\")");

    free(code);
}

TEST(test_compile_string_with_loop) {
    const char *src =
        "ul\n"
        "  - for (int i = 0; i < count; i++)\n"
        "    li Item\n";

    char *code = NULL;
    int rc = kern_tpl_compile_string(src, "kern_render_loop", &code);
    ASSERT(rc == 0);
    ASSERT(code != NULL);

    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<ul\")");
    ASSERT_CONTAINS(code, "for (int i = 0; i < count; i++) {");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<li\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"</li>\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"</ul>\")");

    free(code);
}

TEST(test_compile_hello_fixture) {
    /* Test the hello.khtml fixture file */
    kern_tpl_node_t *ast = kern_tpl_parse_file(TEST_FIXTURES_DIR "/hello.khtml");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_hello");
    ASSERT(code != NULL);

    /* Verify full template structure */
    ASSERT_CONTAINS(code, "<!DOCTYPE html>");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<html\")");
    ASSERT_CONTAINS(code, " lang=\\\"en\\\"");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<head\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<title\")");
    ASSERT_CONTAINS(code, "kern_html_escape(buf, (const char *)(title))");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<body\")");
    ASSERT_CONTAINS(code, "class=\\\"heading\\\"");
    ASSERT_CONTAINS(code, "if (show_message) {");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"Welcome!\")");
    ASSERT_CONTAINS(code, "} else {");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"Goodbye!\")");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_html_escape_in_template_output) {
    /* Simulate what the generated code would do:
     * Render a template with escaped interpolation */
    kern_buf_t *buf = kern_buf_new(256);

    /* Simulating generated template code for: p #{user_input} */
    kern_buf_writes(buf, "<p>");
    kern_html_escape(buf, "<script>alert('xss')</script>");
    kern_buf_writes(buf, "</p>");

    const char *result = kern_buf_data(buf);
    ASSERT_EQ_STR(result, "<p>&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;</p>");

    kern_buf_free(buf);
}

int main(void) {
    printf("=== test_tpl_integration ===\n");

    RUN_TEST(test_compile_string_simple);
    RUN_TEST(test_compile_string_with_interpolation);
    RUN_TEST(test_compile_string_with_conditionals);
    RUN_TEST(test_compile_string_with_loop);
    RUN_TEST(test_compile_hello_fixture);
    RUN_TEST(test_html_escape_in_template_output);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
