<img width="2752" height="1536" alt="TortuLua" src="https://github.com/user-attachments/assets/aea98fc3-c16c-42e2-94d4-b7c94c94ae89" />

# TortuLua

**TortuLua** is the Lua 5.4 scripting engine for **ForeverCore**
([`Karrz1/TrinityCore`](https://github.com/Karrz1/TrinityCore), branch `forever`), loaded through `ScriptMgr` without
touching core C++ scripts. It supports both a sandboxed test toolkit and a clean-room Eluna-compatible API.

> [!WARNING]
> **Test servers only.** Whoever can write to `ForeverLua.ScriptPath` can run code in the server.
> Review the [Security Model](knowledge/architecture/security.md) and [Limits](knowledge/architecture/limits.md) before
> enabling it.

The module folder, config keys and log category are named `ForeverLua`.

**Status: 0.1.0-beta.** See [`CHANGELOG.md`](CHANGELOG.md) for what works and the known limits, and
[`docs/COVERAGE.md`](docs/COVERAGE.md) for the per-name Eluna compatibility matrix.

## Modes

- **`ForeverLua.Mode = "test"`** (default) — Sandboxed toolkit (`Event`, `Timer`, `Cmd`, `World`, `Db`, `Json`, `Gm`),
  strict execution/memory budgets, and read-only schema-validated queries.
- **`ForeverLua.Mode = "eluna"`** — Clean-room Eluna-compatible API (stages E0–E6) with handle classes,
  `Register*Event`, `CreateLuaEvent`, gossip menus, dedicated MySQL connections, and optional
  [Bot API](knowledge/modes/bot-api.md) integration (B1–B2).

## Build

```bash
# inside a ForeverCore checkout (link or clone at src/modules/ForeverLua)
cmake -S . -B build -DBUILD_FOREVER_LUA=1
cmake --build build -j"$(nproc)"

# standalone (engine and Catch2 v3 unit tests, no core needed)
cmake -S . -B build && cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure
```

- `BUILD_FOREVER_LUA=1` enables the module (off by default).
- Copy [`conf/forever_lua.conf.dist`](conf/forever_lua.conf.dist) to `forever_lua.conf` and set `ForeverLua.Enable = 1`.
- Point your editor's `.luarc.json` at [`types/`](types/README.md) for LuaLS (`sumneko`) type hints.

## Documentation

| Area | Topics |
|---|---|
| [Architecture](knowledge/architecture/index.md) | [Overview](knowledge/architecture/overview.md), [Security Model](knowledge/architecture/security.md), [Limits & Core Gaps](knowledge/architecture/limits.md) |
| [Modes & APIs](knowledge/modes/index.md) | [Test Mode API](knowledge/modes/test-mode.md), [Eluna Mode API](knowledge/modes/eluna-mode.md), [Bot API (B1–B2)](knowledge/modes/bot-api.md) |
| [Operations](knowledge/operations/index.md) | [Build, Test & Tooling](knowledge/operations/build-and-test.md), [Configuration Schema](knowledge/operations/configuration.md), [GM Commands](knowledge/operations/commands.md) |

The generated Eluna compatibility matrix is [`docs/COVERAGE.md`](docs/COVERAGE.md); the clean-room provenance log is
[`docs/ELUNA-CLEANROOM.md`](docs/ELUNA-CLEANROOM.md).

## Licence

Licensed under **GPL-2.0-or-later** ([`COPYING`](COPYING), [`NOTICE`](NOTICE)).

**Lua 5.4.9** (MIT, [`dep/lua/LICENSE`](dep/lua/LICENSE)) is vendored unchanged and compiled as C++
([`dep/lua/SOURCE.txt`](dep/lua/SOURCE.txt)). Eluna mode is a clean-room implementation written from published
documentation only ([`docs/ELUNA-CLEANROOM.md`](docs/ELUNA-CLEANROOM.md)). Authors: [`AUTHORS`](AUTHORS).
