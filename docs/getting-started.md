# Getting Started

This guide shows how to create, develop, and build a kern application using the CLI tools.

## Prerequisites

- A Linux or macOS machine (Windows is best-effort)
- GCC 11+ or Clang 15+ installed
- SQLite 3.40+ available (usually pre-installed on macOS/Linux)

## Installation

```bash
# macOS / Linux
$ curl -fsSL https://kern.dev/install.sh | bash

# Verify installation
$ kern --version
kern 0.1.0 (libkern 0.1.0, built with gcc 14.2)
```

The installer:
- Detects your OS and architecture
- Downloads the correct `kern` binary
- Verifies the SHA256 checksum
- Places it in `~/.local/bin/` (or `/usr/local/bin/` with sudo)
- Adds to PATH if needed

## Create a New Project

```bash
$ kern new myapp
Creating myapp...
  + app.c
  + kern.toml
  + pages/index.c
  + views/pages/home.khtml
  + views/layouts/base.khtml
  + models/user.c
  + models/user.h
  + db/migrations/001_users.sql
  + assets/css/app.css
  + assets/img/.gitkeep
  + .gitignore
  + README.md

$ cd myapp
```

## Project Structure

After `kern new`, your project looks like this:

```
myapp/
+-- kern.toml              # app configuration
+-- app.c                  # entry point
+-- pages/                 # file-system routes
|   +-- index.c            # GET /
+-- views/                 # .khtml templates
|   +-- layouts/
|   |   +-- base.khtml     # base layout
|   +-- pages/
|       +-- home.khtml     # home page template
+-- models/                # database models
|   +-- user.c
|   +-- user.h
+-- db/
|   +-- migrations/        # SQL migration files
|       +-- 001_users.sql
+-- assets/                # source assets
|   +-- css/
|       +-- app.css
+-- .gitignore
+-- README.md
```

## Configuration

Open `kern.toml` to see your app settings:

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
level    = "debug"
format   = "text"
output   = "stdout"
```

## Run Database Migrations

Before starting the dev server, set up your database:

```bash
$ kern db.migrate
[info] running migration 001_users... done (2ms)
[info] 1 migration applied
```

Other database commands:

```bash
$ kern db.migrate              # run pending migrations
```

## Start the Development Server

```bash
$ kern dev
[info] scanning pages/         -> 1 route registered
[info] compiling views/        -> 1 template compiled
[info] building assets         -> 1 file hashed
[info] linking                 -> dist/myapp
[info] starting on http://localhost:3000
```

Visit `http://localhost:3000` to see your app running.

The dev server watches for changes:
- **pages/** changes: hot-swaps routes (no restart)
- **views/** changes: recompiles template, hot-swaps (no restart)
- **models/**, **queries/**, **mutations/** changes: full rebuild + restart
- **assets/** changes: re-hash, browser auto-refreshes via SSE
- **kern.toml** changes: full restart

## Your First Page

Look at `pages/index.c`:

```c
#include <kern.h>

KERN_PAGE("/", home);

static kern_response_t *home(kern_req_t *req) {
    kern_dict_t *vars = kern_dict_new();
    kern_dict_set(vars, "name", "World");
    return kern_render(req, "pages/home", vars);
}
```

And its template at `views/pages/home.khtml`:

```pug
extend layouts/base

block body
  h1(class="text-3xl font-bold") Hello, #{name}!
  p Built with kern.
```

## Adding a New Page

Create `pages/about.c`:

```c
#include <kern.h>

KERN_PAGE("/about", about);

static kern_response_t *about(kern_req_t *req) {
    return kern_render(req, "pages/about", KERN_T("title", "About Us"));
}
```

Create `views/pages/about.khtml`:

```pug
extend layouts/base

block body
  h1 #{title}
  p This is a kern application.
```

The dev server automatically picks up the new route. Visit `http://localhost:3000/about`.

## Adding Dynamic Routes

Create `pages/posts/[id]/index.c` for a dynamic route:

```c
#include <kern.h>
#include "models/post.h"

KERN_GET("/posts/:id", post_show);

static kern_response_t *post_show(kern_req_t *req) {
    int64_t id = kern_param_int(req, "id");
    post_t *post = post_find_by_id(id);
    if (!post) return kern_404(req);
    return kern_render(req, "posts/show", KERN_T("post", post));
}
```

The `[id]` directory name maps to the `:id` path parameter.

## Working with the Database

### Create a Migration

Create `db/migrations/002_posts.sql`:

```sql
CREATE TABLE posts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  slug TEXT NOT NULL UNIQUE,
  body TEXT,
  author_id INTEGER NOT NULL REFERENCES users(id),
  published INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX idx_posts_slug ON posts(slug);
CREATE INDEX idx_posts_author ON posts(author_id);
```

Run it:

```bash
$ kern db.migrate
[info] running migration 002_posts... done (3ms)
```

### Query Data

Create `queries/posts/list_published.c`:

```c
#include <kern.h>
#include "models/post.h"

post_t **post_list_published(size_t limit, size_t offset) {
    kern_db_t *db = kern_db_get();
    kern_qb_t *q = kern_qb(db)
        ->select(post, *)
        ->from(post)
        ->where(published, "=", kern_arg_bool(true))
        ->order_by(created_at, DESC)
        ->limit(limit)
        ->offset(offset);
    return kern_qb_all(q, post_t);
}
```

## Building for Production

```bash
$ kern build
[info] scanning pages/         -> 3 routes registered
[info] compiling views/        -> 4 templates compiled
[info] building assets         -> 2 files hashed
[info] compiling (release)     -> -O2 -flto
[info] linking                 -> dist/myapp (1.2 MB)
[info] done in 2.1s

$ ls dist/
myapp       public/
```

The output is a single binary `dist/myapp` plus a `dist/public/` directory for static assets.

### Run the Production Binary

```bash
$ MYAPP_SECRET="your-secret-key-here" ./dist/myapp
[info] myapp v0.1.0 starting on :3000
```

## Common Commands Reference

```bash
kern new <name>              # scaffold a new project
kern dev                     # development server with hot-reload
kern build                   # production build
kern test                    # run tests
kern fmt                     # format C and .khtml files
kern db.migrate              # run pending migrations
kern --help                  # show all available commands
kern --version               # show version info
```

## Deploying

### Manual Deployment

Copy `dist/myapp` and `dist/public/` to your server:

```bash
$ scp -r dist/ user@server:/opt/myapp/
$ ssh user@server
$ cd /opt/myapp
$ MYAPP_SECRET="..." MYAPP_DB_PATH="./db/prod.sqlite" ./myapp
```

### Using kernd (Recommended)

If you have `kernd` installed on your VPS, deployment is automatic:

1. Push to your Git repository
2. kernd detects the push (via webhook or polling)
3. kernd runs `kern build`
4. kernd starts the new binary, stops the old one
5. Your app is live

See the [kernd documentation](https://kern.dev/docs/kernd) for setup instructions.

## Next Steps

- Read [Project Layout](project-layout.md) to understand the convention-based folder structure
- Read [KHTML Syntax](khtml-syntax.md) to learn the template language
- Read [API Reference](api-reference.md) for the complete kern.h API
- Read [Architecture](architecture.md) to understand how kern works internally
