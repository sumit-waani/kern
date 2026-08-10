/*
 * kern_http_parser.c - Zero-copy HTTP/1.1 request parser
 *
 * Parses HTTP request line, headers, and body incrementally.
 * Data may arrive in chunks via TCP; the parser maintains state
 * across calls to kern_http_parser_feed().
 */

#include "kern.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Parser states */
typedef enum {
    PARSER_STATE_REQUEST_LINE,
    PARSER_STATE_HEADERS,
    PARSER_STATE_BODY,
    PARSER_STATE_DONE,
    PARSER_STATE_ERROR
} parser_state_t;

struct kern_http_parser {
    kern_buf_t *buf;            /* Accumulated raw data */
    parser_state_t state;
    kern_method_t method;
    char *method_str;
    char *uri;
    char *path;
    char *query_string;
    char *version;
    kern_dict_t *headers;
    size_t content_length;
    bool has_content_length;
    size_t headers_end_offset;  /* Offset where body starts in buf */
};

kern_http_parser_t *kern_http_parser_new(void) {
    kern_http_parser_t *parser = malloc(sizeof(kern_http_parser_t));
    if (!parser) {
        return NULL;
    }

    parser->buf = kern_buf_new(1024);
    if (!parser->buf) {
        free(parser);
        return NULL;
    }

    parser->state = PARSER_STATE_REQUEST_LINE;
    parser->method = KERN_METHOD_GET;
    parser->method_str = NULL;
    parser->uri = NULL;
    parser->path = NULL;
    parser->query_string = NULL;
    parser->version = NULL;
    parser->headers = NULL;
    parser->content_length = 0;
    parser->has_content_length = false;
    parser->headers_end_offset = 0;

    return parser;
}

void kern_http_parser_free(kern_http_parser_t *parser) {
    if (!parser) {
        return;
    }
    kern_buf_free(parser->buf);
    free(parser->method_str);
    free(parser->uri);
    free(parser->path);
    free(parser->query_string);
    free(parser->version);
    if (parser->headers) {
        kern_dict_free(parser->headers);
    }
    free(parser);
}

void kern_http_parser_reset(kern_http_parser_t *parser) {
    if (!parser) {
        return;
    }
    kern_buf_reset(parser->buf);
    parser->state = PARSER_STATE_REQUEST_LINE;
    free(parser->method_str);
    parser->method_str = NULL;
    free(parser->uri);
    parser->uri = NULL;
    free(parser->path);
    parser->path = NULL;
    free(parser->query_string);
    parser->query_string = NULL;
    free(parser->version);
    parser->version = NULL;
    if (parser->headers) {
        kern_dict_free(parser->headers);
        parser->headers = NULL;
    }
    parser->content_length = 0;
    parser->has_content_length = false;
    parser->headers_end_offset = 0;
}

static kern_method_t parse_method(const char *method_str) {
    if (strcmp(method_str, "GET") == 0) return KERN_METHOD_GET;
    if (strcmp(method_str, "POST") == 0) return KERN_METHOD_POST;
    if (strcmp(method_str, "PUT") == 0) return KERN_METHOD_PUT;
    if (strcmp(method_str, "PATCH") == 0) return KERN_METHOD_PATCH;
    if (strcmp(method_str, "DELETE") == 0) return KERN_METHOD_DELETE;
    if (strcmp(method_str, "HEAD") == 0) return KERN_METHOD_HEAD;
    if (strcmp(method_str, "OPTIONS") == 0) return KERN_METHOD_OPTIONS;
    return (kern_method_t)-1;
}

/* Find \r\n in data starting at offset. Returns offset of \r or -1 */
static int find_crlf(const char *data, size_t len, size_t offset) {
    for (size_t i = offset; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return (int)i;
        }
    }
    return -1;
}

/* Case-insensitive string compare */
static int strcasecmp_c11(const char *a, const char *b) {
    while (*a && *b) {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff != 0) return diff;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int parse_request_line(kern_http_parser_t *parser, const char *line, size_t len) {
    /* Format: METHOD SP URI SP HTTP/x.x */
    const char *end = line + len;

    /* Find method */
    const char *sp1 = memchr(line, ' ', len);
    if (!sp1) {
        return -1;
    }

    size_t method_len = (size_t)(sp1 - line);
    parser->method_str = malloc(method_len + 1);
    if (!parser->method_str) return -1;
    memcpy(parser->method_str, line, method_len);
    parser->method_str[method_len] = '\0';

    kern_method_t m = parse_method(parser->method_str);
    if ((int)m == -1) {
        return -1;
    }
    parser->method = m;

    /* Find URI */
    const char *uri_start = sp1 + 1;
    const char *sp2 = memchr(uri_start, ' ', (size_t)(end - uri_start));
    if (!sp2) {
        return -1;
    }

    size_t uri_len = (size_t)(sp2 - uri_start);
    parser->uri = malloc(uri_len + 1);
    if (!parser->uri) return -1;
    memcpy(parser->uri, uri_start, uri_len);
    parser->uri[uri_len] = '\0';

    /* Parse path and query string from URI */
    const char *qmark = memchr(uri_start, '?', uri_len);
    if (qmark) {
        size_t path_len = (size_t)(qmark - uri_start);
        parser->path = malloc(path_len + 1);
        if (!parser->path) return -1;
        memcpy(parser->path, uri_start, path_len);
        parser->path[path_len] = '\0';

        size_t qs_len = (size_t)(sp2 - (qmark + 1));
        parser->query_string = malloc(qs_len + 1);
        if (!parser->query_string) return -1;
        memcpy(parser->query_string, qmark + 1, qs_len);
        parser->query_string[qs_len] = '\0';
    } else {
        parser->path = malloc(uri_len + 1);
        if (!parser->path) return -1;
        memcpy(parser->path, uri_start, uri_len);
        parser->path[uri_len] = '\0';

        parser->query_string = malloc(1);
        if (!parser->query_string) return -1;
        parser->query_string[0] = '\0';
    }

    /* Parse HTTP version */
    const char *ver_start = sp2 + 1;
    size_t ver_len = (size_t)(end - ver_start);
    parser->version = malloc(ver_len + 1);
    if (!parser->version) return -1;
    memcpy(parser->version, ver_start, ver_len);
    parser->version[ver_len] = '\0';

    return 0;
}

static int parse_header_line(kern_http_parser_t *parser, const char *line, size_t len) {
    /* Format: Key: Value */
    const char *colon = memchr(line, ':', len);
    if (!colon) {
        return -1;
    }

    size_t key_len = (size_t)(colon - line);
    /* Trim trailing whitespace from key */
    while (key_len > 0 && line[key_len - 1] == ' ') {
        key_len--;
    }

    /* Make a lowercase copy of the key for case-insensitive storage */
    char *key = malloc(key_len + 1);
    if (!key) return -1;
    for (size_t i = 0; i < key_len; i++) {
        key[i] = (char)tolower((unsigned char)line[i]);
    }
    key[key_len] = '\0';

    /* Value starts after colon, skip leading whitespace */
    const char *val_start = colon + 1;
    const char *val_end = line + len;
    while (val_start < val_end && *val_start == ' ') {
        val_start++;
    }

    size_t val_len = (size_t)(val_end - val_start);
    char *val = malloc(val_len + 1);
    if (!val) {
        free(key);
        return -1;
    }
    memcpy(val, val_start, val_len);
    val[val_len] = '\0';

    /* Check for Content-Length */
    if (strcasecmp_c11(key, "content-length") == 0) {
        parser->content_length = (size_t)strtoull(val, NULL, 10);
        parser->has_content_length = true;
    }

    kern_dict_set(parser->headers, key, val);
    free(key);

    return 0;
}

int kern_http_parser_feed(kern_http_parser_t *parser, const char *data, size_t len) {
    if (!parser || !data || len == 0) {
        return KERN_HTTP_PARSE_NEED_MORE;
    }

    if (parser->state == PARSER_STATE_ERROR) {
        return KERN_HTTP_PARSE_ERROR;
    }

    if (parser->state == PARSER_STATE_DONE) {
        return KERN_HTTP_PARSE_DONE;
    }

    /* Append data to our internal buffer */
    if (kern_buf_write(parser->buf, data, len) != 0) {
        parser->state = PARSER_STATE_ERROR;
        return KERN_HTTP_PARSE_ERROR;
    }

    const char *buf_data = kern_buf_data(parser->buf);
    size_t buf_len = kern_buf_len(parser->buf);

    /* Parse request line */
    if (parser->state == PARSER_STATE_REQUEST_LINE) {
        int crlf_pos = find_crlf(buf_data, buf_len, 0);
        if (crlf_pos < 0) {
            return KERN_HTTP_PARSE_NEED_MORE;
        }

        if (crlf_pos == 0) {
            parser->state = PARSER_STATE_ERROR;
            return KERN_HTTP_PARSE_ERROR;
        }

        if (parse_request_line(parser, buf_data, (size_t)crlf_pos) != 0) {
            parser->state = PARSER_STATE_ERROR;
            return KERN_HTTP_PARSE_ERROR;
        }

        parser->headers = kern_dict_new();
        if (!parser->headers) {
            parser->state = PARSER_STATE_ERROR;
            return KERN_HTTP_PARSE_ERROR;
        }

        parser->state = PARSER_STATE_HEADERS;
        parser->headers_end_offset = (size_t)crlf_pos + 2;
    }

    /* Parse headers */
    if (parser->state == PARSER_STATE_HEADERS) {
        while (1) {
            int crlf_pos = find_crlf(buf_data, buf_len, parser->headers_end_offset);
            if (crlf_pos < 0) {
                return KERN_HTTP_PARSE_NEED_MORE;
            }

            size_t line_start = parser->headers_end_offset;
            size_t line_len = (size_t)crlf_pos - line_start;

            if (line_len == 0) {
                /* Empty line signals end of headers */
                parser->headers_end_offset = (size_t)crlf_pos + 2;

                if (parser->has_content_length && parser->content_length > 0) {
                    parser->state = PARSER_STATE_BODY;
                    break;
                } else {
                    parser->state = PARSER_STATE_DONE;
                    return KERN_HTTP_PARSE_DONE;
                }
            }

            if (parse_header_line(parser, buf_data + line_start, line_len) != 0) {
                parser->state = PARSER_STATE_ERROR;
                return KERN_HTTP_PARSE_ERROR;
            }

            parser->headers_end_offset = (size_t)crlf_pos + 2;
        }
    }

    /* Parse body */
    if (parser->state == PARSER_STATE_BODY) {
        size_t body_received = buf_len - parser->headers_end_offset;
        if (body_received >= parser->content_length) {
            parser->state = PARSER_STATE_DONE;
            return KERN_HTTP_PARSE_DONE;
        }
        return KERN_HTTP_PARSE_NEED_MORE;
    }

    return KERN_HTTP_PARSE_NEED_MORE;
}

bool kern_http_parser_done(const kern_http_parser_t *parser) {
    if (!parser) return false;
    return parser->state == PARSER_STATE_DONE;
}

bool kern_http_parser_error(const kern_http_parser_t *parser) {
    if (!parser) return true;
    return parser->state == PARSER_STATE_ERROR;
}

kern_method_t kern_http_parser_method(const kern_http_parser_t *parser) {
    if (!parser) return KERN_METHOD_GET;
    return parser->method;
}

const char *kern_http_parser_path(const kern_http_parser_t *parser) {
    if (!parser) return NULL;
    return parser->path;
}

const char *kern_http_parser_query_string(const kern_http_parser_t *parser) {
    if (!parser) return NULL;
    return parser->query_string;
}

const char *kern_http_parser_version(const kern_http_parser_t *parser) {
    if (!parser) return NULL;
    return parser->version;
}

kern_dict_t *kern_http_parser_headers(const kern_http_parser_t *parser) {
    if (!parser) return NULL;
    return parser->headers;
}

kern_str_t kern_http_parser_body(const kern_http_parser_t *parser) {
    kern_str_t body = {NULL, 0};
    if (!parser || parser->state != PARSER_STATE_DONE) {
        return body;
    }
    if (parser->has_content_length && parser->content_length > 0) {
        const char *buf_data = kern_buf_data(parser->buf);
        body.data = buf_data + parser->headers_end_offset;
        body.len = parser->content_length;
    }
    return body;
}

size_t kern_http_parser_content_length(const kern_http_parser_t *parser) {
    if (!parser) return 0;
    return parser->content_length;
}
