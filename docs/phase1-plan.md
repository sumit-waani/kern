# Phase 1 Implementation Plan (v0.1)

## Goal

Deliver a working proof of the core: an HTTP server that routes requests via file-system conventions, renders compiled templates, queries a SQLite database, manages sessions, and provides basic auth. The developer experience is `kern new`, `kern dev`, and `kern build`.

## Milestones

### Milestone 1: Foundation (Weeks 1-2)

**Objective:** Establish the project structure, build system, and core data types.

#### Tasks

1. **Project scaffolding**
   - Create CMake build system for libkern
   - Set up directory structure: `src/`, `include/`, `cli/`, `tests/`, `vendor/`
   - Configure CI flags: `-std=c11 -pedantic -Wall -Wextra`
   - Verify builds with both gcc and clang

2. **Core types and memory**
   - Implement `kern_str_t` (length-prefixed string)
   - Implement `kern_buf_t` (growable buffer)
   - Implement `kern_dict_t` (string-keyed hash map)
   - Implement `kern_arena_t` (per-request arena allocator)
   - Implement `kern_arr_t` (dynamic array)

3. **Configuration parser**
   - Vendor a TOML parser (or implement minimal subset)
   - Implement `kern_config_load("kern.toml")`
   - Support `${env:VAR}` interpolation
   - Expose `kern_config_get_str()`, `kern_config_get_int()`, `kern_config_get_bool()`

4. **Logging**
   - Implement structured logging: `kern_log_info()`, `kern_log_warn()`, `kern_log_error()`, `kern_log_debug()`
   - Support text and JSON output formats
   - Thread-safe log writes

#### Acceptance Criteria
- `cmake .. && make` succeeds with zero warnings on gcc and clang
- Unit tests pass for all core data structures
- `kern_config_load` correctly parses a sample `kern.toml`
- Logging output is correct in both text and JSON modes

---

### Milestone 2: HTTP Server (Weeks 3-4)

**Objective:** A working HTTP/1.1 server using libuv with keep-alive support.

#### Tasks

1. **Event loop integration**
   - Integrate libuv as the reactor
   - Implement TCP listener with `uv_tcp_t`
   - Connection accept and lifecycle management
   - Graceful shutdown on SIGTERM/SIGINT

2. **HTTP/1.1 parser**
   - Implement request line parsing (method, path, version)
   - Header parsing (case-insensitive keys)
   - Body reading (Content-Length and chunked transfer)
   - Keep-alive connection management
   - Proper error responses for malformed requests

3. **Request/response types**
   - Implement `kern_req_t` struct (method, path, headers, params, query, cookies)
   - Implement `kern_res_t` struct (status, headers, body)
   - Cookie parsing
   - Query string parsing
   - Response serialization and sending

4. **Static file serving**
   - Serve files from `public/` directory
   - `sendfile(2)` for efficiency
   - ETag generation and `304 Not Modified`
   - Content-Type detection
   - Range request support (basic)

#### Acceptance Criteria
- Server starts and responds to HTTP/1.1 requests
- Keep-alive works correctly (multiple requests per connection)
- Static files served with proper caching headers
- `wrk` benchmark shows > 10K req/s for static responses
- Graceful shutdown completes in-flight requests

---

### Milestone 3: Fiber Runtime (Weeks 5-6)

**Objective:** Stackful coroutines so request handlers can write synchronous-looking code.

#### Tasks

1. **Context switching**
   - Implement `kern_fiber_t` using `ucontext.h` (portable)
   - Custom asm trampoline for x86_64 (faster path)
   - 1KB stack per fiber with guard pages
   - Fiber pool for reuse (avoid malloc per request)

2. **Scheduler**
   - Cooperative scheduler on the reactor thread
   - `kern_yield()` to suspend current fiber
   - `kern_resume(fiber)` to wake a fiber
   - Integration with libuv: fiber yields while waiting for I/O

3. **Async primitives**
   - `kern_async_t` future type
   - `kern_await()` that yields and resumes on completion
   - Worker thread dispatch for blocking operations
   - Thread-safe completion notification back to reactor

4. **Per-request fiber**
   - Each incoming HTTP request gets its own fiber
   - Request context lives on the fiber (no global state)
   - Fiber cleanup on request completion

#### Acceptance Criteria
- Fibers context-switch in < 100ns on x86_64
- Concurrent requests handled without blocking the event loop
- Blocking DB calls (simulated) yield correctly and resume
- No memory leaks under sustained load (valgrind clean)

---

### Milestone 4: Radix Router (Weeks 6-7)

**Objective:** A high-performance radix tree router with path parameters.

#### Tasks

1. **Radix tree data structure**
   - Implement compressed radix tree (Patricia trie)
   - Support static segments, parameterized segments (`:id`), and wildcards (`*`)
   - Lock-free reads (routes registered at startup only)
   - Method dispatch (GET, POST, PUT, PATCH, DELETE)

2. **Route matching**
   - `kern_router_add(router, method, path, handler)` registration
   - `kern_router_match(router, method, path)` lookup
   - Path parameter extraction into `kern_dict_t`
   - 404 for unknown paths, 405 for wrong method

3. **Parameter types**
   - `kern_param(req, "id")` - raw string
   - `kern_param_int(req, "id")` - parsed int64_t (400 on failure)
   - `kern_param_uuid(req, "id")` - validated UUID format

4. **Integration with HTTP server**
   - Wire router into request dispatch
   - Route lookup per request, dispatch to handler fiber

#### Acceptance Criteria
- Router correctly matches all test patterns including nested params
- `kern_router_match` completes in < 200ns for a tree with 100 routes
- 404 and 405 responses generated correctly
- Parameters extracted and type-coerced properly

---

### Milestone 5: File-System Routing (Weeks 7-8)

**Objective:** Scan `pages/` directory and auto-generate the route registry.

#### Tasks

1. **Directory scanner**
   - Recursive scan of `pages/` at build time
   - Map file paths to URL patterns:
     - `pages/index.c` -> `GET /`
     - `pages/posts/index.c` -> `GET /posts`
     - `pages/posts/[id]/index.c` -> `GET /posts/:id`
   - Parse `KERN_PAGE`, `KERN_GET`, `KERN_POST`, `KERN_PATCH`, `KERN_DELETE` macros

2. **Registry code generation**
   - Generate `build/_pages_registry.c` with all route registrations
   - Emit `extern` declarations for all handler functions
   - Generate `kern_register_all_routes(kern_router_t *r)` function
   - Handle method dispatch from macro type

3. **Layout and middleware support**
   - Detect `_layout.c` files (layout wrappers)
   - Detect `_middleware.c` files (before-handlers)
   - Generate proper call chain ordering

4. **Validation**
   - Detect duplicate routes (build error)
   - Detect malformed `[param]` syntax (build error)
   - Warn on unreachable routes

#### Acceptance Criteria
- Scanner correctly maps test directory structures to routes
- Generated registry compiles without errors
- Routes registered from file system match correctly at runtime
- Duplicate route detection works
- Layouts and middleware applied in correct order

---

### Milestone 6: .khtml Template Compiler (Weeks 8-10)

**Objective:** Compile Pug-style `.khtml` templates to C source files at build time.

#### Tasks

1. **Lexer**
   - Indentation-based tokenization
   - Token types: tag, attribute, text, interpolation, statement, comment
   - Handle `#{expr}` interpolation markers
   - Handle `-` prefix for C statements (if, for, etc.)

2. **Parser**
   - Build AST from indentation levels
   - Node types: Element, Text, Interpolation, Statement, Include, Extend, Block
   - Attribute parsing (static and dynamic `{expr}`)
   - `| text` literal lines
   - `include <path>` directives
   - `extend <layout>` and `block name` for inheritance

3. **Code generator**
   - Emit C source that builds HTML using `kern_buf_t`
   - Auto-escape `#{expr}` through `kern_html_write_esc()`
   - Raw output for `!{expr}` (unescaped)
   - Generate proper tag open/close pairs
   - Self-closing tags for void elements
   - `doctype html` support

4. **Layout inheritance**
   - `extend layouts/base` resolves to `views/layouts/base.khtml`
   - `block name` / `endblock` regions
   - Child blocks override parent blocks
   - Default content in parent blocks

5. **HTML runtime helpers**
   - `kern_html_doctype(buf, "html")`
   - `kern_html_tag_open(buf, "div", ...attrs)`
   - `kern_html_tag_close(buf, "div")`
   - `kern_html_tag_self(buf, "input", ...attrs)`
   - `kern_html_write(buf, text)` - raw write
   - `kern_html_write_esc(buf, text)` - HTML-escaped write

6. **Integration**
   - `kern_render(req, "posts/show", vars)` dispatches to compiled template
   - Template function signature: `kern_response_t *kern_render_<name>(kern_req_t *, kern_dict_t *)`
   - Missing template = build error
   - Missing include = build error

#### Acceptance Criteria
- Templates with tags, attributes, text, and interpolation compile correctly
- Generated C produces valid HTML output
- XSS escaping is automatic for `#{expr}`
- Layout inheritance works (extend + block)
- Include directives resolve and inline correctly
- Build fails on missing templates or includes

---

### Milestone 7: SQLite Driver and Query Builder (Weeks 10-12)

**Objective:** SQLite integration with a type-safe query builder.

#### Tasks

1. **SQLite driver**
   - `kern_db_open(path)` / `kern_db_close(db)`
   - WAL mode enabled by default
   - Prepared statement caching
   - Fiber-aware: yield while waiting for lock, resume on completion
   - Worker thread dispatch for blocking operations

2. **Connection management**
   - In-process pool: unlimited read connections (WAL), single writer with queue
   - `kern_db_get()` acquires from pool
   - Automatic release on request end

3. **Query builder**
   - `kern_qb(db)` - create a query builder
   - `->select(table, cols...)` - SELECT clause
   - `->from(table)` - FROM clause
   - `->where(col, op, value)` - WHERE conditions (parameterized)
   - `->order_by(col, dir)` - ORDER BY
   - `->limit(n)` / `->offset(n)` - pagination
   - `->join(table, on_clause)` - JOIN
   - `kern_qb_one(q, type)` - execute and return single row
   - `kern_qb_all(q, type)` - execute and return all rows
   - `kern_qb_count(q)` - COUNT query

4. **Raw SQL support**
   - `kern_db_exec(db, sql)` - execute statement, return affected rows
   - `kern_db_query(db, sql, params...)` - execute query, return rows
   - Parameterized queries (prevent SQL injection)

5. **Transactions**
   - `kern_tx_begin(db)` / `kern_tx_commit(tx)` / `kern_tx_rollback(tx)`
   - Nested transactions via savepoints

6. **Model macros**
   - `KERN_MODEL(name, cols...)` defines the struct and metadata
   - `KERN_INSERT(tx, model, .field = value, ...)` - insert row
   - Auto-generated `_find_by_id()`, `_create()`, `_update()`, `_delete()`

#### Acceptance Criteria
- SQLite opens in WAL mode, reads and writes succeed
- Query builder produces correct parameterized SQL
- No SQL injection possible through the query builder
- Transactions commit and rollback correctly
- Fiber integration: DB queries do not block the event loop
- Model macros generate correct insert/select/update/delete

---

### Milestone 8: Migrations (Weeks 12-13)

**Objective:** Numbered SQL migration system with up/down support.

#### Tasks

1. **Migration discovery**
   - Scan `db/migrations/` for numbered SQL files
   - Support both flat format (`001_users.sql`) and directory format (`001_users/up.sql`, `001_users/down.sql`)
   - Sort by number, validate sequence (no gaps)

2. **Migration state tracking**
   - Create `kern_migrations` table on first run
   - Track: migration number, name, applied_at, checksum
   - Detect already-applied migrations

3. **CLI commands**
   - `kern db.migrate` - run all pending migrations
   - `kern db.rollback` - roll back last migration
   - `kern db.migrate --to=N` - migrate to specific version
   - `kern db.status` - show migration state

4. **Safety**
   - Wrap each migration in a transaction
   - Checksum validation (detect modified migrations)
   - Lock to prevent concurrent migrations

#### Acceptance Criteria
- Migrations run in order, tracked in `kern_migrations` table
- Rollback reverses the last migration
- `kern db.status` shows correct pending/applied state
- Modified migration files are detected and rejected
- Concurrent migration attempts are serialized

---

### Milestone 9: Sessions and Basic Auth (Weeks 13-14)

**Objective:** Cookie-based sessions and email/password authentication.

#### Tasks

1. **Session management**
   - `kern_session_start(req)` - create or resume session
   - `kern_session_set(req, key, value)` - store value
   - `kern_session_get(req, key)` - retrieve value
   - `kern_session_destroy(req)` - invalidate session
   - Session ID: 256-bit random (`getrandom(2)`)
   - Cookie: HttpOnly, SameSite=Lax, Secure (in prod)
   - HMAC-SHA256 signature against `app.secret`
   - In-memory session store (default)

2. **Password hashing**
   - `kern_password_hash(plain)` - hash with Argon2id
   - `kern_password_verify(plain, hash)` - verify
   - Tunable parameters via config
   - Worker thread dispatch (CPU-intensive)

3. **CSRF protection**
   - Double-submit cookie pattern
   - Per-session token generation
   - Validation on POST/PATCH/DELETE
   - `kern_csrf_field()` - render hidden input in forms
   - `X-CSRF-Token` header support for AJAX

4. **Auth scaffold**
   - Login page (`/login`) with rate limiting
   - Signup page (`/signup`) with validation
   - Logout (`/logout`) - destroy session
   - `kern_current_user(req)` helper
   - `KERN_AUTHORIZE(req, policy, resource)` macro

#### Acceptance Criteria
- Sessions persist across requests via cookies
- Session cookies are properly signed and validated
- Password hashing uses Argon2id with safe defaults
- CSRF tokens validated on all state-changing requests
- Login rate limiting works (5 attempts / 15 min / IP)
- Auth flow: signup, login, access protected page, logout

---

### Milestone 10: Asset Pipeline (Weeks 14-15)

**Objective:** Content-hashed asset fingerprinting for cache-busting.

#### Tasks

1. **Asset scanner**
   - Scan `assets/` directory recursively
   - Compute SHA256 hash of each file
   - Generate fingerprinted filename: `app.css` -> `app-a1b2c3.css`
   - Copy to `public/assets/`

2. **Manifest generation**
   - Generate `build/asset_manifest.h` with `#define` macros
   - Generate `build/asset_manifest.c` with lookup table
   - `kern_asset_url(req, "css/app.css")` returns `/assets/app-a1b2c3.css`

3. **Cache headers**
   - Hashed assets: `Cache-Control: public, max-age=31536000, immutable`
   - Non-hashed files (favicon, robots.txt): `Cache-Control: no-cache`

4. **Template integration**
   - `{asset("css/app.css")}` in templates resolves to hashed URL
   - Build error if referenced asset does not exist

#### Acceptance Criteria
- Assets are fingerprinted with content hash
- Manifest maps original names to hashed names
- `kern_asset_url()` returns correct hashed paths
- Cache headers set correctly for hashed vs. non-hashed files
- Template `{asset(...)}` helper resolves at compile time

---

### Milestone 11: kern CLI (Weeks 15-17)

**Objective:** The `kern new`, `kern dev`, and `kern build` commands.

#### Tasks

1. **`kern new <name>`**
   - Create project directory with full scaffold:
     - `app.c`, `kern.toml`
     - `pages/index.c`
     - `views/pages/home.khtml`, `views/layouts/base.khtml`
     - `models/user.{c,h}`
     - `db/migrations/001_users.sql`
     - `assets/css/app.css`
     - `.gitignore`, `README.md`
   - Initialize git repository
   - Print success message with next steps

2. **`kern build`**
   - Orchestrate the full build pipeline:
     1. Scan pages/ -> generate registry
     2. Compile .khtml -> C
     3. Hash assets -> public/
     4. Invoke cc/clang with proper flags
     5. Link against libkern, sqlite3, libuv
     6. Output `dist/<appname>`
   - Production flags: `-O2 -flto -fvisibility=hidden`
   - Strip debug symbols

3. **`kern dev`**
   - Run the build pipeline
   - Start the compiled server
   - Watch for file changes (via libuv `uv_fs_event_t`)
   - Hot-reload on changes (rebuild affected parts, restart if needed)
   - Print colored log output

4. **CLI framework**
   - Argument parsing
   - Subcommand dispatch
   - `--help` for all commands
   - `--version` output
   - Colored terminal output

#### Acceptance Criteria
- `kern new myapp` creates a complete, buildable project
- `kern build` produces a working single binary
- `kern dev` starts a server and auto-rebuilds on changes
- All commands have working `--help` output
- Error messages are clear and actionable

---

## Dependency Graph

```
Milestone 1 (Foundation)
    │
    ├──► Milestone 2 (HTTP Server)
    │        │
    │        ├──► Milestone 3 (Fiber Runtime)
    │        │        │
    │        │        └──► Milestone 7 (SQLite + Query Builder)
    │        │                  │
    │        │                  ├──► Milestone 8 (Migrations)
    │        │                  └──► Milestone 9 (Sessions + Auth)
    │        │
    │        └──► Milestone 4 (Radix Router)
    │                  │
    │                  └──► Milestone 5 (File-System Routing)
    │
    └──► Milestone 6 (.khtml Compiler)
              │
              └──► Milestone 10 (Asset Pipeline)

Milestone 11 (CLI) depends on all above milestones.
```

## Component Dependencies (Build Order)

| Component | Depends On |
|-----------|-----------|
| Core types (str, buf, dict, arena) | Nothing (pure C) |
| Config (TOML parser) | Core types |
| Logging | Core types |
| HTTP server | libuv, core types, logging |
| Fiber runtime | libuv (for scheduling), core types |
| Radix router | Core types |
| File-system routing scanner | Radix router (generates registrations for it) |
| .khtml compiler | Core types (standalone build tool) |
| SQLite driver | sqlite3, fiber runtime, core types |
| Query builder | SQLite driver, core types |
| Migrations | SQLite driver, CLI framework |
| Sessions | Core types, crypto (HMAC), HTTP (cookies) |
| Auth | Sessions, SQLite driver, password hashing |
| Asset pipeline | Core types, crypto (SHA256) |
| kern CLI | All of the above |

## Timeline Summary

| Weeks | Milestone | Deliverable |
|-------|-----------|-------------|
| 1-2 | Foundation | Build system, core types, config, logging |
| 3-4 | HTTP Server | Working HTTP/1.1 server with static files |
| 5-6 | Fiber Runtime | Cooperative coroutines, async I/O |
| 6-7 | Radix Router | High-performance route matching |
| 7-8 | File-System Routing | Auto-generated route registry from pages/ |
| 8-10 | .khtml Compiler | Template-to-C compilation |
| 10-12 | SQLite + Query Builder | Database layer |
| 12-13 | Migrations | Schema versioning |
| 13-14 | Sessions + Auth | Cookie sessions, login/signup |
| 14-15 | Asset Pipeline | Content-hashed static assets |
| 15-17 | kern CLI | `kern new`, `kern dev`, `kern build` |

**Total estimated: 17 weeks for v0.1**

## Acceptance Criteria for v0.1 Complete

1. `kern new myapp` scaffolds a project with working defaults
2. `kern dev` starts a dev server that responds to HTTP requests
3. `kern build` produces a single static binary
4. File-system routing maps `pages/*.c` to URL handlers
5. `.khtml` templates compile to C and render HTML
6. SQLite queries work through the query builder
7. Migrations run via `kern db.migrate`
8. Sessions persist across requests
9. Login/signup auth flow works end-to-end
10. Assets are fingerprinted and served with proper cache headers
11. The entire system builds with `gcc -std=c11 -pedantic -Wall -Wextra` with zero warnings
12. The entire system also builds with `clang -std=c11`
13. All unit and integration tests pass
