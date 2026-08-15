# Phase 2.1 — Bootstrap CSS (Replace Tailwind)

**Status:** Planned
**Depends on:** v0.2 (shipped)
**Blocks:** v0.3

---

## Problem

v0.2 shipped a custom Tailwind v4 CSS compiler written in C (`kern_tailwind.c`, ~800 lines). The original plan was:

1. Write a Tailwind compiler (subset)
2. Build 25-30 custom components (shadcn-style)
3. Ship a first-class component library

**Reality:** Maintaining a custom Tailwind compiler + building a component library is not feasible for a solo maintainer. The Tailwind subset is incomplete, components were never built, and the maintenance burden grows with every Tailwind update.

## Decision

**Remove Tailwind entirely. Switch to Bootstrap 5 as the default CSS framework.**

### Why Bootstrap

| Reason | Detail |
|--------|--------|
| **Zero maintenance** | Bootstrap is maintained by a large team. We ship it, we don't build it. |
| **Known by agents + humans** | LLMs generate accurate Bootstrap code. Developers already know it. |
| **Rich component library** | Modals, navbars, cards, forms, tables — all built-in, production-ready. |
| **Responsive by default** | Grid system, breakpoints, utilities — no build step required. |
| **No compilation** | Pre-built CSS + JS. No scanner, no JIT, no class extraction. |
| **Customizable later** | v2 can add PurgeCSS/unused-CSS stripping if bundle size matters. |

### What Changes

**Removed:**
- `src/kern_tailwind.c` — the C Tailwind compiler (scanning, compilation, color palettes)
- `tests/test_tailwind.c` — Tailwind compiler tests
- `assets.tailwind` config key in `kern.toml`
- All Tailwind utility class scanning from `kern build`

**Added:**
- Bootstrap 5.3.x minified CSS + JS bundled in libkern (compiled into the binary)
- `kern_bootstrap_css()` — returns pointer to minified Bootstrap CSS (gzip-ready)
- `kern_bootstrap_js()` — returns pointer to minified Bootstrap JS bundle (includes Popper)
- `kern_bootstrap_serve(req)` — serves Bootstrap files with correct Content-Type, cache headers, and gzip encoding
- Auto-mount at `/assets/bootstrap.min.css` and `/assets/bootstrap.bundle.min.js`
- `views/layouts/base.khtml` scaffolded with Bootstrap CDN fallback or embedded assets

## Implementation Plan

### Step 1: Remove Tailwind

- [ ] Delete `src/kern_tailwind.c`
- [ ] Delete `tests/test_tailwind.c`
- [ ] Remove `kern_tw_*` declarations from `include/kern.h`
- [ ] Remove Tailwind compilation step from `cli/cmd_build.c`
- [ ] Remove `assets.tailwind` from `kern.toml` template in `cli/cmd_new.c`
- [ ] Remove Tailwind-related test entries from test runner
- [ ] Verify `kern build` works without Tailwind step

### Step 2: Embed Bootstrap

- [ ] Download Bootstrap 5.3.x minified CSS (`bootstrap.min.css`, ~22 KB gzip)
- [ ] Download Bootstrap 5.3.x minified JS bundle (`bootstrap.bundle.min.js`, ~22 KB gzip, includes Popper)
- [ ] Convert to C byte arrays (like `kern_shards.js` is embedded)
- [ ] Add `kern_bootstrap_css()` and `kern_bootstrap_js()` to libkern
- [ ] Add `kern_bootstrap_serve()` handler
- [ ] Auto-register routes: `GET /assets/bootstrap.min.css`, `GET /assets/bootstrap.bundle.min.js`
- [ ] Serve with `Content-Encoding: gzip`, `Cache-Control: public, max-age=31536000, immutable`
- [ ] Write tests for Bootstrap serving (correct content-type, gzip, caching headers)

### Step 3: Update Scaffolding

- [ ] Update `cli/cmd_new.c`:
  - Remove `assets/css/app.css` scaffold (or make it empty/placeholder)
  - Update `views/layouts/base.khtml` to include Bootstrap CSS + JS
  - Remove `[assets] tailwind = true` from `kern.toml` template
- [ ] Update `views/pages/home.khtml` scaffold to use Bootstrap classes

### Step 4: Update Docs

- [x] Update `phases-overview.md` — add phase 2.1 entry
- [x] Update `architecture.md` — replace Tailwind build step with Bootstrap serving
- [x] Update `project-layout.md` — remove Tailwind entry point references
- [x] Update `getting-started.md` — remove `tailwind = true`, show Bootstrap usage
- [x] Update `api-reference.md` — remove Tailwind API, add Bootstrap API
- [x] Update `docs/README.md` — update status
- [x] Update `khtml-syntax.md` — no changes needed (examples demonstrate template syntax, not CSS framework choice)

## Bootstrap Serving Model

```
Browser request: GET /assets/bootstrap.min.css
       │
       ▼
kern HTTP server
       │
       ├── Check: Accept-Encoding includes gzip?
       │   ├── Yes → serve pre-compressed gzip bytes
       │   └── No  → serve raw minified CSS
       │
       └── Headers:
           Content-Type: text/css
           Content-Encoding: gzip (if applicable)
           Cache-Control: public, max-age=31536000, immutable
           ETag: "<hash>"
```

Bootstrap files are compiled into the binary (static byte arrays). No disk I/O at runtime. No external CDN dependency. The app works offline and has zero external dependencies.

## v0.2 → v2.0 Evolution Path

| Version | Bootstrap Approach |
|---------|-------------------|
| v2.1 (now) | Full Bootstrap 5.3.x, all components available |
| v2.0 (future) | Add PurgeCSS-style unused CSS stripping for smaller bundles |
| v2.0+ | Optional: `kern new --tailwind` to opt back into Tailwind (if community demand) |

## Developer Experience After This Change

```pug
// views/pages/home.khtml
extend layouts/base

block body
  div(class="container py-5")
    h1(class="display-4 fw-bold") Welcome to MyApp
    p(class="lead text-muted") Built with kern.
    a(href="/posts" class="btn btn-primary btn-lg") View Posts
```

No build step. No CSS compilation. No class scanning. Bootstrap utilities + components just work.

## FAQ

**Q: What about bundle size?**
A: Bootstrap 5.3.x minified + gzipped is ~44 KB total (CSS + JS). This is smaller than most hero images. v2.0+ can add unused-CSS stripping if needed.

**Q: What about custom styling?**
A: Users can still add their own `assets/css/custom.css` which loads after Bootstrap. Bootstrap's CSS variables make theming straightforward.

**Q: Do we still support Tailwind?**
A: Not by default. If there's community demand, a future version can add `kern new --tailwind` as an opt-in. The Tailwind compiler code will be preserved in git history.

**Q: What about the Shards system?**
A: Shards (`kern-shards.js`) is unaffected. It handles HTMX-style interactivity. Bootstrap handles styling and components. They complement each other.
