/*
 * test_tailwind.c - Unit tests for Tailwind CSS compiler
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
        fprintf(stderr, "    ASSERT_EQ_STR FAILED:\n"); \
        fprintf(stderr, "      got:  \"%s\"\n", (a)); \
        fprintf(stderr, "      want: \"%s\"\n", (b)); \
        fprintf(stderr, "      at %s:%d\n", __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_CONTAINS(haystack, needle) do { \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "    ASSERT_CONTAINS FAILED:\n"); \
        fprintf(stderr, "      haystack: \"%.200s\"\n", (haystack)); \
        fprintf(stderr, "      needle:   \"%s\"\n", (needle)); \
        fprintf(stderr, "      at %s:%d\n", __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

TEST(test_scan_basic) {
    const char *html = "<div class=\"flex items-center p-4 bg-zinc-900\">";
    kern_buf_t *classes = kern_buf_new(256);
    ASSERT(classes != NULL);

    int rc = kern_tw_scan(html, strlen(html), classes);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(classes);
    ASSERT_CONTAINS(data, "flex\n");
    ASSERT_CONTAINS(data, "items-center\n");
    ASSERT_CONTAINS(data, "p-4\n");
    ASSERT_CONTAINS(data, "bg-zinc-900\n");

    kern_buf_free(classes);
}

TEST(test_scan_deduplication) {
    const char *html = "flex flex flex items-center items-center";
    kern_buf_t *classes = kern_buf_new(256);
    ASSERT(classes != NULL);

    int rc = kern_tw_scan(html, strlen(html), classes);
    ASSERT(rc == 0);

    /* Should only appear once */
    const char *data = kern_buf_data(classes);
    const char *first = strstr(data, "flex\n");
    ASSERT(first != NULL);
    const char *second = strstr(first + 4, "flex\n");
    ASSERT(second == NULL);

    kern_buf_free(classes);
}

TEST(test_compile_padding) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("p-4\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".p-4");
    ASSERT_CONTAINS(data, "padding: 1rem");

    kern_buf_free(css);
}

TEST(test_compile_margin_auto) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("mx-auto\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".mx-auto");
    ASSERT_CONTAINS(data, "margin-left: auto");
    ASSERT_CONTAINS(data, "margin-right: auto");

    kern_buf_free(css);
}

TEST(test_compile_colors) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("bg-zinc-900\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".bg-zinc-900");
    ASSERT_CONTAINS(data, "background-color: #18181b");

    kern_buf_reset(css);
    rc = kern_tw_compile("text-red-600\n", css);
    ASSERT(rc == 0);
    data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".text-red-600");
    ASSERT_CONTAINS(data, "color: #dc2626");

    kern_buf_reset(css);
    rc = kern_tw_compile("border-blue-500\n", css);
    ASSERT(rc == 0);
    data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".border-blue-500");
    ASSERT_CONTAINS(data, "border-color: #3b82f6");

    kern_buf_free(css);
}

TEST(test_compile_flexbox) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("flex\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".flex");
    ASSERT_CONTAINS(data, "display: flex");

    kern_buf_reset(css);
    rc = kern_tw_compile("items-center\n", css);
    ASSERT(rc == 0);
    data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".items-center");
    ASSERT_CONTAINS(data, "align-items: center");

    kern_buf_free(css);
}

TEST(test_compile_typography) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("text-xl\n", css);
    ASSERT(rc == 0);
    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".text-xl");
    ASSERT_CONTAINS(data, "font-size: 1.25rem");

    kern_buf_reset(css);
    rc = kern_tw_compile("font-bold\n", css);
    ASSERT(rc == 0);
    data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".font-bold");
    ASSERT_CONTAINS(data, "font-weight: 700");

    kern_buf_free(css);
}

TEST(test_compile_responsive) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("md:flex\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, "@media (min-width: 768px)");
    ASSERT_CONTAINS(data, ".md\\:flex");
    ASSERT_CONTAINS(data, "display: flex");

    kern_buf_free(css);
}

TEST(test_compile_hover_state) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("hover:bg-zinc-100\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, ".hover\\:bg-zinc-100:hover");
    ASSERT_CONTAINS(data, "background-color: #f4f4f5");

    kern_buf_free(css);
}

TEST(test_compile_multiple_classes) {
    kern_buf_t *css = kern_buf_new(2048);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("flex\nitems-center\np-4\nbg-zinc-900\ntext-white\n", css);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(css);
    ASSERT_CONTAINS(data, "display: flex");
    ASSERT_CONTAINS(data, "align-items: center");
    ASSERT_CONTAINS(data, "padding: 1rem");
    ASSERT_CONTAINS(data, "background-color: #18181b");
    ASSERT_CONTAINS(data, "color: #ffffff");

    kern_buf_free(css);
}

TEST(test_compile_borders) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("border\n", css);
    ASSERT(rc == 0);
    ASSERT_CONTAINS(kern_buf_data(css), "border-width: 1px");

    kern_buf_reset(css);
    rc = kern_tw_compile("rounded-lg\n", css);
    ASSERT(rc == 0);
    ASSERT_CONTAINS(kern_buf_data(css), "border-radius: 0.5rem");

    kern_buf_free(css);
}

TEST(test_compile_grid) {
    kern_buf_t *css = kern_buf_new(512);
    ASSERT(css != NULL);

    int rc = kern_tw_compile("grid-cols-3\n", css);
    ASSERT(rc == 0);
    ASSERT_CONTAINS(kern_buf_data(css), "grid-template-columns: repeat(3, minmax(0, 1fr))");

    kern_buf_reset(css);
    rc = kern_tw_compile("col-span-2\n", css);
    ASSERT(rc == 0);
    ASSERT_CONTAINS(kern_buf_data(css), "grid-column: span 2 / span 2");

    kern_buf_free(css);
}

TEST(test_scan_file) {
    /* Write a test file */
    FILE *f = fopen("/tmp/test_tw_scan.html", "w");
    ASSERT(f != NULL);
    fprintf(f, "<div class=\"flex p-4 hover:bg-zinc-100\">\n");
    fprintf(f, "  <span class=\"text-sm font-bold\">Hello</span>\n");
    fprintf(f, "</div>\n");
    fclose(f);

    kern_buf_t *classes = kern_buf_new(256);
    ASSERT(classes != NULL);

    int rc = kern_tw_scan_file("/tmp/test_tw_scan.html", classes);
    ASSERT(rc == 0);

    const char *data = kern_buf_data(classes);
    ASSERT_CONTAINS(data, "flex\n");
    ASSERT_CONTAINS(data, "p-4\n");
    ASSERT_CONTAINS(data, "hover:bg-zinc-100\n");
    ASSERT_CONTAINS(data, "text-sm\n");
    ASSERT_CONTAINS(data, "font-bold\n");

    kern_buf_free(classes);
}

int main(void) {
    printf("test_tailwind:\n");

    RUN_TEST(test_scan_basic);
    RUN_TEST(test_scan_deduplication);
    RUN_TEST(test_compile_padding);
    RUN_TEST(test_compile_margin_auto);
    RUN_TEST(test_compile_colors);
    RUN_TEST(test_compile_flexbox);
    RUN_TEST(test_compile_typography);
    RUN_TEST(test_compile_responsive);
    RUN_TEST(test_compile_hover_state);
    RUN_TEST(test_compile_multiple_classes);
    RUN_TEST(test_compile_borders);
    RUN_TEST(test_compile_grid);
    RUN_TEST(test_scan_file);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
