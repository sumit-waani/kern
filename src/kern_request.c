/*
 * kern_request.c - HTTP request context
 *
 * Wraps parsed HTTP data into a convenient request object
 * with accessors for method, path, headers, body, query
 * parameters, route parameters, and form data.
 */

#include "kern.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct kern_req {
    kern_method_t method;
    char *path;
    char *query_string;
    kern_dict_t *headers;       /* Owned by parser, borrowed here */
    kern_str_t body;
    kern_dict_t *params;        /* Route parameters (owned) */
    kern_dict_t *query_params;  /* Parsed query string params (lazy, owned) */
    kern_dict_t *form_params;   /* Parsed form body params (lazy, owned) */
    kern_http_parser_t *parser; /* Owns the parser (and thus the raw data) */
};

kern_req_t *kern_req_new(kern_http_parser_t *parser) {
    if (!parser) return NULL;

    kern_req_t *req = calloc(1, sizeof(kern_req_t));
    if (!req) return NULL;

    req->parser = parser;
    req->method = kern_http_parser_method(parser);

    const char *path = kern_http_parser_path(parser);
    if (path) {
        size_t len = strlen(path);
        req->path = malloc(len + 1);
        if (req->path) {
            memcpy(req->path, path, len + 1);
        }
    }

    const char *qs = kern_http_parser_query_string(parser);
    if (qs) {
        size_t len = strlen(qs);
        req->query_string = malloc(len + 1);
        if (req->query_string) {
            memcpy(req->query_string, qs, len + 1);
        }
    }

    req->headers = kern_http_parser_headers(parser);
    req->body = kern_http_parser_body(parser);
    req->params = kern_dict_new_with_free(free);
    req->query_params = NULL;
    req->form_params = NULL;

    return req;
}

void kern_req_free(kern_req_t *req) {
    if (!req) return;

    free(req->path);
    free(req->query_string);

    if (req->params) {
        kern_dict_free(req->params);
    }
    if (req->query_params) {
        kern_dict_free(req->query_params);
    }
    if (req->form_params) {
        kern_dict_free(req->form_params);
    }
    if (req->parser) {
        kern_http_parser_free(req->parser);
    }

    free(req);
}

kern_method_t kern_req_method(const kern_req_t *req) {
    if (!req) return KERN_METHOD_GET;
    return req->method;
}

const char *kern_req_path(const kern_req_t *req) {
    if (!req) return NULL;
    return req->path;
}

const char *kern_req_query_string(const kern_req_t *req) {
    if (!req) return NULL;
    return req->query_string;
}

/* URL-decode a string in place. Returns new length. */
static size_t url_decode_inplace(char *str, size_t len) {
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len) {
            char hi = str[i + 1];
            char lo = str[i + 2];
            int h = -1, l = -1;
            if (hi >= '0' && hi <= '9') h = hi - '0';
            else if (hi >= 'a' && hi <= 'f') h = hi - 'a' + 10;
            else if (hi >= 'A' && hi <= 'F') h = hi - 'A' + 10;
            if (lo >= '0' && lo <= '9') l = lo - '0';
            else if (lo >= 'a' && lo <= 'f') l = lo - 'a' + 10;
            else if (lo >= 'A' && lo <= 'F') l = lo - 'A' + 10;
            if (h >= 0 && l >= 0) {
                str[out++] = (char)((h << 4) | l);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            str[out++] = ' ';
            continue;
        }
        str[out++] = str[i];
    }
    str[out] = '\0';
    return out;
}

/* Parse URL-encoded key=value pairs into a dict */
static kern_dict_t *parse_url_encoded(const char *data, size_t len) {
    kern_dict_t *dict = kern_dict_new_with_free(free);
    if (!dict) return NULL;

    size_t i = 0;
    while (i < len) {
        /* Find key */
        size_t key_start = i;
        while (i < len && data[i] != '=' && data[i] != '&') i++;
        size_t key_len = i - key_start;

        char *key = malloc(key_len + 1);
        if (!key) break;
        memcpy(key, data + key_start, key_len);
        key[key_len] = '\0';
        url_decode_inplace(key, key_len);

        char *val = NULL;
        if (i < len && data[i] == '=') {
            i++; /* skip '=' */
            size_t val_start = i;
            while (i < len && data[i] != '&') i++;
            size_t val_len = i - val_start;

            val = malloc(val_len + 1);
            if (!val) {
                free(key);
                break;
            }
            memcpy(val, data + val_start, val_len);
            val[val_len] = '\0';
            url_decode_inplace(val, val_len);
        } else {
            val = malloc(1);
            if (!val) {
                free(key);
                break;
            }
            val[0] = '\0';
        }

        kern_dict_set(dict, key, val);
        free(key);

        if (i < len && data[i] == '&') i++;
    }

    return dict;
}

static void ensure_query_params(kern_req_t *req) {
    if (req->query_params) return;
    if (!req->query_string || req->query_string[0] == '\0') {
        req->query_params = kern_dict_new_with_free(free);
        return;
    }
    req->query_params = parse_url_encoded(req->query_string, strlen(req->query_string));
}

static void ensure_form_params(kern_req_t *req) {
    if (req->form_params) return;

    /* Only parse if content-type is application/x-www-form-urlencoded */
    const char *ct = kern_header(req, "content-type");
    if (!ct || strstr(ct, "application/x-www-form-urlencoded") == NULL) {
        req->form_params = kern_dict_new_with_free(free);
        return;
    }

    if (req->body.data && req->body.len > 0) {
        req->form_params = parse_url_encoded(req->body.data, req->body.len);
    } else {
        req->form_params = kern_dict_new_with_free(free);
    }
}

const char *kern_param(const kern_req_t *req, const char *name) {
    if (!req || !name || !req->params) return NULL;
    return (const char *)kern_dict_get(req->params, name);
}

const char *kern_query(kern_req_t *req, const char *name) {
    if (!req || !name) return NULL;
    ensure_query_params(req);
    if (!req->query_params) return NULL;
    return (const char *)kern_dict_get(req->query_params, name);
}

const char *kern_header(const kern_req_t *req, const char *name) {
    if (!req || !name || !req->headers) return NULL;

    /* Headers are stored lowercase, so convert name to lowercase */
    size_t len = strlen(name);
    char *lower = malloc(len + 1);
    if (!lower) return NULL;
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)name[i]);
    }
    lower[len] = '\0';

    const char *val = (const char *)kern_dict_get(req->headers, lower);
    free(lower);
    return val;
}

kern_str_t kern_body(const kern_req_t *req) {
    kern_str_t empty = {NULL, 0};
    if (!req) return empty;
    return req->body;
}

const char *kern_form_str(kern_req_t *req, const char *name) {
    if (!req || !name) return NULL;
    ensure_form_params(req);
    if (!req->form_params) return NULL;
    return (const char *)kern_dict_get(req->form_params, name);
}

void kern_req_set_param(kern_req_t *req, const char *name, const char *value) {
    if (!req || !name || !value || !req->params) return;
    size_t vlen = strlen(value);
    char *copy = malloc(vlen + 1);
    if (!copy) return;
    memcpy(copy, value, vlen + 1);
    kern_dict_set(req->params, name, copy);
}
