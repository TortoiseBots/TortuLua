---@meta


---@deprecated not available (this server)
---@return any
function AddTaxiPath() end

---@param entry any
---@param item any
---@param maxcount any
---@param incrtime any
---@param arg5 any
---@return any
function AddVendorItem(entry, item, maxcount, incrtime, arg5) end

---@param sql any
---@return any
function AuthDBExecute(sql) end

---@param sql any
---@return any
function AuthDBQuery(sql) end

---@param sql any
---@param arg2 any
---@return any
function AuthDBQueryAsync(sql, arg2) end

---@param banMode any
---@param nameOrIP any
---@param duration any
---@param reason any
---@param whoBanned any
---@return any
function Ban(banMode, nameOrIP, duration, reason, whoBanned) end

---@param arg1 any
---@param arg2 any
---@return any
function bit_and(arg1, arg2) end

---@param arg1 any
---@param arg2 any
---@return any
function bit_lshift(arg1, arg2) end

---@param arg1 any
---@return any
function bit_not(arg1) end

---@param arg1 any
---@param arg2 any
---@return any
function bit_or(arg1, arg2) end

---@param arg1 any
---@param arg2 any
---@return any
function bit_rshift(arg1, arg2) end

---@param arg1 any
---@param arg2 any
---@return any
function bit_xor(arg1, arg2) end

---@param sql any
---@return any
function CharDBExecute(sql) end

---@param sql any
---@return any
function CharDBQuery(sql) end

---@param sql any
---@param arg2 any
---@return any
function CharDBQueryAsync(sql, arg2) end

---@return any
function ClearBattleGroundEvents() end

---@return any
function ClearCreatureEvents() end

---@return any
function ClearCreatureGossipEvents() end

---@return any
function ClearGameObjectEvents() end

---@return any
function ClearGameObjectGossipEvents() end

---@return any
function ClearGroupEvents() end

---@return any
function ClearGuildEvents() end

---@return any
function ClearInstanceEvents() end

---@return any
function ClearItemEvents() end

---@return any
function ClearItemGossipEvents() end

---@return any
function ClearMapEvents() end

---@return any
function ClearPacketEvents() end

---@return any
function ClearPlayerEvents() end

---@return any
function ClearPlayerGossipEvents() end

---@return any
function ClearServerEvents() end

---@param arg1 any
---@param instance_id any
---@param event_type any
---@return any
function ClearUniqueCreatureEvents(arg1, instance_id, event_type) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function CreateLuaEvent(arg1, arg2, arg3) end

---@param opcode any
---@param arg2 any
---@return any
function CreatePacket(opcode, arg2) end

---@return any
function GetActiveGameEvents() end

---@param areaOrZoneId any
---@return any
function GetAreaName(areaOrZoneId) end

---answers 0 (Classic 1.60)
---@return any
function GetCoreExpansion() end

---answers ForeverCore
---@return any
function GetCoreName() end

---@return any
function GetCoreVersion() end

---@return any
function GetCurrTime() end

---@return any
function GetGameTime() end

---@return any
function GetGUIDEntry() end

---@param arg1 any
---@return any
function GetGUIDLow(arg1) end

---@return any
function GetGUIDType() end

---@param arg1 any
---@return any
function GetGuildByLeaderGUID(arg1) end

---@param name any
---@return any
function GetGuildByName(name) end

---@param entry any
---@return any
function GetItemLink(entry) end

---answers ForeverLua
---@return any
function GetLuaEngine() end

---@param mapId any
---@param arg2 any
---@return any
function GetMapById(mapId, arg2) end

---@deprecated not available (Classic 1.60)
---@return any
function GetOwnerHalaa() end

---@param arg1 any
---@return any
function GetPlayerByGUID(arg1) end

---@param name any
---@return any
function GetPlayerByName(name) end

---@return any
function GetPlayerCount() end

---@param arg1 any
---@param arg2 any
---@return any
function GetPlayersInWorld(arg1, arg2) end

---@param arg1 any
---@param arg2 any
---@return any
function GetPlayersOnMap(arg1, arg2) end

---@param questId any
---@return any
function GetQuest(questId) end

---@param spellId any
---@return any
function GetSpellInfo(spellId) end

---@return any
function GetStateInstanceId() end

---@return any
function GetStateMap() end

---@return any
function GetStateMapId() end

---@param arg1 any
---@return any
function GetTimeDiff(arg1) end

---@return any
function IsCompatibilityMode() end

---@param eventId any
---@return any
function IsGameEventActive(eventId) end

---@param player any
---@return any
function Kick(player) end

---save writes the spawn to the creature/gameobject table (eluna mode trusts its scripts)
---@param spawnType any
---@param entry any
---@param mapId any
---@param instanceId any
---@param x any
---@param y any
---@param z any
---@param o any
---@param arg9 any
---@param arg10 any
---@param arg11 any
---@return any
function PerformIngameSpawn(spawnType, entry, mapId, instanceId, x, y, z, o, arg9, arg10, arg11) end

---@return any
function PrintDebug() end

---@return any
function PrintError() end

---@return any
function PrintInfo() end

---@param event any
---@param arg2 any
---@param arg3 any
---@return any
function RegisterBGEvent(event, arg2, arg3) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterCreatureEvent(entry, event, arg3, arg4) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterCreatureGossipEvent(entry, event, arg3, arg4) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterGameObjectEvent(entry, event, arg3, arg4) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterGameObjectGossipEvent(entry, event, arg3, arg4) end

---@param event any
---@param arg2 any
---@param arg3 any
---@return any
function RegisterGroupEvent(event, arg2, arg3) end

---@param event any
---@param arg2 any
---@param arg3 any
---@return any
function RegisterGuildEvent(event, arg2, arg3) end

---@deprecated needs core hook C11
---@return any
function RegisterInstanceEvent() end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterItemEvent(entry, event, arg3, arg4) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterItemGossipEvent(entry, event, arg3, arg4) end

---@param map_id any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterMapEvent(map_id, event, arg3, arg4) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterPacketEvent(entry, event, arg3, arg4) end

---@param event any
---@param arg2 any
---@param arg3 any
---@return any
function RegisterPlayerEvent(event, arg2, arg3) end

---@param entry any
---@param event any
---@param arg3 any
---@param arg4 any
---@return any
function RegisterPlayerGossipEvent(entry, event, arg3, arg4) end

---@param event any
---@param arg2 any
---@param arg3 any
---@return any
function RegisterServerEvent(event, arg2, arg3) end

---@param arg1 any
---@param instance_id any
---@param event any
---@param arg4 any
---@param arg5 any
---@return any
function RegisterUniqueCreatureEvent(arg1, instance_id, event, arg4, arg5) end

---@deprecated not available (Classic 1.60)
---@return any
function RegisterVehicleEvent() end

---@return any
function ReloadEluna() end

---@param arg1 any
---@param arg2 any
---@return any
function RemoveEventById(arg1, arg2) end

---@param arg1 any
---@return any
function RemoveEvents(arg1) end

---@param command any
---@return any
function RunCommand(command) end

---@return any
function SaveAllPlayers() end

---@param subject any
---@param text any
---@param receiverGUIDLow any
---@param arg4 any
---@param arg5 any
---@param arg6 any
---@param arg7 any
---@param arg8 any
---@return any
function SendMail(subject, text, receiverGUIDLow, arg4, arg5, arg6, arg7, arg8) end

---@param message any
---@return any
function SendWorldMessage(message) end

---@param eventId any
---@param arg2 any
---@return any
function StartGameEvent(eventId, arg2) end

---@param eventId any
---@param arg2 any
---@return any
function StopGameEvent(eventId, arg2) end

---@param entry any
---@return any
function VendorRemoveAllItems(entry) end

---@param entry any
---@param item any
---@return any
function VendorRemoveItem(entry, item) end

---@param sql any
---@return any
function WorldDBExecute(sql) end

---@param sql any
---@return any
function WorldDBQuery(sql) end

---@param sql any
---@param arg2 any
---@return any
function WorldDBQueryAsync(sql, arg2) end

-- Engine extensions

---our extension (B2): the chat form of place/rally/fall back, for one player's bots
---@param player any
---@param words any
---@return any
function BotCommanderPlace(player, words) end

---our extension (B2): the party's interrupt for a unit, one order returned
---@param arg1 any
---@param target any
---@return any
function BotsInterrupt(arg1, target) end

---our extension (B1): clears the bot handlers
---@param event_type any
---@return any
function ClearBotEvents(event_type) end

---our extension (E3): the persistent per-script table
---@class ForeverData
ForeverData = {}

---our extension (E3): the persistent per-script table
---@param key any
---@return any
function ForeverData.get(key) end

---our extension (E3): the persistent per-script table
---@param key any
---@param value any
---@return any
function ForeverData.set(key, value) end

---our extension (E3): the persistent per-script table
---@param key any
---@return any
function ForeverData.delete(key) end

---our extension (E3): the persistent per-script table
---@return any
function ForeverData.keys() end

---our extension (B1): a bot of the snapshot by guid
---@param arg1 any
---@return any
function GetBotByGUID(arg1) end

---our extension (B1): a bot of the snapshot
---@param name any
---@return any
function GetBotByName(name) end

---our extension (B1): the bot snapshot, in name order
---@return any
function GetBots() end

---our extension (B1): the bots of a group
---@param arg1 any
---@return any
function GetBotsInGroup(arg1) end

---our extension (B1): the bots a player masters or commands
---@param player any
---@return any
function GetBotsOfMaster(player) end

---our extension (B1): the bots of a player's Battle.net account
---@param player any
---@return any
function GetBotsOfOwner(player) end

---our extension (B1): the bot API is built into this server
---@return any
function IsBotApiAvailable() end

---Lua sandbox builtin (Api.cpp registerCommon, both modes)
---@param ... any
---@return any
function print(...) end

---our extension (B1): the nine bot events, world state only
---@param event any
---@param arg2 any
---@param arg3 any
---@return any
function RegisterBotEvent(event, arg2, arg3) end

---our extension (E3): a GM command slot, README 'Writing scripts'
---@param command any
---@param security any
---@param handler any
---@return any
function RegisterCommand(command, security, handler) end

---Lua sandbox module loader (Api.cpp registerCommon, both modes)
---@param modname any
---@return any
function require(modname) end
