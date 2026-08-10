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

/* Forward declarations for database/config/session structs */
typedef struct kern_db kern_db_t;
typedef struct kern_db_result kern_db_result_t;
typedef struct kern_qb kern_qb_t;
typedef struct kern_config kern_config_t;
typedef struct kern_session kern_session_t;

/**
 * kern_db_param_type_t - Types for parameterized query values.
 */
typedef enum {
    KERN_DB_PARAM_NULL,
    KERN_DB_PARAM_INT,
    KERN_DB_PARAM_STR,
    KERN_DB_PARAM_DOUBLE
} kern_db_param_type_t;

/**
 * kern_db_param_t - A single parameter for parameterized queries.
 */
typedef struct {
    kern_db_param_type_t type;
    union {
        int64_t i;
        const char *s;
        double d;
    } value;
} kern_db_param_t;

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
 * Callback type for freeing dictionary values on destruction.
 */
typedef void (*kern_dict_free_fn)(void *value);

/**
 * Create a new empty dictionary.
 */
kern_dict_t *kern_dict_new(void);

/**
 * Create a new empty dictionary with a value destructor callback.
 * When set, the free_fn is called on each value during kern_dict_free,
 * kern_dict_del, and when kern_dict_set replaces an existing value.
 */
kern_dict_t *kern_dict_new_with_free(kern_dict_free_fn free_fn);

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
 * Asset Pipeline API (kern_asset.c)
 * ============================================================ */

/**
 * Hash a single file and copy it to output_dir with a content-hash in the name.
 * Returns the hashed filename (e.g., "app-a1b2c3d4.css").
 * Caller must free() the returned string. Returns NULL on error.
 */
char *kern_asset_hash_file(const char *input_path, const char *output_dir);

/**
 * Process all assets in a directory recursively.
 * Hashes each file into public_dir and generates a C manifest file.
 * Returns the number of assets processed, or -1 on error.
 */
int kern_asset_process_dir(const char *assets_dir, const char *public_dir,
                           const char *manifest_path);

/**
 * Runtime lookup for an asset URL by its original path.
 * For v0.1, returns NULL (use compile-time #defines from manifest instead).
 */
const char *kern_asset_url(const char *original_path);

/* ============================================================
 * Application API (kern_app.c)
 * ============================================================ */

/**
 * Create a new application instance.
 * Loads kern.toml config, initializes session store, database, and router.
 */
kern_app_t *kern_app_new(const char *name);

/**
 * Set the listen port for the application.
 */
void kern_app_listen(kern_app_t *app, int port);

/**
 * Run the application event loop. Starts the HTTP server.
 * Returns 0 on clean shutdown, non-zero on error.
 */
int kern_app_run(kern_app_t *app);

/**
 * Get the application router (for registering routes).
 */
kern_router_t *kern_app_router(kern_app_t *app);

/**
 * Get the application database connection (may be NULL if not configured).
 */
kern_db_t *kern_app_db(kern_app_t *app);

/**
 * Get the application name.
 */
const char *kern_app_name(kern_app_t *app);

/**
 * Get the configured port.
 */
int kern_app_port(kern_app_t *app);

/**
 * Free the application and all associated resources.
 */
void kern_app_free(kern_app_t *app);

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

/* ============================================================
 * HTML Utilities API (kern_html.c)
 * ============================================================ */

/**
 * Escape a string for safe HTML output.
 * Replaces &, <, >, ", ' with their HTML entity equivalents.
 * Writes the escaped output into buf.
 * Returns 0 on success, -1 on failure.
 */
int kern_html_escape(kern_buf_t *buf, const char *str);

/* ============================================================
 * Template Engine Types and API
 * ============================================================ */

/**
 * kern_tpl_node_type_t - AST node types for parsed templates.
 */
typedef enum {
    KERN_TPL_ELEMENT,    /* HTML element (tag + attrs + children) */
    KERN_TPL_TEXT,       /* Literal text content */
    KERN_TPL_INTERP,    /* Expression interpolation #{} or !{} */
    KERN_TPL_STATEMENT,  /* C code statement (- if/for/etc) */
    KERN_TPL_INCLUDE,   /* Include a partial template */
    KERN_TPL_EXTEND,    /* Extend a parent layout */
    KERN_TPL_BLOCK,     /* Define a fillable block region */
    KERN_TPL_DOCTYPE    /* Doctype declaration */
} kern_tpl_node_type_t;

/**
 * kern_tpl_attr_t - An attribute on an element node.
 */
typedef struct {
    char *name;       /* Attribute name */
    char *value;      /* Attribute value (NULL for boolean attrs) */
    bool dynamic;     /* true if value is a C expression ({expr}) */
} kern_tpl_attr_t;

/**
 * kern_tpl_node_t - A node in the template AST.
 */
typedef struct kern_tpl_node {
    kern_tpl_node_type_t type;

    /* For ELEMENT nodes */
    char *tag;                     /* Tag name */
    kern_tpl_attr_t *attrs;        /* Array of attributes */
    int attr_count;                /* Number of attributes */
    bool is_void;                  /* True for void/self-closing tags */

    /* For TEXT, STATEMENT, INCLUDE, EXTEND, BLOCK, DOCTYPE nodes */
    char *text;                    /* Text content / path / block name */

    /* For INTERP nodes */
    char *expr;                    /* Expression string */
    bool escaped;                  /* true = #{} (escaped), false = !{} (raw) */

    /* Child nodes */
    struct kern_tpl_node **children;
    size_t children_count;
    size_t children_cap;
} kern_tpl_node_t;

/* ============================================================
 * Template Parser API (kern_tpl_parser.c)
 * ============================================================ */

/**
 * Parse a .khtml template source string into an AST.
 * Returns the root node of the AST. Caller must free with kern_tpl_node_free().
 */
kern_tpl_node_t *kern_tpl_parse(const char *source);

/**
 * Parse a .khtml template file into an AST.
 * Returns the root node of the AST. Caller must free with kern_tpl_node_free().
 */
kern_tpl_node_t *kern_tpl_parse_file(const char *path);

/**
 * Free a template AST node and all its children recursively.
 */
void kern_tpl_node_free(kern_tpl_node_t *node);

/* ============================================================
 * Template Code Generator API (kern_tpl_codegen.c)
 * ============================================================ */

/**
 * Generate C source code from a template AST.
 * The generated function has the signature:
 *   void <func_name>(kern_buf_t *buf, kern_dict_t *vars);
 *
 * Returns a malloc'd string containing the generated C code.
 * Caller must free() the returned string.
 */
char *kern_tpl_codegen(kern_tpl_node_t *ast, const char *func_name);

/* ============================================================
 * Template Compilation API (kern_tpl_compile.c)
 * ============================================================ */

/**
 * Compile a single .khtml file to a .c output file.
 * Returns 0 on success, -1 on failure.
 */
int kern_tpl_compile_file(const char *input_path, const char *output_path);

/**
 * Compile a template from a source string.
 * Writes the generated C code to *out_code (caller must free).
 * Returns 0 on success, -1 on failure.
 */
int kern_tpl_compile_string(const char *source, const char *func_name,
                            char **out_code);

/**
 * Compile all .khtml/.kfrag files in a views directory.
 * Generates a kern_views.h header with extern declarations.
 * Returns the number of templates found, or -1 on error.
 */
int kern_tpl_compile_dir(const char *views_dir, const char *output_dir);

/* ============================================================
 * Database API (kern_db.c)
 * ============================================================ */

/**
 * Open a SQLite database. Enables WAL mode and sets busy timeout to 5000ms.
 * Use ":memory:" for an in-memory database.
 */
kern_db_t *kern_db_open(const char *path);

/**
 * Close the database connection.
 */
void kern_db_close(kern_db_t *db);

/**
 * Execute a SQL statement (no result set). Use for DDL, INSERT, UPDATE, DELETE.
 * Returns number of rows affected, or -1 on error.
 */
int kern_db_exec(kern_db_t *db, const char *sql);

/**
 * Execute a query and return result set.
 * Caller must free with kern_db_result_free().
 */
kern_db_result_t *kern_db_query(kern_db_t *db, const char *sql);

/**
 * Execute a parameterized statement (no result set).
 * Returns number of rows affected, or -1 on error.
 */
int kern_db_exec_params(kern_db_t *db, const char *sql,
                        const kern_db_param_t *params, int param_count);

/**
 * Execute a parameterized query and return result set.
 * Caller must free with kern_db_result_free().
 */
kern_db_result_t *kern_db_query_params(kern_db_t *db, const char *sql,
                                       const kern_db_param_t *params, int param_count);

/**
 * Get a string value from a result set cell.
 * Returns NULL if the cell is SQL NULL or indices are out of bounds.
 */
const char *kern_db_row_str(const kern_db_result_t *result, int row, int col);

/**
 * Get an integer value from a result set cell.
 * Returns 0 if the cell is NULL or indices are out of bounds.
 */
int64_t kern_db_row_int(const kern_db_result_t *result, int row, int col);

/**
 * Get the number of rows in a result set.
 */
int kern_db_result_row_count(const kern_db_result_t *result);

/**
 * Get the number of columns in a result set.
 */
int kern_db_result_col_count(const kern_db_result_t *result);

/**
 * Get the name of a column by index.
 */
const char *kern_db_result_col_name(const kern_db_result_t *result, int col);

/**
 * Free a result set.
 */
void kern_db_result_free(kern_db_result_t *result);

/**
 * Begin a transaction.
 */
int kern_db_begin(kern_db_t *db);

/**
 * Commit the current transaction.
 */
int kern_db_commit(kern_db_t *db);

/**
 * Rollback the current transaction.
 */
int kern_db_rollback(kern_db_t *db);

/* ============================================================
 * Query Builder API (kern_qb.c)
 * ============================================================ */

/**
 * Create a new query builder associated with a database.
 */
kern_qb_t *kern_qb_new(kern_db_t *db);

/**
 * Free the query builder.
 */
void kern_qb_free(kern_qb_t *qb);

/**
 * Set SELECT columns. E.g., "*" or "id, title, slug".
 */
void kern_qb_select(kern_qb_t *qb, const char *columns);

/**
 * Set FROM table.
 */
void kern_qb_from(kern_qb_t *qb, const char *table);

/**
 * Add a WHERE clause with a string parameter.
 * E.g., kern_qb_where_str(qb, "email", "=", "test@example.com")
 */
void kern_qb_where_str(kern_qb_t *qb, const char *col, const char *op, const char *val);

/**
 * Add a WHERE clause with an integer parameter.
 * E.g., kern_qb_where_int(qb, "id", "=", 42)
 */
void kern_qb_where_int(kern_qb_t *qb, const char *col, const char *op, int64_t val);

/**
 * Set ORDER BY clause.
 */
void kern_qb_order(kern_qb_t *qb, const char *col, const char *direction);

/**
 * Set LIMIT.
 */
void kern_qb_limit(kern_qb_t *qb, int n);

/**
 * Set OFFSET.
 */
void kern_qb_offset(kern_qb_t *qb, int n);

/**
 * Start an INSERT INTO statement.
 */
void kern_qb_insert(kern_qb_t *qb, const char *table);

/**
 * Start an UPDATE statement.
 */
void kern_qb_update(kern_qb_t *qb, const char *table);

/**
 * Start a DELETE FROM statement.
 */
void kern_qb_delete(kern_qb_t *qb, const char *table);

/**
 * Set a string column value (for INSERT/UPDATE).
 */
void kern_qb_set_str(kern_qb_t *qb, const char *col, const char *val);

/**
 * Set an integer column value (for INSERT/UPDATE).
 */
void kern_qb_set_int(kern_qb_t *qb, const char *col, int64_t val);

/**
 * Build and return the generated SQL string. Useful for debugging.
 * The returned string is owned by the query builder (do not free).
 */
const char *kern_qb_build(kern_qb_t *qb);

/**
 * Execute the built query (for INSERT/UPDATE/DELETE).
 * Returns rows affected, or -1 on error.
 */
int kern_qb_exec(kern_qb_t *qb);

/**
 * Execute the built query and return result set.
 * Caller must free with kern_db_result_free().
 */
kern_db_result_t *kern_qb_query(kern_qb_t *qb);

/**
 * Execute with implicit LIMIT 1 and return result set.
 * Caller must free with kern_db_result_free().
 */
kern_db_result_t *kern_qb_one(kern_qb_t *qb);

/* ============================================================
 * Migration Runner API (kern_migrate.c)
 * ============================================================ */

/**
 * Initialize the migrations tracking table.
 */
int kern_migrate_init(kern_db_t *db);

/**
 * Run all pending migrations from the given directory.
 * Returns number of migrations applied, or -1 on error.
 */
int kern_migrate_up(kern_db_t *db, const char *migrations_dir);

/**
 * Roll back the last applied migration.
 * Returns 1 on success, 0 if nothing to roll back, -1 on error.
 */
int kern_migrate_down(kern_db_t *db, const char *migrations_dir);

/**
 * Get status of all migrations.
 * Returns a result set with columns: name, status (applied/pending).
 * Caller must free with kern_db_result_free().
 */
kern_db_result_t *kern_migrate_status(kern_db_t *db, const char *migrations_dir);

/* ============================================================
 * Configuration API (kern_config.c)
 * ============================================================ */

/**
 * Load a TOML configuration file.
 * Supports sections, strings, integers, booleans, and ${env:VAR} expansion.
 */
kern_config_t *kern_config_load(const char *path);

/**
 * Get a string value by dotted key (e.g., "app.name").
 * Returns NULL if key not found or not a string.
 */
const char *kern_config_get_str(const kern_config_t *cfg, const char *key);

/**
 * Get an integer value by dotted key.
 * Returns 0 if key not found or not an integer.
 */
int64_t kern_config_get_int(const kern_config_t *cfg, const char *key);

/**
 * Get a boolean value by dotted key.
 * Returns false if key not found or not a boolean.
 */
bool kern_config_get_bool(const kern_config_t *cfg, const char *key);

/**
 * Free the configuration object.
 */
void kern_config_free(kern_config_t *cfg);

/* ============================================================
 * Session API (kern_session.c)
 * ============================================================ */

/**
 * Initialize the global session store.
 */
void kern_session_init(void);

/**
 * Start or resume a session from the request.
 * Looks for "kern_session" cookie; creates a new session if not found.
 */
kern_session_t *kern_session_start(kern_req_t *req);

/**
 * Get a session value by key.
 */
const char *kern_session_get(const kern_session_t *session, const char *key);

/**
 * Set a session value.
 */
void kern_session_set(kern_session_t *session, const char *key, const char *value);

/**
 * Destroy a session by ID.
 */
void kern_session_destroy(const char *session_id);

/**
 * Get the session ID.
 */
const char *kern_session_id(const kern_session_t *session);

/**
 * Get the Set-Cookie header value for this session.
 * Returns a static buffer (not thread-safe for concurrent use).
 */
const char *kern_session_cookie(const kern_session_t *session);

/* ============================================================
 * Password Auth API (kern_auth.c)
 * ============================================================ */

/**
 * Hash a password using SHA-256 with a random salt.
 * Returns allocated string in format "hex_salt$hex_hash".
 * Caller must free the returned string.
 */
char *kern_password_hash(const char *password);

/**
 * Verify a password against a stored hash string.
 * Returns true if the password matches.
 */
bool kern_password_verify(const char *password, const char *hash_str);

#ifdef __cplusplus
}
#endif

#endif /* KERN_H */
