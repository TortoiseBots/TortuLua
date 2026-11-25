// Eluna globals of E2 and E3 (docs: Global/index.html): the wider Register*/Clear* families, the guild/vendor/game
// event functions, Kick/Ban/SendMail/SaveAllPlayers, the asynchronous database functions, our RegisterCommand extension
// and the ForeverData persistent table.
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"
#include "engine/Json.h"
#include "engine/Text.h"

#include <lauxlib.h>
#include <lua.h>

#include <optional>
#include <string>

namespace flu
{
namespace
{
using fl::EluKind;
using fl::EventValue;
using fl::HandleData;
using fl::ObjKind;

// ---- registration ----
int addHandlerAt(lua_State* L, EluKind kind, uint32_t entry, uint32_t event, int fnIndex, uint32_t shots)
{
    luaL_checktype(L, fnIndex, LUA_TFUNCTION);
    noteEventGaps(L, kind, event);
    lua_pushvalue(L, fnIndex);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id = eng(L).addEluHandler(kind, entry, ref, event, shots, std::nullopt, eng(L).where(L));
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "too many event handlers");
    }
    return pushHandlerId(L, id);
}

int pRegisterGroup(lua_State* L) { return addHandlerAt(L, EluKind::Group, 0, argUInt(L, 1, "event"), 2, static_cast<uint32_t>(optUInt(L, 3, 0))); }
int pRegisterGuild(lua_State* L) { return addHandlerAt(L, EluKind::Guild, 0, argUInt(L, 1, "event"), 2, static_cast<uint32_t>(optUInt(L, 3, 0))); }
// The core's InstanceMapScript is bound to the database (instance_template.script), so no module script is ever called for
// a dungeon or raid map: its map events cannot fire. A registration that could never run is an error, never a quiet no-op.
int instanceHookGap(lua_State* L, char const* what)
{
    return luaL_error(L, "%s: not available on this core: it needs core hook C11 (instance script forwarding; the core's "
                         "InstanceMapScript is database-bound, so the module cannot see dungeon and raid maps)", what);
}
int pRegisterMap(lua_State* L)
{
    uint32_t mapId = argUInt(L, 1, "map_id");
    if (host(L).mapIsInstanced(mapId))
        return instanceHookGap(L, "RegisterMapEvent on a dungeon or raid map");
    addHandlerAt(L, EluKind::Map, mapId, argUInt(L, 2, "event"), 3, static_cast<uint32_t>(optUInt(L, 4, 0)));
    return 0;   // the docs say RegisterMapEvent returns nothing
}
int pRegisterInstance(lua_State* L)
{
    argUInt(L, 1, "instance_id");
    return instanceHookGap(L, "RegisterInstanceEvent");   // instance maps are never reached at this core
}
int pRegisterPacket(lua_State* L)
{
    uint32_t opcode = argUInt(L, 1, "entry");
    if (eng(L).noteHookGap("elu-packet-stage"))
        eng(L).host().log(fl::LogLevel::Info,
                          "ForeverLua: packet event handlers are registered; the core's packet hooks are wired in E5");
    return addHandlerAt(L, EluKind::Packet, opcode, argUInt(L, 2, "event"), 3, static_cast<uint32_t>(optUInt(L, 4, 0)));
}

int clearKind(lua_State* L, EluKind kind, bool hasEntry)
{
    std::optional<uint32_t> entry;
    std::optional<uint32_t> event;
    int idx = 1;
    if (hasEntry)
        entry = argUInt(L, idx++, hasEntry ? "entry" : "event_type");
    if (!lua_isnoneornil(L, idx))
        event = argUInt(L, idx, "event_type");
    eng(L).clearEluHandlers(kind, entry, event);
    return 0;
}
int pClearGroup(lua_State* L) { return clearKind(L, EluKind::Group, false); }
int pClearGuild(lua_State* L) { return clearKind(L, EluKind::Guild, false); }
int pClearMap(lua_State* L) { return clearKind(L, EluKind::Map, true); }
int pClearInstance(lua_State* L) { return clearKind(L, EluKind::Instance, true); }
int pClearPacket(lua_State* L) { return clearKind(L, EluKind::Packet, true); }

// ---- guild lookups, saves, kick, ban, mail, vendors, game events ----
int pGetGuildByName(lua_State* L)
{
    HandleData out;
    if (host(L).guildByName(argStr(L, 1, "name"), out))
        fl::pushHandle(L, out);
    else
        lua_pushnil(L);
    return 1;
}
int pGetGuildByLeaderGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 1, guid);
    HandleData out;
    if (host(L).guildByLeaderGuid(guid, out))
        fl::pushHandle(L, out);
    else
        lua_pushnil(L);
    return 1;
}
int pSaveAllPlayers(lua_State* L)
{
    host(L).saveAllPlayers();
    return 0;
}
int pKick(lua_State* L)
{
    HandleData const& player = objArg(L, 1, kPlayer, "player");
    host(L).kickPlayer(player);
    return 0;
}
// Ban(banMode, nameOrIP, duration [, reason [, whoBanned]]): 0 success, 1 syntax, 2 not found, 3 a longer ban exists.
int pBan(lua_State* L)
{
    int mode = argInt(L, 1, "banMode");
    if (mode < 0 || mode > 2)
        return luaL_error(L, "banMode must be 0 (account), 1 (character) or 2 (IP)");
    std::string target = argStr(L, 2, "nameOrIP");
    uint32_t duration = argUInt(L, 3, "duration");
    std::string reason = lua_isnoneornil(L, 4) ? std::string() : argStr(L, 4, "reason");
    std::string who = lua_isnoneornil(L, 5) ? std::string() : argStr(L, 5, "whoBanned");
    int result = host(L).banPlayer(mode, target, duration, reason, who);
    if (result < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, result);
    return 1;
}
// SendMail(subject, text, receiverGUIDLow [, senderGUIDLow [, stationary [, delay [, money [, cod
//          [, entry, amount ...]]]]]]): the low guids of the items that were sent.
int pSendMail(lua_State* L)
{
    std::string subject = argStr(L, 1, "subject");
    std::string text = argStr(L, 2, "text");
    uint32_t receiver = argUInt(L, 3, "receiverGUIDLow");
    uint32_t sender = static_cast<uint32_t>(optUInt(L, 4, 0));
    uint32_t stationery = static_cast<uint32_t>(optUInt(L, 5, 41));   // MAIL_STATIONERY_DEFAULT
    uint32_t delay = static_cast<uint32_t>(optUInt(L, 6, 0));
    uint32_t money = static_cast<uint32_t>(optUInt(L, 7, 0));
    uint32_t cod = static_cast<uint32_t>(optUInt(L, 8, 0));
    std::vector<std::pair<uint32_t, uint32_t>> items;
    int idx = 9;
    while (!lua_isnoneornil(L, idx) && items.size() < 12)
    {
        uint32_t entry = argUInt(L, idx, "entry");
        uint32_t amount = static_cast<uint32_t>(optUInt(L, idx + 1, 0));
        items.emplace_back(entry, amount);
        idx += 2;
    }
    std::vector<uint32_t> guids = host(L).sendMail(subject, text, receiver, sender, stationery, delay, money, cod, items);
    if (guids.empty())
    {
        lua_pushnil(L);
        return 1;
    }
    for (uint32_t g : guids)
        lua_pushinteger(L, static_cast<lua_Integer>(g));
    return static_cast<int>(guids.size());
}
int pAddVendorItem(lua_State* L)
{
    host(L).addVendorItem(argUInt(L, 1, "entry"), argUInt(L, 2, "item"), argInt(L, 3, "maxcount"),
                          argUInt(L, 4, "incrtime"), static_cast<uint32_t>(optUInt(L, 5, 0)));
    return 0;
}
int pVendorRemoveItem(lua_State* L)
{
    host(L).vendorRemoveItem(argUInt(L, 1, "entry"), argUInt(L, 2, "item"));
    return 0;
}
int pVendorRemoveAllItems(lua_State* L)
{
    host(L).vendorRemoveAllItems(argUInt(L, 1, "entry"));
    return 0;
}
int pGetActiveGameEvents(lua_State* L)
{
    std::vector<uint32_t> list = host(L).activeGameEvents();
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 0;
    for (uint32_t id : list)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
int pIsGameEventActive(lua_State* L)
{
    lua_pushboolean(L, host(L).isGameEventActive(argUInt(L, 1, "eventId")));
    return 1;
}
int pStartGameEvent(lua_State* L)
{
    host(L).startGameEvent(argUInt(L, 1, "eventId"), optBool(L, 2, false));
    return 0;
}
int pStopGameEvent(lua_State* L)
{
    host(L).stopGameEvent(argUInt(L, 1, "eventId"), optBool(L, 2, false));
    return 0;
}

// ---- asynchronous database ----
int asyncQuery(lua_State* L, char const* db)
{
    std::string sql = argStr(L, 1, "sql");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string error;
    if (!eng(L).startAsyncQuery(db, sql, ref, error))
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "%s query refused: %s", db, error.c_str());
    }
    return 0;
}
int pWorldDBQueryAsync(lua_State* L) { return asyncQuery(L, "world"); }
int pCharDBQueryAsync(lua_State* L) { return asyncQuery(L, "characters"); }
int pAuthDBQueryAsync(lua_State* L) { return asyncQuery(L, "auth"); }

// ---- RegisterCommand (our extension) ----
// RegisterCommand(name, minSecurity, fn [, help]): the handler gets (player|nil, args table) and returns a string or a
// table of strings that is sent back to the caller.
int pRegisterCommand(lua_State* L)
{
    std::string name = argStr(L, 1, "name");
    int security = argInt(L, 2, "minSecurity");
    luaL_checktype(L, 3, LUA_TFUNCTION);
    std::string helpText = lua_isnoneornil(L, 4) ? std::string() : argStr(L, 4, "help");
    std::string where = eng(L).where(L);
    lua_pushvalue(L, 3);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string error;
    if (!eng(L).registerLuaCommand(name, security, ref, helpText, error, where))
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "RegisterCommand: %s", error.c_str());
    }
    return 0;
}

// ---- ForeverData (E3) ----
// The namespace is the loader's file path of the calling script (see Engine::dataNamespace).
std::string dataScript(lua_State* L) { return eng(L).dataNamespace(L); }
int dGet(lua_State* L)
{
    std::string script = dataScript(L);
    std::string key = argStr(L, 1, "key");
    std::string json;
    if (!eng(L).dataGet(script, key, json))
    {
        lua_pushnil(L);
        return 1;
    }
    std::string error;
    if (!fl::jsonDecode(L, json, error))
    {
        eng(L).logLua(fl::LogLevel::Warn, script + ": ForeverData key '" + key + "' is not valid JSON: " + error);
        lua_pushnil(L);
    }
    return 1;
}
int dSet(lua_State* L)
{
    std::string script = dataScript(L);
    std::string key = argStr(L, 1, "key");
    std::string json;
    std::string error;
    if (!fl::jsonEncode(L, 2, json, error))
        return luaL_error(L, "ForeverData.set: %s", error.c_str());
    if (!eng(L).dataSet(script, key, json, error))
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
int dDelete(lua_State* L)
{
    lua_pushboolean(L, eng(L).dataRemove(dataScript(L), argStr(L, 1, "key")));
    return 1;
}
int dKeys(lua_State* L)
{
    std::vector<std::string> keys = eng(L).dataKeys(dataScript(L));
    lua_createtable(L, static_cast<int>(keys.size()), 0);
    int i = 0;
    for (auto const& k : keys)
    {
        lua_pushlstring(L, k.data(), k.size());
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

void set(lua_State* L, char const* name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}
}  // namespace
}  // namespace flu

namespace fl
{
void registerForeverData(lua_State* L, char const* name)
{
    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, safe<flu::dGet>);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, safe<flu::dSet>);
    lua_setfield(L, -2, "set");
    lua_pushcfunction(L, safe<flu::dDelete>);
    lua_setfield(L, -2, "delete");
    lua_pushcfunction(L, safe<flu::dKeys>);
    lua_setfield(L, -2, "keys");
    lua_setglobal(L, name);
}
}  // namespace fl

namespace flu
{
void registerGameGlobals(lua_State* L)
{
    set(L, "RegisterGroupEvent", fl::safe<pRegisterGroup>);
    set(L, "RegisterGuildEvent", fl::safe<pRegisterGuild>);
    set(L, "RegisterMapEvent", fl::safe<pRegisterMap>);
    set(L, "RegisterInstanceEvent", fl::safe<pRegisterInstance>);
    set(L, "RegisterPacketEvent", fl::safe<pRegisterPacket>);
    set(L, "ClearGroupEvents", fl::safe<pClearGroup>);
    set(L, "ClearGuildEvents", fl::safe<pClearGuild>);
    set(L, "ClearMapEvents", fl::safe<pClearMap>);
    set(L, "ClearInstanceEvents", fl::safe<pClearInstance>);
    set(L, "ClearPacketEvents", fl::safe<pClearPacket>);
    set(L, "GetGuildByName", fl::safe<pGetGuildByName>);
    set(L, "GetGuildByLeaderGUID", fl::safe<pGetGuildByLeaderGUID>);
    set(L, "SaveAllPlayers", fl::safe<pSaveAllPlayers>);
    set(L, "Kick", fl::safe<pKick>);
    set(L, "Ban", fl::safe<pBan>);
    set(L, "SendMail", fl::safe<pSendMail>);
    set(L, "AddVendorItem", fl::safe<pAddVendorItem>);
    set(L, "VendorRemoveItem", fl::safe<pVendorRemoveItem>);
    set(L, "VendorRemoveAllItems", fl::safe<pVendorRemoveAllItems>);
    set(L, "GetActiveGameEvents", fl::safe<pGetActiveGameEvents>);
    set(L, "IsGameEventActive", fl::safe<pIsGameEventActive>);
    set(L, "StartGameEvent", fl::safe<pStartGameEvent>);
    set(L, "StopGameEvent", fl::safe<pStopGameEvent>);
    set(L, "WorldDBQueryAsync", fl::safe<pWorldDBQueryAsync>);
    set(L, "CharDBQueryAsync", fl::safe<pCharDBQueryAsync>);
    set(L, "AuthDBQueryAsync", fl::safe<pAuthDBQueryAsync>);
    set(L, "RegisterCommand", fl::safe<pRegisterCommand>);
    fl::registerForeverData(L, "ForeverData");
}
}  // namespace flu
