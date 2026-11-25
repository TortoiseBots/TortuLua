// E5: the methods of systems Classic 1.60 does not have (spec section 7). They exist in the class tables so a script that
// calls one gets the documented error instead of "attempt to call a nil value":
//     <Method>: not available on Classic 1.60
// The event ids of those systems (player 45 ACHIEVEMENT_COMPLETE, 50 CAN_JOIN_LFG, guild 9/11, game object 7/8) already
// register and log once (src/engine/elu/EluCommon.cpp, classicStubFor).
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
int classicStubMethod(lua_State* L)
{
    char const* name = lua_tostring(L, lua_upvalueindex(1));
    return luaL_error(L, "%s: not available on Classic 1.60", name ? name : "method");
}

int classicStubMethodFor(lua_State* L, char const* name)
{
    return luaL_error(L, "%s: not available on Classic 1.60", name);
}

void addStubMethod(lua_State* L, char const* className, char const* name)
{
    lua_getfield(L, LUA_REGISTRYINDEX, (std::string("Eluna.class.") + className).c_str());
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_pushstring(L, name);
    lua_pushcclosure(L, classicStubMethod, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

void registerStubMethods(lua_State* L)
{
    // Achievements (section 7: PLAYER 45, HasAchieved, SetAchievement).
    for (char const* m : {"HasAchieved", "SetAchievement", "ResetAchievements"})
        addStubMethod(L, "Player", m);
    // Arena (GetArenaPoints, arena teams).
    for (char const* m : {"GetArenaPoints", "SetArenaPoints", "ModifyArenaPoints", "IsInArenaTeam", "RemoveArenaSpellCooldowns"})
        addStubMethod(L, "Player", m);
    // Dual spec and glyphs.
    for (char const* m : {"GetSpecsCount", "GetActiveSpec"})
        addStubMethod(L, "Player", m);
    // LFG dungeon finder (PLAYER 50, GetLFGRole).
    for (char const* m : {"InRandomLfgDungeon", "IsUsingLfg", "GetLFGRole"})
        addStubMethod(L, "Player", m);
    // Vehicles (Unit:GetVehicle and the Vehicle class).
    for (char const* m : {"GetVehicle", "GetVehicleKit", "IsOnVehicle"})
        addStubMethod(L, "Unit", m);
    // Guild bank (GUILD 9/11 never fire; the documented bank method is a stub).
    addStubMethod(L, "Guild", "SetBankTabText");
}

void registerVehicleMethods(lua_State* L)
{
    // The Vehicle class of the docs: every method is a section 7 stub on Classic 1.60.
    for (char const* m : {"AddPassenger", "GetEntry", "GetOwner", "GetPassenger", "IsOnBoard", "RemovePassenger"})
        addStubMethod(L, "Vehicle", m);
}
}  // namespace flu
