# API Reference (v0.2)

This document covers the public API surface exposed by `<kern.h>`. Sections marked **(implemented)** reflect what exists in code today. Sections marked **(planned)** describe the intended API for features not yet built — the signatures may change during implementation.

---

## Core Types

### kern_str_t

Length-prefixed string. Does not require null termination but includes it for C interop.

```c
typedef struct {
    const char *data;
    size_t      len;
} kern_str_t;

// Create from C string literal
kern_str_t s = KERN_STR("hello");

// Create from data + length
kern_str_t s = kern_str_new(data, len);

// Compare
bool eq = kern_str_eq(a, b);

// Duplicate (allocates)
kern_str_t copy = kern_str_dup(s);
```

### kern_buf_t

Growable byte buffer for building responses, HTML output, and string concatenation.

```c
kern_buf_t *buf = kern_buf_new(4096);      // initial capacity
kern_buf_write(buf, "hello", 5);           // append bytes
kern_buf_writef(buf, "count: %d", n);      // printf-style append
kern_buf_write_str(buf, s);                // append kern_str_t
const char *result = kern_buf_str(buf);    // get null-terminated result
size_t len = kern_buf_len(buf);            // current length
kern_buf_reset(buf);                       // clear without freeing
kern_buf_free(buf);                        // release memory
```

### kern_dict_t

String-keyed hash map for request parameters, template variables, and headers.

```c
kern_dict_t *d = kern_dict_new();
kern_dict_set(d, "key", value);            // set (void* value)
void *v = kern_dict_get(d, "key");         // get
bool has = kern_dict_has(d, "key");        // check existence
size_t n = kern_dict_len(d, "key");        // array length (for lists)
kern_dict_del(d, "key");                   // remove
kern_dict_free(d);                         // release
```

### kern_arena_t

Per-request arena allocator. All memory allocated from the arena is freed in one shot when the request completes.

```c
kern_arena_t *a = kern_arena_new(8192);    // 8KB initial block
void *p = kern_arena_alloc(a, size);       // allocate (never returns NULL in practice)
char *s = kern_arena_strdup(a, "hello");   // duplicate string into arena
kern_arena_reset(a);                       // free all allocations at once
kern_arena_free(a);                        // release the arena itself
```

---

## Application Lifecycle

### kern_app_t

The top-level application object.

```c
#include <kern.h>

int main(int argc, char **argv) {
    kern_app_t *app = kern_app_new("myapp");

    // Optional: manual route registration (file-system routing is preferred)
    kern_app_route(app, "GET", "/health", health_handler);

    // Start listening
    kern_app_listen(app, kern_env_int("PORT", 3000));

    // Run the event loop (blocks until shutdown)
    return kern_app_run(app);
}
```

### Configuration

```c
// Load config (called automatically by kern_app_new)
kern_config_t *cfg = kern_config_load("kern.toml");

// Access values
const char *name = kern_config_get_str("app.name");
int port         = kern_config_get_int("app.port");
bool tailwind    = kern_config_get_bool("assets.tailwind");

// Environment variable access
const char *secret = kern_env("MYAPP_SECRET");       // NULL if unset
int port           = kern_env_int("PORT", 3000);     // default value
```

---

## Request / Response

### kern_req_t

The request object, available in every handler.

```c
typedef struct kern_req_s {
    kern_method_t   method;         // GET, POST, PUT, PATCH, DELETE
    kern_str_t      path;           // /posts/42
    kern_str_t      host;           // example.com
    kern_dict_t    *params;         // path params (typed)
    kern_dict_t    *query;          // query string params
    kern_dict_t    *headers;        // request headers
    kern_dict_t    *cookies;        // parsed cookies
    kern_session_t *session;        // lazy-loaded session
    kern_user_t    *current_user;   // lazy-loaded current user
    void           *ud;             // user data pointer
} kern_req_t;
```

### Request Helpers

```c
// Path parameters
const char *id_str = kern_param(req, "id");          // raw string
int64_t id         = kern_param_int(req, "id");      // parsed int (400 on failure)
const char *uuid   = kern_param_uuid(req, "id");     // validated UUID (400 on failure)

// Query parameters
const char *q      = kern_query(req, "q");           // raw string, NULL if missing
int page           = kern_query_int(req, "page", 1); // with default value

// Headers
const char *ct     = kern_header_get(req, "Content-Type");

// Form body (application/x-www-form-urlencoded or multipart/form-data)
const char *email  = kern_form_str(req, "email");
int64_t age        = kern_form_int(req, "age");

// JSON body
cJSON *body        = kern_json_body(req);

// Cookies
const char *token  = kern_cookie_get(req, "token");
```

### Response Builders

Every handler must return a `kern_response_t *`:

```c
// Render a template
kern_response_t *kern_render(kern_req_t *req, const char *view, kern_dict_t *vars);

// JSON response
kern_response_t *kern_json(kern_req_t *req, cJSON *obj);

// Redirect
kern_response_t *kern_redirect(kern_req_t *req, const char *path, int status);
// Shorthand (302):
kern_response_t *kern_redirect(req, "/posts");

// Error pages
kern_response_t *kern_404(kern_req_t *req);
kern_response_t *kern_500(kern_req_t *req, const char *error);

// Static file
kern_response_t *kern_send_file(kern_req_t *req, const char *path);

// SSE stream *(planned — v0.5)*
kern_response_t *kern_sse(kern_req_t *req, kern_sse_t *chan);

// Chunked stream *(planned — v0.5)*
kern_response_t *kern_stream(kern_req_t *req, kern_body_fn body_fn);

// Raw response
kern_response_t *kern_response_new(int status, const char *content_type, const char *body);
```

### Response Modification

```c
// Set response headers
kern_header_set(res, "X-Custom", "value");

// Set cookies
kern_cookie_set(res, "token", value, &(kern_cookie_opts_t){
    .max_age  = 86400,
    .httponly  = true,
    .secure   = true,
    .samesite = KERN_SAMESITE_LAX,
    .path     = "/",
});
```

---

## Router API

### Route Registration Macros

Used in `pages/*.c` files:

```c
// Single method + path
KERN_PAGE("/path", handler_fn);       // GET by default
KERN_GET("/path", handler_fn);
KERN_POST("/path", handler_fn);
KERN_PUT("/path", handler_fn);
KERN_PATCH("/path", handler_fn);
KERN_DELETE("/path", handler_fn);

// Handler signature
static kern_response_t *handler_fn(kern_req_t *req);
```

### Programmatic Registration

For cases where file-system routing is insufficient:

```c
kern_router_t *r = kern_app_router(app);
kern_router_add(r, "GET",    "/api/v1/users",     api_users_list);
kern_router_add(r, "POST",   "/api/v1/users",     api_users_create);
kern_router_add(r, "GET",    "/api/v1/users/:id",  api_users_show);
kern_router_add(r, "PATCH",  "/api/v1/users/:id",  api_users_update);
kern_router_add(r, "DELETE", "/api/v1/users/:id",  api_users_delete);
```

### Layout and Middleware

```c
// Layout wrapper (in pages/_layout.c)
KERN_LAYOUT("section_name") {
    // Runs before every page in this directory
    // Must call kern_pass() to continue or return a response to short-circuit
    kern_layout_with("layouts/admin", ctx);
    return kern_pass();
}

// Middleware (in pages/_middleware.c)
KERN_MIDDLEWARE("section_name") {
    kern_header_set(res, "X-Custom", "value");
    return kern_pass();
}
```

---

## Template Rendering

### kern_render

Render a `.khtml` template with variables:

```c
// Using kern_dict_t explicitly (current API)
kern_dict_t *vars = kern_dict_new();
kern_dict_set(vars, "title", "Hello");
kern_dict_set(vars, "items", item_list);
return kern_render(req, "pages/home", vars);

// KERN_T macro — convenience shorthand *(planned)*
// return kern_render(req, "posts/show", KERN_T(
//     "post", post,
//     "comments", comments,
//     "comments_len", (size_t)count
// ));
```

### Template Helper Variable Macro *(planned)*

```c
// KERN_T creates a kern_dict_t from key-value pairs
// kern_dict_t *vars = KERN_T(
//     "key1", value1,
//     "key2", value2,
//     "key3", value3
// );
```

### Asset URLs in Templates

```c
// In C code
const char *url = kern_asset_url(req, "css/app.css");
// Returns: "/assets/app-a1b2c3.css"

// In .khtml templates, use the {asset(...)} helper:
// link(rel="stylesheet" href={asset("css/app.css")})
```

---

## Database API

### Connection

```c
// Get a database connection (from the pool)
kern_db_t *db = kern_db_get();

// The connection is automatically released when the request ends.
// For explicit lifecycle management:
kern_db_t *db = kern_db_open("path/to/db.sqlite");
kern_db_close(db);
```

### Raw SQL

```c
// Execute (returns affected row count)
int64_t n = kern_db_exec(db, "DELETE FROM sessions WHERE expired = 1");

// Query with parameters (prevents SQL injection)
kern_rows_t *rows = kern_db_query(db, "SELECT * FROM users WHERE email = ?", email);

// Iterate results
while (kern_rows_next(rows)) {
    int64_t id       = kern_rows_int(rows, 0);
    const char *name = kern_rows_str(rows, 1);
}
kern_rows_free(rows);
```

### Query Builder *(implemented)*

The query builder uses a procedural API with typed functions:

```c
// SELECT
kern_qb_t *q = kern_qb_new(db);
kern_qb_select(q, "*");
kern_qb_from(q, "posts");
kern_qb_where_bool(q, "published", "=", true);
kern_qb_order(q, "created_at", "DESC");
kern_qb_limit(q, 20);

kern_db_result_t *result = kern_qb_query(q);  // execute
kern_db_result_t *one    = kern_qb_one(q);     // single row or NULL

// INSERT
kern_qb_t *q = kern_qb_new(db);
kern_qb_insert(q, "posts");
kern_qb_set_str(q, "title", "Hello");
kern_qb_set_str(q, "slug", "hello");
kern_qb_set_int(q, "author_id", 1);
int64_t affected = kern_qb_exec(q);

// UPDATE
kern_qb_t *q = kern_qb_new(db);
kern_qb_update(q, "posts");
kern_qb_set_str(q, "title", "Updated");
kern_qb_where_int(q, "id", "=", 42);
kern_qb_exec(q);

// DELETE
kern_qb_t *q = kern_qb_new(db);
kern_qb_delete(q, "posts");
kern_qb_where_int(q, "id", "=", 42);
kern_qb_exec(q);

// Cleanup
kern_qb_free(q);
```

### Model Macros *(planned)*

The `KERN_MODEL`, `KERN_INSERT`, and `KERN_COL_*` macros are designed to provide a higher-level DX for defining models and performing CRUD. These are not yet implemented — the query builder above is the current API.

```c
// Planned API — signatures may change during implementation:
// KERN_MODEL(post,
//     KERN_COL_INT(id, PK, AUTOINCREMENT),
//     KERN_COL_STR(title, NOT_NULL, MAX 200),
//     KERN_COL_STR(slug, UNIQUE, NOT_NULL),
//     KERN_COL_TEXT(body),
//     KERN_COL_INT(author_id, FK(users.id), NOT_NULL),
//     KERN_COL_BOOL(published, DEFAULT false),
//     KERN_COL_TIMESTAMP(created_at, DEFAULT NOW),
//     KERN_COL_TIMESTAMP(updated_at, ON_UPDATE NOW)
// );
```

### Transactions

```c
kern_db_t *db = kern_db_get();
kern_tx_t *tx = kern_tx_begin(db);

// ... perform query builder operations ...

if (error_condition) {
    kern_tx_rollback(tx);
    return kern_500(req, "failed");
}

kern_tx_commit(tx);
```

---

## Session API

```c
// Start or resume a session
kern_session_start(req);

// Store a value
kern_session_set(req, "user_id", (void *)(intptr_t)user_id);

// Retrieve a value
int64_t uid = (int64_t)(intptr_t)kern_session_get(req, "user_id");

// Destroy the session (logout)
kern_session_destroy(req);

// Flash messages (available for one request, then cleared)
kern_flash_set(req, "Welcome back!");
const char *flash = kern_flash_get(req);
```

### Session Configuration (kern.toml)

```toml
[session]
cookie = "myapp_sess"
ttl    = 604800          # 7 days in seconds
driver = "memory"        # memory | redis | file
```

---

## Authentication API

### Password Hashing

```c
// Hash a password (PBKDF2-HMAC-SHA256, 100K iterations, runs in worker thread)
char *hash = kern_password_hash("plaintext_password");

// Verify a password against a hash
bool valid = kern_password_verify("plaintext_password", stored_hash);
```

### Current User

```c
// Get the currently authenticated user (NULL if not logged in)
kern_user_t *user = kern_current_user(req);

if (!user) {
    return kern_redirect(req, "/login", 302);
}
```

### CSRF Protection

```c
// In a template: render the hidden CSRF input
// form(method="post" action="/posts")
//   input(type="hidden" name="_csrf" value={csrf_token()})

// In C code: generate the token string
const char *token = kern_csrf_token(req);

// Validation is automatic for POST/PATCH/DELETE requests
// when CSRF middleware is active (default for all pages).
```

### Authorization *(planned — v0.3)*

```c
// Define a policy *(planned)*
// KERN_POLICY(post_update) {
//     kern_user_t *u = kern_current_user(req);
//     if (!u) return kern_deny("login_required");
//     if (u->id == post->author_id || u->is_admin) return kern_allow();
//     return kern_deny("forbidden");
// }

// Use in a handler *(planned)*
// KERN_AUTHORIZE(req, post_update, post);
// If denied, automatically returns 403 with the deny reason.
```

---

## Configuration API

### kern.toml Access

```c
// String values
const char *name = kern_config_get_str("app.name");

// Integer values
int port = kern_config_get_int("app.port");

// Boolean values
bool minify = kern_config_get_bool("assets.minify");

// Custom section values
const char *val = kern_config_get_str("custom.my_setting");

// With defaults
int workers = kern_config_get_int_or("app.workers", 4);
```

### Environment Variables

```c
// Direct env access
const char *secret = kern_env("MYAPP_SECRET");       // NULL if unset
int port           = kern_env_int("PORT", 3000);     // with default
bool debug         = kern_env_bool("DEBUG", false);  // with default
```

### kern.toml Structure

```toml
[app]
name     = "myapp"
version  = "0.1.0"
port     = 3000
secret   = "${env:MYAPP_SECRET}"
env      = "dev"

[database]
driver   = "sqlite"
path     = "./db/dev.sqlite"

[session]
cookie   = "myapp_sess"
ttl      = 604800
driver   = "memory"

[assets]
tailwind = true
minify   = true

[logging]
level    = "info"
format   = "text"
output   = "stdout"

[custom]
# Anything here is accessible via kern_config_get_str("custom.key")
```

---

## Logging API

```c
// Log levels
kern_log_debug("detailed info", KERN_F("key", "%s", value));
kern_log_info("user.login", KERN_F("user_id", "%d", uid));
kern_log_warn("rate.limited", KERN_F("ip", "%s", ip));
kern_log_error("db.timeout", KERN_F("query_ms", "%d", ms));

// KERN_F creates structured fields
// Output (text mode):
//   2025-01-15T10:30:00Z [INFO] user.login user_id=42
// Output (JSON mode):
//   {"ts":"2025-01-15T10:30:00Z","level":"info","event":"user.login","user_id":42}
```

---

## Utility Macros

```c
// Template variables shorthand *(planned)*
// KERN_T("key1", val1, "key2", val2, ...)

// Structured log fields
KERN_F("field_name", "format", value)

// Route registration *(implemented)*
KERN_PAGE("/path", handler)
KERN_GET("/path", handler)
KERN_POST("/path", handler)
KERN_PATCH("/path", handler)
KERN_DELETE("/path", handler)

// Model definition *(planned)*
// KERN_MODEL(name, columns...)
// KERN_COL_INT(name, constraints...)
// KERN_COL_STR(name, constraints...)
// KERN_COL_TEXT(name)
// KERN_COL_BOOL(name, constraints...)
// KERN_COL_TIMESTAMP(name, constraints...)

// Database insert *(planned)*
// KERN_INSERT(tx, model, .field = value, ...)

// Authorization *(planned — v0.3)*
// KERN_AUTHORIZE(req, policy, resource)
```

---

## Error Handling

kern does not use exceptions. Errors are returned as values:

```c
// Mutation results
typedef struct {
    bool    ok;
    void   *value;        // success value (e.g., created record)
    kern_validation_t *errors;  // validation errors on failure
} kern_mutation_result_t;

// Check and handle
kern_mutation_result_t r = post_create(req, &input);
if (!r.ok) {
    kern_dict_t *vars = kern_dict_new();
    kern_dict_set(vars, "errors", r.errors);
    kern_dict_set(vars, "input", &input);
    return kern_render(req, "posts/new", vars);
}
// Success: r.value contains the created post
```

### Validation

```c
kern_validation_t *v = kern_validate_start(req);
KERN_REQUIRE_STR(v, title, input->title, MIN 3, MAX 200);
KERN_REQUIRE_EMAIL(v, email, input->email);
KERN_REQUIRE_INT(v, age, input->age, MIN 13, MAX 150);
KERN_REQUIRE_UNIQUE(v, email, input->email, user_find_by_email);

if (kern_validation_failed(v)) {
    return kern_mutation_fail(v);
}
```
