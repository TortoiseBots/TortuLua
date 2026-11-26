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

# Public Text Check

This repository is public, so `tools/check_public_text.py` fails when private or internal details reach a tracked text
file or a commit message. It scans every tracked text file except the vendored `dep/lua/` tree and binary files, plus the
commit messages of a range, for:

- IPv4 addresses outside the documentation ranges `192.0.2.0/24`, `198.51.100.0/24`, `203.0.113.0/24` and the
  literals `127.0.0.1` and `0.0.0.0`;
- home directories (`/home/<name>`, `C:\Users\<name>`);
- the regular expressions in [`tools/public_text_denylist.txt`](../../tools/public_text_denylist.txt) (one per line,
  case-insensitive; private infrastructure shapes and AI tool and model names);
- password literals: a `password` key with `=` or `:`, quoted or not, followed by a value of six or more characters
  (covers ini/TOML, YAML and JSON configuration).

```bash
python3 tools/check_public_text.py                       # tree plus the HEAD commit message
python3 tools/check_public_text.py --base <pr-base-sha>  # tree plus BASE..HEAD commit messages
python3 tests/tools/test_check_public_text.py            # the checker's own tests
```

The `public-text` CI job runs the tests and the check on every pull request and on pushes to `dev` and `main`. Fix real
hits in the text. If a hit is a false positive, add one `path:line-substring` line to
[`tools/public_text_allow.txt`](../../tools/public_text_allow.txt): the substring must occur literally on the reported
line of that exact path (the failure output prints the line to paste), be at least four characters long, and only the
finding it overlaps is suppressed — a second leak on the same line still fails. Commit messages cannot be allow-listed;
reword or rebase them out of the range. The denylist file is itself checked for addresses, home directories and
password literals (only its own expressions are exempt), and its required generic shapes are pinned by the checker's
tests. Never put a private repository name, host, account or path in either tools file — both are public.

Related: [Configuration Schema](configuration.md), [GM Commands](commands.md).
