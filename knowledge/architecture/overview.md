---
type: Architecture
title: TortuLua Architecture Overview
description: Dual-mode Lua 5.4 engine architecture, naming conventions, and map-state model.
tags: [architecture, overview, lua, forevercore]
status: stable
---

# Overview

**TortuLua** is a Lua 5.4.9 scripting module for **ForeverCore** (`Karrz1/TrinityCore`, branch `forever`), loaded through `ScriptMgr` without modifying core C++ AI scripts.

- **Naming:** The display name is **TortuLua**; the module directory, configuration keys, and log category retain the `ForeverLua` prefix.
- **Vendored runtime:** Lua 5.4.9 (MIT) is vendored unchanged and compiled as C++, with a documented pattern-complexity patch in `dep/lua/patches/`.

# Execution Modes

TortuLua runs in one of two mutually exclusive modes selected via `ForeverLua.Mode` (see [Configuration](../operations/configuration.md)):

- **`test` (default):** Sandboxed toolkit exposing namespaced tables (`Event`, `Timer`, `Cmd`, `World`, `Db`, `Json`, `Gm`), strict resource budgets, and no raw SQL. See [Test Mode API](../modes/test-mode.md).
- **`eluna`:** Clean-room Eluna-compatible API (stages E0-E6) with handle classes, `Register*Event`, `CreateLuaEvent`, gossip menus, dedicated MySQL connections, and optional Bot API integration. See [Eluna Mode API](../modes/eluna-mode.md) and [Bot API](../modes/bot-api.md).

# Threading and Map States

- **Default (`MapStates = 0`):** A single world Lua state guarded by one lock handles all events and timers.
- **Per-map states (`MapStates = 1`, Eluna mode only):** Continent maps each get their own Lua state created on their first map update (never under the core map lock). Map-bound events and timers execute on their map thread under their own lock, while player, server, group, guild, packet, and bot events remain in the world state.

See [Security Model](security.md) for isolation guarantees and [Limits and Core Gaps](limits.md) for current boundaries.
