# Eluna clean room

TortuLua's Eluna-compatible mode (E0-E3) is written from Eluna's published API documentation only. Eluna's C++ and
Lua source, its headers and `Hooks.h` were never opened, searched or copied; no name, layout or comment of it is used as
a code source. The event ids of the maintainers' API specification, section 3 are the maintainers' reading of the same documentation; the documentation was checked name by name and argument order by argument
order for every family implemented in E1 and E2.

## Documentation pages used (https://elunaluaengine.github.io/, read 2026-11-21)

Class pages:

* `index.html` (class list and inheritance: Object > WorldObject > Unit > Player|Creature, WorldObject > GameObject,
  Object > Item)
* `Global/index.html`
* `Object/index.html`
* `WorldObject/index.html`
* `Unit/index.html`
* `Player/index.html`
* `Creature/index.html`
* `GameObject/index.html`
* `ElunaQuery/index.html`

Per-function pages, for the signatures, argument defaults and return values of the E1 API:

* Global: `RegisterPlayerEvent`, `RegisterCreatureEvent`, `RegisterGameObjectEvent`, `RegisterItemEvent`,
  `RegisterServerEvent`, `RegisterPlayerGossipEvent`, `RegisterCreatureGossipEvent`,
  `RegisterGameObjectGossipEvent`, `RegisterItemGossipEvent`, `ClearPlayerEvents`,
  `ClearCreatureEvents`, `ClearGameObjectEvents`, `ClearItemEvents`, `ClearServerEvents`,
  `ClearPlayerGossipEvents`, `ClearCreatureGossipEvents`, `ClearGameObjectGossipEvents`,
  `ClearItemGossipEvents`, `CreateLuaEvent`, `RemoveEventById`, `RemoveEvents`, `GetPlayerByName`,
  `GetPlayerByGUID`, `GetPlayersInWorld`, `GetPlayerCount`, `SendWorldMessage`, `PerformIngameSpawn`,
  `GetGameTime`, `GetCurrTime`, `GetTimeDiff`, `GetCoreName`, `GetCoreVersion`, `GetCoreExpansion`,
  `GetLuaEngine`, `GetStateMapId`, `GetStateInstanceId`, `GetGUIDLow`, `GetGUIDType`, `GetGUIDEntry`,
  `GetItemLink`, `GetAreaName`, `GetMapById`, `GetQuest`, `RunCommand`, `PrintInfo`, `PrintError`,
  `PrintDebug`, `WorldDBQuery`, `WorldDBExecute`, `CharDBQuery`, `CharDBExecute`, `AuthDBQuery`,
  `AuthDBExecute`, `ReloadEluna` (and the `bit_*` helpers).
* Fix round (2026-11-21, review findings 16, 4, 6, 15), read for the optional arguments, the handler argument lists and
  the guid types: Unit `GetPower`, `SetPower`, `DealDamage`; WorldObject `RegisterEvent` (the callback's 4th argument is
  the object); Global `RegisterCreatureEvent`, `RegisterGameObjectEvent`, `RegisterPlayerEvent` (the full event tables:
  `on_dialog_status` is `(event, player, object)`, the chat family's last argument is the group/guild/channel),
  `RegisterCreatureGossipEvent`, `RegisterItemGossipEvent` (HELLO/SELECT arguments and the "false = default action,
  and for items also stops the spell cast" rule), `RegisterUniqueCreatureEvent`, `GetGUIDType`, `GetPlayerByGUID`;
  Object `GetGUID` (a 64-bit number in the docs; TortuLua returns all 128 bits as a 32-digit hex string, see `docs/COVERAGE.md`).
* Object: `GetEntry`, `GetGUID`, `GetGUIDLow`, `GetTypeId`, `GetScale`, `SetScale`, `HasFlag`,
  `GetUInt32Value`, `ToPlayer`, `ToCreature`, `ToGameObject`, `ToUnit`.
* WorldObject: `GetName`, `GetMap`, `GetMapId`, `GetInstanceId`, `GetZoneId`, `GetAreaId`, `GetX`, `GetY`,
  `GetZ`, `GetO`, `GetLocation`, `GetDistance`, `IsWithinDistInMap`, `GetNearestPlayer`,
  `GetNearestCreature`, `GetNearestGameObject`, `GetPlayersInRange`, `GetCreaturesInRange`,
  `SpawnCreature`, `SummonGameObject`, `RegisterEvent`, `RemoveEventById`, `RemoveEvents`.
* Unit: `GetLevel`, `SetLevel`, `GetHealth`, `SetHealth`, `GetMaxHealth`, `GetHealthPct`, `GetPower`,
  `SetPower`, `IsAlive`, `IsDead`, `IsInCombat`, `GetVictim`, `Attack`, `AttackStop`, `CastSpell`,
  `CastCustomSpell`, `AddAura`, `RemoveAura`, `HasAura`, `Kill`, `DealDamage`, `MoveTo`, `MoveFollow`,
  `MoveStop`, `MoveHome`, `SetFaction`, `GetFaction`, `IsHostileTo`, `IsFriendlyTo`, `SetDisplayId`,
  `Emote`, `GetOwner`, `GetClass`, `GetRace`, `GetGender`, `SendUnitSay`, `SendUnitYell`,
  `SendUnitWhisper`.
* Player: `SendBroadcastMessage`, `SendNotification`, `GetAccountId`, `GetAccountName`, `GetGMRank`,
  `IsGM`, `GetTeam`, `GetCoinage`, `ModifyMoney`, `AddItem`, `RemoveItem`, `HasItem`, `GetItemCount`,
  `Teleport`, `GiveXP`, `LearnSpell`, `RemoveSpell`, `HasSpell`, `GetGroup`, `GetGuild`, `GetGuildId`,
  `GetQuestStatus`, `AddQuest`, `CompleteQuest`, `HasQuest`, `SaveToDB`, `KickPlayer`, `ResurrectPlayer`,
  `GetSelection`, `IsAlliance`, `IsHorde`, `GossipMenuAddItem`, `GossipSendMenu`, `GossipComplete`,
  `GossipClearMenu`, `GossipAddQuests`.
* Creature: `GetScriptName`, `IsElite`, `IsBoss`, `IsWorldBoss`, `GetHomePosition`, `Respawn`,
  `DespawnOrUnsummon`, `SetReactState`, `GetAITarget`, `GetSpawnId`, `SetRespawnDelay`.
* GameObject: `GetGoState`, `SetGoState`, `GetLootState`, `SetLootState`, `Despawn`, `Respawn`,
  `IsSpawned`, `UseDoorOrButton`, `GetDBTableGUIDLow`.
* ElunaQuery: `IsNull`, `GetColumnCount`, `GetRowCount`, `NextRow`, `GetRow`, `GetBool`, `GetUInt8`,
  `GetUInt16`, `GetUInt32`, `GetUInt64`, `GetInt8`, `GetInt16`, `GetInt32`, `GetInt64`, `GetFloat`,
  `GetDouble`, `GetString`.

### E2 and E3 (read 2026-11-21, the same documentation site)

Class pages: `Group/index.html`, `Guild/index.html`, `Item/index.html`, `Quest/index.html`, `Spell/index.html`,
`Aura/index.html`, `Map/index.html`, `WorldPacket/index.html`; `Unit/index.html` again for `AddAura` (its Aura
return), `Player/index.html` again for the spell event's payload.

Per-function pages:

* Group: `AddMember`, `RemoveMember`, `GetMembers`, `GetLeaderGUID`, `IsRaidGroup`, `SendPacket`, `GetGUID`.
* Guild: `GetName`, `GetMembers`, `GetLeaderGUID`, `SendPacket`, `GetId`.
* Item: `GetItemLink`, `GetCount`, `SetCount`, `GetOwner`, `IsEquipped`, `GetBagSlot`, `GetSlot` (and `Object`
  `GetEntry`).
* Quest: `GetId`, `GetLevel`, `IsDaily` (`IsRepeatable` read for the flags).
* Spell: `GetEntry`, `GetCaster`, `Cancel`, `GetDuration`.
* Aura: `GetCaster`, `GetAuraId`, `GetDuration`, `SetDuration`.
* Global: `RegisterGroupEvent`, `RegisterGuildEvent`, `RegisterMapEvent`, `RegisterInstanceEvent`,
  `RegisterPacketEvent`, `ClearGroupEvents`, `ClearGuildEvents`, `ClearMapEvents`, `ClearInstanceEvents`,
  `ClearPacketEvents`, `GetGuildByName`, `GetGuildByLeaderGUID`, `SaveAllPlayers`, `Kick`, `Ban`, `SendMail`,
  `AddVendorItem`, `VendorRemoveItem`, `VendorRemoveAllItems`, `GetActiveGameEvents`, `IsGameEventActive`,
  `StartGameEvent`, `StopGameEvent`, `WorldDBQueryAsync`, `CharDBQueryAsync`, `AuthDBQueryAsync`.

### E6 (read 2026-11-21, the same documentation site)

Per-function pages, for the event ids wired to the core hooks C1-C4 and the argument lists of the events that become
global with them: `RegisterPlayerEvent` (the full table, especially `on_command` and `on_kill_creature`/`on_died`),
`RegisterCreatureEvent` (all ids, including `on_add`/`on_remove`), `RegisterGameObjectEvent` (all ids, including
`on_add`/`on_remove`), `RegisterServerEvent` (the full table, especially `on_world_delete_creature`/
`on_world_delete_gameobject`), `RegisterItemEvent` (the `on_use` "can return false to stop the spell casting" rule),
`RegisterItemGossipEvent` and `RegisterPlayerGossipEvent` (the SELECT argument list and the "menu_id is only for player
gossip" note). No Eluna C++/Lua source was opened; the engine itself is still `src/host/EluHooks.cpp` plus the
game-free `src/host/CoreHooks.cpp`.

Our own extensions are documented in [`knowledge/modes/eluna-mode.md`](../knowledge/modes/eluna-mode.md), not in Eluna's pages: `RegisterCommand` and the
`ForeverData` table (`Data` in test mode), the `.lua profile` report and the transient Spell/Aura lifetime rule (the
latter follows the spec's section 1 sentence about objects that are invalid after their call).

### Long tail round 1: Spell, Aura and SpellInfo (read 2026-11-24, the same documentation site)

Class pages: `Spell/index.html`, `Aura/index.html`, `SpellInfo/index.html`. Per-function pages: every documented method
of those three classes (the 40 Spell, the 14 Aura and the 190 SpellInfo names of `docs/coverage/eluna-documented-api.json`;
`Aura/GetEntry` has no page, the name is spec section 4's) and `Global/GetSpellInfo.html` for the global's `nil` rule.
The pages give the synopsis, the argument ranges and the return types used for the checks in this round. No Eluna source
was opened; every body was written from the core's own `Spell`, `Aura` and `SpellInfo` API (`~/ForeverCore`, read only).

### Long tail: Quest and Map (2026-11-26)

Uses the documented method names in `docs/coverage/eluna-documented-api.json` (`Quest/index.html`, `Map/index.html`)
already summarised in this repo. No Eluna source was opened; every Quest body was written from ForeverCore's own `Quest`
class (`src/server/game/Quests/QuestDef.h`), and `Map:GetInstanceData` / `Map:SaveInstanceData` are core stubs because
ForeverCore's `InstanceMap::GetInstanceScript` returns a database-bound C++ `InstanceScript` with no Lua `InstanceData`
or `SaveToDB` API.

## What the documentation does not say, and where the answer came from

* The documentation gives the event table per `Register*` family as *names*, *state* and *argument lists*. A reviewer
  believed the pages also print the numeric ids; the pages were read again for the fix round and the event tables as
  fetched carry names and argument lists but no numbers (the `event` argument is described as "refer to table above").
  The ids are taken from the maintainers' API specification, section 3 (the maintainers' reading), and the names, the argument order
  and the "can return ..." comments were verified against the pages above. If a page does print numbers, the spec
  ids are to be compared against them in the next stage; nothing in the code takes an id from outside the spec.
* The documentation's method names and argument lists are followed exactly (camel case, 0-based query columns,
  optional arguments and their defaults). Anything the pinned core cannot answer (for example the generic update-field
  readers) raises "not available on this server" instead of guessing.

## Files

* Engine side (game-free, tested without the core): `src/engine/Elu.h`, `src/engine/EluAlias.cpp`,
  `src/engine/elu/*`.
* Game side: `src/host/EluHooks.cpp` (the ScriptMgr hooks and AI scripts), `src/host/EluWorld.cpp` (the class
  methods' game work), `src/host/LuaDb.cpp` (raw SQL on the module's own connections).

### E4 and E5 (read 2026-11-21, the same documentation site)

E4 (per-map states) reads the multistate part of the same pages:

* Global: `GetStateMap`, `GetStateMapId`, `GetStateInstanceId`, `GetPlayersOnMap`, `IsCompatibilityMode` (the pages say
  which of them exist in a MAP state only, and that `GetStateMap` is nil for the world state).
* `RegisterMapEvent`/`RegisterInstanceEvent` and `RegisterPlayerEvent`/`RegisterServerEvent` again, for the state column
  of their event tables (MAP vs WORLD).

E5 (the long tail) reads:

* Global: `RegisterBGEvent`, `ClearBattleGroundEvents`, `CreatePacket`, `AddTaxiPath`, `GetOwnerHalaa` and
  `RegisterVehicleEvent`. The last two are named in the maintainers' API specification, section 7; the current site's Global
  index does not list them, so their names come from the spec and their error text is the spec's section 7 sentence.
* Class pages: `Corpse/index.html` (`GetGhostTime`, `GetOwnerGUID`, `GetType`, `ResetGhostTime`, `SaveToDB`),
  `BattleGround/index.html` (name, status, winner, end time, bracket, level and player bounds, free slots, alive count,
  bonus honor), `WorldPacket/index.html` (opcode, size, the Read*/Write* set), `Vehicle/index.html` (the six methods,
  all section 7 stubs).
* Per-function pages: `Map/SetWeather`, `Map/GetPlayerCount`, `Map/GetPlayers`, `Map/GetWorldObject`, `Map/IsDungeon`,
  `Map/IsRaid`, `Map/IsArena`, `Map/IsBattleground`, `Map/IsEmpty`, `Map/IsHeroic`, `Map/GetDifficulty`, `Map/GetAreaId`,
  `Map/GetHeight`, `Map/Data`; `Player/GetCorpse`, `Player/SendPacket`; `RegisterPacketEvent` (WORLD state, arguments
  event, packet, player, read-only copy).
* Section 7 stub method pages read for their names and argument lists: Player `HasAchieved`, `SetAchievement`,
  `ResetAchievements`, `GetArenaPoints`, `SetArenaPoints`, `ModifyArenaPoints`, `IsInArenaTeam`, `GetSpecsCount`,
  `GetActiveSpec`, `InRandomLfgDungeon`, `IsUsingLfg`; Unit `GetVehicle`, `GetVehicleKit`, `IsOnVehicle`; Guild
  `SetBankTabText`.

What the documentation does not say, and where the answer came from: the `RegisterBGEvent` and `RegisterPacketEvent`
tables print names, states and arguments but (like the E1/E2 tables) no numbers; the ids 1-4 (BG) and 5/7 (packet) are
the spec's section 3. The core's `BattlegroundScript` (`OnInit`/`OnStart`/`OnEnd`) and `BattlegroundMapScript` provide the
BG hooks at this pin, so the spec's C12 was not needed. `AddTaxiPath` is impossible without a DB2 insert API: it raises
"not available on this server" (not a section 7 message: taxis do exist on Classic 1.60).

E4/E5 review round (2026-11-21): `Player/index.html` and `Guild/index.html` were read again for the method names of the
Classic-gap systems. The Player index lists no glyph, currency (other than honor), Battlefield or Wintergrasp method, and
the Guild index's only bank method is `SetBankTabText`; the arena method `RemoveArenaSpellCooldowns` was added to the
section 7 stubs. No other page was read and no source was opened.

### Player long tail (read 2026-11-26, the same documentation site)

Per-function pages: `Player/GetChatTag`, `Player/GetLifetimeKills`, `Player/GetMailItem`,
`Player/IsHonorOrXPTarget`, `Player/IsVisibleForPlayer` for their synopsis, argument ranges and return types, and
`Player/GetPhaseMaskForSpawn` again for the phase-mask contract that keeps it a stub. The pages' argument lists set the
checks of those bindings; the rest of the round's Player methods take the documented names and argument lists from the
maintained list the report is generated from. No Eluna source was opened; every body was written from the core's own
Player API (`~/ForeverCore`, read only).
