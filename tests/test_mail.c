/*
 * test_mail.c - Unit tests for kern_mail
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

/* ============================================================
 * Tests
 * ============================================================ */

TEST(test_mail_new_free) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);
    kern_mail_free(mail);
}

TEST(test_mail_setters_getters) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);

    kern_mail_to(mail, "user@example.com");
    kern_mail_from(mail, "sender@example.com");
    kern_mail_subject(mail, "Hello World");
    kern_mail_body(mail, "This is the body.");

    ASSERT_EQ_STR(kern_mail_get_to(mail), "user@example.com");
    ASSERT_EQ_STR(kern_mail_get_from(mail), "sender@example.com");
    ASSERT_EQ_STR(kern_mail_get_subject(mail), "Hello World");
    ASSERT_EQ_STR(kern_mail_get_body(mail), "This is the body.");

    kern_mail_free(mail);
}

TEST(test_mail_overwrite_fields) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);

    kern_mail_to(mail, "first@example.com");
    kern_mail_to(mail, "second@example.com");
    ASSERT_EQ_STR(kern_mail_get_to(mail), "second@example.com");

    kern_mail_subject(mail, "Subject 1");
    kern_mail_subject(mail, "Subject 2");
    ASSERT_EQ_STR(kern_mail_get_subject(mail), "Subject 2");

    kern_mail_free(mail);
}

TEST(test_mail_null_getters) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);

    /* Before setting, getters return NULL */
    ASSERT(kern_mail_get_to(mail) == NULL);
    ASSERT(kern_mail_get_from(mail) == NULL);
    ASSERT(kern_mail_get_subject(mail) == NULL);
    ASSERT(kern_mail_get_body(mail) == NULL);

    kern_mail_free(mail);
}

TEST(test_mail_null_safety) {
    /* NULL mail should not crash */
    kern_mail_free(NULL);
    kern_mail_to(NULL, "x");
    kern_mail_from(NULL, "x");
    kern_mail_subject(NULL, "x");
    kern_mail_body(NULL, "x");
    kern_mail_header(NULL, "k", "v");

    ASSERT(kern_mail_get_to(NULL) == NULL);
    ASSERT(kern_mail_get_from(NULL) == NULL);
    ASSERT(kern_mail_get_subject(NULL) == NULL);
    ASSERT(kern_mail_get_body(NULL) == NULL);

    /* NULL values should not crash */
    kern_mail_t *mail = kern_mail_new();
    kern_mail_to(mail, NULL);
    kern_mail_from(mail, NULL);
    kern_mail_subject(mail, NULL);
    kern_mail_body(mail, NULL);
    kern_mail_header(mail, NULL, NULL);
    kern_mail_free(mail);
}

TEST(test_mail_headers) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);

    kern_mail_header(mail, "X-Custom", "value1");
    kern_mail_header(mail, "X-Another", "value2");

    /* No crash, headers are stored internally */
    kern_mail_free(mail);
}

TEST(test_mail_send_log) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);

    kern_mail_to(mail, "test@test.com");
    kern_mail_from(mail, "from@test.com");
    kern_mail_subject(mail, "Test Subject");
    kern_mail_body(mail, "Test Body Content");

    /* Log driver should succeed without crashing */
    int rc = kern_mail_send_log(mail);
    ASSERT(rc == 0);

    kern_mail_free(mail);
}

TEST(test_mail_send_log_null) {
    int rc = kern_mail_send_log(NULL);
    ASSERT(rc == -1);
}

TEST(test_mail_send_null_cfg_uses_log) {
    kern_mail_t *mail = kern_mail_new();
    ASSERT(mail != NULL);

    kern_mail_to(mail, "test@test.com");
    kern_mail_from(mail, "from@test.com");
    kern_mail_body(mail, "body");

    /* NULL config should use log driver */
    int rc = kern_mail_send(mail, NULL);
    ASSERT(rc == 0);

    kern_mail_free(mail);
}

TEST(test_mail_send_null_mail) {
    int rc = kern_mail_send(NULL, NULL);
    ASSERT(rc == -1);
}

TEST(test_smtp_send_null) {
    /* NULL params should return -1 */
    ASSERT(kern_smtp_send(NULL, NULL) == -1);

    kern_smtp_config_t cfg = {0};
    ASSERT(kern_smtp_send(&cfg, NULL) == -1);

    kern_mail_t *mail = kern_mail_new();
    ASSERT(kern_smtp_send(NULL, mail) == -1);
    kern_mail_free(mail);
}

TEST(test_smtp_send_no_host) {
    kern_smtp_config_t cfg = {0};
    cfg.host = NULL;
    cfg.port = 25;

    kern_mail_t *mail = kern_mail_new();
    kern_mail_to(mail, "user@test.com");
    kern_mail_from(mail, "from@test.com");

    /* Should fail because host is NULL */
    int rc = kern_smtp_send(&cfg, mail);
    ASSERT(rc == -1);

    kern_mail_free(mail);
}

TEST(test_smtp_send_unreachable) {
    /* Attempt to connect to an unreachable address should fail gracefully */
    kern_smtp_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.port = 19999;  /* Unlikely to be listening */

    kern_mail_t *mail = kern_mail_new();
    kern_mail_to(mail, "user@test.com");
    kern_mail_from(mail, "from@test.com");
    kern_mail_body(mail, "test");

    int rc = kern_smtp_send(&cfg, mail);
    ASSERT(rc == -1);

    kern_mail_free(mail);
}

int main(void) {
    printf("test_mail:\n");

    RUN_TEST(test_mail_new_free);
    RUN_TEST(test_mail_setters_getters);
    RUN_TEST(test_mail_overwrite_fields);
    RUN_TEST(test_mail_null_getters);
    RUN_TEST(test_mail_null_safety);
    RUN_TEST(test_mail_headers);
    RUN_TEST(test_mail_send_log);
    RUN_TEST(test_mail_send_log_null);
    RUN_TEST(test_mail_send_null_cfg_uses_log);
    RUN_TEST(test_mail_send_null_mail);
    RUN_TEST(test_smtp_send_null);
    RUN_TEST(test_smtp_send_no_host);
    RUN_TEST(test_smtp_send_unreachable);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
