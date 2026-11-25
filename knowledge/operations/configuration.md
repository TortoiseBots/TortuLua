---
type: Reference
title: Configuration Schema
description: All ForeverLua config keys, defaults, and mode-specific behavior.
tags: [configuration, conf, operations]
status: stable
---

# Schema

Copy `conf/forever_lua.conf.dist` to `forever_lua.conf` under section `[forever_lua]`.

| Key | Default | Notes |
|---|---|---|
| `ForeverLua.Enable` | `0` | Turns the module on (off by default). |
| `ForeverLua.Mode` | `"test"` | `"test"` (sandboxed toolkit) or `"eluna"` (Eluna-compatible API; trusts scripts). |
| `ForeverLua.ScriptPath` | `"lua_scripts"` | Root folder scripts load from; server user must own it. |
| `ForeverLua.RequirePath` | `""` | Eluna mode only: extra space-separated `require()` search folders. |
| `ForeverLua.OpenStdLibs` | `0` | Eluna mode only: `1` opens `io`/`os`/`debug`/`package` and keeps `dofile`/`loadfile`/`string.dump`. |
| `ForeverLua.CommandRoots` | `"tt"` | Space-separated roots forwarded whole to `Cmd.register` (adding a root requires restart). |
| `ForeverLua.MemoryLimitMB` | `128` | Lua state memory cap in MiB. |
| `ForeverLua.EventBudgetMs` | `20` (`test`) / `0` (`eluna`) | Wall-clock budget per event/timer call (`0` = off; Eluna disables clock/count hooks at `0`). |
| `ForeverLua.CommandBudgetMs` | `250` (`test`) / `0` (`eluna`) | Wall-clock budget per command call. |
| `ForeverLua.TickBudgetMs` | `25` (`test`) / `0` (`eluna`) | Maximum Lua time per world tick. |
| `ForeverLua.DbTimeoutMs` | `2000` | Client-side timeout (ms) of one `Db.select` call. |
| `ForeverLua.SyncDbTimeoutMs` | `5000` | Read/connect timeout (ms) of Eluna mode's own MySQL connections. |
| `ForeverLua.ShutdownBudgetMs` | `5000` | Maximum time (ms) the shutdown drain may spend. |
| `ForeverLua.MapStates` | `0` | `1` enables per-continent map states (`Mode = "eluna"` required). |
| `ForeverLua.SlowHandlerMs` | `5` | Threshold (ms) for logging slow handlers (`0` disables logging). |
| `ForeverLua.DataFlushMs` | `5000` | Write-behind flush interval (ms) for `ForeverData`. |
| `ForeverLua.AdminSecurity` | `3` | Minimum account security level for `.lua` commands (also requires RBAC 607). |
| `ForeverLua.BridgeDeny` | `"lua server account rbac bnetaccount pdump ban"` | Command roots refused by `Gm.run`. |
| `ForeverLua.BridgeSecurity` | `3` | RBAC security level used by the GM bridge. |

Related: [Security Model](../architecture/security.md), [GM Commands](commands.md), [Build, Test, and Tooling](build-and-test.md).
