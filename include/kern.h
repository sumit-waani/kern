/*
 * kern.h - Public header for the kern web framework
 *
 * This is the single public header that exposes all core types and
 * function declarations for the kern framework library.
 */

#ifndef KERN_H
#define KERN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Core Types
 * ============================================================ */

/**
 * kern_str_t - Non-owning string slice.
 * Points to a substring without owning the memory.
 */
typedef struct {
    const char *data;
    size_t len;
} kern_str_t;

/**
 * kern_buf_t - Growable byte buffer (opaque).
 * Used for building strings, HTTP responses, etc.
 */
typedef struct kern_buf kern_buf_t;

/**
 * kern_dict_t - String-keyed hash map (opaque).
 * Maps null-terminated string keys to void* values.
 */
typedef struct kern_dict kern_dict_t;

/**
 * kern_arena_t - Arena allocator (opaque).
 * Efficient bump allocator for per-request allocations.
 */
typedef struct kern_arena kern_arena_t;

/**
 * kern_method_t - HTTP request methods.
 */
typedef enum {
    KERN_METHOD_GET,
    KERN_METHOD_POST,
    KERN_METHOD_PUT,
    KERN_METHOD_PATCH,
    KERN_METHOD_DELETE,
    KERN_METHOD_HEAD,
    KERN_METHOD_OPTIONS
} kern_method_t;

/* Forward declarations for request/response/app/server/parser/router structs */
typedef struct kern_req kern_req_t;
typedef struct kern_res kern_res_t;
typedef struct kern_app kern_app_t;
typedef struct kern_response kern_response_t;
typedef struct kern_server kern_server_t;
typedef struct kern_http_parser kern_http_parser_t;
typedef struct kern_router kern_router_t;

/**
 * kern_handler_fn - HTTP request handler callback.
 * Receives a request, returns a response. The caller frees the response.
 */
typedef kern_response_t *(*kern_handler_fn)(kern_req_t *req);

/**
 * kern_middleware_fn - Middleware function type.
 * Receives a request and a "next" handler to call.
 * Can short-circuit by returning a response without calling next.
 */
typedef kern_response_t *(*kern_middleware_fn)(kern_req_t *req, kern_handler_fn next);

/**
 * kern_route_status_t - Result status from route matching.
 */
typedef enum {
    KERN_ROUTE_OK = 0,
    KERN_ROUTE_NOT_FOUND = 1,
    KERN_ROUTE_METHOD_NOT_ALLOWED = 2
} kern_route_status_t;

/**
 * kern_route_result_t - Result from kern_router_match.
 */
typedef struct {
    kern_handler_fn handler;
    kern_route_status_t status;
} kern_route_result_t;

/**
 * kern_fs_entry_t - A single file-system route entry.
 */
typedef struct {
    char *route_path;
    char *file_path;
    char *methods[8];
    int method_count;
    char *_func_name;  /* Internal: used during code generation */
} kern_fs_entry_t;

/* HTTP parser return codes */
#define KERN_HTTP_PARSE_NEED_MORE  0
#define KERN_HTTP_PARSE_DONE       1
#define KERN_HTTP_PARSE_ERROR     -1

/* ============================================================
 * Buffer API (kern_buf.c)
 * ============================================================ */

/**
 * Create a new buffer with the given initial capacity.
 * Returns NULL on allocation failure.
 */
kern_buf_t *kern_buf_new(size_t initial_cap);

/**
 * Append raw bytes to the buffer.
 * Returns 0 on success, -1 on allocation failure.
 */
int kern_buf_write(kern_buf_t *buf, const char *data, size_t len);

/**
 * Append a null-terminated string to the buffer.
 * Returns 0 on success, -1 on allocation failure.
 */
int kern_buf_writes(kern_buf_t *buf, const char *str);

/**
 * Printf-style append to the buffer.
 * Returns 0 on success, -1 on failure.
 */
int kern_buf_writef(kern_buf_t *buf, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Reset buffer length to 0 without freeing memory.
 */
void kern_buf_reset(kern_buf_t *buf);

/**
 * Free the buffer and all associated memory.
 */
void kern_buf_free(kern_buf_t *buf);

/**
 * Get a pointer to the buffer's data (null-terminated).
 */
const char *kern_buf_data(const kern_buf_t *buf);

/**
 * Get the current length of data in the buffer.
 */
size_t kern_buf_len(const kern_buf_t *buf);

/* ============================================================
 * String Utilities API (kern_str.c)
 * ============================================================ */

/**
 * Create a kern_str_t from a null-terminated C string.
 */
kern_str_t kern_str(const char *cstr);

/**
 * Create a heap-allocated null-terminated copy of a string slice.
 * Caller must free() the returned pointer.
 */
char *kern_str_dup(kern_str_t str);

/**
 * Compare two string slices for equality.
 */
bool kern_str_eq(kern_str_t a, kern_str_t b);

/**
 * Check if str starts with the given prefix.
 */
bool kern_str_starts_with(kern_str_t str, kern_str_t prefix);

/**
 * Split a string slice by a single-character delimiter.
 * Writes up to max_parts pointers into out_parts and sets *out_count.
 * The out_parts array must be pre-allocated by the caller.
 */
void kern_str_split(kern_str_t str, char delim,
                    kern_str_t *out_parts, size_t max_parts,
                    size_t *out_count);

/**
 * Trim leading and trailing whitespace from a string slice.
 */
kern_str_t kern_str_trim(kern_str_t str);

/**
 * URL-encode a string slice, appending the result to buf.
 */
int kern_url_encode(kern_str_t str, kern_buf_t *buf);

/**
 * URL-decode a string slice, appending the result to buf.
 */
int kern_url_decode(kern_str_t str, kern_buf_t *buf);

/* ============================================================
 * Dictionary (Hash Map) API (kern_dict.c)
 * ============================================================ */

/**
 * Callback type for dictionary iteration.
 * Return false to stop iteration early.
 */
typedef bool (*kern_dict_iter_fn)(const char *key, void *value, void *userdata);

/**
 * Create a new empty dictionary.
 */
kern_dict_t *kern_dict_new(void);

/**
 * Set a key-value pair. The key is copied internally.
 * If the key already exists, its value is replaced.
 * Returns 0 on success, -1 on allocation failure.
 */
int kern_dict_set(kern_dict_t *dict, const char *key, void *value);

/**
 * Get the value for a key. Returns NULL if not found.
 */
void *kern_dict_get(const kern_dict_t *dict, const char *key);

/**
 * Check if a key exists in the dictionary.
 */
bool kern_dict_has(const kern_dict_t *dict, const char *key);

/**
 * Delete a key from the dictionary.
 * Returns true if the key was found and removed.
 */
bool kern_dict_del(kern_dict_t *dict, const char *key);

/**
 * Get the number of entries in the dictionary.
 */
size_t kern_dict_count(const kern_dict_t *dict);

/**
 * Free the dictionary and all copied keys.
 * Does NOT free the stored values.
 */
void kern_dict_free(kern_dict_t *dict);

/**
 * Iterate over all entries in the dictionary.
 * Calls the callback for each entry. If callback returns false, stops early.
 */
void kern_dict_iter(const kern_dict_t *dict, kern_dict_iter_fn callback,
                    void *userdata);

/* ============================================================
 * Arena Allocator API (kern_arena.c)
 * ============================================================ */

/**
 * Create a new arena with the given block size.
 * The arena allocates memory in blocks of this size.
 */
kern_arena_t *kern_arena_new(size_t block_size);

/**
 * Allocate memory from the arena (8-byte aligned).
 * Returns NULL if the arena cannot grow.
 */
void *kern_arena_alloc(kern_arena_t *arena, size_t size);

/**
 * Reset the arena, making all previously allocated memory reusable.
 * Does not free the underlying blocks.
 */
void kern_arena_reset(kern_arena_t *arena);

/**
 * Free the arena and all its blocks.
 */
void kern_arena_free(kern_arena_t *arena);

/* ============================================================
 * HTTP Parser API (kern_http_parser.c)
 * ============================================================ */

/**
 * Create a new HTTP parser instance.
 */
kern_http_parser_t *kern_http_parser_new(void);

/**
 * Free the HTTP parser and all parsed data.
 */
void kern_http_parser_free(kern_http_parser_t *parser);

/**
 * Reset the parser for a new request (reuse without reallocation).
 */
void kern_http_parser_reset(kern_http_parser_t *parser);

/**
 * Feed data to the parser. May be called multiple times as data arrives.
 * Returns: KERN_HTTP_PARSE_NEED_MORE, KERN_HTTP_PARSE_DONE, or KERN_HTTP_PARSE_ERROR.
 */
int kern_http_parser_feed(kern_http_parser_t *parser, const char *data, size_t len);

/**
 * Check if parsing is complete.
 */
bool kern_http_parser_done(const kern_http_parser_t *parser);

/**
 * Check if the parser encountered an error.
 */
bool kern_http_parser_error(const kern_http_parser_t *parser);

/**
 * Get the parsed HTTP method.
 */
kern_method_t kern_http_parser_method(const kern_http_parser_t *parser);

/**
 * Get the parsed request path (without query string).
 */
const char *kern_http_parser_path(const kern_http_parser_t *parser);

/**
 * Get the parsed query string (without leading '?').
 */
const char *kern_http_parser_query_string(const kern_http_parser_t *parser);

/**
 * Get the HTTP version string (e.g., "HTTP/1.1").
 */
const char *kern_http_parser_version(const kern_http_parser_t *parser);

/**
 * Get the parsed headers dictionary (keys are lowercase).
 */
kern_dict_t *kern_http_parser_headers(const kern_http_parser_t *parser);

/**
 * Get the request body as a string slice.
 * Only valid after parsing is done and Content-Length was present.
 */
kern_str_t kern_http_parser_body(const kern_http_parser_t *parser);

/**
 * Get the Content-Length value (0 if not present).
 */
size_t kern_http_parser_content_length(const kern_http_parser_t *parser);

/* ============================================================
 * HTTP Request API (kern_request.c)
 * ============================================================ */

/**
 * Create a request object from a completed parser.
 * Takes ownership of the parser (will free it when request is freed).
 */
kern_req_t *kern_req_new(kern_http_parser_t *parser);

/**
 * Free the request and all associated resources.
 */
void kern_req_free(kern_req_t *req);

/**
 * Get the request method.
 */
kern_method_t kern_req_method(const kern_req_t *req);

/**
 * Get the request path.
 */
const char *kern_req_path(const kern_req_t *req);

/**
 * Get the raw query string.
 */
const char *kern_req_query_string(const kern_req_t *req);

/**
 * Get a route parameter by name. Returns NULL if not found.
 */
const char *kern_param(const kern_req_t *req, const char *name);

/**
 * Get a query string parameter by name. Returns NULL if not found.
 */
const char *kern_query(kern_req_t *req, const char *name);

/**
 * Get a request header by name (case-insensitive). Returns NULL if not found.
 */
const char *kern_header(const kern_req_t *req, const char *name);

/**
 * Get the request body.
 */
kern_str_t kern_body(const kern_req_t *req);

/**
 * Get a URL-encoded form field by name. Returns NULL if not found.
 */
const char *kern_form_str(kern_req_t *req, const char *name);

/**
 * Set a route parameter (used internally by the router).
 */
void kern_req_set_param(kern_req_t *req, const char *name, const char *value);

/* ============================================================
 * HTTP Response API (kern_response.c)
 * ============================================================ */

/**
 * Create a new response with the given HTTP status code.
 */
kern_response_t *kern_response_new(int status);

/**
 * Set a response header. Replaces existing value if key already exists.
 */
void kern_response_header(kern_response_t *res, const char *key, const char *val);

/**
 * Set the response body from raw data.
 */
void kern_response_body(kern_response_t *res, const char *data, size_t len);

/**
 * Set the response body from a null-terminated string.
 */
void kern_response_body_str(kern_response_t *res, const char *str);

/**
 * Get the response status code.
 */
int kern_response_status(const kern_response_t *res);

/**
 * Free the response and all associated memory.
 */
void kern_response_free(kern_response_t *res);

/**
 * Serialize the response to HTTP wire format.
 * Returns a buffer containing the full HTTP response. Caller frees.
 */
kern_buf_t *kern_response_serialize(kern_response_t *res);

/**
 * Create a 404 Not Found response.
 */
kern_response_t *kern_404_response(void);

/**
 * Create a 500 Internal Server Error response with optional message.
 */
kern_response_t *kern_500_response(const char *msg);

/**
 * Create a redirect response (301, 302, 307, 308).
 */
kern_response_t *kern_redirect_response(const char *url, int status);

/* ============================================================
 * HTTP Server API (kern_http_server.c)
 * ============================================================ */

/* Requires: #include <uv.h> before including kern.h if using server API */
struct uv_loop_s; /* Forward declare so kern.h doesn't require uv.h */

/**
 * Create a new HTTP server bound to the given event loop.
 */
kern_server_t *kern_server_new(struct uv_loop_s *loop);

/**
 * Set the request handler function.
 */
void kern_server_set_handler(kern_server_t *server, kern_handler_fn handler);

/**
 * Set connection timeout in milliseconds (default: 60000).
 */
void kern_server_set_timeout(kern_server_t *server, uint64_t timeout_ms);

/**
 * Start listening on the given host and port.
 * Returns 0 on success, libuv error code on failure.
 */
int kern_server_listen(kern_server_t *server, const char *host, int port);

/**
 * Stop accepting new connections and close the listening socket.
 */
void kern_server_stop(kern_server_t *server);

/**
 * Free the server struct. Call after the event loop has stopped.
 */
void kern_server_free(kern_server_t *server);

/**
 * Get the actual port the server is listening on.
 * Useful when binding to port 0 (OS-assigned port).
 */
int kern_server_port(kern_server_t *server);

/* ============================================================
 * Static File Handler API (kern_static.c)
 * ============================================================ */

/**
 * Serve a static file from the given public directory.
 * Uses the request path to locate the file. Handles:
 *   - Content-Type based on file extension
 *   - ETag / If-None-Match (304 Not Modified)
 *   - Directory traversal prevention
 */
kern_response_t *kern_static_handler(kern_req_t *req, const char *public_dir);

/* ============================================================
 * Router API (kern_router.c)
 * ============================================================ */

/**
 * Create a new router instance.
 */
kern_router_t *kern_router_new(void);

/**
 * Free the router and all registered routes.
 */
void kern_router_free(kern_router_t *router);

/**
 * Add a route with a string method.
 * Path supports: static ("/posts"), parameterized ("/:id"), wildcard (star-path).
 * Returns 0 on success, -1 on failure.
 */
int kern_router_add(kern_router_t *router, const char *method, const char *path,
                    kern_handler_fn handler);

/**
 * Add a route with a kern_method_t enum value.
 */
int kern_router_add_method(kern_router_t *router, kern_method_t method, const char *path,
                           kern_handler_fn handler);

/**
 * Match a request path against registered routes.
 * Fills params dict with captured path parameters.
 * Returns result with handler and status (OK, NOT_FOUND, METHOD_NOT_ALLOWED).
 */
kern_route_result_t kern_router_match(kern_router_t *router, kern_method_t method,
                                      const char *path, kern_dict_t *params);

/**
 * Register middleware for all routes under a given prefix.
 * Middleware runs in registration order before the final handler.
 */
int kern_router_use(kern_router_t *router, const char *prefix, kern_middleware_fn fn);

/**
 * Dispatch a request through the middleware chain and then to the handler.
 */
kern_response_t *kern_router_dispatch(kern_router_t *router, kern_req_t *req,
                                      kern_handler_fn handler);

/* ============================================================
 * Middleware Utilities (kern_middleware.c)
 * ============================================================ */

/**
 * Built-in logging middleware. Logs method and path to stderr.
 */
kern_response_t *kern_middleware_logger(kern_req_t *req, kern_handler_fn next);

/* ============================================================
 * File-System Router API (kern_fs_router.c)
 * ============================================================ */

/**
 * Scan a pages directory and generate route entries.
 * Returns an array of entries (caller must free with kern_fs_entries_free).
 * Sets *out_count to the number of entries found.
 */
kern_fs_entry_t *kern_fs_scan(const char *pages_dir, int *out_count);

/**
 * Free the array of FS route entries.
 */
void kern_fs_entries_free(kern_fs_entry_t *entries, int count);

/**
 * Generate a C source file with route registration code.
 * Writes to output_path. Returns 0 on success, -1 on failure.
 */
int kern_fs_generate_registry(kern_fs_entry_t *entries, int count, const char *output_path);

/* ============================================================
 * Server-Router Integration (kern_http_server.c)
 * ============================================================ */

/**
 * Set the router for the server. When set, the server uses router-based
 * dispatch instead of the simple handler. The simple handler (set_handler)
 * becomes a fallback if no route matches.
 */
void kern_server_set_router(kern_server_t *server, kern_router_t *router);

#ifdef __cplusplus
}
#endif

#endif /* KERN_H */
