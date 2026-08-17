/*
 * test_kernd_config.c - Unit tests for kernd configuration loading
 */

#include "../kernd/kernd_config.h"

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
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %d != %d (%s:%d)\n", \
                (int)(a), (int)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "    ASSERT_NULL FAILED: %p (%s:%d)\n", \
                (void *)(ptr), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "    ASSERT_NOT_NULL FAILED (%s:%d)\n", \
                __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

TEST(test_config_defaults) {
    kernd_config_t *cfg = kernd_config_defaults();
    ASSERT_NOT_NULL(cfg);

    ASSERT_EQ_INT(cfg->admin_port, 8443);
    ASSERT_EQ_INT(cfg->http_port, 80);
    ASSERT_EQ_INT(cfg->https_port, 443);
    ASSERT_EQ_STR(cfg->data_dir, "/var/lib/kernd");
    ASSERT_EQ_STR(cfg->log_dir, "/var/log/kernd");
    ASSERT_NULL(cfg->dashboard_secret);
    ASSERT_NULL(cfg->admin_password_hash);

    kernd_config_free(cfg);
}

TEST(test_config_load_valid_toml) {
    /* Write a temporary TOML file */
    char tmppath[] = "/tmp/kernd_test_XXXXXX";
    int fd = mkstemp(tmppath);
    ASSERT(fd >= 0);

    const char *content =
        "[admin]\n"
        "port = 9443\n"
        "password_hash = \"pbkdf2_sha256$100000$salt$hash\"\n"
        "\n"
        "[http]\n"
        "port = 8080\n"
        "\n"
        "[https]\n"
        "port = 8443\n"
        "\n"
        "[paths]\n"
        "data_dir = \"/opt/kernd/data\"\n"
        "log_dir = \"/opt/kernd/logs\"\n"
        "\n"
        "[security]\n"
        "dashboard_secret = \"abcdef0123456789\"\n";

    ssize_t written = write(fd, content, strlen(content));
    ASSERT(written > 0);
    close(fd);

    kernd_config_t *cfg = kernd_config_load(tmppath);
    ASSERT_NOT_NULL(cfg);

    ASSERT_EQ_INT(cfg->admin_port, 9443);
    ASSERT_EQ_INT(cfg->http_port, 8080);
    ASSERT_EQ_INT(cfg->https_port, 8443);
    ASSERT_EQ_STR(cfg->data_dir, "/opt/kernd/data");
    ASSERT_EQ_STR(cfg->log_dir, "/opt/kernd/logs");
    ASSERT_EQ_STR(cfg->dashboard_secret, "abcdef0123456789");
    ASSERT_EQ_STR(cfg->admin_password_hash, "pbkdf2_sha256$100000$salt$hash");

    kernd_config_free(cfg);
    unlink(tmppath);
}

TEST(test_config_load_missing_file) {
    /* Loading a non-existent file should return defaults */
    kernd_config_t *cfg = kernd_config_load("/tmp/nonexistent_kernd_config_12345.toml");
    ASSERT_NOT_NULL(cfg);

    ASSERT_EQ_INT(cfg->admin_port, 8443);
    ASSERT_EQ_INT(cfg->http_port, 80);
    ASSERT_EQ_INT(cfg->https_port, 443);
    ASSERT_EQ_STR(cfg->data_dir, "/var/lib/kernd");
    ASSERT_EQ_STR(cfg->log_dir, "/var/log/kernd");

    kernd_config_free(cfg);
}

TEST(test_config_load_null_path) {
    /* NULL path should return defaults */
    kernd_config_t *cfg = kernd_config_load(NULL);
    ASSERT_NOT_NULL(cfg);

    ASSERT_EQ_INT(cfg->admin_port, 8443);
    ASSERT_EQ_INT(cfg->http_port, 80);
    ASSERT_EQ_INT(cfg->https_port, 443);

    kernd_config_free(cfg);
}

TEST(test_config_partial_toml) {
    /* Only set some values, rest should be defaults */
    char tmppath[] = "/tmp/kernd_test_XXXXXX";
    int fd = mkstemp(tmppath);
    ASSERT(fd >= 0);

    const char *content =
        "[admin]\n"
        "port = 7777\n";

    ssize_t written = write(fd, content, strlen(content));
    ASSERT(written > 0);
    close(fd);

    kernd_config_t *cfg = kernd_config_load(tmppath);
    ASSERT_NOT_NULL(cfg);

    ASSERT_EQ_INT(cfg->admin_port, 7777);
    ASSERT_EQ_INT(cfg->http_port, 80);       /* default */
    ASSERT_EQ_INT(cfg->https_port, 443);     /* default */
    ASSERT_EQ_STR(cfg->data_dir, "/var/lib/kernd");  /* default */
    ASSERT_EQ_STR(cfg->log_dir, "/var/log/kernd");   /* default */

    kernd_config_free(cfg);
    unlink(tmppath);
}

TEST(test_config_free_null) {
    /* Should not crash */
    kernd_config_free(NULL);
}

int main(void) {
    printf("test_kernd_config:\n");

    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_load_valid_toml);
    RUN_TEST(test_config_load_missing_file);
    RUN_TEST(test_config_load_null_path);
    RUN_TEST(test_config_partial_toml);
    RUN_TEST(test_config_free_null);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
