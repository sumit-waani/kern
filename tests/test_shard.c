/*
 * test_shard.c - Unit tests for the shards system
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
    const char *_a = (a); const char *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                _a ? _a : "(null)", _b ? _b : "(null)", __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a); int _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %d != %d (%s:%d)\n", \
                _a, _b, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_NULL(a) do { \
    if ((a) != NULL) { \
        fprintf(stderr, "    ASSERT_NULL FAILED: %p (%s:%d)\n", \
                (void *)(a), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* Test handler for shard registration */
static kern_response_t *shard_handler(kern_req_t *req) {
    (void)req;
    return kern_shard_response(200, "<div>fragment</div>");
}

/* --- Tests --- */

TEST(test_shard_response_basic) {
    kern_response_t *res = kern_shard_response(200, "<p>Hello</p>");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);

    /* Verify the response serializes with correct headers */
    kern_buf_t *buf = kern_response_serialize(res);
    ASSERT(buf != NULL);

    const char *data = kern_buf_data(buf);
    ASSERT(strstr(data, "X-Kern-Shard: true") != NULL);
    ASSERT(strstr(data, "Content-Type: text/html; charset=utf-8") != NULL);
    ASSERT(strstr(data, "<p>Hello</p>") != NULL);

    kern_buf_free(buf);
    kern_response_free(res);
}

TEST(test_shard_response_status_codes) {
    /* Test 200 */
    kern_response_t *res = kern_shard_response(200, "ok");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);
    kern_response_free(res);

    /* Test 404 */
    res = kern_shard_response(404, "not found");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 404);
    kern_response_free(res);

    /* Test 500 */
    res = kern_shard_response(500, "error");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 500);
    kern_response_free(res);
}

TEST(test_shard_response_null_html) {
    kern_response_t *res = kern_shard_response(200, NULL);
    ASSERT_NULL(res);
}

TEST(test_shard_response_buf) {
    kern_buf_t *buf = kern_buf_new(64);
    ASSERT(buf != NULL);
    kern_buf_writes(buf, "<div>buffered content</div>");

    kern_response_t *res = kern_shard_response_buf(200, buf);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);

    /* Verify headers in serialized response */
    kern_buf_t *serial = kern_response_serialize(res);
    ASSERT(serial != NULL);

    const char *data = kern_buf_data(serial);
    ASSERT(strstr(data, "X-Kern-Shard: true") != NULL);
    ASSERT(strstr(data, "Content-Type: text/html; charset=utf-8") != NULL);
    ASSERT(strstr(data, "<div>buffered content</div>") != NULL);

    kern_buf_free(serial);
    kern_response_free(res);
    kern_buf_free(buf);
}

TEST(test_shard_response_buf_null) {
    kern_response_t *res = kern_shard_response_buf(200, NULL);
    ASSERT_NULL(res);
}

TEST(test_shard_register) {
    kern_router_t *router = kern_router_new();
    ASSERT(router != NULL);

    /* Register a shard handler */
    int result = kern_shard_register(router, "/shards/counter", shard_handler);
    ASSERT_EQ_INT(result, 0);

    /* Verify the handler is registered and matchable */
    kern_dict_t *params = kern_dict_new();
    kern_route_result_t match = kern_router_match(router, KERN_METHOD_GET,
                                                   "/shards/counter", params);
    ASSERT(match.status == KERN_ROUTE_OK);
    ASSERT(match.handler == shard_handler);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_shard_register_null_args) {
    kern_router_t *router = kern_router_new();

    ASSERT_EQ_INT(kern_shard_register(NULL, "/test", shard_handler), -1);
    ASSERT_EQ_INT(kern_shard_register(router, NULL, shard_handler), -1);
    ASSERT_EQ_INT(kern_shard_register(router, "/test", NULL), -1);

    kern_router_free(router);
}

TEST(test_shard_register_with_params) {
    kern_router_t *router = kern_router_new();

    /* Shards can have route params just like normal routes */
    int result = kern_shard_register(router, "/shards/item/:id", shard_handler);
    ASSERT_EQ_INT(result, 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t match = kern_router_match(router, KERN_METHOD_GET,
                                                   "/shards/item/42", params);
    ASSERT(match.status == KERN_ROUTE_OK);
    ASSERT(match.handler == shard_handler);

    const char *id = (const char *)kern_dict_get(params, "id");
    ASSERT_EQ_STR(id, "42");
    free((void *)id);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_is_shard_request_null) {
    /* NULL request should return false */
    ASSERT(kern_is_shard_request(NULL) == false);
}

TEST(test_shards_js_content) {
    /* Verify the embedded JS content is available */
    const char *js = kern_shards_js();
    ASSERT(js != NULL);
    ASSERT(strlen(js) > 0);

    /* Should contain signature content */
    ASSERT(strstr(js, "kern-shards.js") != NULL);
    ASSERT(strstr(js, "data-shard") != NULL);
    ASSERT(strstr(js, "X-Kern-Shard") != NULL);
}

TEST(test_shards_js_len) {
    size_t len = kern_shards_js_len();
    ASSERT(len > 0);
    ASSERT(len < 12000); /* Must be under 12KB */
    ASSERT(len == strlen(kern_shards_js()));
}

TEST(test_shards_js_serve) {
    kern_response_t *res = kern_shards_js_serve(NULL);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);

    kern_buf_t *buf = kern_response_serialize(res);
    ASSERT(buf != NULL);

    const char *data = kern_buf_data(buf);
    ASSERT(strstr(data, "Content-Type: application/javascript; charset=utf-8") != NULL);
    ASSERT(strstr(data, "Cache-Control: public, max-age=86400") != NULL);
    ASSERT(strstr(data, "kern-shards.js") != NULL);

    kern_buf_free(buf);
    kern_response_free(res);
}

int main(void) {
    printf("Running shard tests...\n");

    RUN_TEST(test_shard_response_basic);
    RUN_TEST(test_shard_response_status_codes);
    RUN_TEST(test_shard_response_null_html);
    RUN_TEST(test_shard_response_buf);
    RUN_TEST(test_shard_response_buf_null);
    RUN_TEST(test_shard_register);
    RUN_TEST(test_shard_register_null_args);
    RUN_TEST(test_shard_register_with_params);
    RUN_TEST(test_is_shard_request_null);
    RUN_TEST(test_shards_js_content);
    RUN_TEST(test_shards_js_len);
    RUN_TEST(test_shards_js_serve);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
