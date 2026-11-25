---
type: Security Model
title: Security and Sandbox Model
description: Sandbox restrictions, execution budgets, async DB guard, and GM bridge limits.
tags: [security, sandbox, budgets, db]
status: stable
---

# Scope

TortuLua is designed for **test servers only**. Whoever can write to `ForeverLua.ScriptPath` can run code in the server process.

- In **`test` mode**, scripts run inside a hardened sandbox with strict budgets and read-only schema-validated queries.
- In **`eluna` mode**, scripts are trusted (raw SQL is enabled, execution budgets default to `0`, and `OpenStdLibs = 1` can open `io`/`os`/`debug`/`package`).

# Sandbox Controls

- **Restricted standard library:** Only `base`, `string`, `table`, `math`, `utf8`, and `coroutine` are opened by default. `io`, `os`, `debug`, and `package` are omitted (no `package.loadlib`); `dofile`, `loadfile`, and `string.dump` are removed; `load` accepts text only; custom `require` restricts paths to `[A-Za-z0-9_/]+` inside the script folder and refuses symlinks escaping it; `print` routes to the server log.
- **No `.lua eval` and no raw SQL (test mode):** `Db.select` constructs `SELECT` queries strictly from identifiers validated against an asynchronous `information_schema` snapshot, preventing core aborts on unknown tables/columns or SQL parse errors. Queries run on the asynchronous DB connection and answer via callback on a later tick, bounded by `DbTimeoutMs` (covering MariaDB, which ignores `MAX_EXECUTION_TIME`).
- **Execution budgets and memory cap:** Every call enforces instruction and wall-clock budgets (catching loops inside `pcall` or coroutines and checking the clock before every Lua call) plus `MemoryLimitMB`. Errors are logged and never escape the call.
- **Bounded C functions:** `string.rep`, `table.move`, `table.sort`, `table.insert`/`table.remove` (using raw length; mismatched `__len` is refused), `Json.encode`, `Db.select` (rows, bytes, and 32 in-flight cap), `load` (1 MB), and Lua pattern matching (1 MiB subject, 512-byte pattern, step counter aborting with `"pattern too complex"` via a vendored Lua patch) are all bounded.
- **Allocation and GC limits:** Single Lua allocations are capped at 4 MiB so no `malloc` stalls the world thread. `__gc` metatables are refused and `collectgarbage` allows only `count`/`step` (step clamped to 64 KB).
- **Guarded GM bridge:** `Gm.run` and `Player:run` reject `BridgeDeny` and forwarded command roots by prefix, forbid re-entry, and execute at `ForeverLua.BridgeSecurity` RBAC level rather than console privileges.

Related: [Configuration](../operations/configuration.md), [Limits and Core Gaps](limits.md), [Test Mode API](../modes/test-mode.md).
