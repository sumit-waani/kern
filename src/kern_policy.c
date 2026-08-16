/*
 * kern_policy.c - Authorization policy helpers
 *
 * Provides macros and functions for declarative authorization checks.
 * Policies return allow/deny results, and the KERN_AUTHORIZE macro
 * short-circuits handler execution on denial with a 403 response.
 */

#include "kern.h"

#include <stdlib.h>
#include <string.h>

kern_policy_result_t kern_allow(void) {
    kern_policy_result_t r;
    r.allowed = true;
    r.reason = NULL;
    return r;
}

kern_policy_result_t kern_deny(const char *reason) {
    kern_policy_result_t r;
    r.allowed = false;
    r.reason = reason;
    return r;
}

kern_response_t *kern_response_forbidden(const char *reason) {
    kern_response_t *res = kern_response_new(403);
    if (!res) return NULL;
    kern_response_header(res, "Content-Type", "text/plain");
    if (reason) {
        kern_response_body_str(res, reason);
    } else {
        kern_response_body_str(res, "403 Forbidden");
    }
    return res;
}
