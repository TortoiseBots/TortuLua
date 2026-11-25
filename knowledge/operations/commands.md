---
type: Reference
title: GM Commands
description: In-game .lua administration commands, permissions, and custom command roots.
tags: [commands, gm, operations, admin]
status: stable
---

# Admin Commands

All `.lua` commands require **RBAC 607** and `ForeverLua.AdminSecurity` (default `3`).

| Command | Behavior |
|---|---|
| `.lua status` | Reports memory usage, active handlers, disabled handlers, queue drops, and recent errors. |
| `.lua reload` | Builds a second Lua state and swaps it in atomically only if every file loads cleanly; otherwise keeps the old state. |
| `.lua run <name>` | Executes `<name>.lua` once. |
| `.lua errors [clear]` | Prints or clears recorded Lua errors. |
| `.lua profile [top N\|reset]` | Shows or resets per-handler call counts and timings (E3). |
| `.lua commands` | Lists active `RegisterCommand` slots. |

# Script-Registered Commands

- **Test mode (`ForeverLua.CommandRoots`):** Each configured root (default `tt`) is forwarded whole to the callback registered via `Cmd.register(root, fn, minSecurity)`.
- **Eluna mode (`RegisterCommand`):** Registers up to 32 top-level GM commands. The command map rebuilds on the next world tick after the command that modified the table returns (never re-entrantly).

Related: [Configuration Schema](configuration.md), [Test Mode API](../modes/test-mode.md), [Eluna Mode API](../modes/eluna-mode.md).
