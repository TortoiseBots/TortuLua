#include "engine/Handle.h"

#include "engine/Elu.h"
#include "engine/Engine.h"
#include "engine/Safe.h"

#include <lauxlib.h>
#include <lua.h>

#include <new>

namespace fl
{
namespace
{
char const* const kMeta = "fl.Handle";

HandleData const& self(lua_State* L)
{
    return *static_cast<HandleData const*>(luaL_checkudata(L, 1, kMeta));
}

Host& hostOf(lua_State* L) { return Engine::from(L)->host(); }

// Reads: nil when the object is gone.
int pushLooked(lua_State* L, int (*pusher)(lua_State*, ObjectInfo const&))
{
    HandleData const& h = self(L);
    ObjectInfo info;
    if (!hostOf(L).lookup(h, &info))
    {
        lua_pushnil(L);
        return 1;
    }
    return pusher(L, info);
}

int hValid(lua_State* L)
{
    ObjectInfo info;
    lua_pushboolean(L, hostOf(L).lookup(self(L), &info));
    return 1;
}
int hGuid(lua_State* L)
{
    std::string s = hostOf(L).guidToString(self(L).guid);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
int hKind(lua_State* L)
{
    lua_pushstring(L, kindName(self(L).kind));
    return 1;
}
int hMapId(lua_State* L)
{
    lua_pushinteger(L, self(L).mapId);
    return 1;
}
int hInstanceId(lua_State* L)
{
    lua_pushinteger(L, self(L).instanceId);
    return 1;
}
int hEntry(lua_State* L)
{
    return pushLooked(L, [](lua_State* S, ObjectInfo const& i) { lua_pushinteger(S, i.entry); return 1; });
}
int hName(lua_State* L)
{
    return pushLooked(L, [](lua_State* S, ObjectInfo const& i) { lua_pushlstring(S, i.name.data(), i.name.size()); return 1; });
}
int hAlive(lua_State* L)
{
    return pushLooked(L, [](lua_State* S, ObjectInfo const& i) { lua_pushboolean(S, i.alive); return 1; });
}
int hPos(lua_State* L)
{
    return pushLooked(L, [](lua_State* S, ObjectInfo const& i)
    {
        lua_pushnumber(S, i.x);
        lua_pushnumber(S, i.y);
        lua_pushnumber(S, i.z);
        lua_pushnumber(S, i.o);
        return 4;
    });
}
// Actions: false, "gone" when the object no longer exists.
int hDespawn(lua_State* L)
{
    if (hostOf(L).despawn(self(L)))
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushliteral(L, "gone");
    return 2;
}
int hEq(lua_State* L)
{
    HandleData const* a = toHandle(L, 1);
    HandleData const* b = toHandle(L, 2);
    // A map handle has no guid (two maps would compare equal): the whole identity counts.
    lua_pushboolean(L, a && b && a->kind == b->kind && a->guid == b->guid && a->mapId == b->mapId &&
                           a->instanceId == b->instanceId);
    return 1;
}
int hToString(lua_State* L)
{
    std::string s = hostOf(L).guidToString(self(L).guid);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
int hNewIndex(lua_State* L)
{
    return luaL_error(L, "handles are read-only");
}
}  // namespace

char const* kindName(ObjKind kind)
{
    switch (kind)
    {
        case ObjKind::Player: return "player";
        case ObjKind::Creature: return "creature";
        case ObjKind::GameObject: return "gameobject";
        case ObjKind::Map: return "map";
        case ObjKind::Item: return "item";
        case ObjKind::Group: return "group";
        case ObjKind::Guild: return "guild";
        case ObjKind::Quest: return "quest";
        case ObjKind::Spell: return "spell";
        case ObjKind::Aura: return "aura";
        case ObjKind::Query: return "query";
        case ObjKind::Packet: return "packet";
        case ObjKind::Corpse: return "corpse";
        case ObjKind::BattleGround: return "battleground";
        case ObjKind::Vehicle: return "vehicle";
        case ObjKind::Bot: return "bot";
        case ObjKind::SpellInfo: return "spellinfo";
    }
    return "unknown";
}

void registerHandleType(lua_State* L)
{
    static luaL_Reg const methods[] = {
        {"valid", safe<hValid>},   {"guid", safe<hGuid>},     {"kind", safe<hKind>},       {"mapId", safe<hMapId>},
        {"instanceId", safe<hInstanceId>}, {"entry", safe<hEntry>}, {"name", safe<hName>}, {"alive", safe<hAlive>},
        {"pos", safe<hPos>},       {"despawn", safe<hDespawn>}, {nullptr, nullptr}};
    luaL_newmetatable(L, kMeta);
    lua_pushstring(L, kMeta);
    lua_setfield(L, -2, "__name");
    lua_pushstring(L, kMeta);
    lua_setfield(L, -2, "__metatable");  // scripts cannot swap the metatable
    lua_pushcfunction(L, safe<hEq>);
    lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, safe<hToString>);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, safe<hNewIndex>);
    lua_setfield(L, -2, "__newindex");
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

void pushHandle(lua_State* L, HandleData const& data)
{
    if (Engine::from(L)->config().mode == EngineMode::Eluna)
    {
        eluPushHandle(L, data);   // the Eluna class of the handle kind
        return;
    }
    void* ud = lua_newuserdatauv(L, sizeof(HandleData), 0);
    new (ud) HandleData(data);
    luaL_setmetatable(L, kMeta);
}

HandleData const* toHandle(lua_State* L, int idx)
{
    return static_cast<HandleData const*>(luaL_testudata(L, idx, kMeta));
}

HandleData const* toHandleAny(lua_State* L, int idx)
{
    if (HandleData const* h = toHandle(L, idx))
        return h;
    // A full userdata of exactly our size whose metatable IS the Eluna class metatable of the kind it claims. Anything
    // else (a file handle given a copied metatable with debug.setmetatable, a light userdata) is not ours: reading it as
    // a HandleData would be a type confusion.
    if (lua_type(L, idx) != LUA_TUSERDATA || lua_rawlen(L, idx) != sizeof(HandleData))
        return nullptr;
    if (!lua_getmetatable(L, idx))
        return nullptr;
    lua_getfield(L, -1, "__handle");
    bool const claims = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    HandleData const* h = nullptr;
    if (claims)
    {
        HandleData const* data = static_cast<HandleData const*>(lua_touserdata(L, idx));
        if (eluPushClassMetatable(L, data->kind))   // pushes the registered metatable of the claimed kind
        {
            if (lua_rawequal(L, -1, -2))
                h = data;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return h;
}
}  // namespace fl
