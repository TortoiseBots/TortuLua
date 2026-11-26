---
type: Reference
title: Limits and Core Gaps
description: Known runtime limits in test mode, Eluna mode, and MapStates.
tags: [limits, caveats, hooks, mapstates]
status: stable
---

# Test Mode and General Limits

- **Event timing and scope:** Death events arrive one tick late; `creature.spawn` covers Lua spawns only; there is no chat veto and no packet hook in test mode.
- **Database guard:** `Db.select` is read-only (`LIMIT 500`, `maxResultBytes`, max 32 in-flight requests; excess returns `busy`).
- **GM bridge:** Commands executed through `Gm.run` may reach handlers that inspect `GetSession()` as if invoked from console; keep the module on test servers only.
- **Pattern complexity:** Matching near the 1 MiB subject / 512-byte pattern caps can hit `"pattern too complex"` inside a 20 ms event budget; raise the event budget if scripts legitimately scan large text.

# Eluna Mode Core-Pin Gaps

Eluna mode (E2-E6) shares the test-server boundary plus gaps imposed by the pinned ForeverCore version:

- **Missing core hooks:** Group `CREATE` requires core hook C10; map `LOAD`, `CREATURE_CREATE`, `GAMEOBJECT_CREATE`, and `CHECK_ENCOUNTER` on dungeon/raid maps require C11 (`InstanceScript` forwarding).
- **Packets:** `RegisterPacketEvent` runs on a read-only copy from `ServerScript` (returning `false` cannot alter the wire packet and logs once). `Player`/`Group`/`Guild:SendPacket` work via `CreatePacket`; `SendPacket` takes any opcode and can send to any player, group, guild or creature set with no allowlist, so scripts are trusted code.
- **Taxi paths:** `AddTaxiPath` raises an error because taxi paths are DB2-backed and cannot be extended by scripts.
- **Classic 1.60 stubs:** Guild bank, vehicle, achievement, arena, dual spec, LFG, and destructible gameobject methods/events are documented stubs (see the per-symbol notes in [`docs/COVERAGE.md`](../../docs/COVERAGE.md)).

# MapStates Constraints

- `MapStates = 1` requires `Mode = "eluna"`.
- Only continent maps receive a dedicated map state (created lazily on the map's first update, never under the core map lock).
- Dungeon and raid maps keep their events in the world state; a battleground map requires its `battleground_scripts` row naming `forever_lua_bg_map_<id>`.
- With the default `MapStates = 0`, one lock and one world state behave as in E0-E3.

Related: [Architecture Overview](overview.md), [Eluna Mode API](../modes/eluna-mode.md).
