# TortuLua: Eluna-mode API coverage

What an Eluna-style script can call in TortuLua, and every documented Eluna name it cannot. The engine side is read from the registration code itself (`src/engine/elu/*.cpp`, `src/engine/EluAlias.cpp`), the documented side from `docs/coverage/eluna-documented-api.json` (Eluna's published documentation plus the maintainers' API specification, sections 2-4 and 7). Regenerate with `tools/coverage/gen_coverage.py --write`; `tools/coverage/gen_coverage.py --check` fails when the engine registers a name this file does not list.

Status is computed for the pinned core **without** the C1-C4 core hooks: an event whose notification needs a hook registers and logs `needs core hook Cn` once. `Requires` names that hook; a conditional hook (`C1`/`C2`/`C3`) also has the interim ScriptName path the engine names in that line, an unconditional one (C5-C11) has no path on this core.

Status legend: **implemented** = registered and works; **stub** = the call or the registration raises the documented error (`not available on Classic 1.60`, `needs core hook Cn`); **missing** = documented and not in the engine; **extra** = in the engine and not in the documented API (our extensions, the Lua sandbox).

## Summary

| Family | Documented | Implemented | Stub | Missing | Implemented % | Resolvable % |
|---|---|---|---|---|---|---|
| Global | 110 | 94 | 4 | 12 | 85% | 89% |
| Object | 30 | 14 | 0 | 16 | 47% | 47% |
| WorldObject | 51 | 23 | 0 | 28 | 45% | 45% |
| Unit | 167 | 132 | 3 | 32 | 79% | 81% |
| Player | 263 | 129 | 13 | 121 | 49% | 54% |
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
| **all** | **1443** | **917** | **74** | **452** | **64%** | **69%** |

`Implemented %` counts the names a script can call and get a result; `Resolvable %` also counts the documented stubs (the call exists and raises the documented error instead of `attempt to call a nil value`). A class row counts the class's own documented methods; inherited ones are counted on the class the documentation declares them on.

Test-mode only: 19 names in 8 toolkit tables (`Event`, `Timer`, `Cmd`, `World`, `Db`, `Json`, `Gm`, the `Data` alias) are installed with `Mode = test` and are not part of the eluna surface (appendix A).

## The surface

### Global functions

| Documented global | Status | Note |
|---|---|---|
| `AddTaxiPath` | **stub** | stub: not available (this server) |
| `AddVendorItem` | implemented |  |
| `AuthDBExecute` | implemented |  |
| `AuthDBQuery` | implemented |  |
| `AuthDBQueryAsync` | implemented |  |
| `Ban` | implemented |  |
| `CharDBExecute` | implemented |  |
| `CharDBQuery` | implemented |  |
| `CharDBQueryAsync` | implemented |  |
| `ClearBattleGroundEvents` | implemented |  |
| `ClearCreatureEvents` | implemented |  |
| `ClearCreatureGossipEvents` | implemented |  |
| `ClearGameObjectEvents` | implemented |  |
| `ClearGameObjectGossipEvents` | implemented |  |
| `ClearGroupEvents` | implemented |  |
| `ClearGuildEvents` | implemented |  |
| `ClearInstanceEvents` | implemented |  |
| `ClearItemEvents` | implemented |  |
| `ClearItemGossipEvents` | implemented |  |
| `ClearMapEvents` | implemented |  |
| `ClearPacketEvents` | implemented |  |
| `ClearPlayerEvents` | implemented |  |
| `ClearPlayerGossipEvents` | implemented |  |
| `ClearServerEvents` | implemented |  |
| `ClearUniqueCreatureEvents` | implemented |  |
| `CreateLongLong` | **missing** |  |
| `CreateLuaEvent` | implemented |  |
| `CreatePacket` | implemented |  |
| `CreateULongLong` | **missing** |  |
| `GetActiveGameEvents` | implemented |  |
| `GetAreaName` | implemented |  |
| `GetCoreExpansion` | implemented | answers 0 (Classic 1.60) |
| `GetCoreName` | implemented | answers ForeverCore |
| `GetCoreVersion` | implemented |  |
| `GetCurrTime` | implemented |  |
| `GetGUIDEntry` | implemented |  |
| `GetGUIDLow` | implemented |  |
| `GetGUIDType` | implemented |  |
| `GetGameTime` | implemented |  |
| `GetGuildByLeaderGUID` | implemented |  |
| `GetGuildByName` | implemented |  |
| `GetItemGUID` | **missing** |  |
| `GetItemLink` | implemented |  |
| `GetLuaEngine` | implemented | answers ForeverLua |
| `GetMapById` | implemented |  |
| `GetObjectGUID` | **missing** |  |
| `GetOwnerHalaa` | **stub** | stub: not available (Classic 1.60) |
| `GetPlayerByGUID` | implemented |  |
| `GetPlayerByName` | implemented |  |
| `GetPlayerCount` | implemented |  |
| `GetPlayerGUID` | **missing** |  |
| `GetPlayersInWorld` | implemented |  |
| `GetPlayersOnMap` | implemented |  |
| `GetQuest` | implemented |  |
| `GetRealmID` | **missing** |  |
| `GetSpellInfo` | implemented |  |
| `GetStateInstanceId` | implemented |  |
| `GetStateMap` | implemented |  |
| `GetStateMapId` | implemented |  |
| `GetTimeDiff` | implemented |  |
| `GetUnitGUID` | **missing** |  |
| `IsBagPos` | **missing** |  |
| `IsBankPos` | **missing** |  |
| `IsCompatibilityMode` | implemented |  |
| `IsEquipmentPos` | **missing** |  |
| `IsGameEventActive` | implemented |  |
| `IsInventoryPos` | **missing** |  |
| `Kick` | implemented |  |
| `PerformIngameSpawn` | implemented | save writes the spawn to the creature/gameobject table (eluna mode trusts its scripts) |
| `PrintDebug` | implemented |  |
| `PrintError` | implemented |  |
| `PrintInfo` | implemented |  |
| `RegisterBGEvent` | implemented |  |
| `RegisterCreatureEvent` | implemented |  |
| `RegisterCreatureGossipEvent` | implemented |  |
| `RegisterGameObjectEvent` | implemented |  |
| `RegisterGameObjectGossipEvent` | implemented |  |
| `RegisterGroupEvent` | implemented |  |
| `RegisterGuildEvent` | implemented |  |
| `RegisterInstanceEvent` | **stub** | stub: needs core hook C11 |
| `RegisterItemEvent` | implemented |  |
| `RegisterItemGossipEvent` | implemented |  |
| `RegisterMapEvent` | implemented |  |
| `RegisterPacketEvent` | implemented |  |
| `RegisterPlayerEvent` | implemented |  |
| `RegisterPlayerGossipEvent` | implemented |  |
| `RegisterServerEvent` | implemented |  |
| `RegisterSpellEvent` | **missing** |  |
| `RegisterUniqueCreatureEvent` | implemented |  |
| `RegisterVehicleEvent` | **stub** | stub: not available (Classic 1.60) |
| `ReloadEluna` | implemented |  |
| `RemoveEventById` | implemented |  |
| `RemoveEvents` | implemented |  |
| `RunCommand` | implemented |  |
| `SaveAllPlayers` | implemented |  |
| `SendMail` | implemented |  |
| `SendWorldMessage` | implemented |  |
| `StartGameEvent` | implemented |  |
| `StopGameEvent` | implemented |  |
| `VendorRemoveAllItems` | implemented |  |
| `VendorRemoveItem` | implemented |  |
| `WorldDBExecute` | implemented |  |
| `WorldDBQuery` | implemented |  |
| `WorldDBQueryAsync` | implemented |  |
| `bit_and` | implemented |  |
| `bit_lshift` | implemented |  |
| `bit_not` | implemented |  |
| `bit_or` | implemented |  |
| `bit_rshift` | implemented |  |
| `bit_xor` | implemented |  |
### Global extras (in the engine, not in the documented API)

| Name | Note |
|---|---|
| `BotCommanderPlace` | our extension (B2): the chat form of place/rally/fall back, for one player's bots |
| `BotsInterrupt` | our extension (B2): the party's interrupt for a unit, one order returned |
| `ClearBotEvents` | our extension (B1): clears the bot handlers |
| `ForeverData` | our extension (E3): the persistent per-script table |
| `GetBotByGUID` | our extension (B1): a bot of the snapshot by guid |
| `GetBotByName` | our extension (B1): a bot of the snapshot |
| `GetBots` | our extension (B1): the bot snapshot, in name order |
| `GetBotsInGroup` | our extension (B1): the bots of a group |
| `GetBotsOfMaster` | our extension (B1): the bots a player masters or commands |
| `GetBotsOfOwner` | our extension (B1): the bots of a player's Battle.net account |
| `IsBotApiAvailable` | our extension (B1): the bot API is built into this server |
| `RegisterBotEvent` | our extension (B1): the nine bot events, world state only |
| `RegisterCommand` | our extension (E3): a GM command slot, README 'Writing scripts' |
| `print` | Lua sandbox builtin (Api.cpp registerCommon, both modes) |
| `require` | Lua sandbox module loader (Api.cpp registerCommon, both modes) |

### Class `Object`

| Documented method | Status | Note |
|---|---|---|
| `GetByteValue` | **missing** |  |
| `GetEntry` | implemented |  |
| `GetFloatValue` | **missing** |  |
| `GetGUID` | implemented | returns all 128 bits as a 32-digit hex string (the docs type is a 64-bit number); the type, entry and map live in the high half |
| `GetGUIDLow` | implemented |  |
| `GetInt32Value` | **missing** |  |
| `GetScale` | implemented |  |
| `GetTypeId` | implemented |  |
| `GetUInt16Value` | **missing** |  |
| `GetUInt32Value` | implemented |  |
| `GetUInt64Value` | **missing** |  |
| `HasFlag` | implemented |  |
| `IsInWorld` | implemented |  |
| `IsPlayer` | implemented |  |
| `RemoveFlag` | **missing** |  |
| `SetByteValue` | **missing** |  |
| `SetFlag` | **missing** |  |
| `SetFloatValue` | **missing** |  |
| `SetInt16Value` | **missing** |  |
| `SetInt32Value` | **missing** |  |
| `SetScale` | implemented |  |
| `SetUInt16Value` | **missing** |  |
| `SetUInt32Value` | **missing** |  |
| `SetUInt64Value` | **missing** |  |
| `ToCorpse` | **missing** |  |
| `ToCreature` | implemented |  |
| `ToGameObject` | implemented |  |
| `ToPlayer` | implemented |  |
| `ToUnit` | implemented |  |
| `UpdateUInt32Value` | **missing** |  |
### Class `WorldObject` (inherits `Object`)

| Documented method | Status | Note |
|---|---|---|
| `Data` | **missing** |  |
| `GetAngle` | **missing** |  |
| `GetAreaId` | implemented |  |
| `GetCreaturesInRange` | implemented |  |
| `GetDistance` | implemented |  |
| `GetDistance2d` | **missing** |  |
| `GetExactDistance` | **missing** |  |
| `GetExactDistance2d` | **missing** |  |
| `GetGameObjectsInRange` | **missing** |  |
| `GetInstanceId` | implemented |  |
| `GetLocation` | implemented |  |
| `GetMap` | implemented |  |
| `GetMapId` | implemented |  |
| `GetName` | implemented |  |
| `GetNearestCreature` | implemented |  |
| `GetNearestGameObject` | implemented |  |
| `GetNearestPlayer` | implemented |  |
| `GetNearObject` | **missing** |  |
| `GetNearObjects` | **missing** |  |
| `GetO` | implemented |  |
| `GetPhaseMask` | **missing** |  |
| `GetPlayersInRange` | implemented |  |
| `GetRelativePoint` | **missing** |  |
| `GetX` | implemented |  |
| `GetY` | implemented |  |
| `GetZ` | implemented |  |
| `GetZoneId` | implemented |  |
| `IsInBack` | **missing** |  |
| `IsInFront` | **missing** |  |
| `IsInMap` | **missing** |  |
| `IsInRange` | **missing** |  |
| `IsInRange2d` | **missing** |  |
| `IsInRange3d` | **missing** |  |
| `IsWithinDist` | **missing** |  |
| `IsWithinDist2d` | **missing** |  |
| `IsWithinDist3d` | **missing** |  |
| `IsWithinDistInMap` | implemented |  |
| `IsWithinLoS` | **missing** |  |
| `PlayDirectSound` | **missing** |  |
| `PlayDistanceSound` | **missing** |  |
| `PlayMusic` | **missing** |  |
| `RegisterEvent` | implemented |  |
| `RemoveEventById` | implemented |  |
| `RemoveEvents` | implemented |  |
| `SendPacket` | **missing** | the engine registers it on `Player` only |
| `SendUnitSay` | **missing** | the engine registers it on `Unit` only |
| `SendUnitWhisper` | **missing** | the engine registers it on `Unit` only |
| `SendUnitYell` | **missing** | the engine registers it on `Unit` only |
| `SetPhaseMask` | **missing** |  |
| `SpawnCreature` | implemented |  |
| `SummonGameObject` | implemented |  |
### Class `Unit` (inherits `WorldObject`)

| Documented method | Status | Note |
|---|---|---|
| `AddAura` | implemented |  |
| `AddFlatStatModifier` | **missing** |  |
| `AddPctStatModifier` | **missing** |  |
| `AddUnitState` | implemented |  |
| `Attack` | implemented |  |
| `AttackStop` | implemented |  |
| `CanModifyStats` | **missing** |  |
| `CastCustomSpell` | implemented |  |
| `CastSpell` | implemented |  |
| `CastSpellAoF` | **missing** |  |
| `ClearInCombat` | implemented |  |
| `ClearUnitState` | implemented |  |
| `CountPctFromCurHealth` | **missing** |  |
| `CountPctFromMaxHealth` | **missing** |  |
| `DealDamage` | implemented |  |
| `DealHeal` | implemented |  |
| `DeMorph` | implemented |  |
| `DisableMelee` | **missing** |  |
| `Dismount` | implemented |  |
| `Emote` | implemented |  |
| `EmoteState` | implemented |  |
| `GetAura` | implemented |  |
| `GetBaseSpellPower` | implemented |  |
| `GetCharmerGUID` | implemented |  |
| `GetCharmGUID` | **missing** |  |
| `GetClass` | implemented |  |
| `GetClassAsString` | implemented |  |
| `GetClassMask` | implemented |  |
| `GetControllerGUID` | implemented |  |
| `GetControllerGUIDS` | **missing** |  |
| `GetCreatorGUID` | implemented |  |
| `GetCreatureType` | implemented |  |
| `GetCritterGUID` | **missing** |  |
| `GetCurrentSpell` | implemented |  |
| `GetDisplayId` | implemented |  |
| `GetFaction` | implemented |  |
| `GetFriendlyUnitsInRange` | implemented |  |
| `GetGender` | implemented |  |
| `GetHealth` | implemented |  |
| `GetHealthPct` | implemented |  |
| `GetLevel` | implemented |  |
| `GetMaxHealth` | implemented |  |
| `GetMaxPower` | implemented |  |
| `GetMountId` | implemented |  |
| `GetMovementType` | implemented |  |
| `GetNativeDisplayId` | implemented |  |
| `GetOwnedAura` | implemented |  |
| `GetOwner` | implemented |  |
| `GetOwnerGUID` | implemented |  |
| `GetPetGUID` | implemented |  |
| `GetPower` | implemented |  |
| `GetPowerPct` | implemented |  |
| `GetPowerType` | implemented |  |
| `GetRace` | implemented |  |
| `GetRaceAsString` | implemented |  |
| `GetRaceMask` | implemented |  |
| `GetSpeed` | implemented |  |
| `GetStandState` | **missing** |  |
| `GetStat` | implemented |  |
| `GetUnfriendlyUnitsInRange` | implemented |  |
| `GetVehicle` | **stub** | stub: not available (Classic 1.60) |
| `GetVehicleKit` | **stub** | stub: not available (Classic 1.60) |
| `GetVictim` | implemented |  |
| `HasAura` | implemented |  |
| `HasUnitState` | implemented |  |
| `HealthAbovePct` | implemented |  |
| `HealthBelowPct` | implemented |  |
| `InterruptSpell` | implemented |  |
| `IsAlive` | implemented |  |
| `IsArmorer` | **missing** |  |
| `IsAttackingPlayer` | implemented |  |
| `IsAuctioneer` | **missing** |  |
| `IsBanker` | implemented |  |
| `IsBattleMaster` | **missing** |  |
| `IsCasting` | implemented |  |
| `IsCharmed` | implemented |  |
| `IsDead` | implemented |  |
| `IsDying` | **missing** |  |
| `IsFlying` | implemented |  |
| `IsFriendlyTo` | implemented |  |
| `IsFullHealth` | implemented |  |
| `IsGossip` | implemented |  |
| `IsGuildMaster` | **missing** |  |
| `IsHostileTo` | implemented |  |
| `IsInAccessiblePlaceFor` | **missing** |  |
| `IsInCombat` | implemented |  |
| `IsInnkeeper` | implemented |  |
| `IsInWater` | implemented |  |
| `IsMounted` | implemented |  |
| `IsMoving` | implemented |  |
| `IsOnVehicle` | **stub** | stub: not available (Classic 1.60) |
| `IsPvPFlagged` | implemented |  |
| `IsQuestGiver` | implemented |  |
| `IsRooted` | implemented |  |
| `IsServiceProvider` | implemented |  |
| `IsSpiritGuide` | **missing** |  |
| `IsSpiritHealer` | implemented |  |
| `IsSpiritService` | **missing** |  |
| `IsStandState` | implemented |  |
| `IsStopped` | implemented |  |
| `IsTabardDesigner` | **missing** |  |
| `IsTaxi` | **missing** |  |
| `IsTrainer` | implemented |  |
| `IsUnderWater` | implemented |  |
| `IsVendor` | implemented |  |
| `IsVisible` | implemented |  |
| `Kill` | implemented |  |
| `ModifyPower` | implemented |  |
| `Mount` | implemented |  |
| `MoveChase` | implemented |  |
| `MoveClear` | implemented |  |
| `MoveConfused` | implemented |  |
| `MoveExpire` | implemented |  |
| `MoveFleeing` | implemented |  |
| `MoveFollow` | implemented |  |
| `MoveHome` | implemented |  |
| `MoveIdle` | implemented |  |
| `MoveJump` | implemented |  |
| `MoveRandom` | implemented |  |
| `MoveStop` | implemented |  |
| `MoveTo` | implemented |  |
| `NearTeleport` | implemented |  |
| `PerformEmote` | implemented |  |
| `RemoveAllAuras` | implemented |  |
| `RemoveArenaAuras` | implemented |  |
| `RemoveAura` | implemented |  |
| `RemoveBindSightAuras` | **missing** |  |
| `RemoveCharmAuras` | **missing** |  |
| `RestoreDisplayId` | implemented |  |
| `RestoreFaction` | implemented |  |
| `SendChatMessageToPlayer` | implemented |  |
| `SendUnitEmote` | **missing** |  |
| `SendUnitSay` | implemented |  |
| `SendUnitWhisper` | implemented |  |
| `SendUnitYell` | implemented |  |
| `SetCanFly` | **missing** |  |
| `SetConfused` | implemented |  |
| `SetCreatorGUID` | **missing** |  |
| `SetCritterGUID` | **missing** |  |
| `SetDisplayId` | implemented |  |
| `SetFacing` | implemented |  |
| `SetFacingToObject` | implemented |  |
| `SetFaction` | implemented |  |
| `SetFeared` | implemented |  |
| `SetFFA` | **missing** | the engine registers it on `Player` only |
| `SetHealth` | implemented |  |
| `SetImmuneTo` | **missing** |  |
| `SetInCombatWith` | implemented |  |
| `SetLevel` | implemented |  |
| `SetMaxHealth` | implemented |  |
| `SetMaxPower` | implemented |  |
| `SetName` | **missing** |  |
| `SetNativeDisplayId` | implemented |  |
| `SetOwnerGUID` | implemented |  |
| `SetPetGUID` | **missing** |  |
| `SetPower` | implemented |  |
| `SetPowerType` | implemented |  |
| `SetPvP` | implemented |  |
| `SetRooted` | implemented |  |
| `SetSanctuary` | implemented |  |
| `SetSheath` | implemented |  |
| `SetSpeed` | implemented |  |
| `SetStandState` | implemented |  |
| `SetStunned` | implemented |  |
| `SetVisible` | **missing** |  |
| `SetWaterWalk` | implemented |  |
| `StopSpellCast` | implemented |  |
### Class `Player` (inherits `Unit`)

| Documented method | Status | Note |
|---|---|---|
| `AddComboPoints` | **missing** |  |
| `AddItem` | implemented |  |
| `AddLifetimeKills` | **missing** |  |
| `AddQuest` | implemented |  |
| `AddTalent` | **missing** |  |
| `AdvanceAllSkills` | **missing** |  |
| `AdvanceSkill` | **missing** |  |
| `AdvanceSkillsToMax` | **missing** |  |
| `AreaExploredOrEventHappens` | implemented |  |
| `BindToInstance` | **missing** |  |
| `CanBlock` | **missing** |  |
| `CanCompleteQuest` | implemented |  |
| `CanCompleteRepeatableQuest` | **missing** |  |
| `CanEquipItem` | implemented |  |
| `CanFly` | **missing** |  |
| `CanParry` | **missing** |  |
| `CanRewardQuest` | implemented |  |
| `CanShareQuest` | **missing** |  |
| `CanSpeak` | **missing** |  |
| `CanTitanGrip` | **missing** |  |
| `CanUninviteFromGroup` | **missing** |  |
| `CanUseItem` | implemented |  |
| `ClearComboPoints` | **missing** |  |
| `CompleteQuest` | implemented |  |
| `DurabilityLoss` | **missing** |  |
| `DurabilityLossAll` | **missing** |  |
| `DurabilityPointLossForEquipSlot` | **missing** |  |
| `DurabilityPointsLoss` | **missing** |  |
| `DurabilityPointsLossAll` | **missing** |  |
| `DurabilityRepair` | **missing** |  |
| `DurabilityRepairAll` | implemented |  |
| `EquipItem` | implemented |  |
| `FailQuest` | implemented |  |
| `GetAccountId` | implemented |  |
| `GetAccountName` | implemented |  |
| `GetActiveSpec` | **stub** | stub: not available (Classic 1.60) |
| `GetArenaPoints` | **stub** | stub: not available (Classic 1.60) |
| `GetBaseSkillValue` | implemented |  |
| `GetBattlegroundId` | implemented |  |
| `GetBattlegroundTypeId` | implemented |  |
| `GetChampioningFaction` | **missing** |  |
| `GetChatTag` | **missing** |  |
| `GetClass` | implemented | declared on `Unit` |
| `GetCoinage` | implemented |  |
| `GetComboPoints` | **missing** |  |
| `GetComboTarget` | **missing** |  |
| `GetCorpse` | implemented |  |
| `GetDbcLocale` | **missing** |  |
| `GetDbLocaleIndex` | implemented |  |
| `GetDifficulty` | **missing** |  |
| `GetDrunkValue` | **missing** |  |
| `GetEquippedItemBySlot` | implemented |  |
| `GetFreeTalentPoints` | **missing** |  |
| `GetGender` | implemented | declared on `Unit` |
| `GetGMRank` | implemented |  |
| `GetGossipTextId` | **missing** |  |
| `GetGroup` | implemented |  |
| `GetGroupInvite` | **missing** |  |
| `GetGuild` | implemented |  |
| `GetGuildId` | implemented |  |
| `GetGuildName` | implemented |  |
| `GetGuildRank` | **missing** |  |
| `GetHealthBonusFromStamina` | **missing** |  |
| `GetHonorPoints` | implemented |  |
| `GetInGameTime` | **missing** |  |
| `GetItemByEntry` | implemented |  |
| `GetItemByGUID` | implemented |  |
| `GetItemByPos` | implemented |  |
| `GetItemCount` | implemented |  |
| `GetLatency` | implemented |  |
| `GetLevelPlayedTime` | implemented |  |
| `GetLFGRole` | **stub** | stub: not available (Classic 1.60) |
| `GetLifetimeKills` | **missing** |  |
| `GetMailCount` | **missing** |  |
| `GetMailItem` | **missing** |  |
| `GetManaBonusFromIntellect` | **missing** |  |
| `GetMaxSkillValue` | implemented |  |
| `GetNearbyGameObject` | **missing** |  |
| `GetNextRandomRaidMember` | **missing** |  |
| `GetOriginalGroup` | **missing** |  |
| `GetOriginalSubGroup` | **missing** |  |
| `GetPhaseMaskForSpawn` | **missing** |  |
| `GetPlayerIP` | implemented |  |
| `GetPureMaxSkillValue` | implemented |  |
| `GetPureSkillValue` | implemented |  |
| `GetQuestLevel` | **missing** |  |
| `GetQuestRewardStatus` | implemented |  |
| `GetQuestStatus` | implemented |  |
| `GetRace` | implemented | declared on `Unit` |
| `GetRecruiterId` | **missing** |  |
| `GetReputation` | implemented |  |
| `GetReputationRank` | implemented |  |
| `GetReqKillOrCastCurrentCount` | implemented |  |
| `GetRestBonus` | implemented |  |
| `GetSelectedPlayer` | **missing** |  |
| `GetSelectedUnit` | **missing** |  |
| `GetSelection` | implemented |  |
| `GetShieldBlockValue` | **missing** |  |
| `GetSkillPermBonusValue` | implemented |  |
| `GetSkillTempBonusValue` | implemented |  |
| `GetSkillValue` | implemented |  |
| `GetSpecsCount` | **stub** | stub: not available (Classic 1.60) |
| `GetSpellCooldownDelay` | implemented |  |
| `GetSubGroup` | **missing** |  |
| `GetTeam` | implemented |  |
| `GetTotalPlayedTime` | implemented |  |
| `GetXP` | implemented |  |
| `GetXPForNextLevel` | implemented |  |
| `GetXPRestBonus` | **missing** |  |
| `GiveXP` | implemented |  |
| `GossipAddQuests` | implemented |  |
| `GossipClearMenu` | implemented |  |
| `GossipComplete` | implemented |  |
| `GossipMenuAddItem` | implemented |  |
| `GossipSendMenu` | implemented |  |
| `GossipSendPOI` | **missing** |  |
| `GroupCreate` | **missing** |  |
| `GroupEventHappens` | implemented |  |
| `GroupInvite` | **missing** |  |
| `HasAchieved` | **stub** | stub: not available (Classic 1.60) |
| `HasAtLoginFlag` | **missing** |  |
| `HasItem` | implemented |  |
| `HasPendingBind` | **missing** |  |
| `HasQuest` | implemented |  |
| `HasQuestForGO` | implemented |  |
| `HasQuestForItem` | implemented |  |
| `HasReceivedQuestReward` | implemented |  |
| `HasRecruited` | **missing** |  |
| `HasSkill` | implemented |  |
| `HasSpell` | implemented |  |
| `HasSpellCooldown` | implemented |  |
| `HasTalent` | **missing** |  |
| `HasTitle` | **missing** |  |
| `InArena` | **missing** |  |
| `InBattleground` | implemented |  |
| `InBattlegroundQueue` | **missing** |  |
| `IncompleteQuest` | implemented |  |
| `InRandomLfgDungeon` | **stub** | stub: not available (Classic 1.60) |
| `IsAcceptingWhispers` | **missing** |  |
| `IsAFK` | implemented |  |
| `IsAlliance` | implemented |  |
| `IsARecruiter` | **missing** |  |
| `IsDND` | implemented |  |
| `IsFalling` | implemented |  |
| `IsFlying` | implemented |  |
| `IsGM` | implemented |  |
| `IsGMChat` | **missing** |  |
| `IsGMVisible` | **missing** |  |
| `IsGroupVisibleFor` | **missing** |  |
| `IsHonorOrXPTarget` | **missing** |  |
| `IsHorde` | implemented |  |
| `IsImmuneToDamage` | implemented |  |
| `IsImmuneToEnvironmentalDamage` | implemented |  |
| `IsInArenaTeam` | **stub** | stub: not available (Classic 1.60) |
| `IsInGroup` | implemented |  |
| `IsInGuild` | implemented |  |
| `IsInSameGroupWith` | **missing** |  |
| `IsInSameRaidWith` | **missing** |  |
| `IsInWater` | implemented |  |
| `IsMoving` | implemented |  |
| `IsNeverVisible` | **missing** |  |
| `IsOutdoorPvPActive` | **missing** |  |
| `IsRecruited` | **missing** |  |
| `IsRested` | implemented |  |
| `IsTaxiCheater` | implemented |  |
| `IsUsingLfg` | **stub** | stub: not available (Classic 1.60) |
| `IsVisibleForPlayer` | **missing** |  |
| `KickPlayer` | implemented |  |
| `KilledMonsterCredit` | implemented |  |
| `KilledPlayerCredit` | **missing** |  |
| `KillGOCredit` | **missing** |  |
| `KillPlayer` | **missing** |  |
| `LearnSpell` | implemented |  |
| `LearnTalent` | **missing** |  |
| `LeaveBattleground` | **missing** |  |
| `LogoutPlayer` | implemented |  |
| `ModifyArenaPoints` | **stub** | stub: not available (Classic 1.60) |
| `ModifyHonorPoints` | implemented |  |
| `ModifyMoney` | implemented |  |
| `Mute` | **missing** |  |
| `RemoveActiveQuest` | implemented |  |
| `RemoveArenaSpellCooldowns` | **stub** | stub: not available (Classic 1.60) |
| `RemovedInsignia` | **missing** |  |
| `RemoveFromBattlegroundRaid` | **missing** |  |
| `RemoveFromGroup` | **missing** |  |
| `RemoveItem` | implemented |  |
| `RemoveLifetimeKills` | **missing** |  |
| `RemovePet` | **missing** |  |
| `RemoveQuest` | implemented |  |
| `RemoveRewardedQuest` | implemented |  |
| `RemoveSpell` | implemented |  |
| `ResetAchievements` | **stub** | stub: not available (Classic 1.60) |
| `ResetAllCooldowns` | implemented |  |
| `ResetPetTalents` | **missing** |  |
| `ResetSpellCooldown` | implemented |  |
| `ResetTalents` | **missing** |  |
| `ResetTalentsCost` | **missing** |  |
| `ResetTypeCooldowns` | implemented |  |
| `ResurrectPlayer` | implemented |  |
| `RewardQuest` | implemented |  |
| `RunCommand` | **missing** |  |
| `SaveToDB` | implemented |  |
| `Say` | implemented |  |
| `SendAddonMessage` | **missing** |  |
| `SendAreaTriggerMessage` | **missing** |  |
| `SendAuctionMenu` | **missing** |  |
| `SendBroadcastMessage` | implemented |  |
| `SendCinematicStart` | **missing** |  |
| `SendGuildInvite` | **missing** |  |
| `SendListInventory` | implemented |  |
| `SendMovieStart` | **missing** |  |
| `SendNotification` | implemented |  |
| `SendPacket` | implemented |  |
| `SendQuestTemplate` | **missing** |  |
| `SendShowBank` | implemented |  |
| `SendShowMailBox` | **missing** |  |
| `SendSpiritResurrect` | **missing** |  |
| `SendTabardVendorActivate` | **missing** |  |
| `SendTaxiMenu` | implemented |  |
| `SendTrainerList` | implemented |  |
| `SendUpdateWorldState` | **missing** |  |
| `SetAcceptWhispers` | **missing** |  |
| `SetAchievement` | **stub** | stub: not available (Classic 1.60) |
| `SetArenaPoints` | **stub** | stub: not available (Classic 1.60) |
| `SetAtLoginFlag` | **missing** |  |
| `SetBindPoint` | implemented |  |
| `SetCoinage` | implemented |  |
| `SetDrunkValue` | **missing** |  |
| `SetFactionForRace` | **missing** |  |
| `SetFFA` | implemented |  |
| `SetFreeTalentPoints` | **missing** |  |
| `SetGameMaster` | **missing** |  |
| `SetGender` | **missing** |  |
| `SetGMChat` | **missing** |  |
| `SetGMVisible` | **missing** |  |
| `SetGuildRank` | **missing** |  |
| `SetHonorPoints` | implemented |  |
| `SetKnownTitle` | **missing** |  |
| `SetLevel` | implemented | declared on `Unit` |
| `SetLifetimeKills` | **missing** |  |
| `SetMovement` | **missing** |  |
| `SetPlayerLock` | **missing** |  |
| `SetPvPDeath` | **missing** |  |
| `SetQuestStatus` | **missing** |  |
| `SetReputation` | implemented |  |
| `SetRestBonus` | implemented |  |
| `SetSheath` | implemented | declared on `Unit` |
| `SetSkill` | implemented |  |
| `SetTaxiCheat` | implemented |  |
| `SpawnBones` | **missing** |  |
| `StartTaxi` | **missing** |  |
| `SummonPet` | **missing** |  |
| `SummonPlayer` | implemented |  |
| `TalkedToCreature` | implemented |  |
| `Teleport` | implemented |  |
| `TextEmote` | implemented |  |
| `ToggleAFK` | implemented |  |
| `ToggleDND` | implemented |  |
| `UnbindAllInstances` | **missing** |  |
| `UnbindInstance` | **missing** |  |
| `UnsetKnownTitle` | **missing** |  |
| `Whisper` | implemented |  |
| `Yell` | implemented |  |
### Class `Creature` (inherits `Unit`)

| Documented method | Status | Note |
|---|---|---|
| `AddLootMode` | implemented |  |
| `AddThreat` | implemented |  |
| `AttackStart` | implemented |  |
| `CallAssistance` | implemented |  |
| `CallForHelp` | implemented |  |
| `CanAggro` | **missing** |  |
| `CanAssistTo` | implemented |  |
| `CanCompleteQuest` | **missing** |  |
| `CanFly` | implemented |  |
| `CanStartAttack` | implemented |  |
| `CanSwim` | implemented |  |
| `CanWalk` | **missing** |  |
| `ClearFixate` | implemented |  |
| `ClearThreat` | implemented |  |
| `ClearThreatList` | implemented |  |
| `DespawnOrUnsummon` | implemented |  |
| `FixateTarget` | implemented |  |
| `FleeToGetAssistance` | implemented |  |
| `GetAggroRange` | implemented |  |
| `GetAIName` | implemented |  |
| `GetAITarget` | implemented |  |
| `GetAITargets` | implemented |  |
| `GetAITargetsCount` | implemented |  |
| `GetAttackDistance` | implemented |  |
| `GetCorpseDelay` | implemented |  |
| `GetCreatureFamily` | implemented |  |
| `GetCreatureSpellCooldownDelay` | implemented |  |
| `GetCurrentWaypointId` | implemented |  |
| `GetDBTableGUIDLow` | implemented |  |
| `GetDefaultMovementType` | implemented |  |
| `GetExtraFlags` | implemented |  |
| `GetHomePosition` | implemented |  |
| `GetLootMode` | implemented |  |
| `GetLootRecipient` | implemented |  |
| `GetLootRecipientGroup` | implemented |  |
| `GetNPCFlags` | implemented |  |
| `GetRank` | implemented |  |
| `GetRespawnDelay` | implemented |  |
| `GetScriptId` | implemented |  |
| `GetScriptName` | implemented |  |
| `GetShieldBlockValue` | implemented |  |
| `GetSpawnId` | implemented |  |
| `GetThreat` | implemented |  |
| `GetWanderRadius` | implemented |  |
| `GetWaypointPath` | implemented |  |
| `HasCategoryCooldown` | implemented |  |
| `HasLootMode` | implemented |  |
| `HasLootRecipient` | implemented |  |
| `HasQuest` | implemented |  |
| `HasSearchedAssistance` | implemented |  |
| `HasSpell` | implemented |  |
| `HasSpellCooldown` | implemented |  |
| `IsBoss` | implemented |  |
| `IsCivilian` | implemented |  |
| `IsDamageEnoughForLootingAndReward` | **missing** |  |
| `IsDungeonBoss` | implemented |  |
| `IsElite` | implemented |  |
| `IsGuard` | implemented |  |
| `IsInEvadeMode` | implemented |  |
| `IsRacialLeader` | implemented |  |
| `IsRegeneratingHealth` | implemented |  |
| `IsReputationGainDisabled` | implemented |  |
| `IsTappedBy` | implemented |  |
| `IsTargetableForAttack` | **missing** |  |
| `IsTrigger` | implemented |  |
| `IsWorldBoss` | implemented |  |
| `MoveWaypoint` | implemented |  |
| `RemoveCorpse` | implemented |  |
| `RemoveFromWorld` | **missing** |  |
| `RemoveLootMode` | implemented |  |
| `ResetAllThreat` | implemented |  |
| `ResetLootMode` | implemented |  |
| `Respawn` | implemented |  |
| `SaveToDB` | implemented |  |
| `SelectVictim` | implemented |  |
| `SetAggroEnabled` | **missing** |  |
| `SetDeathState` | implemented |  |
| `SetDefaultMovementType` | implemented |  |
| `SetDisableGravity` | implemented |  |
| `SetDisableReputationGain` | implemented |  |
| `SetEquipmentSlots` | implemented |  |
| `SetHomePosition` | implemented |  |
| `SetHover` | implemented |  |
| `SetInCombatWithZone` | implemented |  |
| `SetLootMode` | implemented |  |
| `SetNoCallAssistance` | implemented |  |
| `SetNoSearchAssistance` | implemented |  |
| `SetNPCFlags` | implemented |  |
| `SetReactState` | implemented |  |
| `SetRegeneratingHealth` | implemented |  |
| `SetRespawnDelay` | implemented |  |
| `SetWalk` | implemented |  |
| `SetWanderRadius` | implemented |  |
| `UpdateEntry` | implemented |  |
### Class `GameObject` (inherits `WorldObject`)

| Documented method | Status | Note |
|---|---|---|
| `Despawn` | implemented |  |
| `GetDBTableGUIDLow` | implemented |  |
| `GetDisplayId` | **missing** |  |
| `GetGoState` | implemented |  |
| `GetLootRecipient` | **missing** |  |
| `GetLootRecipientGroup` | **missing** |  |
| `GetLootState` | implemented |  |
| `HasQuest` | **missing** |  |
| `IsActive` | **missing** |  |
| `IsDestructible` | **missing** |  |
| `IsSpawned` | implemented |  |
| `IsTransport` | **missing** |  |
| `RemoveFromWorld` | **missing** |  |
| `Respawn` | implemented |  |
| `SaveToDB` | **missing** |  |
| `SetGoState` | implemented |  |
| `SetLootState` | implemented |  |
| `SetRespawnTime` | **missing** |  |
| `SetSpawnedByDefault` | **missing** |  |
| `UseDoorOrButton` | implemented |  |
### Class `Item` (inherits `Object`)

| Documented method | Status | Note |
|---|---|---|
| `CanBeTraded` | implemented |  |
| `ClearEnchantment` | implemented |  |
| `GetAllowableClass` | implemented |  |
| `GetAllowableRace` | implemented |  |
| `GetArmor` | implemented |  |
| `GetBagSize` | implemented |  |
| `GetBagSlot` | implemented |  |
| `GetBuyCount` | implemented |  |
| `GetBuyPrice` | implemented |  |
| `GetClass` | implemented |  |
| `GetCount` | implemented |  |
| `GetDamageInfo` | implemented |  |
| `GetDisplayId` | implemented |  |
| `GetDurability` | implemented |  |
| `GetEnchantmentId` | implemented |  |
| `GetEntry` | implemented |  |
| `GetExtraFlags` | **missing** |  |
| `GetFlags` | implemented |  |
| `GetFlags2` | implemented |  |
| `GetInventoryType` | implemented |  |
| `GetItemId` | implemented |  |
| `GetItemLevel` | implemented |  |
| `GetItemLink` | implemented |  |
| `GetItemSet` | implemented |  |
| `GetMaxDurability` | implemented |  |
| `GetMaxStackCount` | implemented |  |
| `GetName` | implemented |  |
| `GetOwner` | implemented |  |
| `GetOwnerGUID` | implemented |  |
| `GetQuality` | implemented |  |
| `GetRandomProperty` | **missing** |  |
| `GetRandomSuffix` | **missing** |  |
| `GetRequiredLevel` | implemented |  |
| `GetSellPrice` | implemented |  |
| `GetSlot` | implemented |  |
| `GetSpeed` | **missing** |  |
| `GetSpellId` | implemented |  |
| `GetSpellTrigger` | implemented |  |
| `GetStatInfo` | implemented |  |
| `GetStatsCount` | implemented |  |
| `GetSubClass` | implemented |  |
| `HasQuest` | implemented |  |
| `IsArmorVellum` | **stub** | stub: not available (Classic 1.60) |
| `IsBag` | implemented |  |
| `IsBoundAccountWide` | implemented |  |
| `IsBoundByEnchant` | implemented |  |
| `IsBroken` | implemented |  |
| `IsConjuredConsumable` | implemented |  |
| `IsCurrencyToken` | implemented |  |
| `IsEquipped` | implemented |  |
| `IsInBag` | implemented |  |
| `IsInTrade` | implemented |  |
| `IsLocked` | implemented |  |
| `IsNotBoundToPlayer` | implemented |  |
| `IsNotEmptyBag` | implemented |  |
| `IsPotion` | implemented |  |
| `IsRefundExpired` | implemented |  |
| `IsSoulBound` | implemented |  |
| `IsWeaponVellum` | **stub** | stub: not available (Classic 1.60) |
| `SaveToDB` | **stub** | stub: not available (this server) |
| `SetBinding` | implemented |  |
| `SetCount` | implemented |  |
| `SetEnchantment` | implemented |  |
| `SetOwner` | **stub** | stub: not available (this server) |
| `SetRandomProperty` | **missing** |  |
| `SetRandomSuffix` | **missing** |  |
### Class `Group`

| Documented method | Status | Note |
|---|---|---|
| `AddMember` | implemented |  |
| `ConvertToLFG` | **missing** |  |
| `ConvertToRaid` | **missing** |  |
| `Disband` | **missing** |  |
| `GetGUID` | **missing** |  |
| `GetLeaderGUID` | implemented |  |
| `GetMemberFlags` | **missing** |  |
| `GetMemberGroup` | **missing** |  |
| `GetMemberGUID` | **missing** |  |
| `GetMembers` | implemented |  |
| `GetMembersCount` | **missing** |  |
| `HasFreeSlotSubGroup` | **missing** |  |
| `IsAssistant` | **missing** |  |
| `IsBFGroup` | **missing** |  |
| `IsBGGroup` | **missing** |  |
| `IsFull` | **missing** |  |
| `IsLeader` | **missing** |  |
| `IsLFGGroup` | **missing** |  |
| `IsMember` | **missing** |  |
| `IsRaidGroup` | implemented |  |
| `RemoveMember` | implemented |  |
| `SameSubGroup` | **missing** |  |
| `SendPacket` | implemented |  |
| `SetLeader` | **missing** |  |
| `SetMemberFlag` | **missing** |  |
| `SetMembersGroup` | **missing** |  |
| `SetTargetIcon` | **missing** |  |
### Class `Guild`

| Documented method | Status | Note |
|---|---|---|
| `AddMember` | **missing** |  |
| `DeleteMember` | **missing** |  |
| `Disband` | **missing** |  |
| `GetId` | **missing** |  |
| `GetInfo` | **missing** |  |
| `GetLeader` | **missing** |  |
| `GetLeaderGUID` | implemented |  |
| `GetMemberCount` | **missing** |  |
| `GetMembers` | implemented |  |
| `GetMOTD` | **missing** |  |
| `GetName` | implemented |  |
| `SendPacket` | implemented |  |
| `SendPacketToRanked` | **missing** |  |
| `SetBankTabText` | **stub** | stub: not available (Classic 1.60) |
| `SetLeader` | **missing** |  |
| `SetMemberRank` | **missing** |  |
### Class `Quest`

| Documented method | Status | Note |
|---|---|---|
| `GetFlags` | **missing** |  |
| `GetId` | implemented |  |
| `GetLevel` | implemented |  |
| `GetMaxLevel` | **missing** |  |
| `GetMinLevel` | **missing** |  |
| `GetNextQuestId` | **missing** |  |
| `GetNextQuestInChain` | **missing** |  |
| `GetPrevQuestId` | **missing** |  |
| `GetTitle` | implemented |  |
| `GetType` | **missing** |  |
| `HasFlag` | **missing** |  |
| `IsDaily` | implemented |  |
| `IsRepeatable` | **missing** |  |
### Class `Spell`

| Documented method | Status | Note |
|---|---|---|
| `CalculateDamage` | implemented |  |
| `Cancel` | implemented |  |
| `Cast` | implemented |  |
| `CleanupTargetList` | implemented |  |
| `Finish` | implemented |  |
| `GetCaster` | implemented |  |
| `GetCastTime` | implemented |  |
| `GetDelayMoment` | implemented |  |
| `GetDelayStart` | implemented |  |
| `GetDuration` | implemented |  |
| `GetEntry` | implemented |  |
| `GetGameObjectTargetCountForEffect` | implemented |  |
| `GetItemTargetCountForEffect` | implemented |  |
| `GetOriginalCaster` | implemented |  |
| `GetPowerCost` | implemented |  |
| `GetRuneState` | implemented |  |
| `GetSpellInfo` | implemented |  |
| `GetState` | implemented |  |
| `GetTarget` | implemented |  |
| `GetTargetDest` | implemented |  |
| `GetUnitTargetCountForEffect` | implemented |  |
| `IsAutoActionResetSpell` | implemented |  |
| `IsAutoRepeat` | implemented |  |
| `IsChannelActive` | implemented |  |
| `IsDeletable` | implemented |  |
| `IsIgnoringCooldowns` | implemented |  |
| `IsInterruptable` | implemented |  |
| `IsNeedSendToClient` | implemented |  |
| `IsPositive` | implemented |  |
| `IsProcDisabled` | implemented |  |
| `IsTriggered` | implemented |  |
| `IsTriggeredByAura` | implemented |  |
| `SetAutoRepeat` | implemented |  |
| `SetDelayStart` | implemented |  |
| `SetExecutedCurrently` | implemented |  |
| `SetReferencedFromCurrent` | implemented |  |
| `SetRuneState` | implemented |  |
| `SetSpellValue` | implemented |  |
| `SetState` | implemented |  |
| `UpdatePointers` | implemented |  |
### Class `Aura`

| Documented method | Status | Note |
|---|---|---|
| `GetAuraId` | implemented |  |
| `GetCaster` | implemented |  |
| `GetCasterGUID` | implemented |  |
| `GetCasterLevel` | implemented |  |
| `GetDuration` | implemented |  |
| `GetEntry` | implemented |  |
| `GetMaxDuration` | implemented |  |
| `GetOwner` | implemented |  |
| `GetSpellInfo` | implemented |  |
| `GetStackAmount` | implemented |  |
| `Remove` | implemented |  |
| `SetDuration` | implemented |  |
| `SetMaxDuration` | implemented |  |
| `SetStackAmount` | implemented |  |
### Class `Map`

| Documented method | Status | Note |
|---|---|---|
| `Data` | implemented |  |
| `GetAreaId` | implemented |  |
| `GetDifficulty` | implemented |  |
| `GetHeight` | implemented |  |
| `GetInstanceData` | **missing** |  |
| `GetInstanceId` | implemented |  |
| `GetMapId` | implemented |  |
| `GetName` | implemented |  |
| `GetPlayerCount` | implemented |  |
| `GetPlayers` | implemented |  |
| `GetWorldObject` | implemented |  |
| `IsArena` | implemented |  |
| `IsBattleground` | implemented |  |
| `IsDungeon` | implemented |  |
| `IsEmpty` | implemented |  |
| `IsHeroic` | implemented |  |
| `IsRaid` | implemented |  |
| `SaveInstanceData` | **missing** |  |
| `SetWeather` | implemented |  |
### Class `ElunaQuery`

| Documented method | Status | Note |
|---|---|---|
| `GetBool` | implemented |  |
| `GetColumnCount` | implemented |  |
| `GetDouble` | implemented |  |
| `GetFloat` | implemented |  |
| `GetInt16` | implemented |  |
| `GetInt32` | implemented |  |
| `GetInt64` | implemented |  |
| `GetInt8` | implemented |  |
| `GetRow` | implemented |  |
| `GetRowCount` | implemented |  |
| `GetString` | implemented |  |
| `GetUInt16` | implemented |  |
| `GetUInt32` | implemented |  |
| `GetUInt64` | implemented |  |
| `GetUInt8` | implemented |  |
| `IsNull` | implemented |  |
| `NextRow` | implemented |  |
### Class `WorldPacket`

| Documented method | Status | Note |
|---|---|---|
| `GetOpcode` | implemented | only what CreatePacket made or a packet event passes; the events get a read-only copy |
| `GetSize` | implemented |  |
| `ReadByte` | **missing** |  |
| `ReadDouble` | implemented |  |
| `ReadFloat` | implemented |  |
| `ReadGUID` | **missing** |  |
| `ReadLong` | **missing** |  |
| `ReadShort` | **missing** |  |
| `ReadString` | implemented |  |
| `ReadUByte` | **missing** |  |
| `ReadULong` | **missing** |  |
| `ReadUShort` | **missing** |  |
| `SetOpcode` | implemented |  |
| `WriteByte` | **missing** |  |
| `WriteDouble` | implemented |  |
| `WriteFloat` | implemented |  |
| `WriteGUID` | **missing** |  |
| `WriteLong` | **missing** |  |
| `WriteShort` | **missing** |  |
| `WriteString` | implemented |  |
| `WriteUByte` | **missing** |  |
| `WriteULong` | **missing** |  |
| `WriteUShort` | **missing** |  |
### Class `Corpse` (inherits `WorldObject`)

| Documented method | Status | Note |
|---|---|---|
| `GetGhostTime` | implemented |  |
| `GetOwnerGUID` | implemented |  |
| `GetType` | implemented |  |
| `ResetGhostTime` | implemented |  |
| `SaveToDB` | implemented |  |
### Class `BattleGround`

| Documented method | Status | Note |
|---|---|---|
| `GetAlivePlayersCountByTeam` | implemented |  |
| `GetBonusHonorFromKillCount` | implemented |  |
| `GetBracketId` | implemented |  |
| `GetEndTime` | implemented |  |
| `GetFreeSlotsForTeam` | implemented |  |
| `GetInstanceId` | implemented |  |
| `GetMap` | implemented |  |
| `GetMapId` | implemented |  |
| `GetMaxLevel` | implemented |  |
| `GetMaxPlayers` | implemented |  |
| `GetMaxPlayersPerTeam` | implemented |  |
| `GetMinLevel` | implemented |  |
| `GetMinPlayers` | implemented |  |
| `GetMinPlayersPerTeam` | implemented |  |
| `GetName` | implemented |  |
| `GetStatus` | implemented |  |
| `GetTypeId` | implemented |  |
| `GetWinner` | implemented |  |
### Class `Vehicle`

| Documented method | Status | Note |
|---|---|---|
| `AddPassenger` | **stub** | stub: not available (Classic 1.60) |
| `GetEntry` | **stub** | stub: not available (Classic 1.60) |
| `GetOwner` | **stub** | stub: not available (Classic 1.60) |
| `GetPassenger` | **stub** | stub: not available (Classic 1.60) |
| `IsOnBoard` | **stub** | stub: not available (Classic 1.60) |
| `RemovePassenger` | **stub** | stub: not available (Classic 1.60) |
### Class `Bot` (engine only: no documented counterpart)

`Bot` extras (in the engine, not in the documented API): `Assist` (our extension (B2): the order of this bot), `Attack` (our extension (B2): the order of this bot), `ClearCrowdControl` (our extension (B2): the order of this bot), `Come` (our extension (B2): the order of this bot), `CorpseRun` (our extension (B2): the order of this bot), `CrowdControl` (our extension (B2): the order of this bot), `EndPull` (our extension (B2): the order of this bot), `Engage` (our extension (B2): the order of this bot), `Flee` (our extension (B2): the order of this bot), `Follow` (our extension (B2): the order of this bot), `Free` (our extension (B2): the order of this bot), `GetClass` (our extension (B1)), `GetCommander` (our extension (B1)), `GetEngineState` (our extension (B1)), `GetGroup` (our extension (B1)), `GetGUID` (our extension (B1)), `GetLastOrder` (our extension (B1)), `GetLevel` (our extension (B1)), `GetMaster` (our extension (B1)), `GetName` (our extension (B1)), `GetPlayer` (our extension (B1)), `GetRole` (our extension (B1)), `GetRun` (our extension (B1)), `GetSpec` (our extension (B1)), `Guard` (our extension (B2): the order of this bot), `HasEngine` (our extension (B1)), `Hold` (our extension (B2): the order of this bot), `IsAlive` (our extension (B1)), `IsInCombat` (our extension (B1)), `IsRoleForced` (our extension (B1)), `IsValid` (our extension (B1)), `PauseRun` (our extension (B2): the order of this bot), `Pull` (our extension (B2): the order of this bot), `Rebuff` (our extension (B2): the order of this bot), `Release` (our extension (B2): the order of this bot), `ResumeRun` (our extension (B2): the order of this bot), `SetToggle` (our extension (B2): the order of this bot), `Stance` (our extension (B2): the order of this bot), `StartRun` (our extension (B2): the order of this bot), `Stay` (our extension (B2): the order of this bot), `Stop` (our extension (B2): the order of this bot), `StopRun` (our extension (B2): the order of this bot)


### Class `AuraEffect`

| Documented method | Status | Note |
|---|---|---|
| `CalculateAmount` | **missing** |  |
| `CalculatePeriodic` | **missing** |  |
| `CanBeRecalculated` | **missing** |  |
| `ChangeAmount` | **missing** |  |
| `GetAmount` | **missing** |  |
| `GetAmplitude` | **missing** |  |
| `GetAuraType` | **missing** |  |
| `GetBase` | **missing** |  |
| `GetBaseAmount` | **missing** |  |
| `GetCaster` | **missing** |  |
| `GetCasterGUID` | **missing** |  |
| `GetEffIndex` | **missing** |  |
| `GetId` | **missing** |  |
| `GetPeriodicTimer` | **missing** |  |
| `GetRemainingTicks` | **missing** |  |
| `GetSpellInfo` | **missing** |  |
| `GetTargetList` | **missing** |  |
| `GetTickNumber` | **missing** |  |
| `GetTotalTicks` | **missing** |  |
| `RecalculateAmount` | **missing** |  |
| `SetAmount` | **missing** |  |
| `SetCanBeRecalculated` | **missing** |  |
| `SetPeriodicTimer` | **missing** |  |
### Class `ElunaProcInfo`

| Documented method | Status | Note |
|---|---|---|
| `GetActionTarget` | **missing** |  |
| `GetActor` | **missing** |  |
| `GetAttackType` | **missing** |  |
| `GetBlock` | **missing** |  |
| `GetDamage` | **missing** |  |
| `GetDamageAbsorb` | **missing** |  |
| `GetDamageType` | **missing** |  |
| `GetEffectiveHeal` | **missing** |  |
| `GetHeal` | **missing** |  |
| `GetHealAbsorb` | **missing** |  |
| `GetHitMask` | **missing** |  |
| `GetMap` | **missing** |  |
| `GetProcSpell` | **missing** |  |
| `GetResist` | **missing** |  |
| `GetSchoolMask` | **missing** |  |
| `GetSpellInfo` | **missing** |  |
| `GetSpellPhaseMask` | **missing** |  |
| `GetSpellTypeMask` | **missing** |  |
| `GetTypeMask` | **missing** |  |
| `HasDamage` | **missing** |  |
| `HasHeal` | **missing** |  |
| `SetAbsorbDamage` | **missing** |  |
| `SetAbsorbHeal` | **missing** |  |
| `SetBlockDamage` | **missing** |  |
| `SetDamage` | **missing** |  |
| `SetEffectiveHeal` | **missing** |  |
| `SetHeal` | **missing** |  |
| `SetResistDamage` | **missing** |  |
### Class `SpellInfo`

| Documented method | Status | Note |
|---|---|---|
| `CalcCastTime` | implemented |  |
| `CanBeUsedInCombat` | implemented |  |
| `CanDispelAura` | implemented |  |
| `CanPierceImmuneAura` | implemented |  |
| `CanSpellProvideImmunityAgainstAura` | implemented |  |
| `CheckTarget` | **missing** |  |
| `CheckTargetCreatureType` | **missing** |  |
| `EffectIsAreaAuraEffect` | **missing** |  |
| `EffectIsAura` | **missing** |  |
| `EffectIsAuraType` | **missing** |  |
| `EffectIsEffect` | **missing** |  |
| `EffectIsEffectType` | **missing** |  |
| `EffectIsTargetingArea` | **missing** |  |
| `EffectIsUnitOwnedAuraEffect` | **missing** |  |
| `GetActiveIconID` | implemented |  |
| `GetAllEffectsMechanicMask` | **missing** |  |
| `GetAllowedMechanicMask` | **missing** |  |
| `GetAreaGroupId` | implemented |  |
| `GetAttackType` | implemented |  |
| `GetAttributes` | implemented |  |
| `GetAttributesCu` | implemented |  |
| `GetAttributesEx` | implemented |  |
| `GetAttributesEx2` | implemented |  |
| `GetAttributesEx3` | implemented |  |
| `GetAttributesEx4` | implemented |  |
| `GetAttributesEx5` | implemented |  |
| `GetAttributesEx6` | implemented |  |
| `GetAttributesEx7` | implemented |  |
| `GetAuraInterruptFlags` | implemented |  |
| `GetAuraState` | implemented |  |
| `GetBaseLevel` | implemented |  |
| `GetCasterAuraSpell` | implemented |  |
| `GetCasterAuraState` | implemented |  |
| `GetCasterAuraStateNot` | implemented |  |
| `GetCategory` | implemented |  |
| `GetCategoryRecoveryTime` | implemented |  |
| `GetChannelInterruptFlags` | implemented |  |
| `GetDiminishingReturnsGroupForSpell` | **missing** |  |
| `GetDiminishingReturnsGroupType` | **missing** |  |
| `GetDiminishingReturnsLimitDuration` | **missing** |  |
| `GetDiminishingReturnsMaxLevel` | **missing** |  |
| `GetDispel` | implemented |  |
| `GetDmgClass` | implemented |  |
| `GetDuration` | implemented |  |
| `GetEffectAmplitude` | **missing** |  |
| `GetEffectApplyAuraName` | **missing** |  |
| `GetEffectBasePoints` | **missing** |  |
| `GetEffectBonusMultiplier` | **missing** |  |
| `GetEffectCalcRadius` | **missing** |  |
| `GetEffectCalcValue` | **missing** |  |
| `GetEffectChainTarget` | **missing** |  |
| `GetEffectDamageMultiplier` | **missing** |  |
| `GetEffectDieSides` | **missing** |  |
| `GetEffectHasRadius` | **missing** |  |
| `GetEffectImplicitTargetType` | **missing** |  |
| `GetEffectIndex` | **missing** |  |
| `GetEffectItemType` | **missing** |  |
| `GetEffectMechanic` | **missing** |  |
| `GetEffectMechanicMask` | **missing** |  |
| `GetEffectMiscValue` | **missing** |  |
| `GetEffectMiscValueB` | **missing** |  |
| `GetEffectPointsPerComboPoint` | **missing** |  |
| `GetEffectProvidedTargetMask` | **missing** |  |
| `GetEffectRealPointsPerLevel` | **missing** |  |
| `GetEffectSpellClassMask` | **missing** |  |
| `GetEffectTargetACheckType` | **missing** |  |
| `GetEffectTargetADirectionAngle` | **missing** |  |
| `GetEffectTargetADirectionType` | **missing** |  |
| `GetEffectTargetAIsArea` | **missing** |  |
| `GetEffectTargetAObjectType` | **missing** |  |
| `GetEffectTargetAReferenceType` | **missing** |  |
| `GetEffectTargetASelectionCategory` | **missing** |  |
| `GetEffectTargetATarget` | **missing** |  |
| `GetEffectTargetBCheckType` | **missing** |  |
| `GetEffectTargetBDirectionAngle` | **missing** |  |
| `GetEffectTargetBDirectionType` | **missing** |  |
| `GetEffectTargetBIsArea` | **missing** |  |
| `GetEffectTargetBObjectType` | **missing** |  |
| `GetEffectTargetBReferenceType` | **missing** |  |
| `GetEffectTargetBSelectionCategory` | **missing** |  |
| `GetEffectTargetBTarget` | **missing** |  |
| `GetEffectTriggerSpell` | **missing** |  |
| `GetEffectType` | **missing** |  |
| `GetEffectUsedTargetObjectType` | **missing** |  |
| `GetEffectValueMultiplier` | **missing** |  |
| `GetEquippedItemClass` | implemented |  |
| `GetEquippedItemInventoryTypeMask` | implemented |  |
| `GetEquippedItemSubClassMask` | implemented |  |
| `GetExcludeCasterAuraSpell` | **missing** |  |
| `GetExcludeTargetAuraSpell` | **missing** |  |
| `GetExplicitTargetMask` | implemented |  |
| `GetFacingCasterFlags` | implemented |  |
| `GetId` | implemented |  |
| `GetInterruptFlags` | implemented |  |
| `GetManaCost` | implemented |  |
| `GetManaCostPercentage` | **missing** |  |
| `GetManaCostPerlevel` | implemented |  |
| `GetManaPerSecond` | implemented |  |
| `GetManaPerSecondPerLevel` | **missing** |  |
| `GetMaxAffectedTargets` | implemented |  |
| `GetMaxDuration` | implemented |  |
| `GetMaxLevel` | implemented |  |
| `GetMaxRange` | implemented |  |
| `GetMaxTargetLevel` | implemented |  |
| `GetMaxTicks` | **missing** |  |
| `GetMechanic` | implemented |  |
| `GetMinRange` | implemented |  |
| `GetPowerType` | implemented |  |
| `GetPreventionType` | implemented |  |
| `GetPriority` | **missing** |  |
| `GetProcChance` | implemented |  |
| `GetProcCharges` | implemented |  |
| `GetProcFlags` | implemented |  |
| `GetRank` | implemented |  |
| `GetReagent` | implemented |  |
| `GetReagentCount` | implemented |  |
| `GetRecoveryTime` | implemented |  |
| `GetRequiresSpellFocus` | implemented |  |
| `GetRuneCostID` | **missing** |  |
| `GetSchoolMask` | implemented |  |
| `GetSpeed` | implemented |  |
| `GetSpellFamilyFlags` | implemented |  |
| `GetSpellFamilyName` | implemented |  |
| `GetSpellIconID` | implemented |  |
| `GetSpellLevel` | implemented |  |
| `GetSpellMechanicMaskByEffectMask` | **missing** |  |
| `GetSpellSpecific` | implemented |  |
| `GetSpellVisual` | **missing** |  |
| `GetStackAmount` | implemented |  |
| `GetStances` | implemented |  |
| `GetStancesNot` | implemented |  |
| `GetStartRecoveryCategory` | implemented |  |
| `GetStartRecoveryTime` | implemented |  |
| `GetTargetAuraSpell` | implemented |  |
| `GetTargetAuraState` | implemented |  |
| `GetTargetAuraStateNot` | implemented |  |
| `GetTargetCreatureType` | implemented |  |
| `GetTargets` | implemented |  |
| `GetTotem` | implemented |  |
| `GetTotemCategory` | implemented |  |
| `HasAreaAuraEffect` | implemented |  |
| `HasAura` | implemented |  |
| `HasEffect` | implemented |  |
| `HasInitialAggro` | implemented |  |
| `HasOnlyDamageEffects` | implemented |  |
| `IsAbilityLearnedWithProfession` | **missing** |  |
| `IsAbilityOfSkillType` | **missing** |  |
| `IsAffected` | **missing** |  |
| `IsAffectedBySpellMod` | **missing** |  |
| `IsAffectedBySpellMods` | implemented |  |
| `IsAffectingArea` | implemented |  |
| `IsAllowingDeadTarget` | implemented |  |
| `IsAuraExclusiveBySpecificPerCasterWith` | **missing** |  |
| `IsAuraExclusiveBySpecificWith` | **missing** |  |
| `IsAutocastable` | implemented |  |
| `IsAutoRepeatRangedSpell` | implemented |  |
| `IsBreakingStealth` | **missing** |  |
| `IsChanneled` | implemented |  |
| `IsCooldownStartedOnEvent` | implemented |  |
| `IsDeathPersistent` | implemented |  |
| `IsDifferentRankOf` | implemented |  |
| `IsExplicitDiscovery` | **missing** |  |
| `IsGroupBuff` | implemented |  |
| `IsHighRankOf` | implemented |  |
| `IsItemFitToSpellRequirements` | **missing** |  |
| `IsLootCrafting` | **missing** |  |
| `IsMoveAllowedChannel` | implemented |  |
| `IsMultiSlotAura` | implemented |  |
| `IsNextMeleeSwingSpell` | implemented |  |
| `IsPassive` | implemented |  |
| `IsPassiveStackableWithRanks` | **missing** |  |
| `IsPositive` | implemented |  |
| `IsPositiveEffect` | implemented |  |
| `IsPrimaryProfession` | **missing** |  |
| `IsPrimaryProfessionFirstRank` | **missing** |  |
| `IsProfession` | **missing** |  |
| `IsProfessionOrRiding` | **missing** |  |
| `IsRangedWeaponSpell` | implemented |  |
| `IsRanked` | implemented |  |
| `IsRankOf` | implemented |  |
| `IsRequiringDeadTarget` | implemented |  |
| `IsSelfCast` | **missing** |  |
| `IsSingleTarget` | implemented |  |
| `IsStackableOnOneSlotWithDifferentCasters` | **missing** |  |
| `IsStackableWithRanks` | implemented |  |
| `IsTargetingArea` | implemented |  |
| `NeedsComboPoints` | **missing** |  |
| `NeedsExplicitUnitTarget` | implemented |  |
| `NeedsToBeTriggeredByCaster` | implemented |  |
| `SpellCancelsAuraEffect` | **missing** |  |
### Events: server

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | NETWORK_START | NETWORK_START | implemented |  |  |
| 2 | NETWORK_STOP | NETWORK_STOP | implemented |  |  |
| 3 | SOCKET_OPEN | SOCKET_OPEN | implemented |  |  |
| 4 | SOCKET_CLOSE | SOCKET_CLOSE | implemented |  |  |
| 5 | PACKET_RECEIVE | PACKET_RECEIVE | implemented |  |  |
| 6 | PACKET_RECEIVE_UNKNOWN | PACKET_RECEIVE_UNKNOWN | implemented |  | the spec names no driving hook (section 3: none); the engine registers it and logs nothing |
| 7 | PACKET_SEND | PACKET_SEND | implemented |  |  |
| 8 | OPEN_STATE_CHANGE | OPEN_STATE_CHANGE | implemented |  |  |
| 9 | CONFIG_LOAD | CONFIG_LOAD | implemented |  |  |
| 11 | SHUTDOWN_INIT | SHUTDOWN_INIT | implemented |  |  |
| 12 | SHUTDOWN_CANCEL | SHUTDOWN_CANCEL | implemented |  |  |
| 13 | UPDATE | UPDATE | implemented |  |  |
| 14 | STARTUP | STARTUP | implemented |  |  |
| 15 | SHUTDOWN | SHUTDOWN | implemented |  |  |
| 16 | LUA_STATE_CLOSE | LUA_STATE_CLOSE | implemented |  |  |
| 17 | MAP_CREATE | MAP_CREATE | implemented |  | world (continent) maps only; dungeons and raids need C11 |
| 18 | MAP_DESTROY | MAP_DESTROY | implemented |  | world (continent) maps only; dungeons and raids need C11 |
| 19 | GRID_LOAD | GRID_LOAD | implemented |  |  |
| 20 | GRID_UNLOAD | GRID_UNLOAD | implemented |  |  |
| 21 | MAP_PLAYER_ENTER | MAP_PLAYER_ENTER | implemented |  | world (continent) maps only; dungeons and raids need C11 |
| 22 | MAP_PLAYER_LEAVE | MAP_PLAYER_LEAVE | implemented |  | world (continent) maps only; dungeons and raids need C11 |
| 23 | MAP_UPDATE | MAP_UPDATE | implemented |  | world (continent) maps only; dungeons and raids need C11 |
| 24 | TRIGGER_ON_TRIGGER | TRIGGER_ON_TRIGGER | **stub** | C8 | stub: needs core hook C8 |
| 25 | WEATHER_CHANGE | WEATHER_CHANGE | implemented |  |  |
| 26 | AUCTION_ADD | AUCTION_ADD | implemented |  |  |
| 27 | AUCTION_REMOVE | AUCTION_REMOVE | implemented |  |  |
| 28 | AUCTION_SUCCESSFUL | AUCTION_SUCCESSFUL | implemented |  |  |
| 29 | AUCTION_EXPIRE | AUCTION_EXPIRE | implemented |  |  |
| 30 | ADDON_ON_MESSAGE | ADDON_ON_MESSAGE | implemented |  |  |
| 31 | DELETE_CREATURE | DELETE_CREATURE | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
| 32 | DELETE_GAMEOBJECT | DELETE_GAMEOBJECT | implemented | C2 | needs core hook C2; the engine names its interim ScriptName path |
| 33 | LUA_STATE_OPEN | LUA_STATE_OPEN | implemented |  |  |
| 34 | GAME_EVENT_START | GAME_EVENT_START | **stub** | C7 | stub: needs core hook C7 |
| 35 | GAME_EVENT_STOP | GAME_EVENT_STOP | **stub** | C7 | stub: needs core hook C7 |
### Events: player

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | CHARACTER_CREATE | CHARACTER_CREATE | implemented |  |  |
| 2 | CHARACTER_DELETE | CHARACTER_DELETE | implemented |  |  |
| 3 | LOGIN | LOGIN | implemented |  |  |
| 4 | LOGOUT | LOGOUT | implemented |  |  |
| 5 | SPELL_CAST | SPELL_CAST | implemented |  |  |
| 6 | KILL_PLAYER | KILL_PLAYER | implemented |  |  |
| 7 | KILL_CREATURE | KILL_CREATURE | implemented |  |  |
| 8 | KILLED_BY_CREATURE | KILLED_BY_CREATURE | implemented |  |  |
| 9 | DUEL_REQUEST | DUEL_REQUEST | implemented |  |  |
| 10 | DUEL_START | DUEL_START | implemented |  |  |
| 11 | DUEL_END | DUEL_END | implemented |  |  |
| 12 | GIVE_XP | GIVE_XP | implemented |  |  |
| 13 | LEVEL_CHANGE | LEVEL_CHANGE | implemented |  |  |
| 14 | MONEY_CHANGE | MONEY_CHANGE | implemented |  |  |
| 15 | REPUTATION_CHANGE | REPUTATION_CHANGE | implemented |  |  |
| 16 | TALENTS_CHANGE | TALENTS_CHANGE | implemented |  |  |
| 17 | TALENTS_RESET | TALENTS_RESET | implemented |  |  |
| 18 | CHAT | CHAT | **stub** | C6 | stub: needs core hook C6 |
| 19 | WHISPER | WHISPER | **stub** | C6 | stub: needs core hook C6 |
| 20 | GROUP_CHAT | GROUP_CHAT | **stub** | C6 | stub: needs core hook C6 |
| 21 | GUILD_CHAT | GUILD_CHAT | **stub** | C6 | stub: needs core hook C6 |
| 22 | CHANNEL_CHAT | CHANNEL_CHAT | **stub** | C6 | stub: needs core hook C6 |
| 23 | EMOTE | EMOTE | **stub** | C5 | stub: needs core hook C5 |
| 24 | TEXT_EMOTE | TEXT_EMOTE | implemented |  |  |
| 25 | SAVE | SAVE | implemented |  |  |
| 26 | BIND_TO_INSTANCE | BIND_TO_INSTANCE | implemented |  |  |
| 27 | UPDATE_ZONE | UPDATE_ZONE | implemented |  |  |
| 28 | MAP_CHANGE | MAP_CHANGE | implemented |  |  |
| 29 | EQUIP | EQUIP | **stub** | C5 | stub: needs core hook C5 |
| 30 | FIRST_LOGIN | FIRST_LOGIN | implemented |  |  |
| 31 | CAN_USE_ITEM | CAN_USE_ITEM | **stub** | C5 | stub: needs core hook C5 |
| 32 | LOOT_ITEM | LOOT_ITEM | implemented |  |  |
| 33 | ENTER_COMBAT | ENTER_COMBAT | **stub** | C5 | stub: needs core hook C5 |
| 34 | LEAVE_COMBAT | LEAVE_COMBAT | **stub** | C5 | stub: needs core hook C5 |
| 35 | REPOP | REPOP | implemented |  |  |
| 36 | RESURRECT | RESURRECT | **stub** | C5 | stub: needs core hook C5 |
| 37 | LOOT_MONEY | LOOT_MONEY | **stub** | C5 | stub: needs core hook C5 |
| 38 | QUEST_ABANDON | QUEST_ABANDON | **stub** | C5 | stub: needs core hook C5 |
| 39 | LEARN_TALENTS | LEARN_TALENTS | **stub** | C5 | stub: needs core hook C5 |
| 40 | ENVIRONMENTAL_DEATH | ENVIRONMENTAL_DEATH | **stub** | C5 | stub: needs core hook C5 |
| 41 | TRADE_ACCEPT | TRADE_ACCEPT | **stub** | C5 | stub: needs core hook C5 |
| 42 | COMMAND | COMMAND | **stub** | C4 | stub: needs core hook C4 |
| 43 | SKILL_CHANGE | SKILL_CHANGE | **stub** | C5 | stub: needs core hook C5 |
| 44 | LEARN_SPELL | LEARN_SPELL | **stub** | C5 | stub: needs core hook C5 |
| 45 | ACHIEVEMENT_COMPLETE | ACHIEVEMENT_COMPLETE | **stub** |  | stub: not available (no achievements on Classic 1.60) |
| 46 | DISCOVER_TAXI | DISCOVER_TAXI | **stub** | C5 | stub: needs core hook C5 |
| 47 | UPDATE_AREA | UPDATE_AREA | implemented |  |  |
| 48 | CAN_INIT_TRADE | CAN_INIT_TRADE | **stub** | C5 | stub: needs core hook C5 |
| 49 | CAN_SEND_MAIL | CAN_SEND_MAIL | **stub** | C5 | stub: needs core hook C5 |
| 50 | CAN_JOIN_LFG | CAN_JOIN_LFG | **stub** |  | stub: not available (no dungeon finder on Classic 1.60) |
| 51 | — | QUEST_STATUS_CHANGED | implemented |  | documented by name; the spec's id table has no id for it |
### Events: creature

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | ENTER_COMBAT | ENTER_COMBAT | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
| 2 | LEAVE_COMBAT | LEAVE_COMBAT | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
| 3 | TARGET_DIED | TARGET_DIED | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 4 | DIED | DIED | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
| 5 | SPAWN | SPAWN | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
| 6 | REACH_WP | REACH_WP | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 7 | AIUPDATE | AIUPDATE | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 8 | RECEIVE_EMOTE | RECEIVE_EMOTE | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 9 | DAMAGE_TAKEN | DAMAGE_TAKEN | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 10 | PRE_COMBAT | PRE_COMBAT | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 12 | OWNER_ATTACKED | OWNER_ATTACKED | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 13 | OWNER_ATTACKED_AT | OWNER_ATTACKED_AT | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 14 | HIT_BY_SPELL | HIT_BY_SPELL | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 15 | SPELL_HIT_TARGET | SPELL_HIT_TARGET | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 19 | JUST_SUMMONED_CREATURE | JUST_SUMMONED_CREATURE | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 20 | SUMMONED_CREATURE_DESPAWN | SUMMONED_CREATURE_DESPAWN | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 21 | SUMMONED_CREATURE_DIED | SUMMONED_CREATURE_DIED | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 22 | SUMMONED | SUMMONED | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 23 | RESET | RESET | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 24 | REACH_HOME | REACH_HOME | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 26 | CORPSE_REMOVED | CORPSE_REMOVED | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 27 | MOVE_IN_LOS | MOVE_IN_LOS | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 30 | DUMMY_EFFECT | DUMMY_EFFECT | **stub** | C9 | stub: needs core hook C9 |
| 31 | QUEST_ACCEPT | QUEST_ACCEPT | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 34 | QUEST_REWARD | QUEST_REWARD | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 35 | DIALOG_STATUS | DIALOG_STATUS | implemented |  | fires only for creatures whose ScriptName is forever_lua |
| 36 | ADD | ADD | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
| 37 | REMOVE | REMOVE | implemented | C1 | needs core hook C1; the engine names its interim ScriptName path |
### Events: gameobject

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | AIUPDATE | AIUPDATE | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 2 | SPAWN | SPAWN | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 3 | DUMMY_EFFECT | DUMMY_EFFECT | **stub** | C9 | stub: needs core hook C9 |
| 4 | QUEST_ACCEPT | QUEST_ACCEPT | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 5 | QUEST_REWARD | QUEST_REWARD | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 6 | DIALOG_STATUS | DIALOG_STATUS | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 7 | DESTROYED | DESTROYED | **stub** |  | stub: not available (no destructible game objects on Classic 1.60) |
| 8 | DAMAGED | DAMAGED | **stub** |  | stub: not available (no destructible game objects on Classic 1.60) |
| 9 | LOOT_STATE_CHANGE | LOOT_STATE_CHANGE | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 10 | GO_STATE_CHANGED | GO_STATE_CHANGED | implemented |  | fires only for game objects whose ScriptName is forever_lua_gameobject |
| 12 | ADD | ADD | implemented | C2 | needs core hook C2; the engine names its interim ScriptName path |
| 13 | REMOVE | REMOVE | implemented | C2 | needs core hook C2; the engine names its interim ScriptName path |
| 14 | USE | USE | implemented | C2 | needs core hook C2; the engine names its interim ScriptName path |
### Events: item

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | DUMMY_EFFECT | DUMMY_EFFECT | **stub** | C9 | stub: needs core hook C9 |
| 2 | USE | USE | implemented | C3 | needs core hook C3; the engine names its interim ScriptName path |
| 3 | QUEST_ACCEPT | QUEST_ACCEPT | implemented |  | fires for items whose ScriptName is forever_lua_item |
| 4 | EXPIRE | EXPIRE | implemented |  | fires for items whose ScriptName is forever_lua_item |
| 5 | REMOVE | REMOVE | implemented |  | fires for items whose ScriptName is forever_lua_item |
| — | on_add | — | **missing** | | documented, no id in the spec |
| — | on_equip | — | **missing** | | documented, no id in the spec |
| — | on_unequip | — | **missing** | | documented, no id in the spec |
### Events: player gossip

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | HELLO | HELLO | implemented |  | the documentation says HELLO is not applicable to player gossip; the engine registers it |
| 2 | SELECT | SELECT | implemented | C3 | needs core hook C3; the engine names its interim ScriptName path |
### Events: creature gossip

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | HELLO | HELLO | implemented |  |  |
| 2 | SELECT | SELECT | implemented |  |  |
### Events: gameobject gossip

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | HELLO | HELLO | implemented |  |  |
| 2 | SELECT | SELECT | implemented |  |  |
### Events: item gossip

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | HELLO | HELLO | implemented | C3 | needs core hook C3; the engine names its interim ScriptName path |
| 2 | SELECT | SELECT | implemented | C3 | needs core hook C3; the engine names its interim ScriptName path |
### Events: group

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | MEMBER_ADD | MEMBER_ADD | implemented |  |  |
| 2 | MEMBER_INVITE | MEMBER_INVITE | implemented |  |  |
| 3 | MEMBER_REMOVE | MEMBER_REMOVE | implemented |  |  |
| 4 | LEADER_CHANGE | LEADER_CHANGE | implemented |  |  |
| 5 | DISBAND | DISBAND | implemented |  |  |
| 6 | CREATE | CREATE | **stub** | C10 | stub: needs core hook C10 |
| — | on_member_accept | — | **missing** | | documented, no id in the spec |
### Events: guild

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | ADD_MEMBER | ADD_MEMBER | implemented |  |  |
| 2 | REMOVE_MEMBER | REMOVE_MEMBER | implemented |  |  |
| 3 | MOTD_CHANGE | MOTD_CHANGE | implemented |  |  |
| 4 | INFO_CHANGE | INFO_CHANGE | implemented |  |  |
| 5 | CREATE | CREATE | implemented |  |  |
| 6 | DISBAND | DISBAND | implemented |  |  |
| 7 | MONEY_WITHDRAW | MONEY_WITHDRAW | implemented |  |  |
| 8 | MONEY_DEPOSIT | MONEY_DEPOSIT | implemented |  |  |
| 9 | ITEM_MOVE | ITEM_MOVE | **stub** |  | stub: not available (no guild bank on Classic 1.60) |
| 10 | EVENT | EVENT | implemented |  |  |
| 11 | BANK_EVENT | BANK_EVENT | **stub** |  | stub: not available (no guild bank on Classic 1.60) |
### Events: map

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | INITIALIZE | INITIALIZE | implemented |  |  |
| 2 | LOAD | LOAD | **stub** | C11 | stub: needs core hook C11 |
| 3 | UPDATE | UPDATE | implemented |  |  |
| 4 | PLAYER_ENTER | PLAYER_ENTER | implemented |  |  |
| 5 | CREATURE_CREATE | CREATURE_CREATE | **stub** | C11 | stub: needs core hook C11 |
| 6 | GAMEOBJECT_CREATE | GAMEOBJECT_CREATE | **stub** | C11 | stub: needs core hook C11 |
| 7 | CHECK_ENCOUNTER | CHECK_ENCOUNTER | **stub** | C11 | stub: needs core hook C11 |
### Events: instance

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | INITIALIZE | INITIALIZE | implemented |  |  |
| 2 | LOAD | LOAD | **stub** | C11 | stub: needs core hook C11 |
| 3 | UPDATE | UPDATE | implemented |  |  |
| 4 | PLAYER_ENTER | PLAYER_ENTER | implemented |  |  |
| 5 | CREATURE_CREATE | CREATURE_CREATE | **stub** | C11 | stub: needs core hook C11 |
| 6 | GAMEOBJECT_CREATE | GAMEOBJECT_CREATE | **stub** | C11 | stub: needs core hook C11 |
| 7 | CHECK_ENCOUNTER | CHECK_ENCOUNTER | **stub** | C11 | stub: needs core hook C11 |
### Events: packet

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 5 | PACKET_RECEIVE | PACKET_RECEIVE | implemented |  | read-only copy of the core packet (ServerScript hook): a returned false cannot change the wire packet |
| 7 | PACKET_SEND | PACKET_SEND | implemented |  | read-only copy of the core packet (ServerScript hook): a returned false cannot change the wire packet |
### Events: battleground

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| 1 | START | START | implemented |  |  |
| 2 | END | END | implemented |  |  |
| 3 | CREATE | CREATE | implemented |  |  |
| 4 | PRE_DESTROY | PRE_DESTROY | implemented |  |  |
### Events: spell

| id | Documented name | Registered name | Status | Requires | Note |
|---|---|---|---|---|---|
| — | on_cast | — | **missing** | | documented, no id in the spec |
| — | on_aura_application | — | **missing** | | documented, no id in the spec |
| — | on_dispel | — | **missing** | | documented, no id in the spec |
| — | on_periodic_tick | — | **missing** | | documented, no id in the spec |
| — | on_periodic_update | — | **missing** | | documented, no id in the spec |
| — | on_aura_calc_amount | — | **missing** | | documented, no id in the spec |
| — | on_calc_periodic | — | **missing** | | documented, no id in the spec |
| — | on_check_proc | — | **missing** | | documented, no id in the spec |
| — | on_proc | — | **missing** | | documented, no id in the spec |
| — | on_check_cast | — | **missing** | | documented, no id in the spec |
| — | on_before_cast | — | **missing** | | documented, no id in the spec |
| — | on_after_cast | — | **missing** | | documented, no id in the spec |
| — | on_object_area_target | — | **missing** | | documented, no id in the spec |
| — | on_object_target | — | **missing** | | documented, no id in the spec |
| — | on_dest_target | — | **missing** | | documented, no id in the spec |
| — | on_effect_launch | — | **missing** | | documented, no id in the spec |
| — | on_effect_launch_target | — | **missing** | | documented, no id in the spec |
| — | on_effect_calc_absorb | — | **missing** | | documented, no id in the spec |
| — | on_effect_hit | — | **missing** | | documented, no id in the spec |
| — | on_before_hit | — | **missing** | | documented, no id in the spec |
| — | on_effect_hit_target | — | **missing** | | documented, no id in the spec |
| — | on_hit | — | **missing** | | documented, no id in the spec |
| — | on_after_hit | — | **missing** | | documented, no id in the spec |
### Events: bot (engine only: no documented counterpart)

| id | Registered name |
|---|---|
| 1 | BOT_ADDED |
| 2 | BOT_REMOVED |
| 3 | ORDER_GIVEN |
| 4 | ORDER_DONE |
| 5 | ORDER_FAILED |
| 6 | BOT_DIED |
| 7 | WIPE |
| 8 | RUN_STEP |
| 9 | RUN_STOPPED |


## Appendix A: test-mode surface (not available in eluna mode)

These are installed only with `Mode = test` (`registerApi`, and the `Data` alias of `ForeverData`); a script running in eluna mode does not see them.

| Table | Members |
|---|---|
| `Cmd` | `register` |
| `Data` | `get`, `set`, `delete`, `keys` |
| `Db` | `select`, `has` |
| `Event` | `on`, `off` |
| `Gm` | `run` |
| `Json` | `encode`, `decode` |
| `Timer` | `after`, `every`, `cancel` |
| `World` | `now`, `clock`, `gameTime`, `log` |

## Appendix B: documented names with no id in the spec's event tables

The documentation pages list event names without numeric ids; the maintainers' API specification, section 3 carries the ids. Where the documentation lists more names than the spec's table (item, group) or the spec has no table at all (spell), those names cannot be joined by id and are counted as missing.

* `group`: `on_member_accept`
* `item`: `on_add`, `on_equip`, `on_unequip`
* `spell`: `on_cast`, `on_aura_application`, `on_dispel`, `on_periodic_tick`, `on_periodic_update`, `on_aura_calc_amount`, `on_calc_periodic`, `on_check_proc`, `on_proc`, `on_check_cast`, `on_before_cast`, `on_after_cast`, `on_object_area_target`, `on_object_target`, `on_dest_target`, `on_effect_launch`, `on_effect_launch_target`, `on_effect_calc_absorb`, `on_effect_hit`, `on_before_hit`, `on_effect_hit_target`, `on_hit`, `on_after_hit`

## Appendix C: how the report is built

* Globals: `set(L, "Name", fl::safe<fn>)` in `registerGlobals`/`registerGameGlobals`, the `ForeverData` table of `registerForeverData`, and `print`/`require` of `registerCommon`.
* Class methods: the `luaL_Reg` tables passed to `addMethods` (plus `Corpse`, `BattleGround`, the `Map` methods of `registerBattleGroundMethods`, `Aura` where the engine has it) and the section 7 stubs of `addStubMethod`; inheritance from the `createClass` calls.
* Events: the `SpecRow` tables of `EluAlias.cpp`, joined to the families by `eluEventSpec`; the hook note comes from `hookGapFor` and the Classic-era note from `classicStubFor` (`EluCommon.cpp`), evaluated for a core without the C1-C4 hooks.
* Stub detection: the registered C++ function (or the single function it only forwards to) raises `not available on Classic 1.60`, `needs core hook Cn` or `not available on this server`.
