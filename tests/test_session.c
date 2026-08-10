/*
 * test_session.c - Unit tests for kern_session
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

/* Helper: create a fake request with a given cookie header */
static kern_req_t *make_req_with_cookie(const char *cookie) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Build a minimal HTTP request with optional cookie */
    kern_buf_t *buf = kern_buf_new(512);
    kern_buf_writes(buf, "GET / HTTP/1.1\r\nHost: localhost\r\n");
    if (cookie) {
        kern_buf_writef(buf, "Cookie: %s\r\n", cookie);
    }
    kern_buf_writes(buf, "\r\n");

    int rc = kern_http_parser_feed(parser, kern_buf_data(buf), kern_buf_len(buf));
    ASSERT(rc == KERN_HTTP_PARSE_DONE);
    kern_buf_free(buf);

    kern_req_t *req = kern_req_new(parser);
    ASSERT(req != NULL);
    return req;
}

TEST(test_session_new) {
    kern_session_init();

    kern_req_t *req = make_req_with_cookie(NULL);
    kern_session_t *session = kern_session_start(req);
    ASSERT(session != NULL);

    const char *id = kern_session_id(session);
    ASSERT(id != NULL);
    ASSERT(strlen(id) == 64);  /* 32 bytes = 64 hex chars */

    kern_req_free(req);
}

TEST(test_session_set_get) {
    kern_session_init();

    kern_req_t *req = make_req_with_cookie(NULL);
    kern_session_t *session = kern_session_start(req);
    ASSERT(session != NULL);

    kern_session_set(session, "username", "alice");
    kern_session_set(session, "role", "admin");

    ASSERT_EQ_STR(kern_session_get(session, "username"), "alice");
    ASSERT_EQ_STR(kern_session_get(session, "role"), "admin");
    ASSERT(kern_session_get(session, "nonexistent") == NULL);

    kern_req_free(req);
}

TEST(test_session_persist) {
    kern_session_init();

    /* Create a session */
    kern_req_t *req1 = make_req_with_cookie(NULL);
    kern_session_t *session1 = kern_session_start(req1);
    ASSERT(session1 != NULL);

    const char *id = kern_session_id(session1);
    char id_copy[128];
    snprintf(id_copy, sizeof(id_copy), "%s", id);

    kern_session_set(session1, "counter", "1");

    /* Simulate a second request with the session cookie */
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "kern_session=%s", id_copy);

    kern_req_t *req2 = make_req_with_cookie(cookie);
    kern_session_t *session2 = kern_session_start(req2);
    ASSERT(session2 != NULL);

    /* Should be the same session */
    ASSERT_EQ_STR(kern_session_id(session2), id_copy);
    ASSERT_EQ_STR(kern_session_get(session2, "counter"), "1");

    kern_req_free(req1);
    kern_req_free(req2);
}

TEST(test_session_destroy) {
    kern_session_init();

    kern_req_t *req = make_req_with_cookie(NULL);
    kern_session_t *session = kern_session_start(req);
    ASSERT(session != NULL);

    const char *id = kern_session_id(session);
    char id_copy[128];
    snprintf(id_copy, sizeof(id_copy), "%s", id);

    kern_session_set(session, "data", "value");

    /* Destroy session */
    kern_session_destroy(id_copy);

    /* New request with the old session ID should create a new session */
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "kern_session=%s", id_copy);

    kern_req_t *req2 = make_req_with_cookie(cookie);
    kern_session_t *session2 = kern_session_start(req2);
    ASSERT(session2 != NULL);

    /* Should be a different session */
    ASSERT(strcmp(kern_session_id(session2), id_copy) != 0);
    ASSERT(kern_session_get(session2, "data") == NULL);

    kern_req_free(req);
    kern_req_free(req2);
}

TEST(test_session_cookie_header) {
    kern_session_init();

    kern_req_t *req = make_req_with_cookie(NULL);
    kern_session_t *session = kern_session_start(req);
    ASSERT(session != NULL);

    const char *cookie = kern_session_cookie(session);
    ASSERT(cookie != NULL);
    ASSERT(strstr(cookie, "kern_session=") != NULL);
    ASSERT(strstr(cookie, "HttpOnly") != NULL);
    ASSERT(strstr(cookie, "SameSite=Lax") != NULL);
    ASSERT(strstr(cookie, "Path=/") != NULL);

    kern_req_free(req);
}

int main(void) {
    printf("test_session:\n");

    RUN_TEST(test_session_new);
    RUN_TEST(test_session_set_get);
    RUN_TEST(test_session_persist);
    RUN_TEST(test_session_destroy);
    RUN_TEST(test_session_cookie_header);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
