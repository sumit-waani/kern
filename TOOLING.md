# kern Tooling Guide

## Build System

kern uses **CMake** internally for building libkern, the CLI, and tests. User projects use the `kern` CLI directly (no CMakeLists.txt exposed to users).

### Building the framework (development)

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Dependencies

| Dependency | Required | Notes |
|-----------|----------|-------|
| libuv | Yes | Event loop, async I/O. Install via package manager (`libuv1-dev` on Debian/Ubuntu). |
| SQLite3 | Yes | Database driver. Usually pre-installed. (`libsqlite3-dev` on Debian/Ubuntu). |
| CMake 3.16+ | Yes | Build system generator. |
| GCC 11+ or Clang 15+ | Yes | C11 compiler with `-pedantic` support. |

### Compiler Flags

```
-std=c11 -pedantic -Wall -Wextra
```

No exceptions. If a third-party header triggers warnings, suppress per-file with `set_source_files_properties()` in CMakeLists.txt (see `kern_shard_static.c` for an example).

## Testing

```bash
cd build
ctest --output-on-failure
```

Or run individual tests:
```bash
./test_dict
./test_router
```

### Test Structure

- `tests/test_<module>.c` — one file per module.
- Each test file includes `kern_test.h` and defines `void test_<thing>(void)` functions.
- Tests register via `kern_test_run(test_fn, "description")` in `main()`.
- Exit code 0 = all pass, 1 = failures.

## CLI (kern)

The CLI binary is built as `kern_cli` (output name: `kern`). It links against libkern.

### Commands

| Command | Status | Description |
|---------|--------|-------------|
| `kern new <name>` | ✅ Implemented | Scaffold a new project |
| `kern dev` | ✅ Implemented | Development server with hot-reload |
| `kern build` | ✅ Implemented | Production build → `dist/<appname>` |
| `kern test` | ✅ Implemented | Run project tests |
| `kern fmt` | ✅ Implemented | Format C and `.khtml` files |
| `kern db.migrate` | ✅ Implemented | Run pending migrations |
| `kern routes` | 📋 Planned | List registered routes |
| `kern db.status` | 📋 Planned | Show migration state |
| `kern db.rollback` | 📋 Planned | Roll back last migration |
| `kern doctor` | 📋 Planned (v0.3) | Project diagnostics |
| `kern --help` | ✅ Implemented | Show help |
| `kern --version` | ✅ Implemented | Show version |

## Project Structure

```
kern/
├── CMakeLists.txt        # Build config (internal)
├── CONVENTIONS.md        # Coding standards
├── TOOLING.md            # This file
├── kern-design.md        # Full design specification
├── include/
│   ├── kern.h            # Public API header (single header)
│   └── kern_test.h       # Test framework header
├── src/                  # libkern source (~8.5K LOC)
├── cli/                  # CLI source (~1.4K LOC)
├── tests/                # Test source (~6K LOC, 25 test files)
├── static/               # Embedded static assets (kern-shards.js)
├── tests/fixtures/       # Test fixture files
└── docs/                 # Documentation
```

## Adding a New Module

1. Create `src/kern_<module>.c` with implementation.
2. Add public API declarations to `include/kern.h`.
3. Create `tests/test_<module>.c` with tests.
4. Add the source file to `CMakeLists.txt` (`add_library(kern ...)` list).
5. Add the test executable to `CMakeLists.txt` (`set(KERN_TESTS ...)` list).
6. Run `cmake .. && make && ctest` — all tests must pass.
7. Update docs if the public API changed.

## Code Review Checklist

- [ ] Builds with `-std=c11 -pedantic -Wall -Wextra` on gcc and clang
- [ ] No memory leaks (valgrind clean)
- [ ] Public API documented in kern.h with doxygen-style comments
- [ ] Tests added for new functionality
- [ ] Naming follows CONVENTIONS.md
- [ ] No magic strings in public API (use C identifiers)
