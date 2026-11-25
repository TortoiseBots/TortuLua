// E5: the BattleGround class (docs: BattleGround/index.html) handed to RegisterBGEvent handlers, and the Map methods the
// E5 list names (Map:SetWeather). A BattleGround handle carries its map (mapId), its instance (guid.lo) and its type
// (guid.hi): GetMapId/GetInstanceId/GetMap/GetTypeId answer from it, every other method asks the host.
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

int bgGetMapId(lua_State* L)
{
    lua_pushinteger(L, self(L, kBattleGround, false).mapId);
    return 1;
}
int bgGetInstanceId(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(self(L, kBattleGround, false).guid.lo));
    return 1;
}
int bgGetTypeId(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(self(L, kBattleGround, false).guid.hi));
    return 1;
}
int bgGetMap(lua_State* L)
{
    fl::HandleData const& h = self(L, kBattleGround, false);
    fl::HandleData map;
    map.kind = fl::ObjKind::Map;
    map.mapId = h.mapId;
    map.instanceId = static_cast<uint32_t>(h.guid.lo);
    fl::pushHandle(L, map);
    return 1;
}
int bgName(lua_State* L) { return readOp(L, kBattleGround, "battleground.getName", "GetName"); }
int bgStatus(lua_State* L) { return readOp(L, kBattleGround, "battleground.getStatus", "GetStatus"); }
int bgWinner(lua_State* L) { return readOp(L, kBattleGround, "battleground.getWinner", "GetWinner"); }
int bgEndTime(lua_State* L) { return readOp(L, kBattleGround, "battleground.getEndTime", "GetEndTime"); }
int bgBracket(lua_State* L) { return readOp(L, kBattleGround, "battleground.getBracketId", "GetBracketId"); }
int bgMaxLevel(lua_State* L) { return readOp(L, kBattleGround, "battleground.getMaxLevel", "GetMaxLevel"); }
int bgMinLevel(lua_State* L) { return readOp(L, kBattleGround, "battleground.getMinLevel", "GetMinLevel"); }
int bgMaxPlayers(lua_State* L) { return readOp(L, kBattleGround, "battleground.getMaxPlayers", "GetMaxPlayers"); }
int bgMinPlayers(lua_State* L) { return readOp(L, kBattleGround, "battleground.getMinPlayers", "GetMinPlayers"); }
int bgMaxPlayersPerTeam(lua_State* L)
{
    return readOp(L, kBattleGround, "battleground.getMaxPlayersPerTeam", "GetMaxPlayersPerTeam");
}
int bgMinPlayersPerTeam(lua_State* L)
{
    return readOp(L, kBattleGround, "battleground.getMinPlayersPerTeam", "GetMinPlayersPerTeam");
}
int bgFreeSlots(lua_State* L)
{
    return readOp(L, kBattleGround, "battleground.getFreeSlotsForTeam", "GetFreeSlotsForTeam",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "team")))});
}
int bgAliveCount(lua_State* L)
{
    return readOp(L, kBattleGround, "battleground.getAlivePlayersCountByTeam", "GetAlivePlayersCountByTeam",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "team")))});
}
int bgBonusHonor(lua_State* L)
{
    return readOp(L, kBattleGround, "battleground.getBonusHonorFromKillCount", "GetBonusHonorFromKillCount",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "kills")))});
}

// ---- Map methods of the E5 long tail (docs: Map/index.html) ----
fl::HandleData const& mapSelf(lua_State* L)
{
    fl::HandleData const& h = self(L, kMap, false);
    if (!host(L).mapExists(h.mapId, h.instanceId))
        luaL_error(L, "Map no longer exists");
    return h;
}

int mSetWeather(lua_State* L)
{
    mapSelf(L);
    return actionOp(L, kMap, "map.setWeather", "SetWeather",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "zone"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 3, "type"))),
                     EventValue(static_cast<double>(luaL_checknumber(L, 4)))});
}
int mGetPlayerCount(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.getPlayerCount", "GetPlayerCount");
}
int mGetPlayers(lua_State* L)
{
    mapSelf(L);
    return listOp(L, kMap, "map.getPlayers", "GetPlayers");
}
int mGetWorldObject(lua_State* L)
{
    mapSelf(L);
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readHandleOp(L, kMap, "map.getWorldObject", "GetWorldObject", {EventValue(fl::guidText(guid))});
}
int mIsDungeon(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.isDungeon", "IsDungeon");
}
int mIsRaid(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.isRaid", "IsRaid");
}
int mIsArena(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.isArena", "IsArena");
}
int mIsBattleground(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.isBattleground", "IsBattleground");
}
int mIsEmpty(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.isEmpty", "IsEmpty");
}
int mIsHeroic(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.isHeroic", "IsHeroic");
}
int mGetDifficulty(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.getDifficulty", "GetDifficulty");
}
int mGetAreaId(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.getAreaId", "GetAreaId",
                  {EventValue(static_cast<double>(luaL_checknumber(L, 2))),
                   EventValue(static_cast<double>(luaL_checknumber(L, 3))),
                   EventValue(static_cast<double>(luaL_checknumber(L, 4)))});
}
int mGetHeight(lua_State* L)
{
    mapSelf(L);
    return readOp(L, kMap, "map.getHeight", "GetHeight",
                  {EventValue(static_cast<double>(luaL_checknumber(L, 2))),
                   EventValue(static_cast<double>(luaL_checknumber(L, 3)))});
}
// Map:Data(): a runtime cache tied to the map; one table per state (the state dies with the map, E4).
int mData(lua_State* L)
{
    fl::HandleData const& h = mapSelf(L);
    std::string key = "fl.MapData." + std::to_string(h.mapId) + "." + std::to_string(h.instanceId);
    lua_getfield(L, LUA_REGISTRYINDEX, key.c_str());
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, key.c_str());
    }
    return 1;
}
}  // namespace

void registerBattleGroundMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetMapId", fl::safe<bgGetMapId>},           {"GetInstanceId", fl::safe<bgGetInstanceId>},
        {"GetTypeId", fl::safe<bgGetTypeId>},         {"GetMap", fl::safe<bgGetMap>},
        {"GetName", fl::safe<bgName>},                {"GetStatus", fl::safe<bgStatus>},
        {"GetWinner", fl::safe<bgWinner>},            {"GetEndTime", fl::safe<bgEndTime>},
        {"GetBracketId", fl::safe<bgBracket>},        {"GetMaxLevel", fl::safe<bgMaxLevel>},
        {"GetMinLevel", fl::safe<bgMinLevel>},        {"GetMaxPlayers", fl::safe<bgMaxPlayers>},
        {"GetMinPlayers", fl::safe<bgMinPlayers>},    {"GetMaxPlayersPerTeam", fl::safe<bgMaxPlayersPerTeam>},
        {"GetMinPlayersPerTeam", fl::safe<bgMinPlayersPerTeam>},
        {"GetFreeSlotsForTeam", fl::safe<bgFreeSlots>},
        {"GetAlivePlayersCountByTeam", fl::safe<bgAliveCount>},
        {"GetBonusHonorFromKillCount", fl::safe<bgBonusHonor>},
        {nullptr, nullptr}};
    addMethods(L, "BattleGround", regs);

    luaL_Reg const mapRegs[] = {
        {"SetWeather", fl::safe<mSetWeather>},
        {"GetPlayerCount", fl::safe<mGetPlayerCount>},
        {"GetPlayers", fl::safe<mGetPlayers>},
        {"GetWorldObject", fl::safe<mGetWorldObject>},
        {"IsDungeon", fl::safe<mIsDungeon>},
        {"IsRaid", fl::safe<mIsRaid>},
        {"IsArena", fl::safe<mIsArena>},
        {"IsBattleground", fl::safe<mIsBattleground>},
        {"IsEmpty", fl::safe<mIsEmpty>},
        {"IsHeroic", fl::safe<mIsHeroic>},
        {"GetDifficulty", fl::safe<mGetDifficulty>},
        {"GetAreaId", fl::safe<mGetAreaId>},
        {"GetHeight", fl::safe<mGetHeight>},
        {"Data", fl::safe<mData>},
        {nullptr, nullptr}};
    addMethods(L, "Map", mapRegs);
}
}  // namespace flu
