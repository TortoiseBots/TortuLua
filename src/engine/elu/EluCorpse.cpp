// E5: the Corpse class (docs: Corpse/index.html), obtained from Player:GetCorpse. The handle names the corpse's map and
// guid, like every other world object: a corpse the core has removed raises "Corpse no longer exists".
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

int cGetGhostTime(lua_State* L) { return readOp(L, kCorpse, "corpse.getGhostTime", "GetGhostTime"); }
// docs type: number. The owner is a player; the low guid counter is what GetPlayerByGUID's integer form takes.
int cGetOwnerGUID(lua_State* L) { return readOp(L, kCorpse, "corpse.getOwnerGuid", "GetOwnerGUID"); }
int cGetType(lua_State* L) { return readOp(L, kCorpse, "corpse.getType", "GetType"); }
int cResetGhostTime(lua_State* L) { return actionOp(L, kCorpse, "corpse.resetGhostTime", "ResetGhostTime"); }
int cSaveToDB(lua_State* L) { return actionOp(L, kCorpse, "corpse.saveToDB", "SaveToDB"); }
}  // namespace

void registerCorpseMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetGhostTime", fl::safe<cGetGhostTime>},
        {"GetOwnerGUID", fl::safe<cGetOwnerGUID>},
        {"GetType", fl::safe<cGetType>},
        {"ResetGhostTime", fl::safe<cResetGhostTime>},
        {"SaveToDB", fl::safe<cSaveToDB>},
        {nullptr, nullptr}};
    addMethods(L, "Corpse", regs);
}
}  // namespace flu
