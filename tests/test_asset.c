/*
 * test_asset.c - Unit tests for kern_asset (asset fingerprinting)
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

#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)

/* Helper: create a temp directory */
static char tmp_dir[] = "/tmp/test_asset_XXXXXX";
static char input_dir[512];
static char output_dir[512];

static void setup(void) {
    char *d = mkdtemp(tmp_dir);
    ASSERT(d != NULL);
    snprintf(input_dir, sizeof(input_dir), "%s/input", tmp_dir);
    snprintf(output_dir, sizeof(output_dir), "%s/output", tmp_dir);
    mkdir(input_dir, 0755);
    mkdir(output_dir, 0755);
}

static void write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fputs(content, f);
    fclose(f);
}

/* ============================================================
 * Tests
 * ============================================================ */

TEST(test_hash_file_produces_hashed_name) {
    char input_path[512];
    snprintf(input_path, sizeof(input_path), "%s/app.css", input_dir);
    write_test_file(input_path, "body { color: red; }");

    char *result = kern_asset_hash_file(input_path, output_dir);
    ASSERT_NOT_NULL(result);

    /* Should start with "app-" and end with ".css" */
    ASSERT(strncmp(result, "app-", 4) == 0);
    ASSERT(strlen(result) > strlen("app-.css"));
    ASSERT(strcmp(result + strlen(result) - 4, ".css") == 0);

    /* Hash should be 8 hex chars */
    ASSERT(strlen(result) == 4 + 8 + 4); /* "app-" + 8 hex + ".css" */

    /* Output file should exist */
    char expected_path[512];
    snprintf(expected_path, sizeof(expected_path), "%s/%s", output_dir, result);
    struct stat st;
    ASSERT(stat(expected_path, &st) == 0);

    free(result);
}

TEST(test_same_content_same_hash) {
    char path1[512], path2[512];
    snprintf(path1, sizeof(path1), "%s/file1.css", input_dir);
    snprintf(path2, sizeof(path2), "%s/file2.css", input_dir);

    write_test_file(path1, "same content here");
    write_test_file(path2, "same content here");

    char out1[512], out2[512];
    snprintf(out1, sizeof(out1), "%s/out1", output_dir);
    snprintf(out2, sizeof(out2), "%s/out2", output_dir);
    mkdir(out1, 0755);
    mkdir(out2, 0755);

    char *hash1 = kern_asset_hash_file(path1, out1);
    char *hash2 = kern_asset_hash_file(path2, out2);

    ASSERT_NOT_NULL(hash1);
    ASSERT_NOT_NULL(hash2);

    /* Extract just the hash portion: after "file1-" or "file2-", before ".css" */
    /* hash1 = "file1-XXXXXXXX.css", hash2 = "file2-XXXXXXXX.css" */
    char *h1 = hash1 + 6; /* skip "file1-" */
    char *h2 = hash2 + 6; /* skip "file2-" */

    /* Compare just the 8-char hash (same content should give same hash) */
    ASSERT(strncmp(h1, h2, 8) == 0);

    free(hash1);
    free(hash2);
}

TEST(test_different_content_different_hash) {
    char path1[512], path2[512];
    snprintf(path1, sizeof(path1), "%s/a.js", input_dir);
    snprintf(path2, sizeof(path2), "%s/b.js", input_dir);

    write_test_file(path1, "content A");
    write_test_file(path2, "content B");

    char out1[512], out2[512];
    snprintf(out1, sizeof(out1), "%s/outA", output_dir);
    snprintf(out2, sizeof(out2), "%s/outB", output_dir);
    mkdir(out1, 0755);
    mkdir(out2, 0755);

    char *hash1 = kern_asset_hash_file(path1, out1);
    char *hash2 = kern_asset_hash_file(path2, out2);

    ASSERT_NOT_NULL(hash1);
    ASSERT_NOT_NULL(hash2);

    /* Different content should produce different hashes */
    /* hash1 = "a-XXXXXXXX.js", hash2 = "b-XXXXXXXX.js" */
    char *h1 = hash1 + 2; /* skip "a-" */
    char *h2 = hash2 + 2; /* skip "b-" */
    ASSERT(strncmp(h1, h2, 8) != 0);

    free(hash1);
    free(hash2);
}

TEST(test_manifest_generation) {
    /* Create an assets directory with some files */
    char assets_dir[512];
    snprintf(assets_dir, sizeof(assets_dir), "%s/assets", tmp_dir);
    mkdir(assets_dir, 0755);

    char css_dir[512];
    snprintf(css_dir, sizeof(css_dir), "%s/css", assets_dir);
    mkdir(css_dir, 0755);

    char css_path[512];
    snprintf(css_path, sizeof(css_path), "%s/css/app.css", assets_dir);
    write_test_file(css_path, "body { margin: 0; }");

    char public_dir[512];
    snprintf(public_dir, sizeof(public_dir), "%s/public", tmp_dir);

    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.h", tmp_dir);

    int count = kern_asset_process_dir(assets_dir, public_dir, manifest_path);
    ASSERT(count == 1);

    /* Read and verify the manifest */
    FILE *f = fopen(manifest_path, "r");
    ASSERT(f != NULL);

    char content[4096] = {0};
    size_t n = fread(content, 1, sizeof(content) - 1, f);
    fclose(f);
    ASSERT(n > 0);

    /* Should contain a #define for the CSS file */
    ASSERT(strstr(content, "#define KERN_ASSET_CSS_APP") != NULL);
    ASSERT(strstr(content, "/assets/css/app-") != NULL);
    ASSERT(strstr(content, ".css") != NULL);
}

TEST(test_hash_file_null_input) {
    char *result = kern_asset_hash_file(NULL, output_dir);
    ASSERT(result == NULL);

    char path[512];
    snprintf(path, sizeof(path), "%s/app.css", input_dir);
    write_test_file(path, "test");

    result = kern_asset_hash_file(path, NULL);
    ASSERT(result == NULL);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("test_asset:\n");

    setup();

    RUN_TEST(test_hash_file_produces_hashed_name);
    RUN_TEST(test_same_content_same_hash);
    RUN_TEST(test_different_content_different_hash);
    RUN_TEST(test_manifest_generation);
    RUN_TEST(test_hash_file_null_input);

    printf("\n  %d/%d tests passed\n\n", tests_passed, tests_run);

    /* Cleanup temp directory */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_dir);
    system(cmd);

    return tests_passed == tests_run ? 0 : 1;
}
