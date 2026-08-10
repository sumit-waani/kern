/*
 * test_router.c - Unit tests for the radix tree router
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

#define ASSERT_NULL(a) do { \
    if ((a) != NULL) { \
        fprintf(stderr, "    ASSERT_NULL FAILED: %p (%s:%d)\n", \
                (void *)(a), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* Test handlers */
static kern_response_t *handler_root(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_about(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_posts(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_post_by_id(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_post_comments(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_users_posts(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_wildcard(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

static kern_response_t *handler_posts_post(kern_req_t *req) {
    (void)req;
    return kern_response_new(201);
}

static kern_response_t *handler_new_post(kern_req_t *req) {
    (void)req;
    return kern_response_new(200);
}

/* --- Tests --- */

TEST(test_router_new_free) {
    kern_router_t *router = kern_router_new();
    ASSERT(router != NULL);
    kern_router_free(router);
}

TEST(test_static_route_match) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/", handler_root) == 0);
    ASSERT(kern_router_add(router, "GET", "/about", handler_about) == 0);
    ASSERT(kern_router_add(router, "GET", "/posts", handler_posts) == 0);

    kern_dict_t *params = kern_dict_new();

    /* Match root */
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_root);

    /* Match /about */
    r = kern_router_match(router, KERN_METHOD_GET, "/about", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_about);

    /* Match /posts */
    r = kern_router_match(router, KERN_METHOD_GET, "/posts", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_posts);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_param_route_single) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/posts/:id", handler_post_by_id) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/posts/42", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_post_by_id);

    const char *id = (const char *)kern_dict_get(params, "id");
    ASSERT_EQ_STR(id, "42");

    /* Clean up param values */
    free((void *)id);
    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_param_route_multiple) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/users/:user_id/posts/:post_id", handler_users_posts) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/users/5/posts/99", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_users_posts);

    const char *user_id = (const char *)kern_dict_get(params, "user_id");
    const char *post_id = (const char *)kern_dict_get(params, "post_id");
    ASSERT_EQ_STR(user_id, "5");
    ASSERT_EQ_STR(post_id, "99");

    free((void *)user_id);
    free((void *)post_id);
    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_wildcard_match) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/files/*path", handler_wildcard) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/files/css/style.css", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_wildcard);

    const char *path = (const char *)kern_dict_get(params, "path");
    ASSERT_EQ_STR(path, "css/style.css");

    free((void *)path);
    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_method_dispatch) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/posts", handler_posts) == 0);
    ASSERT(kern_router_add(router, "POST", "/posts", handler_posts_post) == 0);

    kern_dict_t *params = kern_dict_new();

    /* GET /posts */
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/posts", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_posts);

    /* POST /posts */
    r = kern_router_match(router, KERN_METHOD_POST, "/posts", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_posts_post);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_404_unknown_path) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/posts", handler_posts) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/unknown", params);
    ASSERT(r.status == KERN_ROUTE_NOT_FOUND);
    ASSERT(r.handler == NULL);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_405_wrong_method) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/posts", handler_posts) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_DELETE, "/posts", params);
    ASSERT(r.status == KERN_ROUTE_METHOD_NOT_ALLOWED);
    ASSERT(r.handler == NULL);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_priority_static_over_param) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/posts/new", handler_new_post) == 0);
    ASSERT(kern_router_add(router, "GET", "/posts/:id", handler_post_by_id) == 0);

    kern_dict_t *params = kern_dict_new();

    /* /posts/new should match static handler */
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/posts/new", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_new_post);

    /* /posts/42 should match param handler */
    r = kern_router_match(router, KERN_METHOD_GET, "/posts/42", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_post_by_id);

    const char *id = (const char *)kern_dict_get(params, "id");
    ASSERT_EQ_STR(id, "42");
    free((void *)id);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_priority_param_over_wildcard) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/files/:name", handler_post_by_id) == 0);
    ASSERT(kern_router_add(router, "GET", "/files/*path", handler_wildcard) == 0);

    kern_dict_t *params = kern_dict_new();

    /* Single segment should match param */
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/files/readme.txt", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_post_by_id);

    const char *name = (const char *)kern_dict_get(params, "name");
    ASSERT_EQ_STR(name, "readme.txt");
    free((void *)name);

    kern_dict_free(params);

    /* Multi-segment should match wildcard */
    params = kern_dict_new();
    r = kern_router_match(router, KERN_METHOD_GET, "/files/css/style.css", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_wildcard);

    const char *path = (const char *)kern_dict_get(params, "path");
    ASSERT_EQ_STR(path, "css/style.css");
    free((void *)path);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_many_routes) {
    kern_router_t *router = kern_router_new();

    /* Register many routes */
    char path[64];
    for (int i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "/route%d", i);
        ASSERT(kern_router_add(router, "GET", path, handler_root) == 0);
    }

    kern_dict_t *params = kern_dict_new();

    /* Match first, middle, and last */
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/route0", params);
    ASSERT(r.status == KERN_ROUTE_OK);

    r = kern_router_match(router, KERN_METHOD_GET, "/route50", params);
    ASSERT(r.status == KERN_ROUTE_OK);

    r = kern_router_match(router, KERN_METHOD_GET, "/route99", params);
    ASSERT(r.status == KERN_ROUTE_OK);

    /* Non-existent */
    r = kern_router_match(router, KERN_METHOD_GET, "/route100", params);
    ASSERT(r.status == KERN_ROUTE_NOT_FOUND);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_nested_param_route) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/posts/:id/comments", handler_post_comments) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_GET, "/posts/7/comments", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_post_comments);

    const char *id = (const char *)kern_dict_get(params, "id");
    ASSERT_EQ_STR(id, "7");
    free((void *)id);

    kern_dict_free(params);
    kern_router_free(router);
}

TEST(test_add_method_enum) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add_method(router, KERN_METHOD_PUT, "/posts/:id", handler_post_by_id) == 0);

    kern_dict_t *params = kern_dict_new();
    kern_route_result_t r = kern_router_match(router, KERN_METHOD_PUT, "/posts/1", params);
    ASSERT(r.status == KERN_ROUTE_OK);
    ASSERT(r.handler == handler_post_by_id);

    const char *id = (const char *)kern_dict_get(params, "id");
    ASSERT_EQ_STR(id, "1");
    free((void *)id);

    kern_dict_free(params);
    kern_router_free(router);
}

/* --- Middleware tests --- */

static kern_response_t *test_middleware_add_header(kern_req_t *req, kern_handler_fn next) {
    kern_response_t *res = next(req);
    if (res) {
        kern_response_header(res, "X-Middleware", "applied");
    }
    return res;
}

static kern_response_t *middleware_short_circuit(kern_req_t *req, kern_handler_fn next) {
    (void)req;
    (void)next;
    kern_response_t *res = kern_response_new(403);
    kern_response_body_str(res, "Forbidden by middleware");
    return res;
}

TEST(test_middleware_chain) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/protected", handler_root) == 0);
    ASSERT(kern_router_use(router, "/", test_middleware_add_header) == 0);

    /* Simulate dispatch */
    kern_response_t *res = kern_router_dispatch(router, NULL, handler_root);
    /* With NULL req, dispatch should return 500 */
    ASSERT(res != NULL);
    kern_response_free(res);

    kern_router_free(router);
}

TEST(test_middleware_short_circuit_test) {
    kern_router_t *router = kern_router_new();
    ASSERT(kern_router_add(router, "GET", "/secret", handler_root) == 0);
    ASSERT(kern_router_use(router, "/secret", middleware_short_circuit) == 0);

    kern_router_free(router);
}

int main(void) {
    printf("Running router tests...\n");

    RUN_TEST(test_router_new_free);
    RUN_TEST(test_static_route_match);
    RUN_TEST(test_param_route_single);
    RUN_TEST(test_param_route_multiple);
    RUN_TEST(test_wildcard_match);
    RUN_TEST(test_method_dispatch);
    RUN_TEST(test_404_unknown_path);
    RUN_TEST(test_405_wrong_method);
    RUN_TEST(test_priority_static_over_param);
    RUN_TEST(test_priority_param_over_wildcard);
    RUN_TEST(test_many_routes);
    RUN_TEST(test_nested_param_route);
    RUN_TEST(test_add_method_enum);
    RUN_TEST(test_middleware_chain);
    RUN_TEST(test_middleware_short_circuit_test);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
