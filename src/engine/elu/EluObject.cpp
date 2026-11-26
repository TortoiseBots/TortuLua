// Eluna Object and WorldObject methods of E1 (docs: Object/index.html, WorldObject/index.html, getters and setters as
// documented). Object:GetGUID/GetGUIDLow/GetEntry/GetTypeId/IsInWorld answer from the handle even when the object is
// gone; every other method raises "<Class> no longer exists" (docs/E0-E1-NOTES.md).
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"
#include "engine/Handle.h"

#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <optional>

namespace flu
{
namespace
{
using fl::EventValue;
using fl::HandleData;
using fl::ObjKind;

// Object type ids of the core's TypeID (Object.h): 1 item, 3 unit, 4 player, 5 game object.
int typeIdOf(ObjKind kind)
{
    switch (kind)
    {
        case ObjKind::Item: return 1;
        case ObjKind::Creature: return 3;
        case ObjKind::Player: return 4;
        case ObjKind::GameObject: return 5;
        case ObjKind::Corpse: return 7;   // the docs' TypeID (Eluna numbering, as in E1)
        default: return 0;
    }
}

int oGetEntry(lua_State* L)
{
    HandleData const& h = self(L, kObject, false);
    uint32_t entry = 0;
    host(L).guidInfo(h.guid, &entry, nullptr);
    lua_pushinteger(L, static_cast<lua_Integer>(entry));
    return 1;
}

int oGetGUID(lua_State* L)
{
    HandleData const& h = self(L, kObject, false);
    // All 128 bits (high half first): the type, the entry and the map live in the high half (engine/GuidText.h).
    std::string text = fl::guidText(h.guid);
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int oGetGUIDLow(lua_State* L)
{
    HandleData const& h = self(L, kObject, false);
    lua_pushinteger(L, static_cast<lua_Integer>(h.guid.lo & 0xFFFFFFFFu));
    return 1;
}

int oGetTypeId(lua_State* L)
{
    HandleData const& h = self(L, kObject, false);
    lua_pushinteger(L, typeIdOf(h.kind));
    return 1;
}

int oIsPlayer(lua_State* L)
{
    lua_pushboolean(L, self(L, kObject, false).kind == ObjKind::Player);
    return 1;
}

int toKind(lua_State* L, ObjKind want, char const* display)
{
    HandleData const& h = self(L, kObject);
    (void)display;
    if (h.kind == want)
        fl::pushHandle(L, h);
    else
        lua_pushnil(L);
    return 1;
}

int oToPlayer(lua_State* L) { return toKind(L, ObjKind::Player, "ToPlayer"); }
int oToCreature(lua_State* L) { return toKind(L, ObjKind::Creature, "ToCreature"); }
int oToGameObject(lua_State* L) { return toKind(L, ObjKind::GameObject, "ToGameObject"); }
int oToCorpse(lua_State* L) { return toKind(L, ObjKind::Corpse, "ToCorpse"); }
int oToUnit(lua_State* L)
{
    HandleData const& h = self(L, kObject);
    if (h.kind == ObjKind::Player || h.kind == ObjKind::Creature)
        fl::pushHandle(L, h);
    else
        lua_pushnil(L);
    return 1;
}

int oIsInWorld(lua_State* L)
{
    HandleData const& h = self(L, kObject, false);
    if (h.removed)
    {
        lua_pushboolean(L, 0);   // a REMOVE event's handle: the object left the world
        return 1;
    }
    // Access check first, like every other method: a stale handle is "not in the world", a foreign map's object is
    // refused, and only then does the host look the object up.
    switch (host(L).checkAccess(h))
    {
        case fl::Access::Gone: lua_pushboolean(L, 0); return 1;
        case fl::Access::OtherMap: return luaL_error(L, "object is on another map");
        case fl::Access::Ok: break;
    }
    fl::ObjectInfo info;
    lua_pushboolean(L, host(L).lookup(h, &info));
    return 1;
}

int oGetScale(lua_State* L) { return readOp(L, kObject, "object.getScale", "GetScale"); }
int oSetScale(lua_State* L)
{
    return actionOp(L, kObject, "object.setScale", "SetScale", {EventValue(argFloat(L, 2, "scale"))});
}
int oHasFlag(lua_State* L)
{
    return readOp(L, kObject, "object.hasFlag", "HasFlag",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "index"))), EventValue(static_cast<int64_t>(argUInt(L, 3, "flag")))});
}
int oGetUInt32Value(lua_State* L)
{
    return readOp(L, kObject, "object.getUInt32", "GetUInt32Value",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}

// ---- WorldObject ----
int wGetName(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    fl::ObjectInfo info;
    if (h.removed || !host(L).lookup(h, &info))
        return luaL_error(L, "%s no longer exists", fl::eluClassName(h.kind)), 0;
    lua_pushlstring(L, info.name.data(), info.name.size());
    return 1;
}
// The map the object is on NOW (from the host's own record), not the one the handle was made on.
void liveMap(lua_State* L, HandleData const& h, uint32_t& mapId, uint32_t& instanceId)
{
    if (h.removed || !host(L).currentMap(h, mapId, instanceId))
        luaL_error(L, "%s no longer exists", fl::eluClassName(h.kind));
}
int wGetMap(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    fl::HandleData map;
    map.kind = ObjKind::Map;
    liveMap(L, h, map.mapId, map.instanceId);
    fl::pushHandle(L, map);
    return 1;
}
int wGetMapId(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    uint32_t m = 0, i = 0;
    liveMap(L, h, m, i);
    lua_pushinteger(L, m);
    return 1;
}
int wGetInstanceId(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    uint32_t m = 0, i = 0;
    liveMap(L, h, m, i);
    lua_pushinteger(L, i);
    return 1;
}
int wGetZoneId(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getZoneId", "GetZoneId"); }
int wGetAreaId(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getAreaId", "GetAreaId"); }
int wGetX(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getX", "GetX"); }
int wGetY(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getY", "GetY"); }
int wGetZ(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getZ", "GetZ"); }
int wGetO(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getO", "GetO"); }
int wGetLocation(lua_State* L) { return readOp(L, kWorldObject, "worldobject.getLocation", "GetLocation"); }
namespace
{
constexpr float kPi = 3.14159265358979323846f;
float optFloat(lua_State* L, int idx, float def, char const* what)
{
    if (lua_isnoneornil(L, idx))
        return def;
    return argFloat(L, idx, what);
}
int targetOrCoords2d(lua_State* L, char const* op, char const* display)
{
    std::vector<EventValue> args;
    if (fl::toHandleAny(L, 2))
        args.emplace_back(objArg(L, 2, kWorldObject, "target"));
    else
    {
        args.emplace_back(argFloat(L, 2, "x"));
        args.emplace_back(argFloat(L, 3, "y"));
    }
    return readOp(L, kWorldObject, op, display, args);
}
int targetOrCoords3d(lua_State* L, char const* op, char const* display)
{
    std::vector<EventValue> args;
    if (fl::toHandleAny(L, 2))
        args.emplace_back(objArg(L, 2, kWorldObject, "target"));
    else
    {
        args.emplace_back(argFloat(L, 2, "x"));
        args.emplace_back(argFloat(L, 3, "y"));
        args.emplace_back(argFloat(L, 4, "z"));
    }
    return readOp(L, kWorldObject, op, display, args);
}
int soundToPlayer(lua_State* L, char const* op, char const* display, char const* idName)
{
    std::vector<EventValue> args{EventValue(static_cast<int64_t>(argUInt(L, 2, idName)))};
    if (!lua_isnoneornil(L, 3))
        args.emplace_back(objArg(L, 3, kPlayer, "player"));
    return actionOp(L, kWorldObject, op, display, args);
}
}  // namespace
int wData(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    std::string const key = fl::guidText(h.guid);
    lua_getfield(L, LUA_REGISTRYINDEX, "fl.WorldObjectData");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "fl.WorldObjectData");
    }
    lua_getfield(L, -1, key.c_str());
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, key.c_str());
    }
    lua_remove(L, -2);
    return 1;
}
int wGetAngle(lua_State* L) { return targetOrCoords2d(L, "worldobject.getAngle", "GetAngle"); }
int wGetDistance(lua_State* L) { return targetOrCoords3d(L, "worldobject.getDistance", "GetDistance"); }
int wGetDistance2d(lua_State* L) { return targetOrCoords2d(L, "worldobject.getDistance2d", "GetDistance2d"); }
int wGetExactDistance(lua_State* L) { return targetOrCoords3d(L, "worldobject.getExactDistance", "GetExactDistance"); }
int wGetExactDistance2d(lua_State* L)
{
    return targetOrCoords2d(L, "worldobject.getExactDistance2d", "GetExactDistance2d");
}
int wGetRelativePoint(lua_State* L)
{
    return readOp(L, kWorldObject, "worldobject.getRelativePoint", "GetRelativePoint",
                  {EventValue(argFloat(L, 2, "distance")), EventValue(argFloat(L, 3, "angle"))});
}
int wIsInFront(lua_State* L)
{
    HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return readOp(L, kWorldObject, "worldobject.isInFront", "IsInFront",
                  {EventValue(target), EventValue(optFloat(L, 3, kPi, "arc"))});
}
int wIsInBack(lua_State* L)
{
    HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return readOp(L, kWorldObject, "worldobject.isInBack", "IsInBack",
                  {EventValue(target), EventValue(optFloat(L, 3, kPi, "arc"))});
}
int wIsInMap(lua_State* L)
{
    HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return readOp(L, kWorldObject, "worldobject.isInMap", "IsInMap", {EventValue(target)});
}
int wIsInRange(lua_State* L)
{
    HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return readOp(L, kWorldObject, "worldobject.isInRange", "IsInRange",
                  {EventValue(target), EventValue(argFloat(L, 3, "minrange")), EventValue(argFloat(L, 4, "maxrange")),
                   EventValue(optBool(L, 5, true))});
}
int wIsInRange2d(lua_State* L)
{
    return readOp(L, kWorldObject, "worldobject.isInRange2d", "IsInRange2d",
                  {EventValue(argFloat(L, 2, "x")), EventValue(argFloat(L, 3, "y")),
                   EventValue(argFloat(L, 4, "minrange")), EventValue(argFloat(L, 5, "maxrange"))});
}
int wIsInRange3d(lua_State* L)
{
    return readOp(L, kWorldObject, "worldobject.isInRange3d", "IsInRange3d",
                  {EventValue(argFloat(L, 2, "x")), EventValue(argFloat(L, 3, "y")), EventValue(argFloat(L, 4, "z")),
                   EventValue(argFloat(L, 5, "minrange")), EventValue(argFloat(L, 6, "maxrange"))});
}
int wIsWithinDist(lua_State* L)
{
    HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return readOp(L, kWorldObject, "worldobject.isWithinDist", "IsWithinDist",
                  {EventValue(target), EventValue(argFloat(L, 3, "distance")), EventValue(optBool(L, 4, true))});
}
int wIsWithinDist2d(lua_State* L)
{
    std::vector<EventValue> args;
    if (fl::toHandleAny(L, 2))
    {
        args.emplace_back(objArg(L, 2, kWorldObject, "target"));
        args.emplace_back(argFloat(L, 3, "distance"));
    }
    else
    {
        args.emplace_back(argFloat(L, 2, "x"));
        args.emplace_back(argFloat(L, 3, "y"));
        args.emplace_back(argFloat(L, 4, "distance"));
    }
    return readOp(L, kWorldObject, "worldobject.isWithinDist2d", "IsWithinDist2d", args);
}
int wIsWithinDist3d(lua_State* L)
{
    std::vector<EventValue> args;
    if (fl::toHandleAny(L, 2))
    {
        args.emplace_back(objArg(L, 2, kWorldObject, "target"));
        args.emplace_back(argFloat(L, 3, "distance"));
    }
    else
    {
        args.emplace_back(argFloat(L, 2, "x"));
        args.emplace_back(argFloat(L, 3, "y"));
        args.emplace_back(argFloat(L, 4, "z"));
        args.emplace_back(argFloat(L, 5, "distance"));
    }
    return readOp(L, kWorldObject, "worldobject.isWithinDist3d", "IsWithinDist3d", args);
}
int wIsWithinDistInMap(lua_State* L)
{
    HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return readOp(L, kWorldObject, "worldobject.isWithinDistInMap", "IsWithinDistInMap",
                  {EventValue(target), EventValue(argFloat(L, 3, "distance")), EventValue(optBool(L, 4, true))});
}
int wIsWithinLoS(lua_State* L) { return targetOrCoords3d(L, "worldobject.isWithinLoS", "IsWithinLoS"); }
int wPlayDirectSound(lua_State* L)
{
    return soundToPlayer(L, "worldobject.playDirectSound", "PlayDirectSound", "sound");
}
int wPlayDistanceSound(lua_State* L)
{
    return soundToPlayer(L, "worldobject.playDistanceSound", "PlayDistanceSound", "sound");
}
int wPlayMusic(lua_State* L) { return soundToPlayer(L, "worldobject.playMusic", "PlayMusic", "music"); }
int wSendPacket(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    sendPacketArg(L, h, 2, "SendPacket");
    return 0;
}
int wSendUnitSay(lua_State* L)
{
    std::string const msg = argStr(L, 2, "msg");
    return actionOp(L, kWorldObject, "worldobject.say", "SendUnitSay",
                    {EventValue(msg), EventValue(static_cast<int64_t>(argUInt(L, 3, "language")))});
}
int wSendUnitYell(lua_State* L)
{
    std::string const msg = argStr(L, 2, "msg");
    return actionOp(L, kWorldObject, "worldobject.yell", "SendUnitYell",
                    {EventValue(msg), EventValue(static_cast<int64_t>(argUInt(L, 3, "language")))});
}
int wSendUnitWhisper(lua_State* L)
{
    std::string const msg = argStr(L, 2, "msg");
    HandleData const& receiver = objArg(L, 4, kPlayer, "receiver");
    return actionOp(L, kWorldObject, "worldobject.whisper", "SendUnitWhisper",
                    {EventValue(msg), EventValue(static_cast<int64_t>(argUInt(L, 3, "lang"))), EventValue(receiver),
                     EventValue(optBool(L, 5, false))});
}

float rangeArg(lua_State* L, int idx) { return static_cast<float>(luaL_optnumber(L, idx, 533.33333)); }

int rangeList(lua_State* L, int kind, uint32_t entry, int hostile, int dead)
{
    HandleData const& h = self(L, kWorldObject);
    std::vector<HandleData> list = host(L).objectsInRange(h, kind, rangeArg(L, 2), entry, hostile, dead);
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 0;
    for (auto const& o : list)
    {
        fl::pushHandle(L, o);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

int wGetPlayersInRange(lua_State* L)
{
    int hostile = static_cast<int>(optUInt(L, 3, 0));
    int dead = static_cast<int>(optUInt(L, 4, 1));
    return rangeList(L, 0, 0, hostile, dead);
}
int wGetCreaturesInRange(lua_State* L)
{
    uint32_t entry = static_cast<uint32_t>(optUInt(L, 3, 0));
    int hostile = static_cast<int>(optUInt(L, 4, 0));
    int dead = static_cast<int>(optUInt(L, 5, 1));
    return rangeList(L, 1, entry, hostile, dead);
}
int wGetGameObjectsInRange(lua_State* L)
{
    uint32_t entry = static_cast<uint32_t>(optUInt(L, 3, 0));
    int hostile = static_cast<int>(optUInt(L, 4, 0));
    return rangeList(L, 2, entry, hostile, 1);
}
std::vector<EventValue> nearArgs(lua_State* L)
{
    float const range = rangeArg(L, 2);
    uint32_t const type = static_cast<uint32_t>(optUInt(L, 3, 0));
    uint32_t const entry = static_cast<uint32_t>(optUInt(L, 4, 0));
    uint32_t const hostile = static_cast<uint32_t>(optUInt(L, 5, 0));
    uint32_t const dead = static_cast<uint32_t>(optUInt(L, 6, 1));
    return {EventValue(range), EventValue(static_cast<int64_t>(type)), EventValue(static_cast<int64_t>(entry)),
            EventValue(static_cast<int64_t>(hostile)), EventValue(static_cast<int64_t>(dead))};
}
int wGetNearObjects(lua_State* L)
{
    return listOp(L, kWorldObject, "worldobject.getNearObjects", "GetNearObjects", nearArgs(L));
}
int wGetNearObject(lua_State* L)
{
    return readHandleOp(L, kWorldObject, "worldobject.getNearObject", "GetNearObject", nearArgs(L));
}
int nearest(lua_State* L, int kind, uint32_t entry, int hostile, int dead)
{
    HandleData const& h = self(L, kWorldObject);
    std::vector<HandleData> list = host(L).objectsInRange(h, kind, rangeArg(L, 2), entry, hostile, dead);
    if (list.empty())
        lua_pushnil(L);
    else
        fl::pushHandle(L, list.front());
    return 1;
}
int wGetNearestPlayer(lua_State* L)
{
    int hostile = static_cast<int>(optUInt(L, 3, 0));
    int dead = static_cast<int>(optUInt(L, 4, 1));
    return nearest(L, 0, 0, hostile, dead);
}
int wGetNearestCreature(lua_State* L)
{
    uint32_t entry = static_cast<uint32_t>(optUInt(L, 3, 0));
    int hostile = static_cast<int>(optUInt(L, 4, 0));
    int dead = static_cast<int>(optUInt(L, 5, 1));
    return nearest(L, 1, entry, hostile, dead);
}
int wGetNearestGameObject(lua_State* L)
{
    uint32_t entry = static_cast<uint32_t>(optUInt(L, 3, 0));
    int hostile = static_cast<int>(optUInt(L, 4, 0));
    return nearest(L, 2, entry, hostile, 1);
}

int wSpawnCreature(lua_State* L)
{
    uint32_t entry = argUInt(L, 2, "entry");
    return readHandleOp(L, kWorldObject, "worldobject.spawnCreature", "SpawnCreature",
                        {EventValue(static_cast<int64_t>(entry)), EventValue(argFloat(L, 3, "x")),
                         EventValue(argFloat(L, 4, "y")), EventValue(argFloat(L, 5, "z")),
                         EventValue(argFloat(L, 6, "o")), EventValue(static_cast<int64_t>(optUInt(L, 7, 0))),
                         EventValue(static_cast<int64_t>(optUInt(L, 8, 0)))});
}
int wSummonGameObject(lua_State* L)
{
    uint32_t entry = argUInt(L, 2, "entry");
    return readHandleOp(L, kWorldObject, "worldobject.summonGameObject", "SummonGameObject",
                        {EventValue(static_cast<int64_t>(entry)), EventValue(argFloat(L, 3, "x")),
                         EventValue(argFloat(L, 4, "y")), EventValue(argFloat(L, 5, "z")),
                         EventValue(argFloat(L, 6, "o")), EventValue(static_cast<int64_t>(optUInt(L, 7, 30)))});
}

// ---- object-bound timed events (same semantics as CreateLuaEvent) ----
int wRegisterEvent(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    uint64_t lo = 0, hi = 0;
    if (lua_istable(L, 3))
    {
        lua_rawgeti(L, 3, 1);
        lo = static_cast<uint64_t>(std::max<lua_Integer>(0, luaL_checkinteger(L, -1)));
        lua_rawgeti(L, 3, 2);
        hi = static_cast<uint64_t>(std::max<lua_Integer>(lo, luaL_checkinteger(L, -1)));
        lua_pop(L, 2);
    }
    else
    {
        lo = hi = static_cast<uint64_t>(std::max<lua_Integer>(0, luaL_checkinteger(L, 3)));
    }
    uint32_t repeats = static_cast<uint32_t>(optUInt(L, 4, 1));
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id = eng(L).addLuaEvent(ref, lo, hi, repeats, h);
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}
int wRemoveEventById(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    uint64_t id = static_cast<uint64_t>(argI64(L, 2, "eventId"));
    eng(L).removeLuaEvent(id, h);   // object-bound: the owner must match
    return 0;
}
int wRemoveEvents(lua_State* L)
{
    HandleData const& h = self(L, kWorldObject);
    eng(L).removeLuaEvents(h);
    return 0;
}
}  // namespace

void registerObjectMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetEntry", fl::safe<oGetEntry>},       {"GetGUID", fl::safe<oGetGUID>},
        {"GetGUIDLow", fl::safe<oGetGUIDLow>},   {"GetTypeId", fl::safe<oGetTypeId>},
        {"IsPlayer", fl::safe<oIsPlayer>},       {"ToPlayer", fl::safe<oToPlayer>},
        {"ToCreature", fl::safe<oToCreature>},   {"ToGameObject", fl::safe<oToGameObject>},
        {"ToCorpse", fl::safe<oToCorpse>},
        {"ToUnit", fl::safe<oToUnit>},           {"IsInWorld", fl::safe<oIsInWorld>},
        {"GetScale", fl::safe<oGetScale>},
        {"SetScale", fl::safe<oSetScale>},       {"HasFlag", fl::safe<oHasFlag>},
        {"GetUInt32Value", fl::safe<oGetUInt32Value>}, {nullptr, nullptr}};
    addMethods(L, "Object", regs);
}

namespace
{
int mGetMapId(lua_State* L)
{
    // A Map handle has no world object behind it: it answers from its own data (WorldObject:GetMap).
    lua_pushinteger(L, self(L, kMap, false).mapId);
    return 1;
}
int mGetInstanceId(lua_State* L)
{
    lua_pushinteger(L, self(L, kMap, false).instanceId);
    return 1;
}
int mGetName(lua_State* L)
{
    fl::HandleData const& h = self(L, kMap, false);
    if (!host(L).mapExists(h.mapId, h.instanceId))
        return luaL_error(L, "Map no longer exists"), 0;
    return readOp(L, kMap, "map.getName", "GetName");
}
}  // namespace

void registerMapMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetMapId", fl::safe<mGetMapId>},
        {"GetInstanceId", fl::safe<mGetInstanceId>},
        {"GetName", fl::safe<mGetName>},
        {nullptr, nullptr}};
    addMethods(L, "Map", regs);
}

void registerWorldObjectMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"Data", fl::safe<wData>},
        {"GetName", fl::safe<wGetName>},           {"GetMap", fl::safe<wGetMap>},
        {"GetMapId", fl::safe<wGetMapId>},         {"GetInstanceId", fl::safe<wGetInstanceId>},
        {"GetZoneId", fl::safe<wGetZoneId>},       {"GetAreaId", fl::safe<wGetAreaId>},
        {"GetX", fl::safe<wGetX>},                 {"GetY", fl::safe<wGetY>},
        {"GetZ", fl::safe<wGetZ>},                 {"GetO", fl::safe<wGetO>},
        {"GetLocation", fl::safe<wGetLocation>},   {"GetAngle", fl::safe<wGetAngle>},
        {"GetDistance", fl::safe<wGetDistance>},   {"GetDistance2d", fl::safe<wGetDistance2d>},
        {"GetExactDistance", fl::safe<wGetExactDistance>},
        {"GetExactDistance2d", fl::safe<wGetExactDistance2d>},
        {"GetRelativePoint", fl::safe<wGetRelativePoint>},
        {"IsInFront", fl::safe<wIsInFront>},       {"IsInBack", fl::safe<wIsInBack>},
        {"IsInMap", fl::safe<wIsInMap>},           {"IsInRange", fl::safe<wIsInRange>},
        {"IsInRange2d", fl::safe<wIsInRange2d>},   {"IsInRange3d", fl::safe<wIsInRange3d>},
        {"IsWithinDist", fl::safe<wIsWithinDist>}, {"IsWithinDist2d", fl::safe<wIsWithinDist2d>},
        {"IsWithinDist3d", fl::safe<wIsWithinDist3d>},
        {"IsWithinDistInMap", fl::safe<wIsWithinDistInMap>},
        {"IsWithinLoS", fl::safe<wIsWithinLoS>},
        {"GetNearestPlayer", fl::safe<wGetNearestPlayer>},
        {"GetNearestCreature", fl::safe<wGetNearestCreature>},
        {"GetNearestGameObject", fl::safe<wGetNearestGameObject>},
        {"GetPlayersInRange", fl::safe<wGetPlayersInRange>},
        {"GetCreaturesInRange", fl::safe<wGetCreaturesInRange>},
        {"GetGameObjectsInRange", fl::safe<wGetGameObjectsInRange>},
        {"GetNearObject", fl::safe<wGetNearObject>},
        {"GetNearObjects", fl::safe<wGetNearObjects>},
        {"PlayDirectSound", fl::safe<wPlayDirectSound>},
        {"PlayDistanceSound", fl::safe<wPlayDistanceSound>},
        {"PlayMusic", fl::safe<wPlayMusic>},
        {"SendPacket", fl::safe<wSendPacket>},
        {"SendUnitSay", fl::safe<wSendUnitSay>},
        {"SendUnitWhisper", fl::safe<wSendUnitWhisper>},
        {"SendUnitYell", fl::safe<wSendUnitYell>},
        {"SpawnCreature", fl::safe<wSpawnCreature>}, {"SummonGameObject", fl::safe<wSummonGameObject>},
        {"RegisterEvent", fl::safe<wRegisterEvent>}, {"RemoveEventById", fl::safe<wRemoveEventById>},
        {"RemoveEvents", fl::safe<wRemoveEvents>},   {nullptr, nullptr}};
    addMethods(L, "WorldObject", regs);
}
}  // namespace flu
