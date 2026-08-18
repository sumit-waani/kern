# kern Documentation

> A monolithic, batteries-included, C-powered full-stack web framework.

## Table of Contents

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | High-level architecture, runtime model, build pipeline, and the three deliverables |
| [Phases Overview](phases-overview.md) | All phases from v0.1 through v0.7 + post-v0.7 roadmap |
| [API Reference](api-reference.md) | Public API surface for kern.h: types, request/response, router, DB, sessions, config |
| [Getting Started](getting-started.md) | How to use `kern new`, `kern dev`, `kern build` |
| [Project Layout](project-layout.md) | Convention-based folder structure for kern apps |
| [KHTML Syntax](khtml-syntax.md) | The `.khtml` template language reference |

## Quick Links

- **New to kern?** Start with [Getting Started](getting-started.md)
- **Building an app?** See [Project Layout](project-layout.md) and [API Reference](api-reference.md)
- **Writing templates?** See [KHTML Syntax](khtml-syntax.md)
- **Contributing to kern itself?** See [Architecture](architecture.md), [CONVENTIONS.md](../CONVENTIONS.md), and [TOOLING.md](../TOOLING.md)

## Design Principles

1. **One binary per app.** No runtimes, no interpreters, no GC.
2. **Conventions over configuration.** Folder structure is the contract.
3. **Server-rendered everything.** Components are functions that can hit the database.
4. **Batteries included.** Auth, sessions, mailer, queue, scheduler, ORM-lite, asset pipeline, dashboard.
5. **C11, no compiler extensions.** Must build with stock `gcc` and `clang`.
6. **Linux-first, POSIX-portable.** macOS dev works. Windows is best-effort.
7. **No magic strings.** Routes, asset URLs, model columns referenced via C identifiers.
8. **The CLI is the build system.** `kern dev`, `kern build`, `kern test`.
9. **Reactivity is opt-in.** Default = server-rendered, no client JS.
10. **No containers, no Docker.** The dashboard uses cgroups for isolation.

## Status

kern v0.4 is shipped. v0.5  is next. See [Phases Overview](phases-overview.md) for the full roadmap.
