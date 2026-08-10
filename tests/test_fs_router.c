/*
 * test_fs_router.c - Unit tests for the file-system routing scanner
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    const char *_a = (a); const char *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                _a ? _a : "(null)", _b ? _b : "(null)", __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* Helper: create directory (and parents) */
static void mkdirs(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

/* Helper: write a file with content */
static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

/* Helper: remove directory tree */
static void rmtree(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    (void)system(cmd);
}

/* Create a test pages directory structure */
static const char *TEST_DIR = "/tmp/kern_test_pages";

static void setup_test_pages(void) {
    rmtree(TEST_DIR);

    /* Create directory structure */
    mkdirs(TEST_DIR);
    char path[1024];

    /* pages/index.c -> GET / */
    snprintf(path, sizeof(path), "%s/index.c", TEST_DIR);
    write_file(path, "/* KERN_GET */\n#include \"kern.h\"\n");

    /* pages/about.c -> GET /about */
    snprintf(path, sizeof(path), "%s/about.c", TEST_DIR);
    write_file(path, "/* KERN_PAGE */\n#include \"kern.h\"\n");

    /* pages/posts/index.c -> GET /posts */
    snprintf(path, sizeof(path), "%s/posts", TEST_DIR);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/posts/index.c", TEST_DIR);
    write_file(path, "/* KERN_GET */\n");

    /* pages/posts/new.c -> GET /posts/new */
    snprintf(path, sizeof(path), "%s/posts/new.c", TEST_DIR);
    write_file(path, "/* No macro - defaults to GET */\n");

    /* pages/posts/[id]/index.c -> GET /posts/:id */
    snprintf(path, sizeof(path), "%s/posts/[id]", TEST_DIR);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/posts/[id]/index.c", TEST_DIR);
    write_file(path, "/* KERN_GET */\n/* KERN_POST */\n");

    /* pages/posts/[id]/edit.c -> GET /posts/:id/edit */
    snprintf(path, sizeof(path), "%s/posts/[id]/edit.c", TEST_DIR);
    write_file(path, "/* KERN_GET */\n");

    /* pages/_layout.c - should be excluded */
    snprintf(path, sizeof(path), "%s/_layout.c", TEST_DIR);
    write_file(path, "/* Layout file */\n");

    /* pages/_middleware.c - should be excluded */
    snprintf(path, sizeof(path), "%s/_middleware.c", TEST_DIR);
    write_file(path, "/* Middleware file */\n");
}

static void teardown_test_pages(void) {
    rmtree(TEST_DIR);
}

/* --- Tests --- */

TEST(test_fs_scan_basic) {
    setup_test_pages();

    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan(TEST_DIR, &count);
    ASSERT(entries != NULL);
    ASSERT(count > 0);

    /* Verify we got at least the expected entries */
    bool found_root = false;
    bool found_about = false;
    bool found_posts = false;
    bool found_posts_new = false;
    bool found_posts_id = false;
    bool found_posts_id_edit = false;

    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].route_path, "/") == 0) found_root = true;
        if (strcmp(entries[i].route_path, "/about") == 0) found_about = true;
        if (strcmp(entries[i].route_path, "/posts") == 0) found_posts = true;
        if (strcmp(entries[i].route_path, "/posts/new") == 0) found_posts_new = true;
        if (strcmp(entries[i].route_path, "/posts/:id") == 0) found_posts_id = true;
        if (strcmp(entries[i].route_path, "/posts/:id/edit") == 0) found_posts_id_edit = true;
    }

    ASSERT(found_root);
    ASSERT(found_about);
    ASSERT(found_posts);
    ASSERT(found_posts_new);
    ASSERT(found_posts_id);
    ASSERT(found_posts_id_edit);

    kern_fs_entries_free(entries, count);
    teardown_test_pages();
}

TEST(test_fs_scan_excludes_special) {
    setup_test_pages();

    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan(TEST_DIR, &count);
    ASSERT(entries != NULL);

    /* Verify _layout.c and _middleware.c are not included */
    for (int i = 0; i < count; i++) {
        ASSERT(strstr(entries[i].file_path, "_layout") == NULL);
        ASSERT(strstr(entries[i].file_path, "_middleware") == NULL);
    }

    kern_fs_entries_free(entries, count);
    teardown_test_pages();
}

TEST(test_fs_scan_methods) {
    setup_test_pages();

    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan(TEST_DIR, &count);
    ASSERT(entries != NULL);

    /* Find posts/[id]/index.c entry - should have both GET and POST */
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].route_path, "/posts/:id") == 0) {
            ASSERT(entries[i].method_count == 2);
            /* Should have GET and POST (order may vary) */
            bool has_get = false, has_post = false;
            for (int j = 0; j < entries[i].method_count; j++) {
                if (strcmp(entries[i].methods[j], "GET") == 0) has_get = true;
                if (strcmp(entries[i].methods[j], "POST") == 0) has_post = true;
            }
            ASSERT(has_get);
            ASSERT(has_post);
            break;
        }
    }

    kern_fs_entries_free(entries, count);
    teardown_test_pages();
}

TEST(test_fs_scan_bracket_conversion) {
    setup_test_pages();

    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan(TEST_DIR, &count);
    ASSERT(entries != NULL);

    /* Verify [id] was converted to :id */
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (strstr(entries[i].route_path, ":id") != NULL) {
            found = true;
            /* Should NOT contain brackets */
            ASSERT(strchr(entries[i].route_path, '[') == NULL);
            ASSERT(strchr(entries[i].route_path, ']') == NULL);
        }
    }
    ASSERT(found);

    kern_fs_entries_free(entries, count);
    teardown_test_pages();
}

TEST(test_fs_generate_registry) {
    setup_test_pages();

    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan(TEST_DIR, &count);
    ASSERT(entries != NULL);
    ASSERT(count > 0);

    const char *output = "/tmp/kern_test_registry.c";
    int rc = kern_fs_generate_registry(entries, count, output);
    ASSERT(rc == 0);

    /* Read the generated file and verify it contains expected content */
    FILE *f = fopen(output, "r");
    ASSERT(f != NULL);

    char content[4096];
    size_t len = fread(content, 1, sizeof(content) - 1, f);
    content[len] = '\0';
    fclose(f);

    /* Check for expected content */
    ASSERT(strstr(content, "#include \"kern.h\"") != NULL);
    ASSERT(strstr(content, "kern_register_all_routes") != NULL);
    ASSERT(strstr(content, "kern_router_add") != NULL);
    ASSERT(strstr(content, "extern") != NULL);

    /* Clean up */
    unlink(output);
    kern_fs_entries_free(entries, count);
    teardown_test_pages();
}

TEST(test_fs_scan_null_input) {
    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan(NULL, &count);
    ASSERT(entries == NULL);
}

TEST(test_fs_scan_nonexistent_dir) {
    int count = 0;
    kern_fs_entry_t *entries = kern_fs_scan("/tmp/nonexistent_kern_dir_xyz", &count);
    ASSERT(entries == NULL || count == 0);
    if (entries) kern_fs_entries_free(entries, count);
}

int main(void) {
    printf("Running fs_router tests...\n");

    RUN_TEST(test_fs_scan_basic);
    RUN_TEST(test_fs_scan_excludes_special);
    RUN_TEST(test_fs_scan_methods);
    RUN_TEST(test_fs_scan_bracket_conversion);
    RUN_TEST(test_fs_generate_registry);
    RUN_TEST(test_fs_scan_null_input);
    RUN_TEST(test_fs_scan_nonexistent_dir);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
