# **kern** — A Monolithic, Batteries-Included, C-Powered Full-Stack Web Framework

> *One binary per app. One command to ship. One dashboard to run your fleet.*

**Status:** Design specification, v0.7 draft. Sections marked with *(implemented)* reflect what exists in code today. Unmarked sections are planned.
**Audience:** Systems-side developers who want the productivity of Rails/Lucky/Phoenix without leaving the C runtime.

---

## Table of Contents

1. [Why kern exists](#1-why-kern-exists)
2. [The inspiration matrix](#2-the-inspiration-matrix)
3. [Design principles](#3-design-principles)
4. [High-level architecture](#4-high-level-architecture)
5. [The Core Framework — `libkern`](#5-the-core-framework--libkern)
6. [Templating — the `.khtml` engine](#6-templating--the-khtml-engine)
7. [Project conventions & folder layout](#7-project-conventions--folder-layout)
8. [Routing — file-system first](#8-routing--file-system-first)
9. [Reactivity — shards (HTMX-style) + SSE](#9-reactivity--shards-htmx-style--sse)
10. [Database — SQLite by default, Postgres optional](#10-database--sqlite-by-default-postgres-optional)
11. [Auth, sessions, security](#11-auth-sessions-security)
12. [Mailer, queue, scheduler, jobs](#12-mailer-queue-scheduler-jobs)
13. [Asset pipeline](#13-asset-pipeline)
14. [Configuration — `kern.toml`](#14-configuration--kerntoml)
15. [The CLI — `kern` command](#15-the-cli--kern-command)
16. [The Dashboard — `kernd` (the production half)](#16-the-dashboard--kernd-the-production-half)
17. [The install experience](#17-the-install-experience)
18. [UI / component library](#18-ui--component-library)
19. [Testing](#19-testing)
20. [Observability](#20-observability)
21. [Performance & scaling](#21-performance--scaling)
22. [Security model](#22-security-model)
23. [Roadmap](#23-roadmap)
24. [Comparison to reference frameworks](#24-comparison-to-reference-frameworks)
25. [Open questions](#25-open-questions)
26. [Appendix A: code samples](#26-appendix-a-code-samples)
27. [Note on the name "kern"](#27-note-on-the-name-kern)

---

## 1. Why kern exists

If you're a systems-side dev shipping a small to mid-size web product today, you've been here:

- **JS land:** Next/Remix/Laravel. You spend two days picking the right stack, then six weeks wondering if your deps will update without breaking.
- **Go land:** Great runtime, but every project reinvents auth/mail/queue/ORM.
- **Rust land:** The language is great. The build is a tax. The "batteries-included" framework story is still being written (Topcoat, Leptos — both early).
- **C land:** Nothing that feels like a framework. Just `libuv` + a router you wrote last weekend.

You don't want to learn a new language. You want a single opinionated toolchain that gets out of your way, ships to a single static binary, and doesn't pretend `node_modules` is a feature.

**kern is that toolchain. Written in C, because C is the kernel; kern is the OS.**

It targets the same developer who reaches for Rails or Lucky, but who would rather have:
- one binary per app, no runtime, no container, no language server tax
- direct access to libc, syscalls, cgroups, epoll when needed
- a toolchain that doesn't change under their feet
- a dashboard for shipping to a VPS without learning Kubernetes, nginx, systemd unit files, and Let's Encrypt acme.sh

### Scope (v0.7)

| In | Out |
|---|---|
| Server-side rendered apps | SPAs / WASM / heavy client reactivity |
| HTTP/1.1 (HTTP/2 in v0.6) | HTTP/3 (QUIC), WebSocket (post-v0.7) |
| SQLite (PostgreSQL post-v0.7) | MongoDB, Redis-as-primary (Redis supported for cache/queue only) |
| Single-VPS deployment via dashboard | Multi-region / k8s / multi-host federation (out of scope) |
| Linux server (dashboard) | Windows server (macOS dev supported, Windows dev best-effort) |
| Tailwind v4, hand-written CSS | SASS/Less (Tailwind is the default; you can skip it) |
| Cron-style jobs, queue | Long-lived stream processors |

---

## 2. The inspiration matrix

I chose **what to copy** from each of the four references you mentioned.

| Pattern | Source | Why |
|---|---|---|
| SSR-only, async components, file-system routing, no client build step, shadcn-style copy-in components, asset hash pipeline, request memoization | **Topcoat** (Rust) | This is the *exact* DX target. No WASM, no API layer, no Node toolchain. C gets the same story with even less runtime cost. (Note: Topcoat uses WebSocket-based shards; kern uses HTMX-style fetch/swap + SSE — simpler, stateless, no persistent connection needed.) |
| **Compile-time Pug-style templates**, fiber-style "looks synchronous" API, JSON/HTML form auto-binding from data classes, OpenSSL-or-Botan TLS abstraction | **Vibe.d** (D) | C has no CTFE. We *emulate* it with a build-time template compiler (the `kern` CLI) that emits C. End result: zero runtime template cost, like Vibe's Diet. |
| **Strict folder conventions** (`pages/`, `queries/`, `mutations/`, `components/`, `models/`), "action must end with render/redirect", `lucky dev` watch mode, `lucky exec` REPL, opinionated scaffolding | **Lucky** (Crystal) | The convention-over-configuration muscle. C is weak on metaprogramming, so the *folder layout is the contract*. Lucky nailed this. |
| Single-`curl` install, file-system routing layout style, declarative components, static site generation path | **Ziex** (Zig) | Light-touch CLI, opinionated install, clean project bootstrap. |
| Compile-time type safety, "batteries included" mental model | **Lucky + Topcoat** | Both of these reject "compose your own stack" — kern does too. |

**What kern does *not* copy:**
- D's CTFE in source (we use a build-time tool — same end result, different mechanism)
- Crystal's type inference (C has none — kern is type-strict by virtue of being C)
- Rust's macro system (we use C macros + a code generator — the C preprocessor is enough for most of it)

---

## 3. Design principles

These are non-negotiable. If a feature conflicts with one, the feature loses.

1. **One binary per app.** No runtimes, no interpreters, no GC pause dances. What you ship is what runs.
2. **Conventions over configuration.** Folder structure *is* the contract. If you put a file in `pages/posts/[id].c`, it routes to `GET /posts/:id`. No router table to maintain.
3. **Server-rendered everything.** Components are functions that can hit the database. There is no client/server mental split. The browser gets HTML.
4. **Batteries included.** Auth, sessions, mailer, queue, scheduler, ORM-lite, asset pipeline, admin scaffold, test framework, dashboard. All in one `kern` CLI + one `libkern` runtime.
5. **C11, no compiler extensions.** Must build with stock `gcc` and `clang`. No `__attribute__((cleanup))` magic beyond what's portable. No inline asm.
6. **Linux-first, POSIX-portable.** macOS dev works. Windows is best-effort. The dashboard binary is Linux-only (it relies on cgroups v2).
7. **No magic strings.** Routes, asset URLs, model columns — all referenced via C identifiers, not strings. Strings only cross the boundary at the HTTP/TPL layer.
8. **The CLI is the build system.** No `Makefile`, no `CMakeLists.txt` in user projects. `kern dev`, `kern build`, `kern test` — that's it.
9. **Reactivity is opt-in.** Default = server-rendered, no client JS. Add `KERN_SHARDS` build flag or import the shards runtime, you get HTMX-style interactivity. You never need a JS toolchain.
10. **No containers, no Docker, no Compose.** The dashboard uses cgroups for isolation. Period. C is small enough that this is fine.

---

## 4. High-level architecture

Three deliverables:

```
kern  =  a C library (libkern)        — the framework runtime
kern  =  a CLI (kern)                  — dev / build / scaffold / test
kernd =  a daemon (kernd)              — VPS-wide app manager + reverse proxy
```

```
┌─────────────────────────────────────────────────────────────────────┐
│                       USER MACHINE (dev)                            │
│                                                                     │
│   $ kern new myapp && cd myapp                                     │
│   $ kern dev                  # watches, builds, hot-reloads       │
│   $ kern build                # produces dist/myapp + public/       │
│   $ git push                  # CI not even needed                 │
│                                                                     │
└───────────────────────────────────┬─────────────────────────────────┘
                                    │ git push (PAT)
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       VPS (production)                              │
│                                                                     │
│   ┌──────────── kernd (the dashboard) ──────────────┐               │
│   │                                                  │               │
│   │   • HTTP reverse proxy (host-header routing)    │               │
│   │   • Git poller + build worker                   │               │
│   │   • cgroup-v2 resource manager (per app)        │               │
│   │   • Log collector (stdout/stderr capture)        │               │
│   │   • Stats scraper (/proc, cgroup fs)            │               │
│   │   • Backup scheduler (S3 / R2 / rclone)         │               │
│   │   • Cloudflare DNS + CDN purge API              │               │
│   │   • Web UI (admin console)                      │               │
│   │                                                  │               │
│   └────┬────────┬────────┬────────┬──────────────────┘               │
│        │        │        │        │                                  │
│   ┌────▼──┐ ┌───▼───┐ ┌──▼───┐ ┌──▼────┐                           │
│   │app-1  │ │app-2  │ │app-3 │ │app-N  │   (one cgroup each)        │
│   │:3001  │ │:3002  │ │:3003 │ │:300N  │                            │
│   └───────┘ └───────┘ └──────┘ └───────┘                            │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Component responsibilities

| Component | Lives in | Job |
|---|---|---|
| `libkern` (shared C lib) | compiled into each app | HTTP server, router, templating runtime, DB, auth, mailer, queue, sessions, logging |
| `kern` CLI | user's dev machine | scaffold, dev server, build, test, fmt, REPL, asset hashing, Tailwind compile, template compile |
| `kernd` dashboard | VPS, runs as root | install target, app lifecycle, reverse proxy, cgroups, logs, stats, backups, Cloudflare |
| `kern-shards.js` (optional) | served as static asset | 10 KB runtime for HTMX-style fetch-and-swap interactivity |

### Build pipeline

```
kern dev / kern build
  │
  ├─► scan pages/        → generate pages/_registry.c
  ├─► scan views/        → compile *.khtml → build/views/*.c (Pug → C)
  ├─► scan assets/       → hash → emit public/assets/<name>-<hash>.<ext>
  ├─► run tailwind       → if tailwind flag set, compile CSS (no Node, uses lightningcss + a Rust-compiled tailwind-cli or our own minimal Tailwind parser)
  ├─► scan db/migrations → track
  ├─► invoke cc / clang  → link against libkern, sqlite3, mbedtls, cjson, libuv
  └─► output: dist/myapp (single binary) + public/ (static assets)
```

---

## 5. The Core Framework — `libkern`

A C11 shared library, ~30 KLOC at v0.7, single header `<kern.h>` for user code.

### 5.1 Language & standards

- **C11.** No GNU extensions required. Code must build with `gcc -std=c11 -pedantic -Wall -Wextra` and `clang -std=c11`.
- **POSIX 2017** baseline for syscalls, threads, sockets.
- No exceptions, no RTTI, no templates. (D/Rust/Crystal users will recognize the trade.)

### 5.2 Runtime model *(implemented)*

We follow a single-threaded event loop model powered by libuv:

- **One main event loop thread** per process. Runs the HTTP server, timers, and async I/O via libuv.
- **A worker thread pool** (size = `KERN_WORKERS`, default = num CPUs - 1). Used for blocking work: file I/O, password hashing (PBKDF2), DB queries.
- **Request handlers are plain C functions.** Each incoming HTTP request dispatches to a handler function. The handler does its work and returns a response. No callbacks, no async/await, no fiber magic.
- **SQLite is the default (and currently only) database.** WAL mode with a single-writer queue handles ~15-18K rps for typical workloads — more than enough for the small-to-mid-size apps kern targets. The local-first monolith philosophy means no connection pool negotiation, no network hop.

A user handler looks like this:

```c
/* pages/posts/[id].c */
#include <kern.h>

KERN_PAGE("/posts/:id", post_show);

static kern_response_t *post_show(kern_req_t *req) {
    const char *id = kern_param(req, "id");
    post_t *p = post_find_by_id(id);
    if (!p) return kern_404(req);

    return kern_render(req, "posts/show", KERN_T("post", p));
}
```

**Design note:** We deliberately chose *not* to implement fibers/coroutines or `io_uring`. For the target workload (small-to-mid-size web apps with SQLite), the added complexity of user-space scheduling doesn't justify the marginal gain. If you're building something that needs 50K+ concurrent connections or sub-millisecond latency, kern isn't the right tool.

### 5.3 Concurrency — why no fibers

Frameworks like Vibe.d and Topcoat use stackful coroutines so that blocking I/O "looks synchronous" to the developer. kern does not.

**Why:** kern targets small-to-mid-size web apps running SQLite on a single VPS. The bottleneck is the database, not the CPU. Adding fibers (1KB stacks, context switching, a user-space scheduler) solves a problem we don't have. The developer writes plain C functions that return responses — no callbacks, no futures, no `await`.

For blocking work (password hashing, file I/O), libuv's built-in thread pool handles dispatch. The main event loop never blocks.

If a future version of kern adds PostgreSQL or needs to handle 10K+ concurrent connections, fibers may be revisited. Until then, simplicity wins.

### 5.4 HTTP server *(implemented)*

- HTTP/1.1 + keep-alive. HTTP/2 planned for v0.6.
- TLS via mbedTLS — planned for v0.3+.
- Static file serving with ETag and `304 Not Modified`.
- Graceful shutdown — finish in-flight requests on `SIGTERM`.
- Per-IP and per-route rate limiting (token bucket, in-memory) — planned for v0.3.

### 5.5 Routing *(implemented)*

File-system routing, Topcoat-style. See [section 8](#8-routing--file-system-first) for full details.

### 5.6 Request / response *(implemented)*

```c
typedef struct kern_req_s {
    kern_method_t   method;
    kern_str_t      path;
    kern_str_t      host;
    kern_dict_t    *params;       // path params
    kern_dict_t    *query;        // query string
    kern_dict_t    *headers;
    kern_dict_t    *cookies;
    kern_session_t *session;      // lazy
    kern_user_t    *current_user; // lazy
    void           *ud;           // user data
} kern_req_t;

typedef struct kern_res_s {
    int             status;
    kern_dict_t    *headers;
    kern_body_t    *body;         // string or file
} kern_res_t;
```

### 5.7 Built-in handlers *(implemented)*

- `kern_404(req)`, `kern_500(req, err)`, `kern_redirect(req, "/path", 302)`
- `kern_render(req, view_name, vars...)` — render a `.khtml` template
- `kern_json(req, obj)` — serialize a `cJSON` tree with proper content-type
- `kern_send_file(req, "path")` — static file with caching headers

---

## 6. Templating — the `.khtml` engine

This is the **most important DX decision** in the whole framework. We borrow Vibe.d's Diet template philosophy: a Pug-style indentation-based syntax, compiled to C at build time, zero runtime parsing.

### 6.1 File extension

`.khtml` for HTML templates, `.ktxt` for plain-text/mail templates, `.kfrag` for partials.

### 6.2 Syntax (Pug-derived, with D-style interpolation)

```pug
doctype html
html(lang="en")
  head
    title #{page_title} — MyApp
    link(rel="stylesheet" href={asset("css/app.css")})
  body(class="bg-zinc-50 text-zinc-900")
    include partials/nav.kfrag
    main(class="container mx-auto p-8")
      h1(class="text-3xl font-bold") #{page_title}

      - if (post_count == 0)
        p(class="text-zinc-500") No posts yet.
      - else
        ul
          - for (i = 0; i < posts_len; i++)
            li
              a(href={ url("posts.show", posts[i].id) })
                | #{posts[i].title}

    include partials/footer.kfrag
```

- Indentation-based, no closing tags.
- `#{expr}` interpolates a C expression (string, number, anything that `kern_html_write_*` accepts).
- `-` prefix = full statement(s) (loops, if, function defs).
- `| foo` = literal text.
- `{expr}` for attribute values that are dynamic.
- `include <path>` for partials.
- `extend <layout>` for layout inheritance.
- `block name ... endblock` for fillable regions.

### 6.3 Compile pipeline

```
views/posts/show.khtml
   │
   │ kern tpl compile   (during `kern build`)
   ▼
build/views/posts/show.c   ←  generated C source
   │
   │ cc -c
   ▼
build/views/posts/show.o   ←  compiled
```

Generated C looks like:

```c
/* AUTO-GENERATED. DO NOT EDIT. */
#include <kern.h>
#include "views/posts/show.khtml.h"

kern_response_t *kern_render_posts_show(kern_req_t *req, kern_dict_t *vars) {
    kern_buf_t *buf = kern_buf_new(4096);
    const char *page_title = kern_dict_get(vars, "page_title");
    kern_post_t **posts = (kern_post_t **) kern_dict_get(vars, "posts");
    size_t posts_len = kern_dict_len(vars, "posts");

    kern_html_doctype(buf, "html");
    kern_html_tag_open(buf, "html", .attrs = { .lang = "en" });
    kern_html_tag_open(buf, "head");
    kern_html_tag_open(buf, "title");
    kern_html_writef(buf, "%s — MyApp", page_title);
    kern_html_tag_close(buf, "title");
    kern_html_tag_self(buf, "link", .rel = "stylesheet", .href = kern_asset_url(req, "/assets/css/app-3a4b.css"));
    kern_html_tag_close(buf, "head");
    kern_html_tag_open(buf, "body", .class_ = "bg-zinc-50 text-zinc-900");
    kern_render_partial(req, buf, "partials/nav");
    kern_html_tag_open(buf, "main", .class_ = "container mx-auto p-8");
    kern_html_tag_open(buf, "h1", .class_ = "text-3xl font-bold");
    kern_html_write_esc(buf, page_title);
    kern_html_tag_close(buf, "h1");

    if (post_count == 0) {
        kern_html_tag_open(buf, "p", .class_ = "text-zinc-500");
        kern_html_write(buf, "No posts yet.");
        kern_html_tag_close(buf, "p");
    } else {
        kern_html_tag_open(buf, "ul");
        for (size_t i = 0; i < posts_len; i++) {
            kern_html_tag_open(buf, "li");
            kern_html_tag_open(buf, "a", .href = kern_url("posts.show", posts[i]->id));
            kern_html_write_esc(buf, posts[i]->title);
            kern_html_tag_close(buf, "a");
            kern_html_tag_close(buf, "li");
        }
        kern_html_tag_close(buf, "ul");
    }
    kern_html_tag_close(buf, "main");
    kern_render_partial(req, buf, "partials/footer");
    kern_html_tag_close(buf, "body");
    kern_html_tag_close(buf, "html");

    kern_response_t *res = kern_response_from_buf(buf, "text/html; charset=utf-8");
    return res;
}
```

**Key wins:**
- No runtime parser. The compiler does it once.
- Type errors in expressions surface at *compile time* (we compile the template against the app's headers, so `posts[i]->title` is type-checked).
- XSS escaping is automatic — `#{expr}` runs through `kern_html_write_esc`. The unsafe variant is `!{expr}` (raw HTML).
- All template paths resolve at compile time. A missing `include` is a build error.

### 6.4 Layout inheritance

```pug
/* views/layouts/base.khtml */
doctype html
html(lang="en")
  head
    block head
      title Default title
  body
    block body
```

```pug
/* views/pages/home.khtml */
extend layouts/base

block head
  title Home — MyApp

block body
  h1 Welcome
```

### 6.5 What we don't do (and why)

- **No runtime conditionals that need a sandbox.** All logic is C. This is the trade for compile-time templates.
- **No `eval`.** Templates are not a scripting language.
- **No `{% raw %}` and friends.** Pug's syntax is small enough that this isn't needed.

---

## 7. Project conventions & folder layout

```
myapp/
├── kern.toml                  # app config (port, db url, secret, env vars schema)
├── app.c                      # entry point — required
├── pages/                     # file-system routing
│   ├── index.c                # → GET /
│   ├── about.c                # → GET /about
│   ├── _layout.c              # layout wrapper (no URL)
│   ├── _middleware.c          # middleware for this branch
│   ├── posts/
│   │   ├── index.c            # → GET /posts
│   │   ├── new.c              # → GET /posts/new
│   │   ├── create.c           # → POST /posts
│   │   └── [id]/
│   │       ├── index.c        # → GET /posts/:id
│   │       ├── edit.c         # → GET /posts/:id/edit
│   │       ├── update.c       # → PATCH /posts/:id
│   │       └── destroy.c      # → DELETE /posts/:id
│   └── api/
│       └── health.c           # → GET /api/health
├── views/                     # .khtml templates
│   ├── layouts/
│   │   ├── base.khtml
│   │   └── admin.khtml
│   ├── pages/
│   │   ├── home.khtml
│   │   └── posts/
│   │       ├── index.khtml
│   │       ├── new.khtml
│   │       └── show.khtml
│   ├── partials/
│   │   ├── nav.kfrag
│   │   ├── footer.kfrag
│   │   └── flash.kfrag
│   └── components/
│       └── ui/
│           ├── button.khtml
│           ├── card.khtml
│           └── modal.khtml
├── models/                    # DB-backed structs
│   ├── user.c
│   ├── user.h
│   ├── post.c
│   └── post.h
├── queries/                   # read-side named queries
│   ├── users/
│   │   ├── find_by_email.c
│   │   └── list_recent.c
│   └── posts/
│       ├── find_by_id.c
│       └── list_published.c
├── mutations/                 # write-side, validated
│   ├── users/
│   │   ├── register.c
│   │   └── update_profile.c
│   └── posts/
│       └── create.c
├── middleware/                # custom middleware
│   └── require_admin.c
├── auth/                      # auth flow
│   ├── login.c
│   ├── logout.c
│   └── callbacks.c
├── jobs/                      # background work
│   ├── send_welcome_email.c
│   └── cleanup_expired.c
├── mailers/                   # email templates
│   ├── welcome.ktxt
│   └── password_reset.ktxt
├── assets/                    # source assets (pre-hash)
│   ├── css/
│   │   ├── app.css            # tailwind entry
│   │   └── admin.css
│   ├── js/
│   │   ├── app.js
│   │   └── shards.js          # optional shard runtime
│   └── img/
├── db/
│   └── migrations/            # numbered SQL
│       ├── 001_users.sql
│       ├── 002_posts.sql
│       └── 003_add_post_slug.sql
├── test/                      # test files
│   ├── unit/
│   │   └── post_model_test.c
│   ├── integration/
│   │   └── auth_flow_test.c
│   └── helpers/
│       └── http_client.c
├── config/                    # environment-specific config
│   ├── dev.c
│   ├── prod.c
│   └── test.c
├── public/                    # built static assets (generated)
│   ├── assets/
│   │   ├── app-a1b2c3.css
│   │   └── shards-d4e5f6.js
│   └── favicon.ico
├── build/                     # generated C code (gitignored)
│   └── ...
└── dist/                      # final binary (gitignored)
    └── myapp
```

### Folder contract

| Folder | What goes here | What doesn't |
|---|---|---|
| `pages/` | One `.c` file per URL segment. Defines a `KERN_PAGE(...)` symbol. | Business logic. Use `queries/`, `mutations/`, `models/`. |
| `views/` | Templates only. | C code. |
| `models/` | DB-backed structs and their lifecycle (create, update, delete, validate). | Business rules across multiple models (use `services/` if needed). |
| `queries/` | Pure read operations. Take a `kern_db_t *tx`, return a struct or list. | Side effects. |
| `mutations/` | Pure write operations. Take user input, validate, run in a transaction, return a result struct. | Reads (use `queries/`). |
| `middleware/` | Cross-cutting concerns. | Request handlers. |
| `auth/` | Login, logout, register, OAuth callbacks. | Authorization policies (use `policies/`). |
| `jobs/` | Background work dispatched via the queue. | Synchronous request work. |
| `mailers/` | `.ktxt` templates + a `.c` send function. | Anything that doesn't send email. |
| `assets/` | Pre-build source. | Anything that shouldn't be hashed and fingerprinted. |
| `public/` | Generated. Don't edit. | — |

### Naming conventions

- Files: `snake_case.c`, `snake_case.h`
- Types: `snake_case_t` (typedef'd struct, e.g. `kern_buf_t`, `post_t`)
- Functions: `module_action_thing(ctx, ...)` (e.g. `post_create(req, input)`)
- Constants: `UPPER_SNAKE_CASE` or `KERN_PREFIXED_UPPER`
- Templates: `kebab-case-or-underscore.khtml`
- DB tables: `snake_case`, plural (`users`, `posts`)
- DB columns: `snake_case`

### `kern new` scaffolding

```bash
$ kern new myapp
Creating myapp...
  ✓ app.c
  ✓ kern.toml
  ✓ pages/index.c
  ✓ views/pages/home.khtml
  ✓ views/layouts/base.khtml
  ✓ models/user.{c,h}
  ✓ models/post.{c,h}
  ✓ db/migrations/001_users.sql
  ✓ db/migrations/002_posts.sql
  ✓ assets/css/app.css
  ✓ assets/img/.gitkeep
  ✓ .gitignore
  ✓ README.md

$ cd myapp
$ kern db.migrate
$ kern dev
Listening on http://localhost:3000
```

---

## 8. Routing — file-system first

### 8.1 Conventions

| Path | HTTP method | File |
|---|---|---|
| `/` | GET | `pages/index.c` |
| `/about` | GET | `pages/about.c` |
| `/posts` | GET | `pages/posts/index.c` |
| `/posts/new` | GET | `pages/posts/new.c` |
| `/posts/:id` | GET | `pages/posts/[id]/index.c` |
| `/posts/:id/edit` | GET | `pages/posts/[id]/edit.c` |
| `/api/health` | GET | `pages/api/health.c` |
| `/users/:user_id/posts/:post_id` | GET | `pages/users/[user_id]/posts/[post_id].c` |

### 8.2 The `[id]` syntax

`[` and `]` delimit path parameters. The captured value is available via `kern_param(req, "id")` and is type-coerced based on the column type if the model is known.

### 8.3 Method dispatch

By default, `pages/posts/index.c` exposes `GET /posts`. For other methods:

```c
/* pages/posts/index.c */
KERN_GET    ("/posts",    list_posts);
KERN_POST   ("/posts",    create_post);
KERN_PATCH  ("/posts/:id", update_post);
KERN_DELETE ("/posts/:id", delete_post);
```

The handler file may declare multiple handlers, but the path is the file's path. If you need a wholly different path, use a `KERN_PAGE` macro with an explicit URL.

### 8.4 Layouts and middleware by folder

`pages/_layout.c` wraps every page in the same directory (and recursively):

```c
/* pages/admin/_layout.c */
KERN_LAYOUT("admin") {
    kern_session_t *s = kern_session_current(req);
    if (!s || !s->user || !s->user->is_admin) {
        return kern_redirect(req, "/login");
    }
    kern_layout_with("layouts/admin", ctx);
    return kern_pass();
}
```

`pages/_middleware.c` runs before every page in the directory:

```c
/* pages/api/_middleware.c */
KERN_MIDDLEWARE("api") {
    kern_header_set(res, "X-Content-Type-Options", "nosniff");
    kern_header_set(res, "Cache-Control", "no-store");
    return kern_pass();
}
```

### 8.5 The generated registry

The kern CLI scans `pages/`, reads every `KERN_*` macro, and emits `build/_pages_registry.c`:

```c
/* AUTO-GENERATED. DO NOT EDIT. */
#include <kern.h>
extern kern_response_t *kern_route_GET_index(kern_req_t *);
extern kern_response_t *kern_route_GET_about(kern_req_t *);
extern kern_response_t *kern_route_GET_posts_index(kern_req_t *);
extern kern_response_t *kern_route_GET_posts_id_index(kern_req_t *);
extern kern_response_t *kern_route_GET_api_health(kern_req_t *);

void kern_register_all_routes(kern_router_t *r) {
    kern_router_add(r, "GET",  "/",              kern_route_GET_index);
    kern_router_add(r, "GET",  "/about",         kern_route_GET_about);
    kern_router_add(r, "GET",  "/posts",         kern_route_GET_posts_index);
    kern_router_add(r, "GET",  "/posts/:id",     kern_route_GET_posts_id_index);
    kern_router_add(r, "GET",  "/api/health",    kern_route_GET_api_health);
    /* ... middleware + layouts in dependency order ... */
}
```

### 8.6 Router internals

- **Radix tree** (like `httprouter` / `actix`), but lock-free reads. Writes happen at startup.
- Type coercion at param lookup: `kern_param_int(req, "id")`, `kern_param_uuid(req, "id")`. Wrong format = 400.
- 405 returned for known path with wrong method. 404 for unknown.

---

## 9. Reactivity — shards (HTMX-style) + SSE

No WebAssembly, no client build step, no Node. Two complementary patterns that cover all reactivity needs for small-to-mid web apps.

### 9.1 The two patterns

kern reactivity has two independent layers:

**Shards (HTMX-style fetch/swap)** — for user-initiated actions:
```
User clicks button → browser fetches URL → server returns HTML fragment → browser swaps it into DOM
```
Stateless HTTP. No persistent connection. Just a regular request that returns HTML instead of a full page.

**SSE (Server-Sent Events)** — for server-pushed updates:
```
Server has new data → pushes HTML fragment over SSE → browser swaps it into DOM
```
One-way persistent connection. Server pushes, browser receives. User actions still go through normal HTTP (shards or regular forms).

These two cover every common interactive pattern:

| Use case | Pattern |
|---|---|
| Like button, form submit, delete | Shard (fetch/swap) |
| Live dashboard metrics | SSE stream |
| Notification toast | SSE event |
| Chat (receive messages) | SSE event |
| Chat (send message) | Normal POST → server broadcasts via SSE |
| Infinite scroll | Shard with `data-shard-trigger="revealed"` |
| Search-as-you-type | Shard with `data-shard-trigger="input"` |

**What kern does NOT do:** WebSocket-based bidirectional reactivity (Phoenix LiveView, Topcoat shards). WebSocket requires persistent two-way state, custom protocol framing, and significant server complexity. It's out of scope for v0.7. See the post-v0.7 roadmap if you genuinely need it (collaborative editing, gaming).

### 9.2 Server side: a shard handler

```c
/* pages/posts/[id].c */
KERN_SHARD("/posts/:id/comments", post_comments_shard);

static kern_response_t *post_comments_shard(kern_req_t *req) {
    const char *post_id = kern_param(req, "id");
    comment_t **list = comment_list_by_post(post_id);
    kern_dict_t *vars = kern_dict_new();
    kern_dict_set(vars, "comments", list);
    kern_dict_set(vars, "comments_len", (size_t) kern_list_len((void **) list));
    return kern_render(req, "posts/_comments", vars);
}
```

A shard returns a **fragment** (no `<html>`, no layout). The runtime JS swaps it in. This is exactly how HTMX works — the server returns a chunk of HTML, the client puts it where it belongs.

### 9.3 Client side: 10 KB of `kern-shards.js`

- Zero dependencies. Vanilla ES2020.
- Loaded once: `<script src="/assets/shards.js" defer></script>` in the base layout.
- Attributes (HTMX-style):
  - `data-shard="<url>"` on any element. When an event fires, fetch the URL, replace the target.
  - `data-shard-target="<selector>"` — where to swap the response.
  - `data-shard-swap="outer|inner|before|after|append|prepend"` — swap mode.
  - `data-shard-trigger="click|submit|input|load|revealed"` — event to listen for.
  - `data-shard-confirm="Delete?"` — confirm prompt before firing.
  - `data-shard-indicator="<selector>"` — loading indicator.
  - `data-shard-headers='{"X-Custom":"value"}'` — extra request headers.

### 9.4 SSE: server-pushed updates

For live data (chat, dashboards, notifications), SSE provides one-way server push:

```c
KERN_SSE("/dashboard/metrics", metrics_stream);

static kern_response_t *metrics_stream(kern_req_t *req) {
    kern_sse_t *sse = kern_sse_open(req);
    while (kern_sse_alive(sse)) {
        kern_sse_send(sse, "metrics", render_metrics());
        kern_sse_flush(sse);
        kern_sleep_ms(1000);
    }
    return kern_sse_close(sse);
}
```

Client-side, `kern-shards.js` supports SSE streams with a `data-shard-stream` attribute:

```html
<div id="metrics"
     data-shard-stream="/dashboard/metrics"
     data-shard-event="metrics"
     data-shard-swap="inner">
  <!-- server pushes HTML fragments here -->
</div>
```

The client opens an `EventSource`, listens for events, and swaps the received HTML into the target element. No custom JS needed.

### 9.5 When you really need vanilla JS

You can put a `.js` file in `assets/js/`, the build pipeline fingerprints it, and you reference it with `<script src={asset("js/app.js")} defer></script>` in your template. No build step. Just a static file. If you want ESM modules, use `<script type="module" src="...">` and the runtime handles imports. If you want to bundle — that's *your* problem, kern is hands-off here.

---

## 10. Database — SQLite by default, Postgres optional

### 10.1 Why SQLite first

- Single file. No daemon. No connection string. Perfect for small/medium apps and the dashboard's built-in apps.
- WAL mode gives concurrent reads + serialized writes — plenty fast for the target audience.
- Trivial to back up: `cp db.sqlite db.sqlite.bak`.
- Postgres becomes necessary above ~100 writes/sec or if you want multi-region. Until then, SQLite.

### 10.2 The data layer

Three pieces, in this order of preference:

1. **Raw SQL in queries/mutations.** Full power, no abstraction.
2. **Query builder** for type-checked SQL composition.
3. **Model structs** with auto-generated insert/update/delete.

A model:

```c
/* models/post.h */
#ifndef KERN_POST_H
#define KERN_POST_H

#include <kern.h>
#include <kern/db.h>

KERN_MODEL(post, KERN_COL_INT(id, PK, AUTOINCREMENT),
                    KERN_COL_STR(title, NOT_NULL, MAX 200),
                    KERN_COL_STR(slug, UNIQUE, NOT_NULL),
                    KERN_COL_TEXT(body),
                    KERN_COL_INT(author_id, FK(users.id), NOT_NULL),
                    KERN_COL_BOOL(published, DEFAULT false),
                    KERN_COL_TIMESTAMP(created_at, DEFAULT NOW),
                    KERN_COL_TIMESTAMP(updated_at, ON_UPDATE NOW));

extern kern_response_t *post_create(kern_req_t *req, post_input_t *in);
extern post_t          *post_find_by_id(int64_t id);
extern post_t         **post_list_published(size_t limit, size_t offset);
extern bool             post_update(int64_t id, post_input_t *in);
extern bool             post_delete(int64_t id);

#endif
```

A query:

```c
/* queries/posts/find_by_id.c */
#include "queries/posts/find_by_id.h"

post_t *post_find_by_id(int64_t id) {
    kern_db_t *db = kern_db_get();
    kern_qb_t *q = kern_qb(db)
        ->select(post, *)
        ->from(post)
        ->where(id, "=", kern_arg_int(id))
        ->where(published, "=", kern_arg_bool(true))
        ->limit(1);
    return kern_qb_one(q, post_t);
}
```

A mutation:

```c
/* mutations/posts/create.c */
#include "mutations/posts/create.h"

kern_mutation_result_t post_create(kern_req_t *req, post_input_t *in) {
    kern_validation_t *v = kern_validate_start(req);
    KERN_REQUIRE_STR(v, title,  in->title,  MIN 3,  MAX 200);
    KERN_REQUIRE_STR(v, slug,   in->slug,   MIN 3,  MAX 200);
    KERN_REQUIRE_STR(v, body,   in->body,   MIN 1,  MAX 50000);
    KERN_REQUIRE_REF(v, author, in->author_id, user_find_by_id);
    if (kern_validation_failed(v)) return kern_mutation_fail(v);

    kern_db_t *db = kern_db_get();
    kern_tx_t  *tx = kern_tx_begin(db);
    int64_t id = KERN_INSERT(tx, post, .title = in->title,
                                          .slug = in->slug,
                                          .body = in->body,
                                          .author_id = in->author_id,
                                          .published = false);
    kern_tx_commit(tx);
    return kern_mutation_ok(post_find_by_id(id));
}
```

### 10.3 Migrations

Pure SQL files, numbered, with `up.sql` and `down.sql`:

```
db/migrations/
├── 001_create_users/
│   ├── up.sql
│   └── down.sql
└── 002_create_posts/
    ├── up.sql
    └── down.sql
```

```sql
-- 001_create_users/up.sql
CREATE TABLE users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  email TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  is_admin INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX idx_users_email ON users(email);
```

```bash
$ kern db.migrate              # run all pending
$ kern db.rollback             # roll back last
$ kern db.migrate --to=003     # migrate to a specific version
$ kern db.status               # list migrations + their status
$ kern db.dump                 # sqlite3 .dump → out.sql
$ kern db.shell                # open sqlite3 shell with app's DB
```

The migration tool is a built-in CLI subcommand, not an external binary. It tracks state in `kern_migrations` table.

### 10.4 Connection pooling

In-process pool. SQLite is single-writer — we use a "writer mutex" with a queue, and unlimited read connections (WAL mode allows it). For Postgres, real connection pool (configurable size).

### 10.5 Postgres support *(future — post-v0.7)*

PostgreSQL support is planned for a future release. kern's local-first monolith philosophy means SQLite is the right default for the target workload (small-to-mid-size web apps on a single VPS). SQLite with WAL mode, a single-writer queue, and batch flush handles ~15-18K rps — more than enough.

When Postgres support lands, it will be a drop-in driver swap via `kern.toml`. The query builder and model macros are designed to be driver-agnostic.

---

## 11. Auth, sessions, security

### 11.1 Sessions

- Cookie-based by default. HTTP-only, SameSite=Lax, Secure (in prod).
- Session ID is 256 bits of randomness (`getrandom(2)`). Signed with HMAC-SHA256 against `app.secret`.
- Storage: in-memory + optional Redis. Default in-memory with periodic snapshot to disk.
- `Kern.session.start(req)`, `Kern.session.set(req, "user_id", u.id)`, `Kern.session.get(req, "user_id")`, `Kern.session.destroy(req)`.

### 11.2 Password hashing *(implemented — PBKDF2)*

- **PBKDF2-HMAC-SHA256** with 100,000 iterations, 16-byte salt, 32-byte derived key. Zero external crypto dependencies.
- Output format: `pbkdf2-sha256:100000:hex_salt:hex_hash`
- Backward-compatible verify detects old (pre-v0.2) SHA-256 hashes for migration.
- `kern_password_hash(plain)` and `kern_password_verify(plain, hash)`.

### 11.3 The built-in auth scaffold

`kern new` includes a working email + password auth with:

- Register (`/signup`)
- Login (`/login`) with rate limit (5 attempts / 15 min / IP)
- Logout (`/logout`)
- Password reset via email token
- Email verification
- "Remember me" optional

The `auth/` folder is yours to extend or replace.

### 11.4 CSRF

- Double-submit cookie pattern.
- Per-session token, validated on every state-changing request (POST/PATCH/DELETE).
- Forms get `kern_csrf_field()` to render the hidden input. AJAX sends `X-CSRF-Token` header.
- The base layout's `<form>` helper auto-injects.

### 11.5 Authorization

A tiny RBAC layer:

```c
/* policies/posts/update.c */
KERN_POLICY(post_update) {
    kern_user_t *u = kern_current_user(req);
    if (!u) return kern_deny("login_required");
    if (u->id == post->author_id || u->is_admin) return kern_allow();
    return kern_deny("forbidden");
}
```

Used in pages:

```c
KERN_PAGE("/posts/:id/edit", post_edit);

static kern_response_t *post_edit(kern_req_t *req) {
    post_t *p = post_find_by_id(kern_param_int(req, "id"));
    KERN_AUTHORIZE(req, post_update, p);
    return kern_render(req, "posts/edit", KERN_T("post", p));
}
```

---

## 12. Mailer, queue, scheduler, jobs

### 12.1 Mailer

```c
/* mailers/welcome.c */
#include "mailers/welcome.h"

kern_mail_t *welcome_email(user_t *u) {
    return kern_mail_new()
        ->to(u->email)
        ->from("hello@myapp.com")
        ->subject("Welcome to MyApp!")
        ->template("welcome", KERN_T("user", u));
}
```

Templates in `mailers/welcome.ktxt` use the same Pug-style syntax as HTML views, but produce plain text (no tags, just text nodes).

SMTP send uses an embedded minimal SMTP client (or queues the mail to a worker — see 12.2). We do not ship a "transactional email service" integration; bring your own SMTP or call SendGrid/Resend via HTTP.

### 12.2 Queue

```c
KERN_QUEUE_HANDLER("send_email", send_email_handler);

static kern_queue_result_t send_email_handler(kern_job_t *job) {
    kern_mail_t *m = kern_job_arg(job, kern_mail_t *);
    kern_smtp_send(m);
    return KERN_OK;
}
```

Dispatch:

```c
kern_queue_dispatch("send_email", kern_arg_mail(mail));
```

The queue runs in-process by default (worker threads in the same binary). For multi-process or multi-host, swap to Redis (we ship a Redis driver).

**Backoff:** retry with exponential backoff, max N attempts (configurable). Dead-letter queue writes to a `kern_failed_jobs` table.

### 12.3 Scheduler (cron-style)

`kern.toml` has a `[scheduler]` section:

```toml
[scheduler]
[[scheduler.tasks]]
cron = "0 3 * * *"               # every day at 3am
job  = "cleanup_expired"

[[scheduler.tasks]]
cron = "*/15 * * * *"
job  = "warm_cache"
```

Or programmatically in `app.c`:

```c
KERN_SCHEDULE("cleanup_expired", "0 3 * * *");
```

A scheduler thread ticks once a minute, computes next-fire times, dispatches via the queue.

### 12.4 Jobs (the actual work)

A job is just a queue handler in `jobs/`:

```c
/* jobs/cleanup_expired.c */
KERN_JOB("cleanup_expired", cleanup_expired_job);

static kern_result_t cleanup_expired_job(kern_job_t *job) {
    kern_db_t *db = kern_db_get();
    int64_t n = kern_db_exec(db, "DELETE FROM sessions WHERE last_seen_at < strftime('%s','now') - 86400 * 30");
    kern_log_info("cleaned %lld expired sessions", (long long) n);
    return KERN_OK;
}
```

---

## 13. Asset pipeline

### 13.1 What it does

- Fingerprints: `app.css` → `app-a1b2c3.css`
- Hashed names live forever in `public/assets/`. Old ones are pruned weekly.
- Sets `Cache-Control: public, max-age=31536000, immutable` for hashed files.
- Sets `Cache-Control: no-cache` for `index.html` (or the shell).
- Minifies CSS (via lightningcss — pure C, no Node).
- Minifies JS (via our own simple minifier, or esbuild sidecar if you install it).
- Compiles Tailwind (see 13.3).
- Copies static files from `public/` (favicon, robots.txt, etc.) untouched.

### 13.2 In templates

```pug
link(rel="stylesheet" href={asset("css/app.css")})
script(src={asset("js/app.js")} defer)
img(src={asset("img/logo.png")} alt="Logo")
```

`asset()` is a runtime helper that consults a generated manifest at startup. The manifest is `build/asset_manifest.h` with `#define KERN_ASSET_CSS_APP "/assets/app-a1b2c3.css"`.

### 13.3 Tailwind

- Default. No Node required.
- `kern` ships a **C-implemented Tailwind v4 compiler**. Tailwind's actual compiler is open source (originally in JS) — we reimplement the subset we need in C. ~3-5 KLOC.
- If you don't want Tailwind, ignore it. The framework doesn't care.
- Class scanning happens at build time, across `.khtml`, `.ktxt`, and `.c` files.
- JIT mode is the only mode.

### 13.4 Other CSS

Plain CSS works out of the box. SCSS/Less are not supported by default (you can add a build step manually — but the philosophy is "no toolchain sprawl").

---

## 14. Configuration — `kern.toml`

```toml
# kern.toml
[app]
name     = "myapp"
version  = "0.1.0"
port     = 3000
secret   = "${env:MYAPP_SECRET}"        # required in prod
env      = "dev"                        # dev | prod | test
timezone = "UTC"

[database]
driver   = "sqlite"                     # sqlite (postgres support planned post-v0.7)
path     = "./db/dev.sqlite"            # for sqlite
# url      = "postgres://..."           # for postgres (future)
# pool     = 16

[session]
cookie   = "myapp_sess"
ttl      = 86400 * 7
driver   = "memory"                     # memory | redis | file
# redis_url = "redis://localhost:6379"

[mail]
driver   = "smtp"                       # smtp | log | file
host     = "localhost"
port     = 1025
from     = "noreply@myapp.local"

[assets]
tailwind = true
minify   = true
# public_dir = "public"

[auth]
password_algo     = "pbkdf2-sha256"
session_cookie    = "myapp_sess"
require_email_verification = false

[shards]
enabled = true                          # include kern-shards.js in base layout

[scheduler]
# see section 12.3

[logging]
level   = "info"                        # debug | info | warn | error
format  = "text"                        # text | json
output  = "stdout"                      # stdout | file
file    = "./logs/app.log"

[custom]
# anything in [custom] is exposed to templates and code as `kern_config_get("custom.foo")`
```

### Env var injection

`${env:VAR}` is resolved at startup, errors if missing. Useful for secrets.

### Per-environment overrides

`config/dev.c`, `config/prod.c`, `config/test.c` can override anything from `kern.toml` programmatically. They're regular C files that call `kern_config_set_*`.

---

## 15. The CLI — `kern` command

Single static binary, `~/.local/bin/kern` or `/usr/local/bin/kern`.

### 15.1 Install

```bash
# macOS / Linux
$ curl -fsSL https://kern.dev/install.sh | bash

# verify
$ kern --version
kern 0.1.0 (libkern 0.1.0, built with gcc 14.2)
```

The install script detects OS/arch, downloads the right binary, chmods it. No dependencies.

### 15.2 Commands

```
kern new <name>                  Scaffold a new project
kern dev                         Run the dev server (watch + hot-reload)
kern build                       Production build
kern run                         Run the built binary
kern test [path]                 Run tests
kern fmt [path]                  Format .c/.h/.khtml files
kern exec                        Open a REPL with the app loaded
kern routes                      List all registered routes
kern db.migrate                  Run migrations
kern db.rollback                 Roll back last migration
kern db.status                   Show migration status
kern db.shell                    Open DB shell
kern db.dump                     Dump DB to SQL
kern db.seed                     Run seed file
kern queue.work                  Run a queue worker (foreground)
kern scheduler.run               Run the scheduler (foreground)
kern ui add <component>          Add a UI component to your project
kern ui list                     List available UI components
kern doctor                      Diagnose project issues
kern update                      Update kern itself
```

### 15.3 `kern dev` — watch mode

1. Initial scan: pages, views, models, assets, kern.toml.
2. Compile everything. Start the server.
3. Watch:
   - `pages/**` → regenerate registry, recompile, **hot swap** (server in place picks up new routes — no restart).
   - `views/**` → recompile only that template's `.c`, **hot swap** (no restart, the new template function is loaded into the live process).
   - `models/**`, `queries/**`, `mutations/**` → full restart (you've changed the schema or business rules).
   - `assets/**` → re-hash, re-emit to `public/`. The browser auto-refreshes via SSE.
   - `kern.toml` → full restart.

Hot-swap is implemented by `dlopen(3)` — the server loads a `.so` for routes and templates, and `kern dev` re-dlopens on change. Old symbols are kept until all in-flight requests finish.

### 15.4 `kern build` — production

1. Full template compile.
2. Asset hash + minify.
3. Compile with `-O2 -fvisibility=hidden -flto` (or `-O2 -flto` for clang).
4. Strip debug info.
5. Link statically against libkern.
6. Output: `dist/myapp` + `dist/public/`. The `myapp` binary is a single file. `dist/public/` is what nginx/kernd serves.

### 15.5 `kern fmt`

- C files: `clang-format` with a vendored config (we ship one). No surprise config files.
- `.khtml`/`.ktxt`/`.kfrag`: our own formatter (small, opinionated, deterministic).

### 15.6 `kern exec` — REPL

Loads the app's compiled `.so`, then drops you into a Lua-like REPL where you can call any function in the app:

```
$ kern exec
kern 0.1.0 — myapp
> post_find_by_id(1)
{ id=1, title="Hello", slug="hello", body="...", author_id=1, published=true, created_at=1725000000 }
> user_create("alice@example.com", "hunter2")
{ id=2, email="alice@example.com", ... }
> kern_db_exec("SELECT count(*) FROM posts")
4
```

Implemented with a small embedded Lua interpreter (Lua is ~150 KB, MIT, perfect for this).

### 15.7 `kern ui add <component>`

shadcn-style. `kern ui add button` copies `components/ui/button.khtml` and `components/ui/button.c` from the kern registry into your project. You own it. Modify freely. Future updates are opt-in.

```
$ kern ui add button
✓ components/ui/button.khtml
✓ components/ui/button.c
✓ components/ui/button.test.c
✓ docs/ui/button.md
```

Available at v0.7: button, card, modal, dropdown, tabs, table, form-field, toast, avatar, badge, input, textarea, select, checkbox, radio, switch, breadcrumb, pagination, tooltip, alert, dialog, accordion, separator, sheet, command, popover, calendar, date-picker, code-block, kbd, skeleton, progress, slider, toggle, toggle-group.

All built on Tailwind. No JS, except where interactivity is needed (then they emit a shard or a small inline script).

---

## 16. The Dashboard — `kernd` (the production half)

This is the part that doesn't exist in any reference framework. It's kern's secret weapon for the "small to mid size project" crowd.

### 16.1 What it is

A single static binary (`kernd`) that runs on a VPS as a long-lived daemon. It's the **only thing the user needs to install** to deploy and run kern apps.

### 16.2 Architecture

```
┌─────────── kernd (root, single process) ─────────────┐
│                                                       │
│  ┌────────────────┐  ┌────────────────┐              │
│  │ Admin Web UI   │  │ Reverse Proxy  │              │
│  │ (port 8443)    │  │ (port 80/443)  │              │
│  └────────┬───────┘  └────────┬───────┘              │
│           │                   │                       │
│  ┌────────┴───────────────────┴────────┐              │
│  │           kernd core                 │             │
│  │  - Auth (admin login)                │             │
│  │  - App registry (config + state)     │             │
│  │  - Deploy worker (git ops + build)   │             │
│  │  - Process manager (fork+exec)       │             │
│  │  - Cgroup v2 manager                 │             │
│  │  - Log capture + rotate              │             │
│  │  - Stats scraper                      │             │
│  │  - Backup scheduler                  │             │
│  │  - Cloudflare API client              │             │
│  │  - Update checker                     │             │
│  └──────────────────────────────────────┘             │
│                                                       │
│  ┌─────┐ ┌─────┐ ┌─────┐  (each: own cgroup)         │
│  │app-1│ │app-2│ │app-3│                             │
│  │:3001│ │:3002│ │:3003│                             │
│  └─────┘ └─────┘ └─────┘                             │
└──────────────────────────────────────────────────────┘
```

### 16.3 Install flow (the "one command")

```bash
# On a fresh Ubuntu 22.04 / Debian 12 / RHEL 9 VPS:
$ curl -fsSL https://kern.dev/install-kernd.sh | sudo bash
```

What the script does:

1. Detect OS, install minimal build tools if needed (for app builds) — but prefer a prebuilt static `kernd` binary.
2. Drop `kernd` at `/usr/local/bin/kernd`.
3. Create `kernd` user (no shell, no login — service account).
4. Create `/etc/kernd/` for state.
5. Run `kernd init`:
   - Generate `DASHBOARD_SECRET` (256 bits, base64).
   - Generate `DASHBOARD_ADMIN_PASSWORD` (print to stdout — user must save).
   - Create `/etc/kernd/dashboard.toml` with the defaults.
6. Create `kernd.service` (systemd unit, runs as `kernd` user for the listener, with capability grants for the manager).
7. Start the service.
8. Print:

```
✔ kernd installed
✔ systemd service enabled
✔ admin user created
  URL:        https://<vps-ip>:8443
  username:   admin
  password:   <generated>   ← save this; it won't be shown again

Next steps:
  1. Open the URL above
  2. Run the first-run wizard (change password, set domain, etc.)
  3. Add your first app
```

### 16.4 First-run wizard

When the user first logs in:

1. **Change admin password** (required).
2. **Dashboard domain** — optional. User can set a CNAME pointing to their VPS, and kernd will auto-provision Let's Encrypt. Or they can keep using `<vps-ip>:8443`.
3. **Cloudflare API token** — optional. If set, kernd can manage DNS for app domains.
4. **Default S3-compatible backup target** — optional. R2, Backblaze B2, AWS S3, MinIO, anything.
5. **Email for alerts** — where to send deploy-failure / disk-full / high-CPU notices.
6. **Timezone** — for log timestamps and the scheduler.

After this, the user lands on the dashboard home: "Add your first app".

### 16.5 The "Add app" flow

This is the killer UX moment. The user:

1. Clicks "Add app".
2. Fills out a form:
   - **Name** (used as subdomain by default, e.g. `myapp` → `myapp.example.com`).
   - **GitHub repo** (full URL, e.g. `https://github.com/me/myapp`).
   - **Branch** (default `main`).
   - **GitHub PAT** (encrypted at rest, used to clone private repos).
   - **Build command** (default: `kern build`).
   - **Start command** (default: `./dist/myapp`).
   - **Env vars** (key/value rows, encrypted at rest).
   - **Port** (auto-assigned next free, shown for awareness).
   - **Domain** (subdomain of dashboard domain, or custom).
   - **Resources** (CPU % cap, memory cap).
   - **Replicas** (default 1; kernd does round-robin within the cgroup).
3. Clicks **Deploy**.

`kernd` then:
1. Records the app config in `/etc/kernd/apps/<name>/app.toml`.
2. Clones the repo into `/var/lib/kernd/apps/<name>/src/`.
3. Runs `kern build` (chdir into the repo).
4. Captures the build log. Shows it in the dashboard (live tail).
5. On success: starts the binary as a child process, with cgroup limits applied.
6. Adds the vhost mapping (domain → 127.0.0.1:port) to the internal reverse-proxy table.
7. (If Cloudflare is configured) creates the DNS A/CNAME record.
8. Marks the app as "live" and shows the URL.

The whole flow: ~30 seconds for a "hello world" app, maybe 2-3 minutes for a non-trivial build.

### 16.6 Subsequent deploys

Two triggers:

1. **Webhook** — kernd exposes `POST /webhook/github` (per-app secret). The user adds this as a GitHub webhook. Every push to the watched branch triggers a deploy.
2. **Manual** — "Deploy" button in the dashboard.
3. **Polling** (fallback) — if no webhook is set, kernd polls the repo every 5 minutes.

Deploy is:
1. `git fetch && git checkout <branch> && git reset --hard origin/<branch>` in the src dir.
2. `kern build`. On failure: keep the previous binary running, show error, send alert.
3. On success: SIGTERM the running process, wait up to 30s, SIGKILL if needed.
4. Start the new binary. Wait for HTTP 200 from `/healthz` before marking live.
5. Rotate logs.
6. Send "deploy complete" notification.

**Zero downtime** for trivial updates, via the SIGTERM-then-SIGKILL flow. The reverse proxy stops routing to the app only during the 1-2 second restart window. (For bigger apps: kernd supports a `pre_start` hook that warms the new process on a different port before the swap — v0.6+.)

### 16.7 Reverse proxy (host-header routing)

kernd has a built-in HTTP/1.1+HTTP/2 reverse proxy (built on the same `libkern` HTTP parser). It:

- Listens on port 80, 443, and (for the dashboard) 8443.
- Routes by `Host` header.
- TLS termination via mbedTLS. Certs from Let's Encrypt (we ship an ACME client — small, ~1 KLOC, supports HTTP-01 and DNS-01 challenges).
- For an app `<name>`, looks up the vhost: `<name>.<dashboard-domain>` or any custom domain in the app's config.
- If no match, returns 404 (or a configurable landing page).
- For the dashboard itself (`<dashboard-domain>`), routes to the admin UI.
- Wildcard certs are supported — one cert for `*.dashboard-domain` covers all app subdomains.

**No nginx, no Caddy, no Traefik.** kernd does it all.

### 16.8 Cgroup resource control

For each app, kernd creates:

```
/sys/fs/cgroup/kernd/app-<name>/
├── memory.max           = <mem cap>
├── memory.swap.max      = 0
├── cpu.max              = <cpu cap, e.g. "200% 100000">   # 2 cores
├── pids.max             = 1024
└── cgroup.procs         = <app pid>
```

The app's binary is `exec`'d into the cgroup. We use libcap to grant `kernd` only the `CAP_SYS_ADMIN` capability (or run kernd as root — we recommend root for simplicity, since the entire point of the dashboard is to manage the host).

The dashboard UI exposes live memory, CPU, PIDs, and I/O bytes from `cgroup.events`, `memory.current`, `cpu.stat`, `io.stat`. Polled every 5 seconds, exposed via SSE in the dashboard.

### 16.9 Logs

- Each app's `stdout` and `stderr` are redirected to `/var/log/kernd/apps/<name>/current.log`.
- `logrotate`-style daily rotation: `current.log` → `2025-08-10.log.gz` (gzip compressed).
- Old logs auto-deleted after N days (configurable, default 30).
- Dashboard shows live tail via SSE (pipes `tail -F` output through a kernd internal stream).
- Grep / search / download buttons in the UI.

### 16.10 Stats

Per-app:
- CPU usage (from `cpu.stat`)
- Memory usage (RSS, cache, swap — from `memory.stat`)
- Network in/out (from `cgroup`'s network classid or per-interface counters)
- Disk I/O (from `io.stat`)
- Request count, error count, p50/p95/p99 latency (from kernd's own access log, only for the reverse proxy requests)
- Process count, thread count

Per-host (top of dashboard):
- Total CPU, memory, disk usage
- Per-app resource share (a stacked bar)
- Uptime, kernel version, kernd version

Historical: kernd writes a sample to `/var/lib/kernd/stats/<app>/<date>.jsonl` every 60s. 30 days retained by default. The dashboard renders graphs from this (lazy load).

For the curious: `/metrics` endpoint in Prometheus exposition format. Drop-in for Grafana if you want.

### 16.11 Backups

Configurable per-app and globally. A backup job:

1. Snapshot the app's source tree (`/var/lib/kernd/apps/<name>/src/`) — or just `git bundle` it.
2. Snapshot the database — `VACUUM INTO 'backup.sqlite'` for SQLite, or `pg_dump` for Postgres.
3. Snapshot `/var/log/kernd/apps/<name>/` if "include logs" is on.
4. Bundle into a tar.gz with a manifest.
5. Upload to the configured target(s). Multiple targets supported.

Targets:
- **Local:** `/var/backups/kernd/`.
- **S3-compatible:** any R2/S3/MinIO endpoint. The kernd config has `endpoint`, `bucket`, `access_key`, `secret_key`, optional `region`.
- **rsync/netcat:** for the tinkerers.

Schedule via cron expressions. Default: daily at 03:00 UTC, retain 7 local + 30 remote, prune after that.

Restore is one click in the UI: pick a backup, kernd stops the app, restores DB + source, restarts, done.

### 16.12 Cloudflare integration

Optional. If configured (API token + zone ID), kernd can:

- **Auto DNS:** when an app is added with a custom domain, create the A/CNAME record in Cloudflare.
- **Auto CDN:** enable Cloudflare proxy on the domain (orange cloud).
- **Cache purge on deploy:** after a successful deploy, send a `purge_cache` for the app's domain.
- **Cache rules (read from app config):** `cache_everything`, `edge_cache_ttl`, `browser_cache_ttl`, custom page rules.

If not configured, the apps just work over HTTPS using kernd's own Let's Encrypt. Cloudflare is opt-in.

### 16.13 Updates

- kernd polls `https://api.kern.dev/releases/kernd/stable` every 6 hours. If a new version is available, the dashboard shows a banner.
- One-click update: kernd downloads the new binary, swaps it in, restarts the service. The running apps are untouched.
- Rollback supported: kernd keeps the last 3 binaries in `/var/lib/kernd/updates/`.
- `kern update` CLI on the host does the same.

### 16.14 Security of kernd itself

- TLS required. Self-signed default cert at install (for the first login over the VPS IP) — replaced by Let's Encrypt as soon as a domain is set.
- bcrypt for the admin password (Argon2id planned).
- Session cookies with `__Host-` prefix.
- Per-app PATs are encrypted at rest with a key derived from `DASHBOARD_SECRET`.
- All env vars and PATs are encrypted with libsodium's secretbox.
- Rate limit on the admin login (5 attempts / 15 min / IP).
- Audit log: every admin action (deploy, config change, app add/remove) is logged to `/var/log/kernd/audit.log` and shown in the dashboard.
- SSH is not touched. The user is free to also SSH in.

### 16.15 Multi-app on a single VPS

This is the default and expected use case. A user might have:
- `blog` (blog.example.com)
- `api` (api.example.com)
- `admin` (admin.example.com)
- `dash` (dash.example.com — the dashboard itself)

All four in one kernd install, each in its own cgroup, all sharing port 80/443 via kernd's reverse proxy.

### 16.16 What kernd is NOT

- Not a Kubernetes replacement. One VPS = one kernd. To scale out: run more VPSes. (Load balancing across them is the user's problem — typically via Cloudflare or a cheap DNS-based round-robin.)
- Not a CI system in the GitHub-Actions sense. It's a deploy receiver. It builds and ships, but doesn't run test matrices.
- Not a DB host. If you want Postgres, run it yourself (or use a managed service). kernd's apps can talk to a remote DB; the dashboard itself uses SQLite for its own state.

---

## 17. The install experience

### 17.1 For app developers (the kern CLI)

```bash
$ curl -fsSL https://kern.dev/install.sh | bash
$ kern new myapp
$ cd myapp
$ kern db.migrate
$ kern dev
```

The install script:
- Detects OS (macOS via brew, Linux via direct download).
- Downloads the right `kern` binary from `releases.kern.dev` (or `ghcr.io/kern-dev/kern` as fallback).
- Verifies SHA256 against a published manifest (signed with our release key).
- Drops it in `~/.local/bin/` (or `/usr/local/bin/` if `sudo`-ed).
- Adds to PATH if needed.

### 17.2 For VPS deployment (kernd)

The single command is `curl -fsSL https://kern.dev/install-kernd.sh | sudo bash` (described in 16.3).

### 17.3 For end users of kern apps

They don't install anything. They visit a URL. That URL is served by kernd, which routes to the right app.

---

## 18. UI / component library

### 18.1 Philosophy

**shadcn/ui for kern.** Each component is a `.khtml` template + an optional `.c` helper. You copy them into your project. You own them. Modify freely. Future updates are opt-in.

### 18.2 Default component set (v0.7)

Buttons, cards, modals, dropdowns, tabs, tables, form fields, toasts, avatars, badges, inputs, textareas, selects, checkboxes, radios, switches, breadcrumbs, pagination, tooltips, alerts, dialogs, accordions, separators, sheets, command palettes, popovers, calendars, date pickers, code blocks, keyboard hints, skeletons, progress bars, sliders, toggles, toggle groups.

All built on Tailwind v4. No JS, except where interactivity is needed (then they emit a shard or a small inline script).

### 18.3 Anatomy of a component

```
components/ui/button/
├── button.khtml       # template
├── button.c           # optional helper function
├── button.test.c      # test
└── README.md          # usage
```

Example: `button.khtml`

```pug
- kern_attr_t attrs = kern_attrs_merge(KERN_T(
    "class", "inline-flex items-center justify-center rounded-md font-medium transition disabled:opacity-50 disabled:pointer-events-none",
    "type", "button"
  ), user_attrs);

- if (variant == "primary")  kern_attrs_add(attrs, "class", "bg-zinc-900 text-white hover:bg-zinc-800 px-4 py-2");
- if (variant == "ghost")    kern_attrs_add(attrs, "class", "hover:bg-zinc-100 px-4 py-2");
- if (variant == "danger")   kern_attrs_add(attrs, "class", "bg-red-600 text-white hover:bg-red-700 px-4 py-2");

button(*attrs)
  block
```

Use:

```pug
+button("primary") Click me
+button("ghost", to="/posts") View posts
```

### 18.4 Why not a JS component library?

shadcn uses Radix (JS) for behavior. We don't have Radix. We have **shards** — server-rendered fragments. So our `Modal` is a form submission that returns a modal, swapped into a `<div id="modal-root">`. Our `Dropdown` is a server-rendered menu. Our `DatePicker` is a tiny inline `<input type="date">` styled with Tailwind.

This is the trade-off for "no JS framework". Most components are still very usable. The few that need real interactivity (drag-and-drop, complex animations) get a small inline `<script>` and we keep moving.

---

## 19. Testing

### 19.1 `kern test`

Runs all tests under `test/`. Three kinds:

1. **Unit tests** (`test/unit/`) — pure functions, no HTTP. Run in-process.
2. **Integration tests** (`test/integration/`) — boot the app, make HTTP requests, assert.
3. **Browser tests** (`test/browser/`) — optional, opt-in. Spawn headless Chromium via WebKitGTK or a small Node sidecar. Marked experimental.

### 19.2 Example unit test

```c
/* test/unit/post_model_test.c */
#include "test/test.h"

TEST(post_create_validates_title) {
    post_input_t in = {
        .title = "Hi",
        .slug  = "hi",
        .body  = "...",
        .author_id = 1,
    };
    kern_mutation_result_t r = post_create(NULL, &in);
    ASSERT(r.ok == false);
    ASSERT(strstr(r.error, "title") != NULL);
}

TEST(post_create_persists) {
    post_input_t in = { .title = "Hello", .slug = "hello", .body = "...", .author_id = 1 };
    kern_mutation_result_t r = post_create(NULL, &in);
    ASSERT(r.ok == true);
    ASSERT(r.value->id > 0);
    ASSERT(strcmp(r.value->title, "Hello") == 0);
}
```

### 19.3 Example integration test

```c
/* test/integration/auth_flow_test.c */
#include "test/test.h"

TEST(auth_signup_login_logout) {
    kern_test_app_t *app = kern_test_app_start();

    http_post(app, "/signup", .form = { .email = "a@b.com", .password = "hunter22!!" })
        ->expect(302);

    http_post(app, "/login", .form = { .email = "a@b.com", .password = "hunter22!!" })
        ->expect(302)
        ->expect_set_cookie("session");

    http_get(app, "/dashboard")->expect(200);

    http_post(app, "/logout")->expect(302);

    http_get(app, "/dashboard")->expect(302)->expect_location("/login");

    kern_test_app_stop(app);
}
```

### 19.4 Test database

A separate SQLite file per test process. Migrations run at boot. Fixtures loaded from `test/fixtures/`. Each test is wrapped in a transaction that rolls back at the end (so tests are isolated and fast).

### 19.5 Coverage

`kern test --coverage` produces a gcov-compatible report. We ship a tiny HTML reporter.

---

## 20. Observability

### 20.1 Structured logging

```c
kern_log_info("user.login", KERN_F("user_id", "%d", u.id));
kern_log_warn ("payment.failed", KERN_F("amount_cents", "%d", cents));
kern_log_error("db.timeout",    KERN_F("query_ms", "%d", ms));
```

Output is line-delimited JSON or human text (configurable). Each line has: `ts`, `level`, `event`, `fields...`, optional `request_id`, `user_id`, `app`, `env`.

### 20.2 Request tracing

Every request gets a `request_id` (UUID v7). The ID is:
- added to the response as `X-Request-Id` header
- logged on every log line for that request
- accepted as `X-Request-Id` from upstream (for cross-service tracing)

### 20.3 Metrics

Optional Prometheus exporter at `/_metrics` (off by default, opt-in via config). Counters, gauges, histograms for: requests, request duration, DB query duration, queue job duration, memory, GC (well, malloc bytes), per-route stats.

### 20.4 Health endpoints

- `/healthz` — 200 if the process is alive. (Set by the reverse proxy — kernd adds this.)
- `/readyz` — 200 if the app can serve traffic (DB reachable, etc.). The dev convention is to put this in `pages/api/readyz.c`.
- `/metrics` — Prometheus format, if enabled.

### 20.5 Crash reports

On SIGSEGV, kern installs a signal handler that:
1. Captures a minimal stack trace (using `backtrace(3)`).
2. Writes to `crash-<timestamp>.log` in the app's directory.
3. Sends to the configured webhook (if any) — opt-in.
4. Re-raises the signal (default behavior preserved).

---

## 21. Performance & scaling

### 21.1 Why C

- **Cold start:** < 5 ms. Try that with Rails.
- **Memory baseline:** ~3 MB resident. Beat that, Rails.
- **Throughput:** one kern process handles ~50K req/s on a single core for "hello world". DB-bound apps hit DB limits, not CPU limits.
- **Binary size:** a hello-world `myapp` is ~1.5 MB stripped. With `--lto`, ~800 KB.

### 21.2 Single-process scale

A single kern process is enough for ~95% of small/medium web apps. The reactor handles ~10K concurrent connections. Worker pool handles blocking work. DB connections are pooled.

### 21.3 Multi-process / multi-host scale

`kern build` produces a single binary. To run multiple instances:
- **Same host:** kernd can run N replicas per app (round-robin via the reverse proxy). cgroup memory splits N ways.
- **Multi-host:** run more VPSes, each with kernd. Front them with Cloudflare or a TCP load balancer.

### 21.4 Profiling

`kern dev` exposes a `/debug/pprof`-like endpoint (off by default). The format is text-based — `go tool pprof` won't read it directly, but we ship `kern pprof` to do the same thing (top, list, web, callgrind export).

### 21.5 Benchmarks

We publish a `kern bench` command that runs `wrk`-style benchmarks against a kern app. Same as Phoenix's `mix phx.bench`, Lucky's `lucky bench`. Output is comparable to topcoat/blog benchmarks.

---

## 22. Security model

| Concern | Mitigation |
|---|---|
| XSS | Templates auto-escape `#{expr}`. Use `!{expr}` only for trusted HTML. |
| SQL injection | Query builder parameterizes. Raw SQL is allowed but lints in `kern doctor` for `$VAR` interpolation. |
| CSRF | Per-session double-submit token, validated on all state-changing requests. |
| Clickjacking | `X-Frame-Options: DENY` + CSP `frame-ancestors 'none'` by default. |
| MIME sniffing | `X-Content-Type-Options: nosniff`. |
| HSTS | Strict-Transport-Security on HTTPS responses (configurable). |
| Cookies | `__Host-` prefix, `Secure`, `HttpOnly`, `SameSite=Lax`. |
| Password storage | PBKDF2-HMAC-SHA256 (100K iterations). |
| Rate limit | Per-IP and per-route. 429 with Retry-After. |
| CSP | Sensible default. Strict by default for new apps. |
| Subresource integrity | Built into `asset()` helper. |
| TLS | TLS 1.2 minimum, 1.3 preferred. Strong cipher suites. HSTS preload eligible. |
| Dependency CVEs | Static linking means fewer moving parts. `kern doctor` checks the bundled lib versions. |
| Secrets | Encrypted at rest in `kernd`. `kern.toml` references `${env:VAR}` — never inlines. |

### Security audit mode

`kern audit` (CLI) and the dashboard's "Security" page do:
- Check for known-bad dependency versions.
- Verify all routes have CSRF middleware.
- Verify all forms have CSRF tokens.
- Check CSP header presence.
- Suggest tightening headers.

---

## 23. Roadmap

### v0.1 — proof of core ✅
- HTTP/1.1 server, radix router, file-system routing.
- `.khtml` compiler → C.
- SQLite driver, query builder, migrations.
- Sessions, basic auth.
- Asset hashing.
- `kern new`, `kern dev`, `kern build`.

### v0.2 — DX ✅
- `kern fmt`, `kern test`.
- Tailwind v4 compile (C implementation).
- Shards (HTMX-style).

### v0.3 — production readiness
- Mailer (SMTP client, REST API fallback, log driver for dev).
- Queue system with exponential backoff and dead-letter handling.
- Scheduler (cron-style task dispatch).
- Authorization policies (`KERN_POLICY`, `KERN_AUTHORIZE`).
- `kern doctor` — project diagnostics.
- Per-IP and per-route rate limiting (token bucket).

### v0.4 — dashboard MVP
- `kernd` daemon binary.
- Install script (single curl command).
- Admin Web UI (app add/remove, deploy trigger).
- Git clone + build worker.
- cgroup v2 resource management (CPU, memory, PIDs per app).
- Built-in reverse proxy (host-header routing, ports 80/443).
- Let's Encrypt integration (ACME HTTP-01 + DNS-01).
- Log capture and rotation.
- Backup system (local + S3-compatible targets).

### v0.5 — dashboard polish + SSE
- Cloudflare DNS + CDN integration (auto-DNS, cache purge on deploy).
- Per-app stats and graphs (CPU, memory, network, request latency).
- Self-update mechanism for kernd.
- Per-app environment variables and secrets UI (encrypted at rest).
- SSE (Server-Sent Events) — built-in server push for live updates, notifications, dashboards.

### v0.6 — modern protocols
- HTTP/2 support (multiplexing, header compression).
- Zero-downtime deploys (pre-start hook: warm new process before swap).

### v0.7 — stable release
- API frozen (semver guarantees from this point).
- Complete documentation.
- Project templates via `kern new --template <name>` (blog, SaaS, API, admin presets).
- Migration guides from Rails, Phoenix, Laravel.
- Production case studies (3+ real deployments).
- Performance benchmarks published.
- Security audit completed.

### Post-v0.7 (public roadmap)

**Framework:**
- PostgreSQL driver.
- Alternative databases (libSQL/Turso, MySQL).
- Built-in static site generation.
- Plugin registry (kern plugins vs. kernd plugins).
- Web-based admin generator (like `rails_admin`).

**Dashboard & Operations:**
- Multi-replica support per app (round-robin via reverse proxy).
- Redis session driver (shared sessions across instances).
- Redis queue driver (shared job queue across instances).
- kernd-to-kernd federation (cluster view across VPSes).
- `kernd ha` for active-passive kernd clusters.
- HTTP/3 (QUIC) support.
- WebSocket support (for apps that genuinely need bidirectional real-time: collaborative editing, gaming).

**Infrastructure & Ecosystem:**
- VSCode extension (LSP for C + `.khtml` syntax highlighting).
- Neovim plugin (same LSP, treesitter grammar for `.khtml`).
- Helm chart for Kubernetes users.
- Terraform module for AWS / Hetzner / DigitalOcean.
- GitHub Actions workflow templates.
- Hosted kern offering (kern.dev SaaS).

---

## 24. Comparison to reference frameworks

| Dimension | kern | Topcoat (Rust) | Vibe.d (D) | Lucky (Crystal) | Ziex (Zig) |
|---|---|---|---|---|---|
| Language | C11 | Rust | D | Crystal | Zig |
| Binary | single static | single static (with rustc) | single | single | single |
| Cold start | < 5 ms | ~30 ms (rustc) | ~5 ms | ~5 ms | ~3 ms |
| Memory baseline | ~3 MB | ~10 MB | ~8 MB | ~15 MB | ~2 MB |
| Runtime | none | none | D runtime (small) | Crystal runtime (small) | none |
| Build time (hello) | 1–2 s | 30–60 s | 5–10 s | 5–10 s | 3–5 s |
| Templating | compile-time Pug | macro `view!` | CTFE Diet | macro `render` | JSX-in-Zig |
| DB default | SQLite | Toasty ORM | MongoDB/Redis | PostgreSQL | n/a (no default) |
| Async model | fibers | Tokio | fibers | fibers (Crystal) | async fn |
| Reactivity | shards (HTMX-style fetch/swap) + SSE | macro to JS | SSE/WebSocket | LiveView (via LuckyLive) | none |
| File-system routing | yes | yes (auto-discover) | no (router table) | yes (manual) | yes |
| Hot reload | yes (dlopen) | yes (cargo) | yes (dub) | yes (lucky dev) | yes |
| UI library | shadcn-style | shadcn-style (Topcoat UI) | none built-in | none built-in | none built-in |
| Dashboard | **kernd** | none | none | none | none |
| Single-install VPS deploy | **yes** | no | no | no | no |
| Maturity | v0.7 target | pre-1.0 | mature | mature | very early |

**The unique thing kern brings to the table is the dashboard.** No other framework on this list ships a first-class "install one binary on a VPS, deploy apps with a click" experience. Topcoat comes closest in core framework quality, but stops at "give you a CLI". kern goes all the way to "ship your fleet without learning nginx, systemd, cgroups, and Let's Encrypt acme.sh".

---

## 25. Open questions

1. ~~**Coroutines: ucontext vs. our own asm?**~~ **Decided: No coroutines.** kern writes plain C functions. libuv thread pool handles blocking work. See §5.3.
2. **TLS library: mbedTLS vs. BearSSL vs. picotls?** All three are embed-friendly. mbedTLS has the best docs and broadest support. BearSSL is the smallest (~80 KB). picotls is the most modern. Leaning: mbedTLS for the binary, with BearSSL considered for the dashboard's static binary.
3. **Tailwind: reimplement in C, or shell out?** **Decided: reimplemented in C.** `kern_tailwind.c` is a pure-C JIT scanner that emits CSS. No Node, no external deps.
4. ~~**Postgres driver: libpq vs. our own?**~~ **Deferred to post-v0.7.** SQLite is the only database for the active roadmap. See §10.5.
5. **REPL: embed Lua or write our own?** Lua is ~150 KB, MIT, and proven. Writing our own is overkill. Leaning: Lua.
6. **Dashboard UI framework: server-rendered `.khtml` too?** Yes. The dashboard is a kern app — it eats its own dog food. Auth pages, app list, log viewer — all `.khtml`. The graphs use uPlot (10 KB, MIT) loaded as a static asset.
7. **Cgroups v1 fallback?** No. Cgroups v2 is in mainline Linux 5.x, which is what every modern VPS runs. We document this in the install prereqs.
8. **ACME client: write our own or shell out to `certbot`/`acme.sh`?** Our own. `acme.sh` is bash, 100 KLOC, and pulls in curl + openssl + a bunch of utils. We can do HTTP-01 + DNS-01 in ~1 KLOC of C, talking to Let's Encrypt directly.
9. **macOS dev: how to test cgroup behavior?** cgroups are Linux-only. The dashboard is a no-op on macOS (runs but doesn't manage resources). For real testing, use a Linux VM or container. We document this.
10. **Long-term: kernd as a paid product?** No. Everything is open source (MIT or Apache 2.0 — undecided). A hosted kern (kern.dev) might be a SaaS offering in 2027+, but the software stays free.

---

## 26. Appendix A: code samples

### A.1 Minimal app

`kern new hello` produces:

**`app.c`**
```c
#include <kern.h>

int main(int argc, char **argv) {
    kern_app_t *app = kern_app_new("hello");
    kern_app_listen(app, kern_env_int("PORT", 3000));
    return kern_app_run(app);
}
```

**`pages/index.c`**
```c
#include <kern.h>

KERN_PAGE("/", home);

static kern_response_t *home(kern_req_t *req) {
    kern_dict_t *vars = kern_dict_new();
    kern_dict_set(vars, "name", "World");
    return kern_render(req, "home", vars);
}
```

**`views/home.khtml`**
```pug
doctype html
html(lang="en")
  head
    title Hello — kern
  body
    h1 Hello, #{name}!
    p Built with kern.
```

**`kern.toml`**
```toml
[app]
name = "hello"
port = 3000
```

**`db/migrations/001_users.sql`**
```sql
CREATE TABLE users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  email TEXT NOT NULL UNIQUE,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
```

Build & run:
```bash
$ kern dev
[info] scanning pages/         → 1 route registered
[info] compiling views/        → 1 template compiled
[info] building assets         → 0 files
[info] linking                 → dist/hello
[info] starting on http://localhost:3000
```

Visit `http://localhost:3000`:
```html
<!doctype html>
<html lang="en">
  <head>
    <title>Hello — kern</title>
  </head>
  <body>
    <h1>Hello, World!</h1>
    <p>Built with kern.</p>
  </body>
</html>
```

### A.2 A blog post page (CRUD)

**`pages/posts/[id].c`**
```c
#include <kern.h>
#include "queries/posts/find_by_id.h"

KERN_GET("/posts/:id", post_show);

static kern_response_t *post_show(kern_req_t *req) {
    int64_t id = kern_param_int(req, "id");
    post_t *p = post_find_by_id(id);
    if (!p || !p->published) {
        return kern_404(req);
    }
    return kern_render(req, "posts/show", KERN_T("post", p));
}
```

**`pages/posts/index.c`**
```c
#include <kern.h>
#include "queries/posts/list_published.h"

KERN_GET("/posts", post_index);

static kern_response_t *post_index(kern_req_t *req) {
    size_t limit  = kern_query_int(req, "limit", 20);
    size_t offset = kern_query_int(req, "offset", 0);
    post_t **list = post_list_published(limit, offset);
    return kern_render(req, "posts/index", KERN_T("posts", list, "posts_len", kern_arr_len(list)));
}
```

**`queries/posts/find_by_id.c`**
```c
#include <kern.h>
#include "models/post.h"

post_t *post_find_by_id(int64_t id) {
    kern_db_t *db = kern_db_get();
    kern_qb_t *q = kern_qb(db)
        ->select(post, *)
        ->from(post)
        ->where(id, "=", kern_arg_int(id))
        ->where(published, "=", kern_arg_bool(true))
        ->limit(1);
    return kern_qb_one(q, post_t);
}
```

**`views/posts/show.khtml`**
```pug
extend layouts/base

block body
  article(class="prose mx-auto p-8")
    h1 #{post.title}
    p(class="text-zinc-500") Published #{format_date(post.created_at)}
    div
      | #{post.body}
```

### A.3 A form (sign up)

**`pages/signup.c`**
```c
#include <kern.h>
#include "mutations/users/register.h"

KERN_GET ("/signup", signup_form);
KERN_POST("/signup", signup_submit);

static kern_response_t *signup_form(kern_req_t *req) {
    return kern_render(req, "signup/form", KERN_T("user", &(user_input_t){0}));
}

static kern_response_t *signup_submit(kern_req_t *req) {
    user_input_t in = {
        .email    = kern_form_str(req, "email"),
        .password = kern_form_str(req, "password"),
    };
    kern_mutation_result_t r = user_register(req, &in);
    if (!r.ok) {
        return kern_render(req, "signup/form", KERN_T("user", &in, "errors", r.errors));
    }
    kern_session_start(req);
    kern_session_set(req, "user_id", r.value->id);
    kern_flash_set(req, "Welcome!");
    return kern_redirect(req, "/dashboard");
}
```

**`mutations/users/register.c`**
```c
#include <kern.h>
#include "models/user.h"

kern_mutation_result_t user_register(kern_req_t *req, user_input_t *in) {
    kern_validation_t *v = kern_validate_start(req);
    KERN_REQUIRE_EMAIL(v, email, in->email);
    KERN_REQUIRE_STR  (v, password, in->password, MIN 8, MAX 200);
    KERN_REQUIRE_UNIQUE(v, email, in->email, user_find_by_email);
    if (kern_validation_failed(v)) return kern_mutation_fail(v);

    kern_db_t *db = kern_db_get();
    kern_tx_t  *tx = kern_tx_begin(db);
    int64_t id = KERN_INSERT(tx, user,
        .email = in->email,
        .password_hash = kern_password_hash(in->password));
    kern_tx_commit(tx);
    return kern_mutation_ok(user_find_by_id(id));
}
```

### A.4 A shard (live comments)

**`pages/posts/[id]/_shards.c`**
```c
#include <kern.h>
#include "queries/comments/list_by_post.h"

KERN_SHARD("/posts/:id/comments", comments_shard);

static kern_response_t *comments_shard(kern_req_t *req) {
    int64_t post_id = kern_param_int(req, "id");
    comment_t **list = comment_list_by_post(post_id);
    return kern_render(req, "posts/_comments", KERN_T("comments", list));
}
```

**`views/posts/_comments.kfrag`**
```pug
div(id="comments" class="space-y-2")
  - for (size_t i = 0; i < comments_len; i++)
    div(class="rounded border p-3")
      p(class="text-sm text-zinc-700") #{comments[i].body}
      p(class="text-xs text-zinc-400") #{format_date(comments[i].created_at)}

  form(method="post"
       action="/posts/:id/comments"
       data-shard-target="#comments"
       data-shard-swap="outer"
       class="mt-4 flex gap-2")
    input(type="hidden" name="_csrf" value={csrf_token()})
    input(name="post_id" type="hidden" value={post.id})
    input(name="body" required placeholder="Add a comment..."
          class="flex-1 border rounded px-3 py-2")
    button(type="submit" class="bg-zinc-900 text-white px-4 py-2 rounded") Post
```

That's it. No JavaScript code in your project. The `<form data-shard-*>` attributes are picked up by the included `kern-shards.js`. The shard handler renders a fragment. The runtime swaps `#comments` with the response.

### A.5 A scheduled job

**`jobs/cleanup_expired.c`**
```c
#include <kern.h>

KERN_JOB("cleanup_expired", cleanup_expired);

static kern_result_t cleanup_expired(kern_job_t *job) {
    kern_db_t *db = kern_db_get();
    int64_t n = kern_db_exec(db,
        "DELETE FROM sessions WHERE last_seen_at < strftime('%s','now') - 86400 * 30");
    kern_log_info("cleaned sessions", KERN_F("count", "%lld", (long long) n));
    return KERN_OK;
}
```

**`kern.toml`**
```toml
[[scheduler.tasks]]
cron = "0 3 * * *"
job  = "cleanup_expired"
```

### A.6 An app deployed via the dashboard

User clicks "Add app", fills:
- Name: `myblog`
- Repo: `https://github.com/me/myblog`
- Branch: `main`
- Build cmd: `kern build`
- Start cmd: `./dist/myblog`
- Env: `MYAPP_SECRET`, `MYAPP_DB_URL=sqlite:/var/lib/kernd/apps/myblog/db.sqlite`
- Domain: `myblog.example.com` (or auto: `myblog.kernd.example.com`)

`kernd` does:
1. `git clone https://<token>@github.com/me/myblog /var/lib/kernd/apps/myblog/src`
2. `cd /var/lib/kernd/apps/myblog/src && kern build`
3. `start-stop-daemon`-equivalent: cgroup setup, exec `./dist/myblog` with env
4. Add vhost `myblog.example.com → 127.0.0.1:3001`
5. (If Cloudflare) `POST zones/<zone>/dns_records` to create the A record
6. Update app state: `live`

User visits `myblog.example.com`. Sees the blog. Done.

---

## 27. Note on the name "kern"

The name "kern" is short, has a C-systems vibe (it means "core" in German, and is the same root as "kernel"), and is what you asked for. Two collisions to be aware of:

1. **`kernlang.dev`** — a UI description language that compiles to Next.js / React / Vue etc. Different space (it's a UI DSL, not a backend framework).
2. **`kern-lang.eu`** — a European backend language (their own language, not C). Different space too, but they use `kern new` as a command.
3. **`kern.js`** — old PHP/Node CMS framework. Effectively dead.

**Recommendation:** the GitHub org / website will be `kern-lang.dev` or `kernstack.dev` or similar. The product is just "kern" everywhere else. We can differentiate by:
- Being explicit in the README: "kern is a C-based full-stack web framework, not to be confused with `kernlang.dev` (UI DSL) or `kern-lang.eu` (their own language)."
- If collisions become a real problem in 6 months, rename to `kernstack`, `kernix`, or something else — but `kern` is the codename in this doc.

If you want a clean-slate name, suggestions in the same vibe: **`lithos`** (Greek for stone, gives a C/Unix feel), **`monobase`**, **`lume`** (Latin for light — small, fast), **`pike`** (the fish, sharp, C-friendly), **`kernel.co`** (no, taken), **`ferrite`**, **`solder`** (meld the stack), **`rivet`**.

But the user said "Let's name it kern", so kern it is.

---

*End of design spec. Next step: a 60-day plan to build v0.1 (core HTTP + routing + `.khtml` + SQLite + `kern new` + `kern dev` + `kern build`).*
