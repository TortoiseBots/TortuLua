# Changelog

## 0.1.0-beta

First public release of TortuLua, the Lua 5.4 scripting engine for ForeverCore, loaded as an optional module through
`ScriptMgr` (`-DBUILD_FOREVER_LUA=1`). GPL-2.0-or-later.

### What works

* **Two modes**, chosen with `ForeverLua.Mode`:
  * `test` (default): the sandboxed toolkit (`Event`, `Timer`, `Cmd`, `World`, `Db`, `Json`, `Gm`) with strict
    execution and memory budgets and read-only, schema-validated queries.
  * `eluna`: a clean-room Eluna-compatible API, written from Eluna's published documentation only (see
    `docs/ELUNA-CLEANROOM.md`). Stages E0-E6: handle classes, `Register*Event`, `CreateLuaEvent`, gossip menus,
    dedicated MySQL connections, `RegisterCommand` and `ForeverData`, per-map Lua states (`MapStates`), packets,
    battleground and map scripts, and the optional core hooks C1-C4 (detected at build time).
* **Bot API interface (B1-B2):** read access to bot snapshots, bot events and queued bot orders through the
  `BotProvider` interface (`src/engine/BotProvider.h`).
* **Tests and CI:** a standalone Catch2 unit suite (no core needed), an AddressSanitizer/UBSan job and a
  ThreadSanitizer job (bot snapshot and event paths) run on every push, and a public-text job that fails pull requests
  and `dev`/`main` pushes when a tracked text file or commit message carries an IP address, a home directory, a
  denylisted word or a password literal (`tools/check_public_text.py`).
* **Editor support:** LuaLS type hints for the Eluna surface in `types/`.

### Eluna coverage

Copied from `docs/COVERAGE.md` (regenerate with `tools/coverage/gen_coverage.py --write`). The report is computed for a
core without the optional C1-C4 core hooks.

| Family | Documented | Implemented | Stub | Missing | Implemented % | Resolvable % |
|---|---|---|---|---|---|---|
| Global | 110 | 94 | 4 | 12 | 85% | 89% |
| Object | 30 | 14 | 0 | 16 | 47% | 47% |
| WorldObject | 51 | 23 | 0 | 28 | 45% | 45% |
| Unit | 167 | 132 | 3 | 32 | 79% | 81% |
| Player | 263 | 195 | 20 | 48 | 74% | 82% |
| Creature | 94 | 87 | 0 | 7 | 93% | 93% |
| GameObject | 20 | 9 | 0 | 11 | 45% | 45% |
| Item | 66 | 56 | 4 | 6 | 85% | 91% |
| Group | 27 | 6 | 0 | 21 | 22% | 22% |
| Guild | 16 | 4 | 1 | 11 | 25% | 31% |
| Quest | 13 | 4 | 0 | 9 | 31% | 31% |
| Spell | 40 | 40 | 0 | 0 | 100% | 100% |
| Aura | 14 | 14 | 0 | 0 | 100% | 100% |
| Map | 19 | 17 | 0 | 2 | 89% | 89% |
| ElunaQuery | 17 | 17 | 0 | 0 | 100% | 100% |
| WorldPacket | 23 | 9 | 0 | 14 | 39% | 39% |
| Corpse | 5 | 5 | 0 | 0 | 100% | 100% |
| BattleGround | 18 | 18 | 0 | 0 | 100% | 100% |
| Vehicle | 6 | 0 | 6 | 0 | 0% | 100% |
| AuraEffect | 23 | 0 | 0 | 23 | 0% | 0% |
| ElunaProcInfo | 28 | 0 | 0 | 28 | 0% | 0% |
| SpellInfo | 190 | 106 | 0 | 84 | 56% | 56% |
| events: server | 34 | 31 | 3 | 0 | 91% | 100% |
| events: player | 51 | 27 | 24 | 0 | 53% | 100% |
| events: creature | 28 | 27 | 1 | 0 | 96% | 100% |
| events: gameobject | 13 | 10 | 3 | 0 | 77% | 100% |
| events: item | 8 | 4 | 1 | 3 | 50% | 62% |
| events: player gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: creature gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: gameobject gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: item gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: group | 7 | 5 | 1 | 1 | 71% | 86% |
| events: guild | 11 | 9 | 2 | 0 | 82% | 100% |
| events: map | 7 | 3 | 4 | 0 | 43% | 100% |
| events: instance | 7 | 3 | 4 | 0 | 43% | 100% |
| events: packet | 2 | 2 | 0 | 0 | 100% | 100% |
| events: battleground | 4 | 4 | 0 | 0 | 100% | 100% |
| events: spell | 23 | 0 | 0 | 23 | 0% | 0% |
| **all** | **1443** | **983** | **81** | **379** | **68%** | **74%** |

### Known limits

* **Test servers only.** Whoever can write to `ForeverLua.ScriptPath` runs code inside the server. Eluna mode trusts its
  scripts and has raw SQL on the module's own connections, and the GM bridge can reach command handlers that expect a
  console. Read the [Security Model](knowledge/architecture/security.md) and [Limits](knowledge/architecture/limits.md)
  before enabling the module.
* **Low-coverage classes.** AuraEffect, ElunaProcInfo and the spell events are not implemented (0%); Group (22%),
  Guild (25%), Quest (31%), WorldPacket (39%), GameObject (45%), WorldObject (45%), Object (47%) and Player (74%) are
  only partly covered. Classic-era systems (guild bank, vehicles, achievements, arena, dual spec, LFG) are documented
  stubs that raise an error; a handful of documented Player calls the core has no API for are stubs that raise
  `not available on this core`.
* **Core hooks.** Some events need the optional core hooks C1-C4 (and further hooks for instance maps); without them
  they register and log `needs core hook Cn` once.
* **The bot API needs a provider module.** Without a bot module that implements `BotProvider`, `IsBotApiAvailable()`
  returns `false` and the bot globals raise an error.
* **Beta.** The API and the configuration keys can still change before 1.0.
