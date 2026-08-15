# Project Layout

kern uses a convention-based folder structure. The layout itself is the contract: placing a file in the right directory gives it the right behavior without additional configuration.

## Complete Directory Structure

```
myapp/
+-- kern.toml                  # app config (port, db, secret, env vars)
+-- app.c                      # entry point (required)
+-- pages/                     # file-system routing
|   +-- index.c                # -> GET /
|   +-- about.c                # -> GET /about
|   +-- _layout.c             # layout wrapper (no URL, wraps children)
|   +-- _middleware.c          # middleware for this directory branch
|   +-- posts/
|   |   +-- index.c            # -> GET /posts
|   |   +-- new.c              # -> GET /posts/new
|   |   +-- create.c           # -> POST /posts
|   |   +-- [id]/
|   |       +-- index.c        # -> GET /posts/:id
|   |       +-- edit.c         # -> GET /posts/:id/edit
|   |       +-- update.c       # -> PATCH /posts/:id
|   |       +-- destroy.c      # -> DELETE /posts/:id
|   +-- api/
|       +-- health.c           # -> GET /api/health
+-- views/                     # .khtml templates
|   +-- layouts/
|   |   +-- base.khtml         # base HTML layout
|   |   +-- admin.khtml        # admin layout
|   +-- pages/
|   |   +-- home.khtml         # home page template
|   |   +-- posts/
|   |       +-- index.khtml    # post list template
|   |       +-- new.khtml      # new post form template
|   |       +-- show.khtml     # single post template
|   +-- partials/
|   |   +-- nav.kfrag          # navigation partial
|   |   +-- footer.kfrag       # footer partial
|   |   +-- flash.kfrag        # flash message partial
|   +-- components/
|       +-- ui/
|           +-- button.khtml   # button component
|           +-- card.khtml     # card component
|           +-- modal.khtml    # modal component
+-- models/                    # DB-backed structs
|   +-- user.c
|   +-- user.h
|   +-- post.c
|   +-- post.h
+-- queries/                   # read-side named queries
|   +-- users/
|   |   +-- find_by_email.c
|   |   +-- list_recent.c
|   +-- posts/
|       +-- find_by_id.c
|       +-- list_published.c
+-- mutations/                 # write-side, validated operations
|   +-- users/
|   |   +-- register.c
|   |   +-- update_profile.c
|   +-- posts/
|       +-- create.c
+-- middleware/                # custom middleware
|   +-- require_admin.c
+-- auth/                      # authentication flow
|   +-- login.c
|   +-- logout.c
|   +-- callbacks.c
+-- jobs/                      # background work
|   +-- send_welcome_email.c
|   +-- cleanup_expired.c
+-- mailers/                   # email templates
|   +-- welcome.ktxt
|   +-- password_reset.ktxt
+-- assets/                    # source assets (pre-hash)
|   +-- css/
|   |   +-- app.css            # custom CSS (loaded after Bootstrap)
|   |   +-- admin.css
|   +-- js/
|   |   +-- app.js
|   |   +-- shards.js          # optional shard runtime
|   +-- img/
+-- db/
|   +-- migrations/            # numbered SQL files
|       +-- 001_users.sql
|       +-- 002_posts.sql
|       +-- 003_add_post_slug.sql
+-- test/                      # test files
|   +-- unit/
|   |   +-- post_model_test.c
|   +-- integration/
|   |   +-- auth_flow_test.c
|   +-- helpers/
|       +-- http_client.c
+-- config/                    # environment-specific config overrides
|   +-- dev.c
|   +-- prod.c
|   +-- test.c
+-- public/                    # built static assets (generated, gitignored)
|   +-- assets/
|   |   +-- app-a1b2c3.css
|   |   +-- shards-d4e5f6.js
|   +-- favicon.ico
+-- build/                     # generated C code (gitignored)
+-- dist/                      # final binary (gitignored)
    +-- myapp
```

## Folder Responsibilities

### `pages/` - Route Handlers

Every `.c` file in `pages/` maps to a URL. The file path determines the route path.

| What goes here | What does NOT go here |
|----------------|----------------------|
| One `.c` file per URL segment | Business logic |
| A `KERN_PAGE(...)` or `KERN_GET/POST/...` macro | Complex queries (use `queries/`) |
| Thin handlers that call queries/mutations | Data validation (use `mutations/`) |

**Special files:**
- `_layout.c` - wraps all pages in the same directory (and subdirectories)
- `_middleware.c` - runs before all pages in the same directory

**Naming rules:**
- `index.c` maps to the directory's path (e.g., `pages/posts/index.c` -> `/posts`)
- `[param]` directories create path parameters (e.g., `pages/posts/[id]/` -> `/posts/:id`)
- Filenames (without `.c`) become the final path segment

### `views/` - Templates

All `.khtml`, `.ktxt`, and `.kfrag` templates live here. No C code.

| Extension | Purpose |
|-----------|---------|
| `.khtml` | HTML templates (full pages or components) |
| `.ktxt` | Plain text templates (emails) |
| `.kfrag` | Partials/fragments (included by other templates) |

**Subdirectories:**
- `views/layouts/` - base layouts with `block` regions
- `views/pages/` - page-specific templates
- `views/partials/` - reusable fragments (nav, footer, etc.)
- `views/components/` - UI components (button, card, etc.)

### `models/` - Data Structures

DB-backed structs and their lifecycle operations. Each model has a `.h` (struct + declarations) and a `.c` (implementation).

| What goes here | What does NOT go here |
|----------------|----------------------|
| Struct definitions with `KERN_MODEL(...)` | Multi-model business rules (use `services/` if needed) |
| Basic CRUD operations | Complex read queries (use `queries/`) |
| Field validations | Cross-model validation (use `mutations/`) |

### `queries/` - Read Operations

Pure read operations. Take a database handle, return data. No side effects.

```c
// queries/posts/find_by_id.c
post_t *post_find_by_id(int64_t id) {
    kern_db_t *db = kern_db_get();
    kern_qb_t *q = kern_qb(db)
        ->select(post, *)
        ->from(post)
        ->where(id, "=", kern_arg_int(id))
        ->limit(1);
    return kern_qb_one(q, post_t);
}
```

**Rules:**
- No writes (INSERT, UPDATE, DELETE)
- No side effects (no email, no queue dispatch)
- May join across tables
- Always return typed results

### `mutations/` - Write Operations

Write operations with validation. Take user input, validate it, execute in a transaction, and return a result.

```c
// mutations/posts/create.c
kern_mutation_result_t post_create(kern_req_t *req, post_input_t *in) {
    kern_validation_t *v = kern_validate_start(req);
    KERN_REQUIRE_STR(v, title, in->title, MIN 3, MAX 200);
    if (kern_validation_failed(v)) return kern_mutation_fail(v);

    kern_db_t *db = kern_db_get();
    kern_tx_t *tx = kern_tx_begin(db);
    int64_t id = KERN_INSERT(tx, post, .title = in->title, ...);
    kern_tx_commit(tx);
    return kern_mutation_ok(post_find_by_id(id));
}
```

**Rules:**
- Always validate input first
- Always use a transaction
- Return `kern_mutation_result_t` (success + value, or failure + errors)
- No reads beyond what is needed to validate (use `queries/` for that)

### `middleware/` - Cross-Cutting Concerns

Custom middleware that applies across multiple routes. Different from `_middleware.c` files in `pages/` (which are directory-scoped).

```c
// middleware/require_admin.c
kern_response_t *require_admin(kern_req_t *req) {
    kern_user_t *u = kern_current_user(req);
    if (!u || !u->is_admin) {
        return kern_redirect(req, "/login", 302);
    }
    return kern_pass();
}
```

### `auth/` - Authentication Flow

Login, logout, signup, OAuth callbacks. This is the code that creates and destroys sessions.

| What goes here | What does NOT go here |
|----------------|----------------------|
| Login flow | Authorization (use `policies/`) |
| Signup flow | User profile updates (use `mutations/`) |
| Logout | User queries (use `queries/`) |
| OAuth callbacks | |
| Password reset | |

### `jobs/` - Background Work

Functions that run asynchronously via the queue. Dispatched by mutations or the scheduler.

```c
// jobs/send_welcome_email.c
KERN_JOB("send_welcome_email", send_welcome_email);

static kern_result_t send_welcome_email(kern_job_t *job) {
    user_t *u = kern_job_arg(job, user_t *);
    kern_mail_t *m = welcome_email(u);
    kern_smtp_send(m);
    return KERN_OK;
}
```

### `mailers/` - Email Templates

`.ktxt` templates for email content, plus a `.c` function that constructs the email.

### `assets/` - Source Assets

Pre-build source files. Everything here gets fingerprinted (hashed) during build and emitted to `public/assets/`.

Bootstrap CSS and JS are served automatically by the runtime at `/assets/bootstrap.min.css` and `/assets/bootstrap.bundle.min.js` — no build step required. Only custom assets need to be placed here.

- `assets/css/` - Custom CSS files (loaded after Bootstrap)
- `assets/js/` - JavaScript files
- `assets/img/` - Images

### `db/migrations/` - Schema Migrations

Numbered SQL files, executed in order. Supports both flat format and directory format:

**Flat format:**
```
db/migrations/001_users.sql
db/migrations/002_posts.sql
```

**Directory format (with rollback):**
```
db/migrations/001_users/up.sql
db/migrations/001_users/down.sql
```

### `test/` - Tests

Three categories:

- `test/unit/` - pure function tests, no HTTP
- `test/integration/` - boot the app, make requests, assert
- `test/helpers/` - shared test utilities

### `config/` - Environment Overrides

C files that can override `kern.toml` settings programmatically per environment:

```c
// config/dev.c - only applied when env = "dev"
void kern_config_dev(kern_config_t *cfg) {
    kern_config_set_str(cfg, "logging.level", "debug");
    kern_config_set_bool(cfg, "assets.minify", false);
}
```

### `public/` - Generated (gitignored)

Output of the asset pipeline. Fingerprinted files served directly by the HTTP server. Do not edit manually.

### `build/` - Generated (gitignored)

Intermediate build artifacts: compiled templates, page registry, asset manifests. Do not edit manually.

### `dist/` - Final Output (gitignored)

The production binary and its public assets. Created by `kern build`.

## Naming Conventions

| Thing | Convention | Example |
|-------|-----------|---------|
| Files | `snake_case.c`, `snake_case.h` | `user_profile.c` |
| Types (struct) | `snake_case_t` | `post_t`, `user_input_t` |
| Functions | `module_action_thing()` | `post_create()`, `user_find_by_email()` |
| Constants | `UPPER_SNAKE_CASE` | `KERN_MAX_HEADER_SIZE` |
| Macros | `KERN_` prefix | `KERN_PAGE`, `KERN_MODEL` |
| Templates | `snake_case.khtml` | `post_show.khtml` |
| DB tables | `snake_case`, plural | `users`, `posts`, `comments` |
| DB columns | `snake_case` | `created_at`, `author_id` |
| URL paths | `kebab-case` | `/blog-posts/:id` |
| Config keys | `dot.separated` | `app.name`, `database.driver` |

## What Gets Gitignored

The `.gitignore` generated by `kern new` excludes:

```gitignore
# Build artifacts
build/
dist/
public/assets/

# Database
db/*.sqlite
db/*.sqlite-wal
db/*.sqlite-shm

# Environment
.env
.env.local

# OS files
.DS_Store
*.swp
*.swo
```
