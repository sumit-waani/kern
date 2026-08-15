# Phases Overview (v0.1 through v0.7)

This document describes the full roadmap from initial proof-of-concept to stable release. Each phase builds on the previous one, and the ordering is intentional: earlier phases establish foundations that later phases depend upon.

---

## v0.1 - Proof of Core ✅

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

**Status:** Shipped.

---

## v0.2 - Developer Experience ✅

**Goal:** Make daily development pleasant and productive. Polish the tools, add testing, and deliver the first interactive features.

**Delivers:**
- `kern fmt` - code formatter for C and `.khtml` files
- `kern test` - test framework with unit and integration support
- Tailwind v4 CSS compilation (C implementation, no Node)
- Shards system (HTMX-style server-rendered interactivity)
- `kern-shards.js` runtime (~10 KB, vanilla ES2020)

**Status:** Shipped.

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
- Per-IP and per-route rate limiting (token bucket)

**Why third:** A real app needs to send emails (signups, password resets), run background jobs (cleanup, notifications), and handle authorization beyond "is the user logged in?" Postgres support unlocks apps that need concurrent writes. Rate limiting hardens the app for public traffic. These are pre-requisites for any production deployment.

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

**Why fifth:** The MVP dashboard (v0.4) handles the critical path: deploy and serve. v0.5 adds the operational polish that makes it a joy to use long-term. Cloudflare integration removes the need for manual DNS. Stats give visibility. Secrets management removes the need for SSH-ing in to set env vars.

---

## v0.6 - Modern Protocols

**Goal:** Protocol upgrades and zero-downtime deployments for production apps that need better performance and reliability.

**Delivers:**
- HTTP/2 support (multiplexing, header compression)
- WebSocket shard streams (polished, production-ready)
- Zero-downtime deploys (pre-start hook: warm new process on a different port before swap)

**Why sixth:** HTTP/2 improves performance for multi-resource loads but is not required for correctness. By this point, kern apps are production-proven on HTTP/1.1. Adding HTTP/2 here means the ecosystem is stable enough to handle the complexity, and the testing infrastructure (from v0.2) can verify the new paths. Zero-downtime deploys are critical for apps serving real traffic — no user should see a 502 during a deploy.

---

## v0.7 - Stable Release

**Goal:** API stability guarantee, complete documentation, and production validation.

**Delivers:**
- API frozen (semver guarantees from this point)
- Complete documentation (all features, all APIs, all conventions)
- Project templates via `kern new --template <name>` (blog, SaaS, API, admin presets)
- Migration guides from Rails, Phoenix, Laravel
- Production case studies (3+ real deployments)
- Performance benchmarks published
- Security audit completed

**Why last:** A stable release is a promise. Everything before v0.7 is allowed to change. By the time we reach v0.7, the framework has been battle-tested across multiple phases, multiple real deployments, and multiple rounds of feedback. The API has been refined through actual use, not guesswork. Templates for common project types reduce boilerplate for new users discovering kern.

---

## Post-v0.7 (Public Roadmap)

These items are planned but not committed to a specific version. They will be prioritized based on real-world usage and community feedback.

**Framework:**
- Alternative databases (libSQL/Turso, MySQL)
- Built-in static site generation
- Plugin registry (kern plugins vs. kernd plugins)
- Web-based admin generator (like `rails_admin`)

**Dashboard & Operations:**
- Multi-replica support per app (round-robin via reverse proxy)
- Redis session driver (shared sessions across instances)
- Redis queue driver (shared job queue across instances)
- kernd-to-kernd federation (cluster view across VPSes)
- `kernd ha` for active-passive kernd clusters
- HTTP/3 (QUIC) support

**Infrastructure & Ecosystem:**
- VSCode extension (LSP for C + `.khtml` syntax highlighting)
- Neovim plugin (same LSP, treesitter grammar for `.khtml`)
- Helm chart for Kubernetes users
- Terraform module for AWS / Hetzner / DigitalOcean
- GitHub Actions workflow templates
- Hosted kern offering (kern.dev SaaS)

---

## Phase Dependency Chain

```
v0.1 (Core) ✅
  └── v0.2 (DX) ✅
        └── v0.3 (Production)
              └── v0.4 (Dashboard MVP)
                    └── v0.5 (Dashboard Polish)
                          └── v0.6 (Modern Protocols)
                                └── v0.7 (Stable)
```

Each phase is designed to be independently shippable. Users can start building real apps at v0.1, deploy at v0.3, and manage a fleet at v0.4. Later phases add polish and protocol upgrades, but the core experience is complete early.
