# kern Coding Conventions

This file defines the coding standards for the kern framework (libkern + CLI). All contributions must follow these rules.

## Language Standard

- **C11** (`-std=c11 -pedantic -Wall -Wextra`)
- No GNU extensions. Code must build with both `gcc` and `clang`.
- `_GNU_SOURCE` is defined for POSIX types (libuv requires it on Linux). Do not use GNU-specific APIs beyond what's already in the codebase.

## Naming

| Thing | Convention | Example |
|-------|-----------|---------|
| Files | `snake_case.c`, `snake_case.h` | `kern_http_parser.c` |
| Types (struct) | `snake_case_t` | `kern_buf_t`, `post_t` |
| Functions (public) | `kern_module_action()` | `kern_buf_write()`, `kern_dict_get()` |
| Functions (internal) | `snake_case()` | `parse_header_line()`, `hex_to_bytes()` |
| Constants | `KERN_UPPER_SNAKE` | `KERN_HTTP_MAX_HEADERS` |
| Macros | `KERN_` prefix | `KERN_PAGE`, `KERN_STR` |
| Enum values | `KERN_PREFIX_VALUE` | `KERN_METHOD_GET`, `KERN_ROUTE_OK` |
| Template files | `snake_case.khtml` | `post_show.khtml` |
| DB tables | `snake_case`, plural | `users`, `posts` |
| DB columns | `snake_case` | `created_at`, `author_id` |

## Header Structure

Every `.c` file starts with:
```c
/*
 * kern_<module>.c - <one-line description>
 *
 * <optional multi-line detail>
 */

#include "kern.h"

#include <stdio.h>   // system headers after kern.h
#include <stdlib.h>
```

## Memory

- Public APIs that return allocated memory document who owns it (caller frees, or "opaque — use `kern_*_free()`").
- Use `kern_arena_t` for per-request allocations where possible.
- Every `malloc` must have a corresponding `free` path. No leaks.

## Error Handling

- Return `int` (0 = success, -1 = failure) for functions that can fail.
- Return `NULL` for allocation failures.
- Log errors via `kern_log_error()` before returning failure.

## Testing

- One test file per module: `tests/test_<module>.c`.
- Each test function is `void test_<thing>(void)`.
- Use `ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_NULL`, `ASSERT_NOT_NULL` from `kern_test.h`.
- Tests must pass with valgrind (no leaks, no invalid reads).

## Formatting

- 4-space indentation, no tabs.
- Opening brace on the same line as the control structure.
- Blank line between function definitions.
- Max line length: 100 characters (soft limit).

## Commits

- Prefix: `feat:`, `fix:`, `docs:`, `chore:`, `refactor:`, `test:`
- Imperative mood ("add feature" not "added feature")
- Short subject line (< 72 chars), blank line, then body if needed.
