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
int gGetDisplayId(lua_State* L)
{
    return readOp(L, kGameObject, "gameobject.getDisplayId", "GetDisplayId");
}
int gGetLootRecipient(lua_State* L)
{
    return readHandleOp(L, kGameObject, "gameobject.getLootRecipient", "GetLootRecipient");
}
int gGetLootRecipientGroup(lua_State* L)
{
    return readHandleOp(L, kGameObject, "gameobject.getLootRecipientGroup", "GetLootRecipientGroup");
}
int gHasQuest(lua_State* L)
{
    return readOp(L, kGameObject, "gameobject.hasQuest", "HasQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int gIsActive(lua_State* L) { return readOp(L, kGameObject, "gameobject.isActive", "IsActive"); }
int gIsDestructible(lua_State* L) { return readOp(L, kGameObject, "gameobject.isDestructible", "IsDestructible"); }
int gIsTransport(lua_State* L) { return readOp(L, kGameObject, "gameobject.isTransport", "IsTransport"); }
int gRemoveFromWorld(lua_State* L)
{
    fl::HandleData const& h = self(L, kGameObject);
    if (optBool(L, 2, false))
        return luaL_error(L, "RemoveFromWorld: database delete blocks the game thread");
    std::vector<EventValue> out;
    gameCall(L, h, "gameobject.removeFromWorld", {}, out, "RemoveFromWorld");
    eng(L).removeWorldObjectData(h);
    return 0;
}
// GameObject::SaveToDB writes shared ObjectMgr spawn data (NewOrExistGameObjectData) without synchronisation from a map thread.
int gSaveToDB(lua_State* L)
{
    self(L, kGameObject);
    return luaL_error(L, "SaveToDB: not available on this server (unsynchronised shared spawn data write from a map thread)");
}
int gSetRespawnTime(lua_State* L)
{
    return actionOp(L, kGameObject, "gameobject.setRespawnTime", "SetRespawnTime",
                    {EventValue(static_cast<int64_t>(optInt(L, 2, 0, "delay")))});
}
int gSetSpawnedByDefault(lua_State* L)
{
    return actionOp(L, kGameObject, "gameobject.setSpawnedByDefault", "SetSpawnedByDefault",
                    {EventValue(optBool(L, 2, true))});
}
}  // namespace

void registerGameObjectMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetGoState", fl::safe<gGetGoState>},       {"SetGoState", fl::safe<gSetGoState>},
        {"GetLootState", fl::safe<gGetLootState>},   {"SetLootState", fl::safe<gSetLootState>},
        {"Despawn", fl::safe<gDespawn>},             {"Respawn", fl::safe<gRespawn>},
        {"IsSpawned", fl::safe<gIsSpawned>},         {"UseDoorOrButton", fl::safe<gUseDoorOrButton>},
        {"GetDBTableGUIDLow", fl::safe<gGetDBTableGUIDLow>},
        {"GetDisplayId", fl::safe<gGetDisplayId>},
        {"GetLootRecipient", fl::safe<gGetLootRecipient>},
        {"GetLootRecipientGroup", fl::safe<gGetLootRecipientGroup>},
        {"HasQuest", fl::safe<gHasQuest>},
        {"IsActive", fl::safe<gIsActive>},
        {"IsDestructible", fl::safe<gIsDestructible>},
        {"IsTransport", fl::safe<gIsTransport>},
        {"RemoveFromWorld", fl::safe<gRemoveFromWorld>},
        {"SaveToDB", fl::safe<gSaveToDB>},
        {"SetRespawnTime", fl::safe<gSetRespawnTime>},
        {"SetSpawnedByDefault", fl::safe<gSetSpawnedByDefault>},
        {nullptr, nullptr}};
    addMethods(L, "GameObject", regs);
}
}  // namespace flu
