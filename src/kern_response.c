/*
 * kern_response.c - HTTP response builder
 *
 * Builds HTTP/1.1 responses with status line, headers, and body.
 * Provides serialization to wire format for writing to TCP connections.
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct kern_response {
    int status;
    kern_dict_t *headers;
    char *body;
    size_t body_len;
};

static const char *status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

kern_response_t *kern_response_new(int status) {
    kern_response_t *res = calloc(1, sizeof(kern_response_t));
    if (!res) return NULL;

    res->status = status;
    res->headers = kern_dict_new();
    if (!res->headers) {
        free(res);
        return NULL;
    }
    res->body = NULL;
    res->body_len = 0;

    return res;
}

void kern_response_header(kern_response_t *res, const char *key, const char *val) {
    if (!res || !key || !val) return;

    /* Free old value if replacing */
    char *old = (char *)kern_dict_get(res->headers, key);
    if (old) {
        free(old);
    }

    size_t len = strlen(val);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, val, len + 1);
    kern_dict_set(res->headers, key, copy);
}

void kern_response_body(kern_response_t *res, const char *data, size_t len) {
    if (!res) return;
    free(res->body);
    if (!data || len == 0) {
        res->body = NULL;
        res->body_len = 0;
        return;
    }
    res->body = malloc(len);
    if (!res->body) {
        res->body_len = 0;
        return;
    }
    memcpy(res->body, data, len);
    res->body_len = len;
}

void kern_response_body_str(kern_response_t *res, const char *str) {
    if (!res || !str) return;
    kern_response_body(res, str, strlen(str));
}

/* Free a header value during iteration */
static bool free_header_value(const char *key, void *value, void *userdata) {
    (void)key;
    (void)userdata;
    free(value);
    return true;
}

void kern_response_free(kern_response_t *res) {
    if (!res) return;
    if (res->headers) {
        kern_dict_iter(res->headers, free_header_value, NULL);
        kern_dict_free(res->headers);
    }
    free(res->body);
    free(res);
}

int kern_response_status(const kern_response_t *res) {
    if (!res) return 0;
    return res->status;
}

/* Callback context for serializing headers */
typedef struct {
    kern_buf_t *buf;
    int error;
} serialize_ctx_t;

static bool serialize_header(const char *key, void *value, void *userdata) {
    serialize_ctx_t *ctx = (serialize_ctx_t *)userdata;
    if (ctx->error) return false;

    if (kern_buf_writes(ctx->buf, key) != 0 ||
        kern_buf_writes(ctx->buf, ": ") != 0 ||
        kern_buf_writes(ctx->buf, (const char *)value) != 0 ||
        kern_buf_writes(ctx->buf, "\r\n") != 0) {
        ctx->error = 1;
        return false;
    }
    return true;
}

kern_buf_t *kern_response_serialize(kern_response_t *res) {
    if (!res) return NULL;

    kern_buf_t *buf = kern_buf_new(256);
    if (!buf) return NULL;

    /* Status line */
    kern_buf_writef(buf, "HTTP/1.1 %d %s\r\n", res->status, status_text(res->status));

    /* Auto-set Content-Length if not already set */
    if (!kern_dict_has(res->headers, "Content-Length")) {
        char cl_buf[32];
        snprintf(cl_buf, sizeof(cl_buf), "%zu", res->body_len);
        kern_response_header(res, "Content-Length", cl_buf);
    }

    /* Auto-set Server */
    if (!kern_dict_has(res->headers, "Server")) {
        kern_response_header(res, "Server", "kern/0.1");
    }

    /* Auto-set Date */
    if (!kern_dict_has(res->headers, "Date")) {
        time_t now = time(NULL);
        struct tm tm;
        gmtime_r(&now, &tm);
        char date_buf[64];
        strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
        kern_response_header(res, "Date", date_buf);
    }

    /* Serialize headers */
    serialize_ctx_t ctx = {buf, 0};
    kern_dict_iter(res->headers, serialize_header, &ctx);
    if (ctx.error) {
        kern_buf_free(buf);
        return NULL;
    }

    /* End of headers */
    kern_buf_writes(buf, "\r\n");

    /* Body */
    if (res->body && res->body_len > 0) {
        kern_buf_write(buf, res->body, res->body_len);
    }

    return buf;
}

kern_response_t *kern_404_response(void) {
    kern_response_t *res = kern_response_new(404);
    if (!res) return NULL;
    kern_response_header(res, "Content-Type", "text/plain");
    kern_response_body_str(res, "404 Not Found");
    return res;
}

kern_response_t *kern_500_response(const char *msg) {
    kern_response_t *res = kern_response_new(500);
    if (!res) return NULL;
    kern_response_header(res, "Content-Type", "text/plain");
    if (msg) {
        kern_response_body_str(res, msg);
    } else {
        kern_response_body_str(res, "500 Internal Server Error");
    }
    return res;
}

kern_response_t *kern_redirect_response(const char *url, int status) {
    if (!url) return NULL;
    if (status < 300 || status > 399) {
        status = 302;
    }
    kern_response_t *res = kern_response_new(status);
    if (!res) return NULL;
    kern_response_header(res, "Location", url);
    kern_response_header(res, "Content-Type", "text/plain");
    kern_response_body_str(res, "Redirecting...");
    return res;
}
