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
- Fiber-based concurrency (stackful coroutines)
- Template rendering runtime
- Database drivers (SQLite, Postgres)
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
- `kern routes` - list registered routes

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

kern follows the libuv/Node model with a critical addition: fibers that make async code look synchronous.

```
                    ┌─────────────────────────────────┐
                    │     Main Reactor Thread          │
                    │                                  │
                    │  ┌─────────────────────────┐    │
                    │  │   libuv Event Loop       │    │
                    │  │   (epoll / io_uring)     │    │
                    │  └─────────────────────────┘    │
                    │                                  │
                    │  ┌─────────────────────────┐    │
                    │  │   Fiber Scheduler        │    │
                    │  │   (cooperative, per-req) │    │
                    │  └─────────────────────────┘    │
                    └────────────┬────────────────────┘
                                 │
                    ┌────────────┴────────────────────┐
                    │       Worker Thread Pool         │
                    │   (size = num_cpus - 1)          │
                    │                                  │
                    │   - File I/O                     │
                    │   - bcrypt / argon2              │
                    │   - Blocking DB queries          │
                    │   - Mail send                    │
                    └─────────────────────────────────┘
```

### Concurrency Primitives

1. **`kern_fiber_t`** - A stackful coroutine (1KB stack). Each HTTP request runs in its own fiber. Fibers are cooperatively scheduled on the reactor thread.

2. **`kern_async_t`** - A future wrapper. `kern_await(kern_async_t *)` yields the current fiber and resumes when the operation completes.

**Implementation:**
- `ucontext.h` where available (portable fallback)
- Custom asm trampoline for x86_64 and aarch64 (faster, ~50ns context switch)
- For Linux: optional `io_uring` reactor (faster than epoll for bursty workloads)

### How It Looks to the Developer

The developer writes straight-line C. No callbacks required:

```c
static kern_response_t *post_show(kern_req_t *req) {
    const char *id = kern_param(req, "id");
    // This call may block on the database. Internally, kern yields the fiber,
    // runs sqlite3_step in a worker thread, resumes when the row is ready.
    post_t *p = post_find_by_id(id);
    if (!p) return kern_404(req);
    return kern_render(req, "posts/show", KERN_T("post", p));
}
```

## Build Pipeline

The `kern dev` and `kern build` commands orchestrate a multi-step build:

```
kern dev / kern build
  │
  ├── scan pages/        → generate pages/_registry.c (route table)
  ├── scan views/        → compile *.khtml → build/views/*.c (Pug to C)
  ├── scan assets/       → hash → emit public/assets/<name>-<hash>.<ext>
  ├── run tailwind       → compile CSS (no Node, C-implemented scanner)
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
2. Asset hash + minify (lightningcss for CSS)
3. Compile with `-O2 -fvisibility=hidden -flto`
4. Strip debug info
5. Link statically against libkern
6. Output: `dist/myapp` (single binary, ~1.5 MB stripped) + `dist/public/`

## External Dependencies

| Dependency | Purpose | License |
|-----------|---------|---------|
| libuv | Event loop, async I/O | MIT |
| SQLite | Default database | Public domain |
| mbedTLS | TLS termination (deferred to v0.3+) | Apache 2.0 |
| cJSON | JSON parsing/serialization | MIT |
| lightningcss | CSS minification | MPL 2.0 |

## Performance Characteristics

| Metric | Value |
|--------|-------|
| Cold start | < 5 ms |
| Memory baseline | ~3 MB resident |
| Throughput (hello world) | ~50K req/s per core |
| Binary size (hello world) | ~1.5 MB stripped, ~800 KB with LTO |
| Context switch (fiber) | ~50 ns on x86_64 |
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
