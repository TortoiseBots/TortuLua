# Changelog

## Unreleased

* **Eluna API coverage (0.3.0, part of #17):** `Creature` 87 -> 90 implemented, 0 -> 4 stubs, 7 -> 0 missing
  (93% -> 96% implemented, 93% -> 100% resolvable). The seven documented Creature methods are implemented or honest
  core stubs, and every remaining `Object`, `Item`, `Guild` and `Map` stub is re-checked against the pinned core with
  its reason recorded in `docs/COVERAGE.md`.

## 0.2.0-beta

Second beta release of TortuLua, expanding Eluna API coverage across object, world, spell, packet, and event families,
tightening runtime safety guards, and updating the Bot API to interface version 2.

### What changed

* **Eluna API coverage:** raised coverage across `Group`, `Guild`, `Object`, `WorldObject`, `GameObject`, `Player`,
  `Unit`, `Item`, `Global`, `Quest`, `Map`, `SpellInfo`, `AuraEffect`, `ElunaProcInfo`, and `WorldPacket` (including
  template binding coverage verification for `WorldPacket`), plus honest stub registrations for the remaining `Item`,
  `Group`, and `Spell` event families.
* **Bot API interface version 2 (`kBotApiVersion = 2`):** added `RunEventKind::Lost` (`lost` drop count) and event 10
  `RUN_LOST` / `bot.run_lost(count)`, one final `takeRunEvents()` call when the provider becomes unavailable, and a final
  `pumpBots()` inside `stopEngine()` before `server.stop`.
* **Host build fix:** `Guild:AddMember` now validates an explicit rank through the core's public `Guild::ChangeMemberRank`
  API instead of private `GetRankInfo` calls, leaving unknown ranks at the default rank with a logged warning.
* **Safety limits and input guards:**
  * `Unit:SetName` refuses pet handles and non-creatures and caps names at 64 bytes.
  * `Player:SetQuestStatus` only writes when the quest has a quest-log slot, and `Player:BindToInstance` requires an
    instance map with its instance script and instance lock.
  * `Player:AdvanceSkill` and `Player:AdvanceAllSkills` require a non-zero 16-bit step capped to the remaining room
    before the skill maximum; `Player:SetGuildRank` refuses rank `0`; `Player:DurabilityRepair` and
    `DurabilityRepairAll` clamp repair discount factors to finite `[0, 1]`.
  * Packet and chat strings are capped to their wire limits (`GossipSendPOI` icon text at 63 bytes, addon message
    prefix at 31 bytes and text at 255 bytes, and `Unit:SendUnitEmote` / `WorldObject:SendUnitSay`/`Yell`/`Whisper` at
    255 bytes), and `SummonPet`, `GossipSendPOI`, and `WorldObject:GetNearObjects`/`GetNearObject` validate finite
    coordinates and ranges.
  * Group and guild writes—including `Player` social writes and `Player:RunCommand`—route through the host's serialized,
    re-entry-guarded social write path.
* **CI public-text check:** added `tools/check_public_text.py` (including checks for forward-slash Windows, macOS, and
  root paths plus author emails) to fail pull requests and `dev`/`main` pushes if private text reaches the public repo.

### Eluna coverage

Copied from `docs/COVERAGE.md` (regenerate with `tools/coverage/gen_coverage.py --write`). The report is computed for a
core without the optional C1-C4 core hooks.

| Family | Documented | Implemented | Stub | Missing | Implemented % | Resolvable % |
|---|---|---|---|---|---|---|
| Global | 110 | 106 | 4 | 0 | 96% | 100% |
| Object | 30 | 13 | 17 | 0 | 43% | 100% |
| WorldObject | 51 | 49 | 2 | 0 | 96% | 100% |
| Unit | 167 | 164 | 3 | 0 | 98% | 100% |
| Player | 263 | 241 | 22 | 0 | 92% | 100% |
| Creature | 94 | 87 | 0 | 7 | 93% | 93% |
| GameObject | 20 | 19 | 1 | 0 | 95% | 100% |
| Item | 66 | 58 | 8 | 0 | 88% | 100% |
| Group | 27 | 27 | 0 | 0 | 100% | 100% |
| Guild | 16 | 14 | 2 | 0 | 88% | 100% |
| Quest | 13 | 13 | 0 | 0 | 100% | 100% |
| Spell | 40 | 40 | 0 | 0 | 100% | 100% |
| Aura | 14 | 14 | 0 | 0 | 100% | 100% |
| Map | 19 | 17 | 2 | 0 | 89% | 100% |
| ElunaQuery | 17 | 17 | 0 | 0 | 100% | 100% |
| WorldPacket | 23 | 23 | 0 | 0 | 100% | 100% |
| Corpse | 5 | 5 | 0 | 0 | 100% | 100% |
| BattleGround | 18 | 18 | 0 | 0 | 100% | 100% |
| Vehicle | 6 | 0 | 6 | 0 | 0% | 100% |
| AuraEffect | 23 | 23 | 0 | 0 | 100% | 100% |
| ElunaProcInfo | 28 | 27 | 1 | 0 | 96% | 100% |
| SpellInfo | 190 | 186 | 4 | 0 | 98% | 100% |
| events: server | 34 | 31 | 3 | 0 | 91% | 100% |
| events: player | 51 | 27 | 24 | 0 | 53% | 100% |
| events: creature | 28 | 27 | 1 | 0 | 96% | 100% |
| events: gameobject | 13 | 10 | 3 | 0 | 77% | 100% |
| events: item | 8 | 4 | 4 | 0 | 50% | 100% |
| events: player gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: creature gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: gameobject gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: item gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: group | 7 | 5 | 2 | 0 | 71% | 100% |
| events: guild | 11 | 9 | 2 | 0 | 82% | 100% |
| events: map | 7 | 3 | 4 | 0 | 43% | 100% |
| events: instance | 7 | 3 | 4 | 0 | 43% | 100% |
| events: packet | 2 | 2 | 0 | 0 | 100% | 100% |
| events: battleground | 4 | 4 | 0 | 0 | 100% | 100% |
| events: spell | 23 | 0 | 23 | 0 | 0% | 100% |
| **all** | **1443** | **1294** | **142** | **7** | **90%** | **100%** |

`AuraEffect` and `ElunaProcInfo` are implemented on the engine and host, and `RegisterSpellEvent` accepts spell event
registrations as honest stubs (`needs core hook C14`), so `AuraEffect` and `ElunaProcInfo` remain unreachable from
scripts at runtime until core spell hooks land.

### Known limits

* **Test servers only.** Whoever can write to `ForeverLua.ScriptPath` runs code inside the server. Eluna mode trusts its
  scripts and has raw SQL on the module's own connections, and the GM bridge (`Gm.run`) can reach command handlers that
  expect a console. Read the [Security Model](knowledge/architecture/security.md) and [Limits](knowledge/architecture/limits.md)
  before enabling the module.
* **Spell events and unreachable spell classes.** All 23 spell events registered via `RegisterSpellEvent` are honest
  stubs until the core has spell hooks (`needs core hook C14`, as ForeverCore's `SpellScript` is database-bound via
  `spell_script_names`). Because those hooks do not exist yet, `AuraEffect` and `ElunaProcInfo` are implemented on the
  engine and host but remain unreachable from scripts at runtime.
* **Vehicle and Classic-era stubs.** `Vehicle` is all stubs (0% implemented; no vehicles in this Classic 1.60 core),
  along with guild bank, achievement, arena, dual spec, LFG, and destructible gameobject methods and events. `AddTaxiPath`
  raises an error because taxi paths are DB2-backed and cannot be extended by scripts. Documented methods the pinned core
  has no API for (across `Object`, `WorldObject`, `Player`, `Item`, `GameObject`, `Guild`, `Map`, `SpellInfo`, and
  `ElunaProcInfo`) are stubs that raise `not available on this core` (or `not available on this server`); `Object` (43%)
  and `Creature` (93%, 7 missing) remain only partly covered.
* **Event stubs and core hooks.** Events listed as stubs in [`docs/COVERAGE.md`](docs/COVERAGE.md) name the core hook
  they need when registered (logging `needs core hook Cn` once): `C4` (player `COMMAND`), `C5` (player
  combat/emote/equip/item/trade/mail/talent/spell hooks), `C6` (player chat hooks), `C7` (`GAME_EVENT_START`/`STOP`),
  `C8` (`TRIGGER_ON_TRIGGER`), `C9` (`DUMMY_EFFECT`), `C10` (group `CREATE`/`MEMBER_ACCEPT`), `C11` (map/instance
  `LOAD`/`CREATURE_CREATE`/`GAMEOBJECT_CREATE`/`CHECK_ENCOUNTER`), `C13` (item `ADD`/`EQUIP`/`UNEQUIP`), and `C14`
  (spell events). Conditional hooks `C1`–`C3` fall back to the `ScriptName` path (`forever_lua`,
  `forever_lua_gameobject`, `forever_lua_item`) when absent.
* **Packets (`SendPacket` and `RegisterPacketEvent`).** `SendPacket` has no opcode allowlist and can send to any player,
  group, guild, or creature set, so scripts are trusted code. `RegisterPacketEvent` runs on a read-only copy from
  `ServerScript`; returning `false` cannot alter the wire packet and logs once.
* **Test-mode and `MapStates` limits.** In test mode, death events arrive one tick late, `creature.spawn` covers Lua
  spawns only, there is no chat veto and no packet hook, `Db.select` is read-only (`LIMIT 500`, `maxResultBytes`, max 32
  in-flight requests), and pattern matching near the 1 MiB subject / 512-byte pattern caps can hit
  `"pattern too complex"` inside a 20 ms event budget. `MapStates = 1` requires `Mode = "eluna"`, creates dedicated
  continent map states lazily (dungeon/raid maps stay in the world state; battlegrounds require `battleground_scripts`
  naming `forever_lua_bg_map_<id>`).
* **The bot API needs a provider module.** Without a bot module that implements `BotProvider`, `IsBotApiAvailable()`
  returns `false` and the bot globals raise an error.
* **Beta.** The API and the configuration keys can still change before 1.0.

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
| Global | 110 | 106 | 4 | 0 | 96% | 100% |
| Object | 30 | 13 | 17 | 0 | 43% | 100% |
| WorldObject | 51 | 49 | 2 | 0 | 96% | 100% |
| Unit | 167 | 164 | 3 | 0 | 98% | 100% |
| Player | 263 | 241 | 22 | 0 | 92% | 100% |
| Creature | 94 | 87 | 0 | 7 | 93% | 93% |
| GameObject | 20 | 19 | 1 | 0 | 95% | 100% |
| Item | 66 | 58 | 8 | 0 | 88% | 100% |
| Group | 27 | 27 | 0 | 0 | 100% | 100% |
| Guild | 16 | 14 | 2 | 0 | 88% | 100% |
| Quest | 13 | 13 | 0 | 0 | 100% | 100% |
| Spell | 40 | 40 | 0 | 0 | 100% | 100% |
| Aura | 14 | 14 | 0 | 0 | 100% | 100% |
| Map | 19 | 17 | 2 | 0 | 89% | 100% |
| ElunaQuery | 17 | 17 | 0 | 0 | 100% | 100% |
| WorldPacket | 23 | 23 | 0 | 0 | 100% | 100% |
| Corpse | 5 | 5 | 0 | 0 | 100% | 100% |
| BattleGround | 18 | 18 | 0 | 0 | 100% | 100% |
| Vehicle | 6 | 0 | 6 | 0 | 0% | 100% |
| AuraEffect | 23 | 23 | 0 | 0 | 100% | 100% |
| ElunaProcInfo | 28 | 27 | 1 | 0 | 96% | 100% |
| SpellInfo | 190 | 186 | 4 | 0 | 98% | 100% |
| events: server | 34 | 31 | 3 | 0 | 91% | 100% |
| events: player | 51 | 27 | 24 | 0 | 53% | 100% |
| events: creature | 28 | 27 | 1 | 0 | 96% | 100% |
| events: gameobject | 13 | 10 | 3 | 0 | 77% | 100% |
| events: item | 8 | 4 | 4 | 0 | 50% | 100% |
| events: player gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: creature gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: gameobject gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: item gossip | 2 | 2 | 0 | 0 | 100% | 100% |
| events: group | 7 | 5 | 2 | 0 | 71% | 100% |
| events: guild | 11 | 9 | 2 | 0 | 82% | 100% |
| events: map | 7 | 3 | 4 | 0 | 43% | 100% |
| events: instance | 7 | 3 | 4 | 0 | 43% | 100% |
| events: packet | 2 | 2 | 0 | 0 | 100% | 100% |
| events: battleground | 4 | 4 | 0 | 0 | 100% | 100% |
| events: spell | 23 | 0 | 23 | 0 | 0% | 100% |
| **all** | **1443** | **1294** | **142** | **7** | **90%** | **100%** |

`AuraEffect` and `ElunaProcInfo` are implemented on the engine and host, and `RegisterSpellEvent` accepts spell event registrations as honest stubs (`needs core hook C14`), so `AuraEffect` and `ElunaProcInfo` remain unreachable from scripts at runtime until core spell hooks land.

### Known limits

* **Test servers only.** Whoever can write to `ForeverLua.ScriptPath` runs code inside the server. Eluna mode trusts its
  scripts and has raw SQL on the module's own connections, and the GM bridge can reach command handlers that expect a
  console. Read the [Security Model](knowledge/architecture/security.md) and [Limits](knowledge/architecture/limits.md)
  before enabling the module.
* **Low-coverage classes.** Spell events register as stubs needing core hook C14 (0% implemented, 100% resolvable, so
  `AuraEffect` and `ElunaProcInfo` are implemented but unreachable at runtime until core spell hooks land); Object (43%)
  is only partly covered. Classic-era systems (guild bank, vehicles, achievements, arena, dual spec, LFG)
  are documented stubs that raise an error; documented Object, WorldObject, Player, Item, Map, SpellInfo and
  ElunaProcInfo calls the core has no API for are stubs that raise `not available on this core`.
* **Core hooks.** Some events need the optional core hooks C1-C4 (and further hooks for instance maps); without them
  they register and log `needs core hook Cn` once.
* **The bot API needs a provider module.** Without a bot module that implements `BotProvider`, `IsBotApiAvailable()`
  returns `false` and the bot globals raise an error.
* **Beta.** The API and the configuration keys can still change before 1.0.
