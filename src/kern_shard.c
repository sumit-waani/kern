/*
 * kern_shard.c - Server-side shard support
 *
 * Shards are route handlers that return HTML fragments without layout.
 * They are the server-side counterpart to kern-shards.js, providing
 * HTMX-style server-rendered interactivity.
 */

#include "kern.h"

#include <stdlib.h>
#include <string.h>

kern_response_t *kern_shard_response(int status, const char *html) {
    if (!html) return NULL;

    kern_response_t *res = kern_response_new(status);
    if (!res) return NULL;

    kern_response_header(res, "X-Kern-Shard", "true");
    kern_response_header(res, "Content-Type", "text/html; charset=utf-8");
    kern_response_body_str(res, html);

    return res;
}

kern_response_t *kern_shard_response_buf(int status, kern_buf_t *buf) {
    if (!buf) return NULL;

    kern_response_t *res = kern_response_new(status);
    if (!res) return NULL;

    kern_response_header(res, "X-Kern-Shard", "true");
    kern_response_header(res, "Content-Type", "text/html; charset=utf-8");

    const char *data = kern_buf_data(buf);
    size_t len = kern_buf_len(buf);
    kern_response_body(res, data, len);

    return res;
}

int kern_shard_register(kern_router_t *router, const char *path, kern_handler_fn handler) {
    if (!router || !path || !handler) return -1;

    /* Shards respond to GET requests by default */
    return kern_router_add(router, "GET", path, handler);
}

bool kern_is_shard_request(const kern_req_t *req) {
    if (!req) return false;

    const char *val = kern_header(req, "X-Kern-Shard");
    if (!val) return false;

    return (strcmp(val, "true") == 0);
}
