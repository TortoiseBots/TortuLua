---
type: API Reference
title: Eluna Mode API
description: Clean-room Eluna-compatible globals, classes, E2-E6 extensions, and core hooks.
tags: [api, eluna, cleanroom, hooks]
status: stable
---

# Overview

`ForeverLua.Mode = "eluna"` recursively loads `*.lua` (`init.lua` first per folder), resolves `require("a.b")` under `ScriptPath`/`RequirePath`, runs event handlers synchronously on the hook thread (nested up to depth 8), and exposes a clean-room Eluna-compatible API.

Check `docs/COVERAGE.md` for the exact per-symbol compatibility matrix.

# Globals and Classes

- **Event registration:** `Register{Player,Creature,GameObject,Item,Server,PlayerGossip,CreatureGossip,GameObjectGossip,ItemGossip}Event`, `RegisterUniqueCreatureEvent`, `Register{Group,Guild,Map,Instance,Packet,BG}Event`, corresponding `Clear*Events`, `CreateLuaEvent`, `RemoveEventById`, `RemoveEvents`.
- **World and DB globals:** `GetPlayerByName/GUID`, `GetPlayersInWorld`, `GetPlayerCount`, `SendWorldMessage`, `PerformIngameSpawn`, `GetGuildByName`, `GetGuildByLeaderGUID`, `SaveAllPlayers`, `Kick`, `Ban`, `SendMail`, `AddVendorItem`, `VendorRemoveItem`, `VendorRemoveAllItems`, `{GetActive,Is,Start,Stop}GameEvent*`, `{World,Char,Auth}DBQuery`, `{World,Char,Auth}DBExecute`, `{World,Char,Auth}DBQueryAsync`, `GetGameTime/CurrTime/TimeDiff`, `GetCoreName/Version/Expansion`, `GetLuaEngine`, `GetStateMapId/InstanceId/Map`, `GetPlayersOnMap`, `IsCompatibilityMode`, `GetGUIDLow/Type/Entry`, `GetItemLink`, `GetAreaName`, `GetMapById`, `GetQuest`, `RunCommand`, `PrintInfo/Error/Debug`, `ReloadEluna`, `bit_*`, `CreatePacket`.
- **Engine extensions:** `RegisterCommand(name, minSecurity, fn [, help])` (up to 32 GM commands; requires GM permission so `minSecurity` only raises above 1; collisions refused) and `ForeverData` (per-script JSON persistence in `characters.forever_lua_data`, 64 KB/value, 1 MB/script, byte-exact 1..255-byte keys, flushed every `DataFlushMs` and at shutdown, surviving `.lua reload`).
- **Classes:** `Object`, `WorldObject`, `Unit`, `Player`, `Creature`, `GameObject`, `Item`, `Map`, `Group`, `Guild`, `Quest`, `Spell`, `Aura`, `AuraEffect`, `ElunaProcInfo`, `SpellInfo`, `Corpse`, `BattleGround`, `ElunaQuery`, `WorldPacket`, `Vehicle` (Classic 1.60 stubs), and `Bot` (see [Bot API](bot-api.md)). A documented method the pinned core has no API for is registered as a stub that raises `<Method>: not available on this core` instead of a nil call. `AuraEffect` and `ElunaProcInfo` are implemented on the engine and host, but unreachable from scripts until spell events are implemented.

# Handles, GUIDs, and Transient Objects

- **Handle resolution:** Re-resolved on every call. Gone objects raise `"<Class> no longer exists"` (except `GetGUID/GetGUIDLow/GetEntry/GetTypeId` and `IsInWorld`). Cross-map handles raise `"object is on another map"`; access rules are checked before lookup.
- **128-bit GUIDs:** `Object:GetGUID()` returns a 32-digit hex string carrying all 128 bits (type, entry, map in the high half). Lookup globals also accept integers (low counter, high half `0`).
- **Transient spell handles:** `Spell`, `Aura`, `AuraEffect` and `ElunaProcInfo` handles are valid only during the Lua call that produced them; use `SpellInfo` (`GetSpellInfo(id)`, `Spell:GetSpellInfo`, `Aura:GetSpellInfo`, `AuraEffect:GetSpellInfo`, `ElunaProcInfo:GetSpellInfo`) for non-stale core spell data.

# Core Hooks vs ScriptName Binding

Events lacking a core hook register and log `event <family> <NAME> needs core hook Cn` once (C1-C12). When C1-C4 core hooks are present (`Karrz1/TrinityCore` PRs #375-#379, auto-detected by CMake), creature/gameobject `ADD`/`REMOVE`, creature combat/death/reset (without a `ScriptName`), gameobject `USE`, global item use, and player-command events fire globally without wrapping any C++ AI.

Other creature/gameobject/item events (or cores without the hooks) use template `ScriptName = 'forever_lua'`, `'forever_lua_gameobject'`, or `'forever_lua_item'`.

Related: [Limits and Core Gaps](../architecture/limits.md), [Bot API](bot-api.md), [Build and Test](../operations/build-and-test.md).
