/*
 * test_tpl_codegen.c - Unit tests for the template code generator
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

#define ASSERT_CONTAINS(haystack, needle) do { \
    if (!strstr((haystack), (needle))) { \
        fprintf(stderr, "    ASSERT_CONTAINS FAILED: could not find \"%s\" in output (%s:%d)\n", \
                (needle), __FILE__, __LINE__); \
        fprintf(stderr, "    Output was:\n%s\n", (haystack)); \
        exit(1); \
    } \
} while (0)

TEST(test_codegen_simple_element) {
    kern_tpl_node_t *ast = kern_tpl_parse("h1 Hello");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_test");
    ASSERT(code != NULL);

    /* Should contain function signature */
    ASSERT_CONTAINS(code, "void kern_render_test(kern_buf_t *buf, kern_dict_t *vars)");
    /* Should open h1 tag */
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<h1\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \">\")");
    /* Should have text content */
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"Hello\")");
    /* Should close h1 tag */
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"</h1>\")");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_interpolation_escaped) {
    kern_tpl_node_t *ast = kern_tpl_parse("p #{name}");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_interp");
    ASSERT(code != NULL);

    /* Should call kern_html_escape for #{} */
    ASSERT_CONTAINS(code, "kern_html_escape(buf, (const char *)(name))");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_interpolation_raw) {
    kern_tpl_node_t *ast = kern_tpl_parse("p !{raw_html}");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_raw");
    ASSERT(code != NULL);

    /* Should use kern_buf_writes for !{} (no escaping) */
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, (const char *)(raw_html))");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_statement_if) {
    const char *src =
        "- if (x > 0)\n"
        "  p Positive\n";

    kern_tpl_node_t *ast = kern_tpl_parse(src);
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_if");
    ASSERT(code != NULL);

    /* Should generate if statement with braces */
    ASSERT_CONTAINS(code, "if (x > 0) {");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<p\")");
    ASSERT_CONTAINS(code, "}");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_attributes) {
    kern_tpl_node_t *ast = kern_tpl_parse("div(class=\"container\" id=\"main\")");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_attrs");
    ASSERT(code != NULL);

    /* Should generate attribute output */
    ASSERT_CONTAINS(code, "class=\\\"container\\\"");
    ASSERT_CONTAINS(code, "id=\\\"main\\\"");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_dynamic_attribute) {
    kern_tpl_node_t *ast = kern_tpl_parse("a(href={url})");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_dynattr");
    ASSERT(code != NULL);

    /* Dynamic attrs should use kern_html_escape */
    ASSERT_CONTAINS(code, "kern_html_escape(buf, (const char *)(url))");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_doctype) {
    kern_tpl_node_t *ast = kern_tpl_parse("doctype html");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_doctype");
    ASSERT(code != NULL);

    ASSERT_CONTAINS(code, "<!DOCTYPE html>");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_void_tag) {
    kern_tpl_node_t *ast = kern_tpl_parse("br");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_void");
    ASSERT(code != NULL);

    /* Void tags should NOT have closing tag */
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \"<br\")");
    ASSERT_CONTAINS(code, "kern_buf_writes(buf, \">\")");
    ASSERT(strstr(code, "</br>") == NULL);

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_include) {
    kern_tpl_node_t *ast = kern_tpl_parse("include partials/nav");
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_inc");
    ASSERT(code != NULL);

    /* Should generate a call to the included template's render function */
    ASSERT_CONTAINS(code, "kern_render_partials_nav(buf, vars)");

    free(code);
    kern_tpl_node_free(ast);
}

TEST(test_codegen_if_else) {
    const char *src =
        "- if (logged_in)\n"
        "  p Welcome\n"
        "- else\n"
        "  p Please login\n";

    kern_tpl_node_t *ast = kern_tpl_parse(src);
    ASSERT(ast != NULL);

    char *code = kern_tpl_codegen(ast, "kern_render_ifelse");
    ASSERT(code != NULL);

    ASSERT_CONTAINS(code, "if (logged_in) {");
    ASSERT_CONTAINS(code, "} else {");

    free(code);
    kern_tpl_node_free(ast);
}

int main(void) {
    printf("=== test_tpl_codegen ===\n");

    RUN_TEST(test_codegen_simple_element);
    RUN_TEST(test_codegen_interpolation_escaped);
    RUN_TEST(test_codegen_interpolation_raw);
    RUN_TEST(test_codegen_statement_if);
    RUN_TEST(test_codegen_attributes);
    RUN_TEST(test_codegen_dynamic_attribute);
    RUN_TEST(test_codegen_doctype);
    RUN_TEST(test_codegen_void_tag);
    RUN_TEST(test_codegen_include);
    RUN_TEST(test_codegen_if_else);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
