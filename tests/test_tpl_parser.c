/*
 * test_tpl_parser.c - Unit tests for the .khtml template parser
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

TEST(test_simple_element) {
    kern_tpl_node_t *root = kern_tpl_parse("h1 Hello");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *h1 = root->children[0];
    ASSERT(h1->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(h1->tag, "h1");
    /* Inline text becomes a TEXT child */
    ASSERT(h1->children_count >= 1);
    ASSERT(h1->children[0]->type == KERN_TPL_TEXT);
    ASSERT_EQ_STR(h1->children[0]->text, "Hello");

    kern_tpl_node_free(root);
}

TEST(test_nested_elements) {
    const char *src =
        "div\n"
        "  p Hello\n"
        "  span World\n";

    kern_tpl_node_t *root = kern_tpl_parse(src);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *div = root->children[0];
    ASSERT(div->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(div->tag, "div");
    ASSERT_EQ_INT(div->children_count, 2);

    kern_tpl_node_t *p = div->children[0];
    ASSERT(p->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(p->tag, "p");

    kern_tpl_node_t *span = div->children[1];
    ASSERT(span->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(span->tag, "span");

    kern_tpl_node_free(root);
}

TEST(test_attributes) {
    kern_tpl_node_t *root = kern_tpl_parse("div(class=\"foo\" id=\"bar\")");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *div = root->children[0];
    ASSERT(div->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(div->tag, "div");
    ASSERT_EQ_INT(div->attr_count, 2);
    ASSERT_EQ_STR(div->attrs[0].name, "class");
    ASSERT_EQ_STR(div->attrs[0].value, "foo");
    ASSERT(div->attrs[0].dynamic == false);
    ASSERT_EQ_STR(div->attrs[1].name, "id");
    ASSERT_EQ_STR(div->attrs[1].value, "bar");

    kern_tpl_node_free(root);
}

TEST(test_dynamic_attribute) {
    kern_tpl_node_t *root = kern_tpl_parse("a(href={url})");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *a = root->children[0];
    ASSERT_EQ_INT(a->attr_count, 1);
    ASSERT_EQ_STR(a->attrs[0].name, "href");
    ASSERT_EQ_STR(a->attrs[0].value, "url");
    ASSERT(a->attrs[0].dynamic == true);

    kern_tpl_node_free(root);
}

TEST(test_interpolation_escaped) {
    kern_tpl_node_t *root = kern_tpl_parse("p #{name}");
    ASSERT(root != NULL);

    kern_tpl_node_t *p = root->children[0];
    ASSERT(p->type == KERN_TPL_ELEMENT);
    /* Should have an interpolation child */
    ASSERT(p->children_count >= 1);
    kern_tpl_node_t *interp = p->children[0];
    ASSERT(interp->type == KERN_TPL_INTERP);
    ASSERT_EQ_STR(interp->expr, "name");
    ASSERT(interp->escaped == true);

    kern_tpl_node_free(root);
}

TEST(test_interpolation_raw) {
    kern_tpl_node_t *root = kern_tpl_parse("p !{html_content}");
    ASSERT(root != NULL);

    kern_tpl_node_t *p = root->children[0];
    kern_tpl_node_t *interp = p->children[0];
    ASSERT(interp->type == KERN_TPL_INTERP);
    ASSERT_EQ_STR(interp->expr, "html_content");
    ASSERT(interp->escaped == false);

    kern_tpl_node_free(root);
}

TEST(test_statement_if) {
    const char *src =
        "- if (x > 0)\n"
        "  p Positive\n";

    kern_tpl_node_t *root = kern_tpl_parse(src);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *stmt = root->children[0];
    ASSERT(stmt->type == KERN_TPL_STATEMENT);
    ASSERT_EQ_STR(stmt->text, "if (x > 0)");
    ASSERT_EQ_INT(stmt->children_count, 1);

    kern_tpl_node_t *p = stmt->children[0];
    ASSERT(p->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(p->tag, "p");

    kern_tpl_node_free(root);
}

TEST(test_statement_for) {
    const char *src =
        "- for (int i = 0; i < 5; i++)\n"
        "  li Item\n";

    kern_tpl_node_t *root = kern_tpl_parse(src);
    ASSERT(root != NULL);

    kern_tpl_node_t *stmt = root->children[0];
    ASSERT(stmt->type == KERN_TPL_STATEMENT);
    ASSERT_EQ_STR(stmt->text, "for (int i = 0; i < 5; i++)");
    ASSERT_EQ_INT(stmt->children_count, 1);

    kern_tpl_node_free(root);
}

TEST(test_literal_text) {
    kern_tpl_node_t *root = kern_tpl_parse("| Some literal text");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *txt = root->children[0];
    ASSERT(txt->type == KERN_TPL_TEXT);
    ASSERT_EQ_STR(txt->text, "Some literal text");

    kern_tpl_node_free(root);
}

TEST(test_include) {
    kern_tpl_node_t *root = kern_tpl_parse("include partials/nav");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *inc = root->children[0];
    ASSERT(inc->type == KERN_TPL_INCLUDE);
    ASSERT_EQ_STR(inc->text, "partials/nav");

    kern_tpl_node_free(root);
}

TEST(test_extend) {
    kern_tpl_node_t *root = kern_tpl_parse("extend layouts/base");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *ext = root->children[0];
    ASSERT(ext->type == KERN_TPL_EXTEND);
    ASSERT_EQ_STR(ext->text, "layouts/base");

    kern_tpl_node_free(root);
}

TEST(test_block) {
    const char *src =
        "block content\n"
        "  p Hello\n";

    kern_tpl_node_t *root = kern_tpl_parse(src);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *blk = root->children[0];
    ASSERT(blk->type == KERN_TPL_BLOCK);
    ASSERT_EQ_STR(blk->text, "content");
    ASSERT_EQ_INT(blk->children_count, 1);

    kern_tpl_node_free(root);
}

TEST(test_doctype) {
    kern_tpl_node_t *root = kern_tpl_parse("doctype html");
    ASSERT(root != NULL);
    ASSERT_EQ_INT(root->children_count, 1);

    kern_tpl_node_t *dt = root->children[0];
    ASSERT(dt->type == KERN_TPL_DOCTYPE);
    ASSERT_EQ_STR(dt->text, "html");

    kern_tpl_node_free(root);
}

TEST(test_void_tag) {
    kern_tpl_node_t *root = kern_tpl_parse("br");
    ASSERT(root != NULL);

    kern_tpl_node_t *br = root->children[0];
    ASSERT(br->type == KERN_TPL_ELEMENT);
    ASSERT_EQ_STR(br->tag, "br");
    ASSERT(br->is_void == true);

    kern_tpl_node_free(root);
}

TEST(test_mixed_text_and_interp) {
    kern_tpl_node_t *root = kern_tpl_parse("p Hello #{name}, welcome!");
    ASSERT(root != NULL);

    kern_tpl_node_t *p = root->children[0];
    /* Should have: TEXT("Hello "), INTERP("name"), TEXT(", welcome!") */
    ASSERT(p->children_count == 3);
    ASSERT(p->children[0]->type == KERN_TPL_TEXT);
    ASSERT_EQ_STR(p->children[0]->text, "Hello ");
    ASSERT(p->children[1]->type == KERN_TPL_INTERP);
    ASSERT_EQ_STR(p->children[1]->expr, "name");
    ASSERT(p->children[2]->type == KERN_TPL_TEXT);
    ASSERT_EQ_STR(p->children[2]->text, ", welcome!");

    kern_tpl_node_free(root);
}

int main(void) {
    printf("=== test_tpl_parser ===\n");

    RUN_TEST(test_simple_element);
    RUN_TEST(test_nested_elements);
    RUN_TEST(test_attributes);
    RUN_TEST(test_dynamic_attribute);
    RUN_TEST(test_interpolation_escaped);
    RUN_TEST(test_interpolation_raw);
    RUN_TEST(test_statement_if);
    RUN_TEST(test_statement_for);
    RUN_TEST(test_literal_text);
    RUN_TEST(test_include);
    RUN_TEST(test_extend);
    RUN_TEST(test_block);
    RUN_TEST(test_doctype);
    RUN_TEST(test_void_tag);
    RUN_TEST(test_mixed_text_and_interp);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
