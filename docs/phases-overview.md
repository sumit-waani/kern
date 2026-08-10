# Phases Overview (v0.1 through v1.0)

This document describes the full roadmap from initial proof-of-concept to stable release. Each phase builds on the previous one, and the ordering is intentional: earlier phases establish foundations that later phases depend upon.

---

## v0.1 - Proof of Core

**Goal:** Demonstrate that a batteries-included C web framework is viable by delivering the minimum end-to-end experience.

**Delivers:**
- HTTP/1.1 server with keep-alive
- Radix tree router
- File-system routing from `pages/` directory
- `.khtml` compiler (Pug-style templates to C)
- SQLite driver with query builder
- Database migrations
- Cookie-based sessions
- Basic email/password authentication
- Asset fingerprinting (content-hash pipeline)
- `kern new`, `kern dev`, `kern build` CLI commands

**Why first:** Everything else depends on having a working HTTP server, router, template engine, and database layer. This phase proves the architecture works end-to-end. Without it, no other feature can be demonstrated or tested in a real app context.

---

## v0.2 - Developer Experience

**Goal:** Make daily development pleasant and productive. Polish the tools, add testing, and deliver the first interactive features.

**Delivers:**
- `kern fmt` - code formatter for C and `.khtml` files
- `kern test` - test framework with unit and integration support
- `kern exec` - REPL for interactive exploration (embedded Lua)
- Tailwind v4 CSS compilation (C implementation, no Node)
- shadcn-style UI component library: button, card, input, form-field, alert
- Shards system (HTMX-style server-rendered interactivity)
- `kern-shards.js` runtime (~10 KB, vanilla ES2020)
- Documentation site

**Why second:** Once the core works, developers need quality-of-life tools. Formatting, testing, and the REPL make iteration fast. Tailwind and UI components make apps look good without custom CSS. Shards add interactivity without a JS build step. These are the features that make people want to keep using kern after the initial "hello world."

---

## v0.3 - Production Readiness

**Goal:** Add the features required to deploy a real application: email, background work, a production-grade database option, and security hardening.

**Delivers:**
- Mailer (SMTP client, log driver for dev)
- Queue system with exponential backoff and dead-letter handling
- Scheduler (cron-style task dispatch)
- PostgreSQL driver (via libpq)
- Authorization policies (`KERN_POLICY`, `KERN_AUTHORIZE`)
- `kern doctor` - project diagnostics
- `kern audit` - security audit
- Per-IP and per-route rate limiting (token bucket)

**Why third:** A real app needs to send emails (signups, password resets), run background jobs (cleanup, notifications), and handle authorization beyond "is the user logged in?" Postgres support unlocks apps that need concurrent writes. Rate limiting and audit tools harden the app for public traffic. These are pre-requisites for any production deployment.

---

## v0.4 - Dashboard MVP

**Goal:** Deliver kernd, the VPS dashboard that makes deployment a one-click operation. This is kern's differentiating feature.

**Delivers:**
- `kernd` daemon binary
- Install script (single curl command)
- Admin Web UI (app add/remove, deploy trigger)
- Git clone + build worker
- cgroup v2 resource management (CPU, memory, PIDs per app)
- Built-in reverse proxy (host-header routing, ports 80/443)
- Let's Encrypt integration (ACME HTTP-01 + DNS-01)
- Log capture and rotation
- Backup system (local + S3-compatible targets)

**Why fourth:** The dashboard is kern's killer feature, but it depends on having apps that can be built and deployed. After v0.3, kern apps are production-ready, which means kernd has something real to manage. Building the dashboard earlier would mean testing against toy apps. Building it later would delay the differentiating value proposition.

---

## v0.5 - Dashboard Polish

**Goal:** Turn the dashboard from functional to delightful. Add the integrations that eliminate manual VPS administration.

**Delivers:**
- Cloudflare DNS + CDN integration (auto-DNS, cache purge on deploy)
- Per-app stats and graphs (CPU, memory, network, request latency)
- Self-update mechanism for kernd
- Per-app environment variables and secrets UI (encrypted at rest)
- Multi-replica support per app (round-robin via reverse proxy)

**Why fifth:** The MVP dashboard (v0.4) handles the critical path: deploy and serve. v0.5 adds the operational polish that makes it a joy to use long-term. Cloudflare integration removes the need for manual DNS. Stats give visibility. Secrets management removes the need for SSH-ing in to set env vars. Replicas add basic horizontal scale within a single VPS.

---

## v0.6 - Scaling and Multi-Host

**Goal:** Support apps that outgrow a single VPS without requiring a complete architecture change.

**Delivers:**
- Redis session driver (shared sessions across instances)
- Redis queue driver (shared job queue across instances)
- Multi-replica app management
- kernd-to-kernd federation (cluster view across VPSes)

**Why sixth:** Most kern apps will run happily on a single VPS for a long time. But when they outgrow it, the path forward should be clear: add another VPS, run kernd, and the two instances federate. Redis as a session/queue backend is the prerequisite for stateless app instances that can run anywhere.

---

## v0.7 - Beyond HTTP/1.1

**Goal:** Modern protocol support for performance-sensitive deployments.

**Delivers:**
- HTTP/2 support (multiplexing, header compression, server push)
- WebSocket shard streams (polished, production-ready)
- HTTP/3 (QUIC) - experimental
- Zero-downtime deploys (pre-start hook: warm new process on a different port before swap)

**Why seventh:** HTTP/2 and HTTP/3 improve performance but are not required for correctness. By this point, kern apps are production-proven on HTTP/1.1. Adding protocol upgrades here means the ecosystem is stable enough to handle the complexity, and the testing infrastructure (from v0.2) can verify the new paths.

---

## v0.8 - Operator Happiness

**Goal:** Templates, infrastructure-as-code support, and deployment automation for teams.

**Delivers:**
- `kern new --template <name>` (blog, SaaS, API, admin presets)
- Helm chart for Kubernetes users
- Terraform module for AWS / Hetzner / DigitalOcean
- GitHub Actions workflow templates

**Why eighth:** Once kern has a user base with diverse needs, templates reduce boilerplate for common patterns. Infrastructure-as-code support acknowledges that some users will want Kubernetes or cloud providers, even though kernd is the recommended path. This is about meeting users where they are.

---

## v0.9 - Ecosystem

**Goal:** Editor integration and community ecosystem infrastructure.

**Delivers:**
- VSCode extension (LSP for C + `.khtml` syntax highlighting)
- Neovim plugin (same LSP, treesitter grammar for `.khtml`)
- `kern new --ai-template` (prebuilt AI chat app template)
- Plugin registry infrastructure

**Why ninth:** Editor support dramatically improves DX but requires a stable syntax and API to target. By v0.9, the `.khtml` syntax, the API surface, and the project conventions are frozen. The LSP can provide completions, diagnostics, and go-to-definition against a stable target.

---

## v1.0 - Stable Release

**Goal:** API stability guarantee, complete documentation, and production validation.

**Delivers:**
- API frozen (semver guarantees from this point)
- Complete documentation (all features, all APIs, all conventions)
- Migration guides from Rails, Phoenix, Laravel
- Production case studies (3+ real deployments)
- Performance benchmarks published
- Security audit completed

**Why last:** A stable release is a promise. Everything before v1.0 is allowed to change. By the time we reach v1.0, the framework has been battle-tested across multiple phases, multiple real deployments, and multiple rounds of feedback. The API has been refined through actual use, not guesswork.

---

## Post-1.0 (Public Roadmap)

These items are planned but not committed to a specific version:

- Alternative databases (libSQL/Turso, MySQL)
- Built-in static site generation
- Plugin registry (kern plugins vs. kernd plugins)
- Web-based admin generator (like `rails_admin`)
- `kernd ha` for active-passive kernd clusters
- Hosted kern offering (kern.dev SaaS)

---

## Phase Dependency Chain

```
v0.1 (Core)
  └── v0.2 (DX)
        └── v0.3 (Production)
              └── v0.4 (Dashboard MVP)
                    └── v0.5 (Dashboard Polish)
                          ├── v0.6 (Scaling)
                          └── v0.7 (Protocols)
                                └── v0.8 (Operator)
                                      └── v0.9 (Ecosystem)
                                            └── v1.0 (Stable)
```

Each phase is designed to be independently shippable. Users can start building real apps at v0.1, deploy at v0.3, and manage a fleet at v0.4. Later phases add polish and scale, but the core experience is complete early.
