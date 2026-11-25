// Eluna global functions of E1 (docs: Global/index.html): the Register*/Clear* family, CreateLuaEvent and the timed
// event functions, the lookups, PerformIngameSpawn, the raw SQL queries, the bit helpers and the print helpers.
#include "engine/elu/EluBind.h"

#include "engine/Elu.h"
#include "engine/GuidText.h"
#include "engine/Handle.h"
#include "engine/Text.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

namespace flu
{
namespace
{
using fl::EluKind;
using fl::EventValue;
using fl::HandleData;
using fl::ObjKind;

// Section 7 globals: systems Classic 1.60 does not have.
int pGetOwnerHalaa(lua_State* L) { return luaL_error(L, "GetOwnerHalaa: not available on Classic 1.60"); }
int pRegisterVehicleEvent(lua_State* L)
{
    return luaL_error(L, "RegisterVehicleEvent: not available on Classic 1.60");
}

// Register<Family>Event(...): the handler id goes into the docs' cancel closure.
int addHandler(lua_State* L, EluKind kind, uint32_t entry, uint32_t event, int fnIndex, uint32_t shots,
               std::optional<fl::UniqueKey> unique = std::nullopt)
{
    luaL_checktype(L, fnIndex, LUA_TFUNCTION);
    noteEventGaps(L, kind, event);
    lua_pushvalue(L, fnIndex);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id = eng(L).addEluHandler(kind, entry, ref, event, shots, unique, eng(L).where(L));
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "too many event handlers");
    }
    return pushHandlerId(L, id);
}

// RegisterPlayerEvent(event, function [, shots]) / RegisterServerEvent(...)
int registerNoEntry(lua_State* L, EluKind kind)
{
    uint32_t event = argUInt(L, 1, "event");
    uint32_t shots = static_cast<uint32_t>(optUInt(L, 3, 0));
    return addHandler(L, kind, 0, event, 2, shots);
}

// RegisterCreatureEvent(entry, event, function [, shots]) and the gossip families.
int registerByEntry(lua_State* L, EluKind kind)
{
    uint32_t entry = argUInt(L, 1, "entry");
    uint32_t event = argUInt(L, 2, "event");
    uint32_t shots = static_cast<uint32_t>(optUInt(L, 4, 0));
    return addHandler(L, kind, entry, event, 3, shots);
}

int pRegisterPlayer(lua_State* L) { return registerNoEntry(L, EluKind::Player); }
int pRegisterServer(lua_State* L) { return registerNoEntry(L, EluKind::Server); }
// RegisterBGEvent(event, function [, shots]); state MAP (docs), so the battleground's map state runs it (E4/E5).
int pRegisterBG(lua_State* L) { return registerNoEntry(L, EluKind::BattleGround); }
int pRegisterCreature(lua_State* L) { return registerByEntry(L, EluKind::Creature); }
int pRegisterGameObject(lua_State* L) { return registerByEntry(L, EluKind::GameObject); }
int pRegisterItem(lua_State* L) { return registerByEntry(L, EluKind::Item); }
int pRegisterPlayerGossip(lua_State* L) { return registerByEntry(L, EluKind::PlayerGossip); }
int pRegisterCreatureGossip(lua_State* L) { return registerByEntry(L, EluKind::CreatureGossip); }
int pRegisterGameObjectGossip(lua_State* L) { return registerByEntry(L, EluKind::GameObjectGossip); }
int pRegisterItemGossip(lua_State* L) { return registerByEntry(L, EluKind::ItemGossip); }

// RegisterUniqueCreatureEvent(guid, instance_id, event, function [, shots])
int pRegisterUniqueCreature(lua_State* L)
{
    fl::Guid guid;
    bool legacy = false;
    argGuid(L, 1, guid, &legacy);
    uint32_t instanceId = argUInt(L, 2, "instance_id");
    uint32_t event = argUInt(L, 3, "event");
    uint32_t shots = static_cast<uint32_t>(optUInt(L, 5, 0));
    // Keyed by the creature's full guid AND its instance (creature counters are per map at this core); an integer guid is
    // the legacy low counter and matches any creature with that low guid in the instance.
    fl::UniqueKey key{guid.hi, guid.lo, instanceId, legacy};
    return addHandler(L, EluKind::Creature, 0, event, 4, shots, key);
}

int clearFamily(lua_State* L, EluKind kind, bool hasEntry)
{
    std::optional<uint32_t> entry;
    std::optional<uint32_t> event;
    int idx = 1;
    if (hasEntry && !lua_isnoneornil(L, idx))
        entry = argUInt(L, idx++, "entry");
    if (!lua_isnoneornil(L, idx))
        event = argUInt(L, idx, "event_type");
    eng(L).clearEluHandlers(kind, entry, event);
    return 0;
}
int pClearPlayer(lua_State* L) { return clearFamily(L, EluKind::Player, false); }
int pClearServer(lua_State* L) { return clearFamily(L, EluKind::Server, false); }
int pClearBG(lua_State* L) { return clearFamily(L, EluKind::BattleGround, false); }
int pClearCreature(lua_State* L) { return clearFamily(L, EluKind::Creature, true); }
int pClearGameObject(lua_State* L) { return clearFamily(L, EluKind::GameObject, true); }
int pClearItem(lua_State* L) { return clearFamily(L, EluKind::Item, true); }
int pClearPlayerGossip(lua_State* L) { return clearFamily(L, EluKind::PlayerGossip, true); }
int pClearCreatureGossip(lua_State* L) { return clearFamily(L, EluKind::CreatureGossip, true); }
int pClearGameObjectGossip(lua_State* L) { return clearFamily(L, EluKind::GameObjectGossip, true); }
int pClearItemGossip(lua_State* L) { return clearFamily(L, EluKind::ItemGossip, true); }

// ClearUniqueCreatureEvents(guid, instance_id [, event_type])
int pClearUniqueCreature(lua_State* L)
{
    fl::Guid guid;
    bool legacy = false;
    argGuid(L, 1, guid, &legacy);
    uint32_t instanceId = argUInt(L, 2, "instance_id");
    std::optional<uint32_t> event;
    if (!lua_isnoneornil(L, 3))
        event = argUInt(L, 3, "event_type");
    eng(L).clearEluUniqueHandlers(EluKind::Creature, fl::UniqueKey{guid.hi, guid.lo, instanceId, legacy}, event);
    return 0;
}

// ---- timed events ----
int pCreateLuaEvent(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    uint64_t lo = 0, hi = 0;
    if (lua_istable(L, 2))
    {
        lua_rawgeti(L, 2, 1);
        lo = static_cast<uint64_t>(std::max<lua_Integer>(0, luaL_checkinteger(L, -1)));
        lua_rawgeti(L, 2, 2);
        hi = static_cast<uint64_t>(std::max<lua_Integer>(static_cast<lua_Integer>(lo), luaL_checkinteger(L, -1)));
        lua_pop(L, 2);
    }
    else
    {
        lo = hi = static_cast<uint64_t>(std::max<lua_Integer>(0, luaL_checkinteger(L, 2)));
    }
    uint32_t repeats = static_cast<uint32_t>(optUInt(L, 3, 1));
    lua_pushvalue(L, 1);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id = eng(L).addLuaEvent(ref, lo, hi, repeats, std::nullopt);
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}
int pRemoveEventById(lua_State* L)
{
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    bool const all = optBool(L, 2, false);   // all_Events = true also removes object-bound events
    eng(L).removeLuaEvent(id, std::nullopt, !all);
    return 0;
}
int pRemoveEvents(lua_State* L)
{
    bool const all = optBool(L, 1, false);
    eng(L).removeLuaEvents(std::nullopt, !all);
    return 0;
}

// ---- lookups ----
int pGetPlayerByName(lua_State* L)
{
    std::string name = argStr(L, 1, "name");
    HandleData out;
    if (host(L).playerByName(name, out))
        fl::pushHandle(L, out);
    else
        lua_pushnil(L);
    return 1;
}
int pGetPlayerByGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 1, guid);
    HandleData out;
    if (host(L).playerByGuid(guid, out))
        fl::pushHandle(L, out);
    else
        lua_pushnil(L);
    return 1;
}
int pGetPlayersInWorld(lua_State* L)
{
    int team = static_cast<int>(optUInt(L, 1, 2));   // TEAM_NEUTRAL in Eluna's TeamId (alliance 0, horde 1, neutral 2)
    bool onlyGm = optBool(L, 2, false);
    std::vector<HandleData> list = host(L).playersInWorld(team, onlyGm);
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 0;
    for (auto const& h : list)
    {
        fl::pushHandle(L, h);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
int pGetPlayerCount(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(host(L).playerCount()));
    return 1;
}
int pSendWorldMessage(lua_State* L)
{
    host(L).sendWorldMessage(argStr(L, 1, "message"));
    return 0;
}
int pPerformIngameSpawn(lua_State* L)
{
    int type = argInt(L, 1, "spawnType");
    uint32_t entry = argUInt(L, 2, "entry");
    uint32_t mapId = argUInt(L, 3, "mapId");
    uint32_t instanceId = argUInt(L, 4, "instanceId");
    HandleData out;
    if (host(L).performSpawn(type, entry, mapId, instanceId, argFloat(L, 5, "x"), argFloat(L, 6, "y"),
                             argFloat(L, 7, "z"), argFloat(L, 8, "o"), optBool(L, 9, false),
                             static_cast<uint32_t>(optUInt(L, 10, 0)), static_cast<uint32_t>(optUInt(L, 11, 1)), out))
        fl::pushHandle(L, out);
    else
        lua_pushnil(L);
    return 1;
}

// ---- clock, core, state ----
int pGetGameTime(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(host(L).gameTime()));
    return 1;
}
int pGetCurrTime(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(host(L).nowMs()));
    return 1;
}
int pGetTimeDiff(lua_State* L)
{
    uint64_t old = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    uint64_t now = host(L).nowMs();
    lua_pushinteger(L, static_cast<lua_Integer>(now > old ? now - old : 0));
    return 1;
}
int pGetCoreName(lua_State* L)
{
    lua_pushliteral(L, "ForeverCore");
    return 1;
}
int pGetCoreVersion(lua_State* L)
{
    std::string v = host(L).coreVersion();
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}
int pGetCoreExpansion(lua_State* L)
{
    lua_pushinteger(L, 0);   // Classic 1.60
    return 1;
}
int pGetLuaEngine(lua_State* L)
{
    lua_pushliteral(L, "ForeverLua");
    return 1;
}
// E4: the map of the state that runs this script (-1 for the world state).
int pGetStateMapId(lua_State* L)
{
    lua_pushinteger(L, eng(L).stateMapId());
    return 1;
}
int pGetStateInstanceId(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(eng(L).stateInstanceId()));
    return 1;
}
int pGetStateMap(lua_State* L)
{
    int32_t const mapId = eng(L).stateMapId();
    if (mapId < 0)
    {
        lua_pushnil(L);
        return 1;
    }
    HandleData map;
    map.kind = ObjKind::Map;
    map.mapId = static_cast<uint32_t>(mapId);
    map.instanceId = eng(L).stateInstanceId();
    fl::pushHandle(L, map);
    return 1;
}
int pGetPlayersOnMap(lua_State* L)
{
    int32_t const mapId = eng(L).stateMapId();
    if (mapId < 0)
        return luaL_error(L, "GetPlayersOnMap: only available in a map state");
    int team = static_cast<int>(optUInt(L, 1, 2));   // docs default TEAM_NEUTRAL
    bool onlyGm = optBool(L, 2, false);
    std::vector<HandleData> list =
        host(L).playersOnMap(static_cast<uint32_t>(mapId), eng(L).stateInstanceId(), team, onlyGm);
    return pushHandleTable(L, list);
}
int pIsCompatibilityMode(lua_State* L)
{
    // Compatibility mode = one state; multistate (MapStates = 1) runs a state per map.
    lua_pushboolean(L, eng(L).config().mapStates == 0 ? 1 : 0);
    return 1;
}
int pAddTaxiPath(lua_State* L)
{
    // The pinned core's taxi paths come from TaxiPath.db2 and TaxiPathNode.db2 (DB2Storage has no insert API), so a
    // script cannot add a path here. StartTaxi still flies every existing path.
    return luaL_error(L, "AddTaxiPath: not available on this server");
}

// ---- guid helpers ----
int pGetGUIDLow(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 1, guid);
    lua_pushinteger(L, static_cast<lua_Integer>(guid.lo & 0xFFFFFFFFu));
    return 1;
}
bool guidInfoOf(lua_State* L, uint32_t* entry, int* type)
{
    fl::Guid g;
    argGuid(L, 1, g);
    return host(L).guidInfo(g, entry, type);
}
int pGetGUIDType(lua_State* L)
{
    int type = -1;
    if (!guidInfoOf(L, nullptr, &type))
        type = -1;
    lua_pushinteger(L, type);
    return 1;
}
int pGetGUIDEntry(lua_State* L)
{
    uint32_t entry = 0;
    if (!guidInfoOf(L, &entry, nullptr))
        entry = 0;
    lua_pushinteger(L, static_cast<lua_Integer>(entry));
    return 1;
}
int pGetItemLink(lua_State* L)
{
    std::string link = host(L).itemLink(argUInt(L, 1, "entry"));
    if (link.empty())
        lua_pushnil(L);
    else
        lua_pushlstring(L, link.data(), link.size());
    return 1;
}
int pGetAreaName(lua_State* L)
{
    std::string name = host(L).areaName(argUInt(L, 1, "areaOrZoneId"));
    if (name.empty())
        lua_pushnil(L);
    else
        lua_pushlstring(L, name.data(), name.size());
    return 1;
}
int pGetMapById(lua_State* L)
{
    uint32_t mapId = argUInt(L, 1, "mapId");
    uint32_t instanceId = static_cast<uint32_t>(optUInt(L, 2, 0));
    if (!host(L).mapExists(mapId, instanceId))
    {
        lua_pushnil(L);
        return 1;
    }
    HandleData h;
    h.kind = ObjKind::Map;
    h.mapId = mapId;
    h.instanceId = instanceId;
    fl::pushHandle(L, h);
    return 1;
}
int pGetQuest(lua_State* L)
{
    uint32_t id = argUInt(L, 1, "questId");
    if (!host(L).questExists(id))
    {
        lua_pushnil(L);
        return 1;
    }
    HandleData h;
    h.kind = ObjKind::Quest;
    h.guid.lo = id;
    fl::pushHandle(L, h);
    return 1;
}

// ---- commands, reload, print ----
// The SpellInfo handle of a spell id (docs: "or nil if the spell does not exist"); the entry's data lives for the
// whole server run, so the handle never goes stale.
int pGetSpellInfo(lua_State* L)
{
    uint32_t const id = argUInt(L, 1, "spellId");
    if (!host(L).spellExists(id))
    {
        lua_pushnil(L);
        return 1;
    }
    HandleData h;
    h.kind = ObjKind::SpellInfo;
    h.guid.lo = id;
    fl::pushHandle(L, h);
    return 1;
}

int pRunCommand(lua_State* L)
{
    std::string line = argStr(L, 1, "command");
    // Eluna trusts its scripts (no bridge deny list); the host runs it on the world thread, queueing it from a map thread.
    host(L).runCommand(line);
    return 0;
}
int pReloadEluna(lua_State* L)
{
    (void)L;
    host(L).requestReload();
    return 0;
}

int logArgs(lua_State* L, fl::LogLevel level)
{
    std::string line;
    int n = lua_gettop(L);
    for (int i = 1; i <= n && line.size() < 1024; ++i)
    {
        size_t len = 0;
        char const* s = luaL_tolstring(L, i, &len);
        if (i > 1)
            line += '\t';
        line.append(s, len > 512 ? 512 : len);
        lua_pop(L, 1);
    }
    eng(L).host().log(level, "[lua] " + line);
    return 0;
}
int pPrintInfo(lua_State* L) { return logArgs(L, fl::LogLevel::Info); }
int pPrintError(lua_State* L) { return logArgs(L, fl::LogLevel::Error); }
int pPrintDebug(lua_State* L) { return logArgs(L, fl::LogLevel::Debug); }

// ---- databases ----
int queryDb(lua_State* L, char const* db)
{
    std::string sql = argStr(L, 1, "sql");
    fl::DbRows rows;
    std::string error;
    if (!host(L).rawQuery(db, sql, 1024, 1u << 20, rows, error) || rows.rows.empty() || rows.columns.empty())
    {
        if (!error.empty())
        {
            std::string where = eng(L).where(L);
            eng(L).logLua(fl::LogLevel::Warn, (where.empty() ? std::string() : where + ": ") + db +
                                                   " query failed: " + error);
        }
        lua_pushnil(L);
        return 1;
    }
    fl::eluPushQuery(L, std::move(rows));
    return 1;
}
int pWorldDBQuery(lua_State* L) { return queryDb(L, "world"); }
int pCharDBQuery(lua_State* L) { return queryDb(L, "characters"); }
int pAuthDBQuery(lua_State* L) { return queryDb(L, "auth"); }
int pWorldDBExecute(lua_State* L)
{
    host(L).rawExecute("world", argStr(L, 1, "sql"));
    return 0;
}
int pCharDBExecute(lua_State* L)
{
    host(L).rawExecute("characters", argStr(L, 1, "sql"));
    return 0;
}
int pAuthDBExecute(lua_State* L)
{
    host(L).rawExecute("auth", argStr(L, 1, "sql"));
    return 0;
}

// ---- bit helpers ----
using U = uint64_t;
int bitAnd(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<U>(luaL_checkinteger(L, 1)) & static_cast<U>(luaL_checkinteger(L, 2))));
    return 1;
}
int bitOr(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<U>(luaL_checkinteger(L, 1)) | static_cast<U>(luaL_checkinteger(L, 2))));
    return 1;
}
int bitXor(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<U>(luaL_checkinteger(L, 1)) ^ static_cast<U>(luaL_checkinteger(L, 2))));
    return 1;
}
int bitNot(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(~static_cast<U>(luaL_checkinteger(L, 1))));
    return 1;
}
int bitLShift(lua_State* L)
{
    uint64_t a = static_cast<U>(luaL_checkinteger(L, 1));
    lua_Integer b = luaL_checkinteger(L, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(b >= 64 ? 0 : b < 0 ? 0 : (a << b)));
    return 1;
}
int bitRShift(lua_State* L)
{
    uint64_t a = static_cast<U>(luaL_checkinteger(L, 1));
    lua_Integer b = luaL_checkinteger(L, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(b >= 64 ? 0 : b < 0 ? 0 : (a >> b)));
    return 1;
}

void set(lua_State* L, char const* name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}
}  // namespace

void registerGlobals(lua_State* L)
{
    set(L, "RegisterPlayerEvent", fl::safe<pRegisterPlayer>);
    set(L, "RegisterServerEvent", fl::safe<pRegisterServer>);
    set(L, "RegisterCreatureEvent", fl::safe<pRegisterCreature>);
    set(L, "RegisterGameObjectEvent", fl::safe<pRegisterGameObject>);
    set(L, "RegisterItemEvent", fl::safe<pRegisterItem>);
    set(L, "RegisterPlayerGossipEvent", fl::safe<pRegisterPlayerGossip>);
    set(L, "RegisterCreatureGossipEvent", fl::safe<pRegisterCreatureGossip>);
    set(L, "RegisterGameObjectGossipEvent", fl::safe<pRegisterGameObjectGossip>);
    set(L, "RegisterItemGossipEvent", fl::safe<pRegisterItemGossip>);
    set(L, "RegisterUniqueCreatureEvent", fl::safe<pRegisterUniqueCreature>);
    set(L, "ClearPlayerEvents", fl::safe<pClearPlayer>);
    set(L, "ClearServerEvents", fl::safe<pClearServer>);
    set(L, "ClearCreatureEvents", fl::safe<pClearCreature>);
    set(L, "ClearGameObjectEvents", fl::safe<pClearGameObject>);
    set(L, "ClearItemEvents", fl::safe<pClearItem>);
    set(L, "ClearPlayerGossipEvents", fl::safe<pClearPlayerGossip>);
    set(L, "ClearCreatureGossipEvents", fl::safe<pClearCreatureGossip>);
    set(L, "ClearGameObjectGossipEvents", fl::safe<pClearGameObjectGossip>);
    set(L, "ClearItemGossipEvents", fl::safe<pClearItemGossip>);
    set(L, "ClearUniqueCreatureEvents", fl::safe<pClearUniqueCreature>);
    set(L, "CreateLuaEvent", fl::safe<pCreateLuaEvent>);
    set(L, "RemoveEventById", fl::safe<pRemoveEventById>);
    set(L, "RemoveEvents", fl::safe<pRemoveEvents>);
    set(L, "GetPlayerByName", fl::safe<pGetPlayerByName>);
    set(L, "GetPlayerByGUID", fl::safe<pGetPlayerByGUID>);
    set(L, "GetPlayersInWorld", fl::safe<pGetPlayersInWorld>);
    set(L, "GetPlayerCount", fl::safe<pGetPlayerCount>);
    set(L, "SendWorldMessage", fl::safe<pSendWorldMessage>);
    set(L, "PerformIngameSpawn", fl::safe<pPerformIngameSpawn>);
    set(L, "GetGameTime", fl::safe<pGetGameTime>);
    set(L, "GetCurrTime", fl::safe<pGetCurrTime>);
    set(L, "GetTimeDiff", fl::safe<pGetTimeDiff>);
    set(L, "GetCoreName", fl::safe<pGetCoreName>);
    set(L, "GetCoreVersion", fl::safe<pGetCoreVersion>);
    set(L, "GetCoreExpansion", fl::safe<pGetCoreExpansion>);
    set(L, "GetLuaEngine", fl::safe<pGetLuaEngine>);
    set(L, "GetStateMapId", fl::safe<pGetStateMapId>);
    set(L, "GetStateInstanceId", fl::safe<pGetStateInstanceId>);
    set(L, "GetGUIDLow", fl::safe<pGetGUIDLow>);
    set(L, "GetGUIDType", fl::safe<pGetGUIDType>);
    set(L, "GetGUIDEntry", fl::safe<pGetGUIDEntry>);
    set(L, "GetItemLink", fl::safe<pGetItemLink>);
    set(L, "GetAreaName", fl::safe<pGetAreaName>);
    set(L, "GetMapById", fl::safe<pGetMapById>);
    set(L, "GetQuest", fl::safe<pGetQuest>);
    set(L, "GetSpellInfo", fl::safe<pGetSpellInfo>);
    set(L, "RunCommand", fl::safe<pRunCommand>);
    set(L, "PrintInfo", fl::safe<pPrintInfo>);
    set(L, "PrintError", fl::safe<pPrintError>);
    set(L, "PrintDebug", fl::safe<pPrintDebug>);
    set(L, "WorldDBQuery", fl::safe<pWorldDBQuery>);
    set(L, "WorldDBExecute", fl::safe<pWorldDBExecute>);
    set(L, "CharDBQuery", fl::safe<pCharDBQuery>);
    set(L, "CharDBExecute", fl::safe<pCharDBExecute>);
    set(L, "AuthDBQuery", fl::safe<pAuthDBQuery>);
    set(L, "AuthDBExecute", fl::safe<pAuthDBExecute>);
    set(L, "ReloadEluna", fl::safe<pReloadEluna>);
    // E4
    set(L, "GetStateMap", fl::safe<pGetStateMap>);
    set(L, "GetPlayersOnMap", fl::safe<pGetPlayersOnMap>);
    set(L, "IsCompatibilityMode", fl::safe<pIsCompatibilityMode>);
    // E5
    set(L, "RegisterBGEvent", fl::safe<pRegisterBG>);
    set(L, "ClearBattleGroundEvents", fl::safe<pClearBG>);
    set(L, "CreatePacket", fl::safe<flu::createPacket>);
    set(L, "AddTaxiPath", fl::safe<pAddTaxiPath>);
    set(L, "GetOwnerHalaa", fl::safe<pGetOwnerHalaa>);
    set(L, "RegisterVehicleEvent", fl::safe<pRegisterVehicleEvent>);
    set(L, "bit_and", fl::safe<bitAnd>);
    set(L, "bit_or", fl::safe<bitOr>);
    set(L, "bit_xor", fl::safe<bitXor>);
    set(L, "bit_not", fl::safe<bitNot>);
    set(L, "bit_lshift", fl::safe<bitLShift>);
    set(L, "bit_rshift", fl::safe<bitRShift>);
    // Our bot API (B1, elu/EluBot.cpp): the read side and the bot event registration.
    set(L, "IsBotApiAvailable", fl::safe<pIsBotApiAvailable>);
    set(L, "GetBots", fl::safe<pGetBots>);
    set(L, "GetBotByName", fl::safe<pGetBotByName>);
    set(L, "GetBotByGUID", fl::safe<pGetBotByGUID>);
    set(L, "GetBotsOfOwner", fl::safe<pGetBotsOfOwner>);
    set(L, "GetBotsOfMaster", fl::safe<pGetBotsOfMaster>);
    set(L, "GetBotsInGroup", fl::safe<pGetBotsInGroup>);
    set(L, "RegisterBotEvent", fl::safe<pRegisterBotEvent>);
    set(L, "ClearBotEvents", fl::safe<pClearBotEvents>);
    // Our bot API (B2, elu/EluBot.cpp): the party-level order calls. The Bot handle's own orders are its methods.
    set(L, "BotsInterrupt", fl::safe<pBotsInterrupt>);
    set(L, "BotCommanderPlace", fl::safe<pBotCommanderPlace>);
}
}  // namespace flu
