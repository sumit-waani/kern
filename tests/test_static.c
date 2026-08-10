/*
 * test_static.c - Unit tests for kern_static (static file handler)
 *
 * Tests directory traversal prevention and symlink escape protection.
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

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %d != %d (%s:%d)\n", \
                (int)(a), (int)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* Helper: create a request for a given path */
static kern_req_t *make_req_for_path(const char *path) {
    kern_http_parser_t *parser = kern_http_parser_new();
    if (!parser) return NULL;

    kern_buf_t *buf = kern_buf_new(512);
    kern_buf_writef(buf, "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", path);

    int rc = kern_http_parser_feed(parser, kern_buf_data(buf), kern_buf_len(buf));
    kern_buf_free(buf);
    if (rc != KERN_HTTP_PARSE_DONE) {
        kern_http_parser_free(parser);
        return NULL;
    }

    return kern_req_new(parser);
}

/* Create a temporary directory structure for testing */
static char g_tmpdir[256];
static char g_public_dir[512];

static void setup_test_dir(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/kern_static_test_%d", (int)getpid());
    snprintf(g_public_dir, sizeof(g_public_dir), "%s/public", g_tmpdir);

    mkdir(g_tmpdir, 0755);
    mkdir(g_public_dir, 0755);

    /* Create a test file */
    char filepath[768];
    snprintf(filepath, sizeof(filepath), "%s/hello.txt", g_public_dir);
    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "Hello, World!");
        fclose(f);
    }

    /* Create a subdirectory with a file */
    char subdir[768];
    snprintf(subdir, sizeof(subdir), "%s/sub", g_public_dir);
    mkdir(subdir, 0755);
    snprintf(filepath, sizeof(filepath), "%s/sub/page.html", g_public_dir);
    f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "<h1>Page</h1>");
        fclose(f);
    }

    /* Create a secret file outside the public dir */
    snprintf(filepath, sizeof(filepath), "%s/secret.txt", g_tmpdir);
    f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "SECRET DATA");
        fclose(f);
    }
}

static void cleanup_test_dir(void) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmpdir);
    (void)system(cmd);
}

TEST(test_serve_existing_file) {
    setup_test_dir();

    kern_req_t *req = make_req_for_path("/hello.txt");
    ASSERT(req != NULL);

    kern_response_t *res = kern_static_handler(req, g_public_dir);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);

    kern_response_free(res);
    kern_req_free(req);
    cleanup_test_dir();
}

TEST(test_serve_nonexistent_file) {
    setup_test_dir();

    kern_req_t *req = make_req_for_path("/nonexistent.txt");
    ASSERT(req != NULL);

    kern_response_t *res = kern_static_handler(req, g_public_dir);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 404);

    kern_response_free(res);
    kern_req_free(req);
    cleanup_test_dir();
}

TEST(test_traversal_dotdot) {
    setup_test_dir();

    kern_req_t *req = make_req_for_path("/../secret.txt");
    ASSERT(req != NULL);

    kern_response_t *res = kern_static_handler(req, g_public_dir);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 403);

    kern_response_free(res);
    kern_req_free(req);
    cleanup_test_dir();
}

TEST(test_symlink_escape) {
    setup_test_dir();

    /* Create a symlink inside public/ that points outside */
    char link_path[768];
    char target_path[768];
    snprintf(link_path, sizeof(link_path), "%s/escape", g_public_dir);
    snprintf(target_path, sizeof(target_path), "%s/secret.txt", g_tmpdir);

    int rc = symlink(target_path, link_path);
    if (rc != 0) {
        /* symlink creation failed (unlikely but handle gracefully) */
        cleanup_test_dir();
        return;
    }

    kern_req_t *req = make_req_for_path("/escape");
    ASSERT(req != NULL);

    kern_response_t *res = kern_static_handler(req, g_public_dir);
    ASSERT(res != NULL);
    /* Should return 403 because resolved path escapes public dir */
    ASSERT_EQ_INT(kern_response_status(res), 403);

    kern_response_free(res);
    kern_req_free(req);
    cleanup_test_dir();
}

TEST(test_symlink_within_public) {
    setup_test_dir();

    /* Create a symlink inside public/ that points to another file inside public/ */
    char link_path[768];
    char target_path[768];
    snprintf(link_path, sizeof(link_path), "%s/link_hello.txt", g_public_dir);
    snprintf(target_path, sizeof(target_path), "%s/hello.txt", g_public_dir);

    int rc = symlink(target_path, link_path);
    if (rc != 0) {
        cleanup_test_dir();
        return;
    }

    kern_req_t *req = make_req_for_path("/link_hello.txt");
    ASSERT(req != NULL);

    kern_response_t *res = kern_static_handler(req, g_public_dir);
    ASSERT(res != NULL);
    /* Should serve normally since link stays within public dir */
    ASSERT_EQ_INT(kern_response_status(res), 200);

    kern_response_free(res);
    kern_req_free(req);
    cleanup_test_dir();
}

int main(void) {
    printf("test_static:\n");

    RUN_TEST(test_serve_existing_file);
    RUN_TEST(test_serve_nonexistent_file);
    RUN_TEST(test_traversal_dotdot);
    RUN_TEST(test_symlink_escape);
    RUN_TEST(test_symlink_within_public);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
