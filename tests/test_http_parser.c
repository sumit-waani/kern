/*
 * test_http_parser.c - Unit tests for HTTP/1.1 parser
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

TEST(test_parse_simple_get) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    const char *req = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);

    ASSERT(kern_http_parser_done(parser));
    ASSERT(!kern_http_parser_error(parser));
    ASSERT_EQ_INT(kern_http_parser_method(parser), KERN_METHOD_GET);
    ASSERT_EQ_STR(kern_http_parser_path(parser), "/hello");
    ASSERT_EQ_STR(kern_http_parser_version(parser), "HTTP/1.1");

    kern_dict_t *headers = kern_http_parser_headers(parser);
    ASSERT(headers != NULL);
    ASSERT_EQ_STR((const char *)kern_dict_get(headers, "host"), "localhost");

    kern_http_parser_free(parser);
}

TEST(test_parse_post_with_body) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    const char *req =
        "POST /submit HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "name=Alice&";

    /* Feed partial data: note that the body is 11 bytes ("name=Alice&") */
    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);

    ASSERT_EQ_INT(kern_http_parser_method(parser), KERN_METHOD_POST);
    ASSERT_EQ_STR(kern_http_parser_path(parser), "/submit");

    kern_str_t body = kern_http_parser_body(parser);
    ASSERT(body.data != NULL);
    ASSERT_EQ_INT((int)body.len, 11);
    ASSERT(memcmp(body.data, "name=Alice&", 11) == 0);

    kern_http_parser_free(parser);
}

TEST(test_parse_multiple_headers) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    const char *req =
        "GET /page HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept: text/html\r\n"
        "User-Agent: test/1.0\r\n"
        "X-Custom: hello-world\r\n"
        "\r\n";

    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);

    kern_dict_t *headers = kern_http_parser_headers(parser);
    ASSERT_EQ_STR((const char *)kern_dict_get(headers, "host"), "example.com");
    ASSERT_EQ_STR((const char *)kern_dict_get(headers, "accept"), "text/html");
    ASSERT_EQ_STR((const char *)kern_dict_get(headers, "user-agent"), "test/1.0");
    ASSERT_EQ_STR((const char *)kern_dict_get(headers, "x-custom"), "hello-world");

    kern_http_parser_free(parser);
}

TEST(test_parse_malformed_request) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Missing HTTP version */
    const char *req = "INVALID\r\n\r\n";
    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_ERROR);
    ASSERT(kern_http_parser_error(parser));

    kern_http_parser_free(parser);
}

TEST(test_parse_unknown_method) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    const char *req = "FROBNICATE /foo HTTP/1.1\r\nHost: x\r\n\r\n";
    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_ERROR);

    kern_http_parser_free(parser);
}

TEST(test_parse_query_string) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    const char *req = "GET /search?q=hello&page=2 HTTP/1.1\r\nHost: x\r\n\r\n";
    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);

    ASSERT_EQ_STR(kern_http_parser_path(parser), "/search");
    ASSERT_EQ_STR(kern_http_parser_query_string(parser), "q=hello&page=2");

    kern_http_parser_free(parser);
}

TEST(test_parse_incremental) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Send data in chunks */
    const char *part1 = "GET /inc";
    const char *part2 = "remental HTTP/1.1\r\n";
    const char *part3 = "Host: test\r\n\r\n";

    int rc = kern_http_parser_feed(parser, part1, strlen(part1));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_NEED_MORE);

    rc = kern_http_parser_feed(parser, part2, strlen(part2));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_NEED_MORE);

    rc = kern_http_parser_feed(parser, part3, strlen(part3));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);

    ASSERT_EQ_STR(kern_http_parser_path(parser), "/incremental");
    ASSERT_EQ_STR((const char *)kern_dict_get(kern_http_parser_headers(parser), "host"), "test");

    kern_http_parser_free(parser);
}

TEST(test_parse_all_methods) {
    const char *methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"};
    kern_method_t expected[] = {
        KERN_METHOD_GET, KERN_METHOD_POST, KERN_METHOD_PUT,
        KERN_METHOD_PATCH, KERN_METHOD_DELETE, KERN_METHOD_HEAD,
        KERN_METHOD_OPTIONS
    };

    for (int i = 0; i < 7; i++) {
        kern_http_parser_t *parser = kern_http_parser_new();
        ASSERT(parser != NULL);

        char req[128];
        snprintf(req, sizeof(req), "%s / HTTP/1.1\r\nHost: x\r\n\r\n", methods[i]);

        int rc = kern_http_parser_feed(parser, req, strlen(req));
        ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);
        ASSERT_EQ_INT(kern_http_parser_method(parser), expected[i]);

        kern_http_parser_free(parser);
    }
}

TEST(test_parse_body_incremental) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    const char *headers =
        "POST /data HTTP/1.1\r\n"
        "Content-Length: 10\r\n"
        "\r\n";

    int rc = kern_http_parser_feed(parser, headers, strlen(headers));
    /* Headers done but body not yet received */
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_NEED_MORE);

    const char *body = "0123456789";
    rc = kern_http_parser_feed(parser, body, strlen(body));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);

    kern_str_t parsed_body = kern_http_parser_body(parser);
    ASSERT_EQ_INT((int)parsed_body.len, 10);
    ASSERT(memcmp(parsed_body.data, "0123456789", 10) == 0);

    kern_http_parser_free(parser);
}

/* ============================================================
 * Size limit tests
 * ============================================================ */

TEST(test_oversized_request_line) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Create a request line longer than 8192 bytes */
    kern_buf_t *buf = kern_buf_new(16384);
    kern_buf_writes(buf, "GET /");
    /* Fill with 'a' to exceed 8192 byte limit */
    char fill[8200];
    memset(fill, 'a', 8199);
    fill[8199] = '\0';
    kern_buf_writes(buf, fill);
    kern_buf_writes(buf, " HTTP/1.1\r\nHost: x\r\n\r\n");

    int rc = kern_http_parser_feed(parser, kern_buf_data(buf), kern_buf_len(buf));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_ERROR);
    ASSERT(kern_http_parser_error(parser));

    kern_buf_free(buf);
    kern_http_parser_free(parser);
}

TEST(test_oversized_headers) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Create headers that exceed 65536 bytes total */
    kern_buf_t *buf = kern_buf_new(128000);
    kern_buf_writes(buf, "GET / HTTP/1.1\r\n");

    /* Add many headers to exceed 65KB */
    char header_line[256];
    for (int i = 0; i < 500; i++) {
        /* Each header line is about 140 bytes: "X-Header-NNNN: <128 chars value>\r\n" */
        char value[129];
        memset(value, 'v', 128);
        value[128] = '\0';
        snprintf(header_line, sizeof(header_line), "X-Header-%04d: %s\r\n", i, value);
        kern_buf_writes(buf, header_line);
    }
    kern_buf_writes(buf, "\r\n");

    int rc = kern_http_parser_feed(parser, kern_buf_data(buf), kern_buf_len(buf));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_ERROR);
    ASSERT(kern_http_parser_error(parser));

    kern_buf_free(buf);
    kern_http_parser_free(parser);
}

TEST(test_oversized_content_length) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Content-Length exceeding 10MB should be rejected */
    const char *req =
        "POST /upload HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 20000000\r\n"
        "\r\n";

    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_ERROR);
    ASSERT(kern_http_parser_error(parser));

    kern_http_parser_free(parser);
}

TEST(test_valid_body_within_limits) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* A small body well within limits should work fine */
    const char *req =
        "POST /data HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    int rc = kern_http_parser_feed(parser, req, strlen(req));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);
    ASSERT(kern_http_parser_done(parser));

    kern_str_t body = kern_http_parser_body(parser);
    ASSERT_EQ_INT((int)body.len, 5);
    ASSERT(memcmp(body.data, "hello", 5) == 0);

    kern_http_parser_free(parser);
}

TEST(test_request_line_at_limit) {
    kern_http_parser_t *parser = kern_http_parser_new();
    ASSERT(parser != NULL);

    /* Create a request line at exactly the limit (should pass) */
    /* "GET /" + path + " HTTP/1.1\r\n" = 5 + path_len + 11 */
    /* We want total line < 8192. Use path of 8170 chars: 5 + 8170 + 9 = 8184 */
    kern_buf_t *buf = kern_buf_new(16384);
    kern_buf_writes(buf, "GET /");
    char path[8171];
    memset(path, 'x', 8170);
    path[8170] = '\0';
    kern_buf_writes(buf, path);
    kern_buf_writes(buf, " HTTP/1.1\r\nHost: x\r\n\r\n");

    int rc = kern_http_parser_feed(parser, kern_buf_data(buf), kern_buf_len(buf));
    ASSERT_EQ_INT(rc, KERN_HTTP_PARSE_DONE);
    ASSERT(kern_http_parser_done(parser));

    kern_buf_free(buf);
    kern_http_parser_free(parser);
}

int main(void) {
    printf("test_http_parser:\n");

    RUN_TEST(test_parse_simple_get);
    RUN_TEST(test_parse_post_with_body);
    RUN_TEST(test_parse_multiple_headers);
    RUN_TEST(test_parse_malformed_request);
    RUN_TEST(test_parse_unknown_method);
    RUN_TEST(test_parse_query_string);
    RUN_TEST(test_parse_incremental);
    RUN_TEST(test_parse_all_methods);
    RUN_TEST(test_parse_body_incremental);
    RUN_TEST(test_oversized_request_line);
    RUN_TEST(test_oversized_headers);
    RUN_TEST(test_oversized_content_length);
    RUN_TEST(test_valid_body_within_limits);
    RUN_TEST(test_request_line_at_limit);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
