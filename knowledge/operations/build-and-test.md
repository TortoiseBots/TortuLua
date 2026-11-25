---
type: Playbook
title: Build, Test, and Tooling
description: ForeverCore and standalone CMake builds, Catch2 tests, coverage, and LuaLS type hints.
tags: [build, cmake, tests, coverage, typehints]
status: stable
---

# Build

Inside a **ForeverCore** checkout, link or clone this repository at `<core>/src/modules/ForeverLua` and enable it via CMake:

```bash
cmake ... -DBUILD_FOREVER_LUA=1
```

Standalone (no core required — builds the engine and Catch2 v3 unit tests):

```bash
cmake -S . -B build && cmake --build build -j4 && ctest --test-dir build --output-on-failure
```

Add `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` for an ASan/UBSan sanitizer run.

Configure with `-DFOREVER_LUA_TSAN=ON` for a ThreadSanitizer build. In that build `ctest -R '^bot'` runs `bot_threads`,
the bot API suites (the tests that drive the world pump and a posting map state from two threads); the rest of the suite
shares the fake host's single-threaded world across test threads and is covered by the ordinary and ASan/UBSan runs. On
kernels where ASLR makes TSan abort at startup, prefix the run with `setarch -R`.

# Tests

- **Unit suite (`tests/`):** Catch2 v3 tests, `catch (...)` grep check, plus fake-world scripts under `tests/lua/*.lua` and `tests/eluna/*.lua`.
- **Live selftests:** `scripts/selftest/selftest.lua` (test mode), `scripts/selftest/eluna_selftest.lua` (Eluna mode), and `scripts/selftest/bot_spread_on_low_hp.lua` (Bot API B2).

# Coverage and Editor Type Hints

Regenerate and validate the Eluna API coverage report (`docs/COVERAGE.md`, `docs/coverage/coverage.json`) offline from `src/engine/elu/*.cpp` and `src/engine/EluAlias.cpp`:

```bash
python3 tools/coverage/gen_coverage.py --write
python3 tools/coverage/gen_coverage.py --check    # also enforced by ctest `coverage_report`
```

Regenerate and validate LuaLS (`sumneko`) `---@meta` editor definitions under `types/` (point `.luarc.json` `workspace.library` at `types/`):

```bash
python3 tools/typehints/gen_typehints.py
python3 tools/typehints/gen_typehints.py --check  # also enforced by ctest `typehints_check`
```

Related: [Configuration Schema](configuration.md), [GM Commands](commands.md).
