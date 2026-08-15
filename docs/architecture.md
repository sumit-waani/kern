# Architecture

## Overview

kern consists of three deliverables that work together to provide a complete web development and deployment experience:

```
kern  =  libkern  (C library)     - the framework runtime
       + kern     (CLI tool)      - dev / build / scaffold / test
       + kernd    (daemon)        - VPS-wide app manager + reverse proxy
```

## The Three Deliverables

### 1. libkern - The Framework Runtime

A C11 shared library (~30 KLOC at v0.7) with a single public header `<kern.h>`. This is what every kern app links against.

**Responsibilities:**
- HTTP/1.1 server with keep-alive
- Radix tree router (lock-free reads)
- Request/response lifecycle
- Request handler dispatch
- Template rendering runtime
- Database driver (SQLite)
- Query builder
- Session management
- Authentication primitives
- Mailer, queue, scheduler
- Structured logging
- Configuration management

**Language and Standards:**
- C11 standard (`-std=c11 -pedantic -Wall -Wextra`)
- No GNU extensions required
- Must build with both `gcc` and `clang`
- POSIX 2017 baseline for syscalls, threads, sockets

### 2. kern CLI - The Developer Tool

A single static binary installed on the developer's machine. The CLI is the build system for user projects (no Makefiles or CMakeLists.txt in user code).

**Key Commands:**
- `kern new <name>` - scaffold a new project
- `kern dev` - watch mode with hot-reload
- `kern build` - production build
- `kern test` - run tests
- `kern db.migrate` - run database migrations
- `kern fmt` - format code

### 3. kernd - The Production Dashboard

A daemon that runs on a VPS as a long-lived process. It manages the full lifecycle of kern apps in production.

**Responsibilities:**
- HTTP reverse proxy (host-header routing)
- Git poller + build worker
- cgroup v2 resource management (per app)
- Log capture and rotation
- Stats collection
- Backup scheduler (S3/R2/rclone)
- Let's Encrypt ACME client
- Cloudflare DNS + CDN integration
- Web UI admin console

## Runtime Model

### Event Loop Architecture

kern follows a single-threaded event loop model powered by libuv. No fibers, no coroutines, no user-space scheduling — the developer writes plain C functions that return responses.

```
                    ┌─────────────────────────────────┐
                    │     Main Event Loop Thread       │
                    │                                  │
                    │  ┌─────────────────────────┐    │
                    │  │   libuv Event Loop       │    │
                    │  │   (epoll on Linux)       │    │
                    │  └─────────────────────────┘    │
                    │                                  │
                    │  Request dispatch to handlers    │
                    └────────────┬────────────────────┘
                                 │
                    ┌────────────┴────────────────────┐
                    │       Worker Thread Pool         │
                    │   (size = num_cpus - 1)          │
                    │                                  │
                    │   - File I/O                     │
                    │   - PBKDF2 password hashing      │
                    │   - Blocking DB queries          │
                    └─────────────────────────────────┘
```

**Why no fibers:** kern targets small-to-mid-size web apps running SQLite on a single VPS. The bottleneck is the database, not concurrency. Adding stackful coroutines solves a problem we don't have. If the target workload changes (PostgreSQL, 10K+ concurrent connections), this decision may be revisited.

### How It Looks to the Developer

The developer writes straight-line C. No callbacks required:

```c
static kern_response_t *post_show(kern_req_t *req) {
    const char *id = kern_param(req, "id");
    post_t *p = post_find_by_id(id);
    if (!p) return kern_404(req);
    return kern_render(req, "posts/show", vars);
}
```

The handler is a plain function. It queries the database, builds a response, returns it. The libuv event loop handles concurrency by dispatching requests to handlers.

## Build Pipeline

The `kern dev` and `kern build` commands orchestrate a multi-step build:

```
kern dev / kern build
  │
  ├── scan pages/        → generate pages/_registry.c (route table)
  ├── scan views/        → compile *.khtml → build/views/*.c (Pug to C)
  ├── scan assets/       → hash → emit public/assets/<name>-<hash>.<ext>
  ├── serve bootstrap    → embedded minified CSS + JS (gzip, immutable cache)
  ├── scan db/migrations → track migration state
  ├── invoke cc / clang  → link against libkern, sqlite3, libuv
  └── output: dist/myapp (single binary) + public/ (static assets)
```

### Development Mode (`kern dev`)

1. Initial scan: pages, views, models, assets, kern.toml
2. Compile everything and start the server
3. Watch for changes:
   - `pages/**` - regenerate registry, hot swap via `dlopen(3)`
   - `views/**` - recompile template, hot swap (no restart)
   - `models/**`, `queries/**`, `mutations/**` - full restart
   - `assets/**` - re-hash, re-emit, browser auto-refresh via SSE
   - `kern.toml` - full restart

### Production Build (`kern build`)

1. Full template compile
2. Asset hash + minify
3. Compile with `-O2 -fvisibility=hidden -flto`
4. Strip debug info
5. Link statically against libkern
6. Output: `dist/myapp` (single binary, ~1.5 MB stripped) + `dist/public/`

## External Dependencies

| Dependency | Purpose | License | Status |
|-----------|---------|---------|--------|
| libuv | Event loop, async I/O | MIT | Implemented |
| SQLite | Default (and only) database | Public domain | Implemented |
| mbedTLS | TLS termination | Apache 2.0 | Planned (v0.3+) |

## Performance Characteristics (design targets)

| Metric | Target |
|--------|--------|
| Cold start | < 5 ms |
| Memory baseline | ~3 MB resident |
| Throughput (hello world) | ~50K req/s per core |
| Binary size (hello world) | ~1.5 MB stripped, ~800 KB with LTO |
| Concurrent connections | ~10K per process |

## Deployment Architecture

```
┌────────────────── Developer Machine ──────────────────┐
│                                                        │
│   $ kern new myapp && cd myapp                        │
│   $ kern dev             # watches, builds, reloads   │
│   $ kern build           # produces dist/myapp        │
│   $ git push             # triggers deploy            │
│                                                        │
└────────────────────────────┬───────────────────────────┘
                             │ git push
                             ▼
┌────────────────── VPS (production) ───────────────────┐
│                                                        │
│   ┌──────────── kernd (the dashboard) ───────────┐    │
│   │  - HTTP reverse proxy (host-header routing)  │    │
│   │  - Git poller + build worker                 │    │
│   │  - cgroup v2 resource manager (per app)      │    │
│   │  - Log collector + stats scraper             │    │
│   │  - Backup scheduler                          │    │
│   │  - Let's Encrypt + Cloudflare integration    │    │
│   │  - Web UI (admin console)                    │    │
│   └──────────────────────────────────────────────┘    │
│                                                        │
│   ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐                │
│   │app-1│  │app-2│  │app-3│  │app-N│  (cgroups)      │
│   │:3001│  │:3002│  │:3003│  │:300N│                  │
│   └─────┘  └─────┘  └─────┘  └─────┘                │
│                                                        │
└────────────────────────────────────────────────────────┘
```
