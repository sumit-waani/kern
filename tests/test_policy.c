/*
 * test_policy.c - Unit tests for kern_policy (authorization)
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

#define ASSERT_EQ_INT(a, b) do { \
    long long a_ = (long long)(a); \
    long long b_ = (long long)(b); \
    if (a_ != b_) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %lld != %lld (%s:%d)\n", \
                a_, b_, __FILE__, __LINE__); \
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
 * Tests for kern_allow / kern_deny
 * ============================================================ */

TEST(test_kern_allow) {
    kern_policy_result_t r = kern_allow();
    ASSERT(r.allowed == true);
    ASSERT(r.reason == NULL);
}

TEST(test_kern_deny) {
    kern_policy_result_t r = kern_deny("forbidden");
    ASSERT(r.allowed == false);
    ASSERT(r.reason != NULL);
    ASSERT_EQ_STR(r.reason, "forbidden");
}

TEST(test_kern_deny_null_reason) {
    kern_policy_result_t r = kern_deny(NULL);
    ASSERT(r.allowed == false);
    ASSERT(r.reason == NULL);
}

/* ============================================================
 * Tests for KERN_POLICY macro
 * ============================================================ */

/* Define a policy that always allows */
KERN_POLICY(always_allow) {
    (void)req;
    (void)resource;
    return kern_allow();
}

/* Define a policy that always denies */
KERN_POLICY(always_deny) {
    (void)req;
    (void)resource;
    return kern_deny("not_authorized");
}

/* Define a policy that checks the resource value */
KERN_POLICY(check_resource) {
    (void)req;
    int *val = (int *)resource;
    if (val && *val == 42) return kern_allow();
    return kern_deny("wrong_value");
}

TEST(test_kern_policy_macro_allow) {
    kern_policy_result_t r = always_allow_policy(NULL, NULL);
    ASSERT(r.allowed == true);
    ASSERT(r.reason == NULL);
}

TEST(test_kern_policy_macro_deny) {
    kern_policy_result_t r = always_deny_policy(NULL, NULL);
    ASSERT(r.allowed == false);
    ASSERT_EQ_STR(r.reason, "not_authorized");
}

TEST(test_kern_policy_macro_resource) {
    int val = 42;
    kern_policy_result_t r = check_resource_policy(NULL, &val);
    ASSERT(r.allowed == true);

    int bad_val = 99;
    r = check_resource_policy(NULL, &bad_val);
    ASSERT(r.allowed == false);
    ASSERT_EQ_STR(r.reason, "wrong_value");
}

/* ============================================================
 * Tests for kern_response_forbidden
 * ============================================================ */

TEST(test_response_forbidden_with_reason) {
    kern_response_t *res = kern_response_forbidden("access_denied");
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 403);
    kern_response_free(res);
}

TEST(test_response_forbidden_null_reason) {
    kern_response_t *res = kern_response_forbidden(NULL);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 403);
    kern_response_free(res);
}

/* ============================================================
 * Tests for KERN_AUTHORIZE macro
 * ============================================================ */

/*
 * KERN_AUTHORIZE must be used inside a function returning kern_response_t*.
 * We create wrapper functions to test both allowed and denied paths.
 */
static kern_response_t *handler_with_allow(kern_req_t *req) {
    KERN_AUTHORIZE(req, always_allow, NULL);
    /* If we get here, authorization passed */
    return kern_response_new(200);
}

static kern_response_t *handler_with_deny(kern_req_t *req) {
    KERN_AUTHORIZE(req, always_deny, NULL);
    /* Should not reach here */
    return kern_response_new(200);
}

static kern_response_t *handler_with_resource_check(kern_req_t *req) {
    int val = 42;
    KERN_AUTHORIZE(req, check_resource, &val);
    return kern_response_new(200);
}

static kern_response_t *handler_with_bad_resource(kern_req_t *req) {
    int val = 0;
    KERN_AUTHORIZE(req, check_resource, &val);
    return kern_response_new(200);
}

TEST(test_authorize_allows) {
    kern_response_t *res = handler_with_allow(NULL);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);
    kern_response_free(res);
}

TEST(test_authorize_denies) {
    kern_response_t *res = handler_with_deny(NULL);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 403);
    kern_response_free(res);
}

TEST(test_authorize_resource_pass) {
    kern_response_t *res = handler_with_resource_check(NULL);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 200);
    kern_response_free(res);
}

TEST(test_authorize_resource_fail) {
    kern_response_t *res = handler_with_bad_resource(NULL);
    ASSERT(res != NULL);
    ASSERT_EQ_INT(kern_response_status(res), 403);
    kern_response_free(res);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("test_policy:\n");

    RUN_TEST(test_kern_allow);
    RUN_TEST(test_kern_deny);
    RUN_TEST(test_kern_deny_null_reason);
    RUN_TEST(test_kern_policy_macro_allow);
    RUN_TEST(test_kern_policy_macro_deny);
    RUN_TEST(test_kern_policy_macro_resource);
    RUN_TEST(test_response_forbidden_with_reason);
    RUN_TEST(test_response_forbidden_null_reason);
    RUN_TEST(test_authorize_allows);
    RUN_TEST(test_authorize_denies);
    RUN_TEST(test_authorize_resource_pass);
    RUN_TEST(test_authorize_resource_fail);

    printf("  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
