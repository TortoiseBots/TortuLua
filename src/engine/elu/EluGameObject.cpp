// Eluna GameObject methods of E1 (docs: GameObject/index.html).
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

int gGetGoState(lua_State* L) { return readOp(L, kGameObject, "gameobject.getGoState", "GetGoState"); }
int gSetGoState(lua_State* L)
{
    return actionOp(L, kGameObject, "gameobject.setGoState", "SetGoState",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "state")))});
}
int gGetLootState(lua_State* L) { return readOp(L, kGameObject, "gameobject.getLootState", "GetLootState"); }
int gSetLootState(lua_State* L)
{
    return actionOp(L, kGameObject, "gameobject.setLootState", "SetLootState",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "state")))});
}
int gDespawn(lua_State* L) { return actionOp(L, kGameObject, "gameobject.despawn", "Despawn"); }
int gRespawn(lua_State* L) { return actionOp(L, kGameObject, "gameobject.respawn", "Respawn"); }
int gIsSpawned(lua_State* L) { return readOp(L, kGameObject, "gameobject.isSpawned", "IsSpawned"); }
int gUseDoorOrButton(lua_State* L)
{
    return actionOp(L, kGameObject, "gameobject.useDoorOrButton", "UseDoorOrButton",
                    {EventValue(static_cast<int64_t>(optUInt(L, 2, 0)))});
}
int gGetDBTableGUIDLow(lua_State* L)
{
    return readOp(L, kGameObject, "gameobject.getDBTableGUIDLow", "GetDBTableGUIDLow");
}
}  // namespace

void registerGameObjectMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetGoState", fl::safe<gGetGoState>},       {"SetGoState", fl::safe<gSetGoState>},
        {"GetLootState", fl::safe<gGetLootState>},   {"SetLootState", fl::safe<gSetLootState>},
        {"Despawn", fl::safe<gDespawn>},             {"Respawn", fl::safe<gRespawn>},
        {"IsSpawned", fl::safe<gIsSpawned>},         {"UseDoorOrButton", fl::safe<gUseDoorOrButton>},
        {"GetDBTableGUIDLow", fl::safe<gGetDBTableGUIDLow>}, {nullptr, nullptr}};
    addMethods(L, "GameObject", regs);
}
}  // namespace flu
