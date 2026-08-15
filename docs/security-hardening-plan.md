# Security Hardening Plan

This document describes the phased approach to addressing the 7 issues
identified in the code review of kern v0.1.

**Status:** All 3 phases completed as of v0.2.

## Issues Summary

| # | Issue | Severity | Phase | Status |
|---|-------|----------|-------|--------|
| 1 | Weak password hashing (SHA-256) | High | 2 | ✅ Fixed — PBKDF2-HMAC-SHA256 |
| 2 | Unbounded HTTP parser buffers | High | 3 | ✅ Fixed — limits added |
| 3 | Session memory leak / no expiration | Medium | 2 | ✅ Fixed — TTL + cap |
| 4 | Dict value ownership gap | Medium | 1 | ✅ Fixed — `kern_dict_new_with_free` |
| 5 | Router 405 path leaks param values | Medium | 1 | ✅ Fixed — free_fn on temp dict |
| 6 | Static file traversal via symlinks | Medium | 3 | ✅ Fixed — realpath() check |
| 7 | Template codegen trusts expression content | Low | 3 | ✅ Documented |

## Phase 1: Foundation - Dict Ownership and Router Leak ✅

**Goal**: Fix the foundational data structure (`kern_dict_t`) so it can
properly manage the lifecycle of heap-allocated values. Then fix the
router 405 leak which depends on this capability.

### Changes

1. Add `kern_dict_new_with_free(void (*free_fn)(void *))` to `kern_dict_t`.
   - When `kern_dict_free` is called and `free_fn` is set, invoke it on
     each live value before releasing memory.
   - When `kern_dict_set` replaces an existing key's value, call `free_fn`
     on the old value.
   - When `kern_dict_del` removes an entry, call `free_fn` on the value.
   - `kern_dict_new()` continues to work as before (no destructor).

2. Update callers that store heap-allocated values to use the new variant:
   - `kern_http_parser.c` - header values (`parse_header_line`)
   - `kern_request.c` - query params, form params, route params
   - `kern_session.c` - session data dict
   - `kern_router.c` - param dicts used in `match_tree`

3. Fix the `path_exists_any_method` function in `kern_router.c`:
   - Use `kern_dict_new_with_free(free)` for the temp dict so that
     strndup'd param values are freed on dict destruction.

### Acceptance Criteria

- All 20 existing tests continue to pass.
- New tests verify that `kern_dict_new_with_free` correctly frees values.
- No memory leaks in the dict/router hot path (verified by code inspection).
- API is backward-compatible: `kern_dict_new()` behavior unchanged.

## Phase 2: Auth and Sessions

**Goal**: Replace weak SHA-256 hashing with PBKDF2-HMAC-SHA256 (100k iterations)
and add session TTL-based expiration with a max sessions cap.

### Changes

1. Implement PBKDF2-HMAC-SHA256 in `kern_auth.c`:
   - Use existing SHA-256 implementation as the PRF base.
   - 100,000 iterations, 16-byte salt, 32-byte derived key.
   - Output format: `pbkdf2-sha256:100000:hex_salt:hex_hash`
   - Keep backward-compatible verify that detects old format.

2. Add session expiration to `kern_session.c`:
   - Track creation time per session.
   - On `kern_session_start`, evict expired sessions (TTL default: 7 days).
   - Cap max sessions (default: 10,000); evict oldest when full.
   - Add `Secure` flag to cookie (configurable).

### Acceptance Criteria

- Password hash/verify round-trips with new format.
- Old hashes can still be verified (migration path).
- Sessions expire after TTL.
- Session count never exceeds the cap.

## Phase 3: HTTP Limits, Static File Safety, Template Docs

**Goal**: Harden the HTTP parser with resource limits, prevent symlink-based
path traversal in static file serving, and document the template trust model.

### Changes

1. HTTP parser limits in `kern_http_parser.c`:
   - `KERN_HTTP_MAX_REQUEST_LINE` = 8192 bytes
   - `KERN_HTTP_MAX_HEADERS` = 65536 bytes (total header section)
   - `KERN_HTTP_MAX_BODY` = 10 MB (configurable via server API)
   - Validate `Content-Length` against max body size.

2. Static file symlink protection in `kern_static.c`:
   - After building the full path, call `realpath()`.
   - Verify the resolved path starts with the canonical public directory.
   - Reject requests that escape the document root.

3. Template security documentation in `kern_tpl_codegen.c`:
   - Add a prominent comment block establishing `.khtml` files as trusted input.
   - Document that user-uploadable templates would allow arbitrary code execution.

### Acceptance Criteria

- Parser rejects oversized request lines, headers, and bodies with PARSE_ERROR.
- Static handler returns 403 for symlink-based traversal attempts.
- Template codegen source file contains clear security documentation.

---

## Implementation Notes

- All changes must compile cleanly with `-std=c11 -pedantic -Wall -Wextra`
  on both gcc 11.5 and clang 15.
- No external crypto libraries are available (no OpenSSL, no mbedTLS).
- `strdup`/`strndup` are available under `_GNU_SOURCE` which is already defined.
- The project uses a single public header (`include/kern.h`).
