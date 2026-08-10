/*
 * test_config.c - Unit tests for kern_config (TOML parser)
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                (a), (b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %lld != %lld (%s:%d)\n", \
                (long long)(a), (long long)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static char tmp_file[256];

static void write_toml(const char *content) {
    snprintf(tmp_file, sizeof(tmp_file), "/tmp/kern_config_test_XXXXXX");
    int fd = mkstemp(tmp_file);
    ASSERT(fd >= 0);
    (void)write(fd, content, strlen(content));
    close(fd);
}

TEST(test_config_basic) {
    write_toml(
        "# Sample kern.toml\n"
        "[app]\n"
        "name = \"My App\"\n"
        "port = 8080\n"
        "debug = true\n"
        "\n"
        "[database]\n"
        "path = \"data/app.db\"\n"
        "pool_size = 5\n"
        "logging = false\n"
    );

    kern_config_t *cfg = kern_config_load(tmp_file);
    ASSERT(cfg != NULL);

    ASSERT_EQ_STR(kern_config_get_str(cfg, "app.name"), "My App");
    ASSERT_EQ_INT(kern_config_get_int(cfg, "app.port"), 8080);
    ASSERT(kern_config_get_bool(cfg, "app.debug") == true);

    ASSERT_EQ_STR(kern_config_get_str(cfg, "database.path"), "data/app.db");
    ASSERT_EQ_INT(kern_config_get_int(cfg, "database.pool_size"), 5);
    ASSERT(kern_config_get_bool(cfg, "database.logging") == false);

    kern_config_free(cfg);
    unlink(tmp_file);
}

TEST(test_config_env_expansion) {
    setenv("KERN_TEST_SECRET", "my-secret-key", 1);

    write_toml(
        "[app]\n"
        "secret = \"${env:KERN_TEST_SECRET}\"\n"
        "name = \"prefix-${env:KERN_TEST_SECRET}-suffix\"\n"
    );

    kern_config_t *cfg = kern_config_load(tmp_file);
    ASSERT(cfg != NULL);

    ASSERT_EQ_STR(kern_config_get_str(cfg, "app.secret"), "my-secret-key");
    ASSERT_EQ_STR(kern_config_get_str(cfg, "app.name"), "prefix-my-secret-key-suffix");

    kern_config_free(cfg);
    unlink(tmp_file);
    unsetenv("KERN_TEST_SECRET");
}

TEST(test_config_missing_keys) {
    write_toml(
        "[app]\n"
        "name = \"Test\"\n"
    );

    kern_config_t *cfg = kern_config_load(tmp_file);
    ASSERT(cfg != NULL);

    /* Missing keys should return NULL/0/false */
    ASSERT(kern_config_get_str(cfg, "nonexistent.key") == NULL);
    ASSERT_EQ_INT(kern_config_get_int(cfg, "nonexistent.key"), 0);
    ASSERT(kern_config_get_bool(cfg, "nonexistent.key") == false);

    kern_config_free(cfg);
    unlink(tmp_file);
}

TEST(test_config_comments_and_empty_lines) {
    write_toml(
        "# This is a comment\n"
        "\n"
        "[server]\n"
        "# Another comment\n"
        "host = \"localhost\"\n"
        "\n"
        "port = 3000\n"
    );

    kern_config_t *cfg = kern_config_load(tmp_file);
    ASSERT(cfg != NULL);

    ASSERT_EQ_STR(kern_config_get_str(cfg, "server.host"), "localhost");
    ASSERT_EQ_INT(kern_config_get_int(cfg, "server.port"), 3000);

    kern_config_free(cfg);
    unlink(tmp_file);
}

TEST(test_config_negative_int) {
    write_toml(
        "[settings]\n"
        "offset = -10\n"
        "timeout = 0\n"
    );

    kern_config_t *cfg = kern_config_load(tmp_file);
    ASSERT(cfg != NULL);

    ASSERT_EQ_INT(kern_config_get_int(cfg, "settings.offset"), -10);
    ASSERT_EQ_INT(kern_config_get_int(cfg, "settings.timeout"), 0);

    kern_config_free(cfg);
    unlink(tmp_file);
}

TEST(test_config_null_safety) {
    ASSERT(kern_config_load(NULL) == NULL);
    ASSERT(kern_config_load("/nonexistent/path.toml") == NULL);
    ASSERT(kern_config_get_str(NULL, "key") == NULL);
    ASSERT_EQ_INT(kern_config_get_int(NULL, "key"), 0);
    ASSERT(kern_config_get_bool(NULL, "key") == false);
    kern_config_free(NULL); /* Should not crash */
}

int main(void) {
    printf("test_config:\n");

    RUN_TEST(test_config_basic);
    RUN_TEST(test_config_env_expansion);
    RUN_TEST(test_config_missing_keys);
    RUN_TEST(test_config_comments_and_empty_lines);
    RUN_TEST(test_config_negative_int);
    RUN_TEST(test_config_null_safety);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
