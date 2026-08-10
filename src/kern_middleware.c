/*
 * kern_middleware.c - Middleware support
 *
 * Middleware is implemented as part of the router dispatch mechanism.
 * This file provides utility functions for common middleware patterns.
 * The core middleware chain logic lives in kern_router.c (kern_router_dispatch).
 */

#include "kern.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* Logging middleware - logs request method and path */
kern_response_t *kern_middleware_logger(kern_req_t *req, kern_handler_fn next) {
    if (!req || !next) return kern_500_response("Middleware error");

    const char *path = kern_req_path(req);
    kern_method_t method = kern_req_method(req);
    const char *method_str = "UNKNOWN";

    switch (method) {
        case KERN_METHOD_GET:     method_str = "GET"; break;
        case KERN_METHOD_POST:    method_str = "POST"; break;
        case KERN_METHOD_PUT:     method_str = "PUT"; break;
        case KERN_METHOD_PATCH:   method_str = "PATCH"; break;
        case KERN_METHOD_DELETE:  method_str = "DELETE"; break;
        case KERN_METHOD_HEAD:    method_str = "HEAD"; break;
        case KERN_METHOD_OPTIONS: method_str = "OPTIONS"; break;
    }

    fprintf(stderr, "[kern] %s %s\n", method_str, path ? path : "/");

    return next(req);
}
