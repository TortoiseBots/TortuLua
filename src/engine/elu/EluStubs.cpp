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

// A documented call the pinned core has no API for (not a Classic-era feature: the core itself never grew it, or uses
// typed fragments / phase shifts instead). Same contract as the Classic stub: the name is in the class table, the call
// raises the documented error.
int coreStubMethod(lua_State* L)
{
    char const* name = lua_tostring(L, lua_upvalueindex(1));
    return luaL_error(L, "%s: not available on this core", name ? name : "method");
}

void addCoreStubMethod(lua_State* L, char const* className, char const* name)
{
    lua_getfield(L, LUA_REGISTRYINDEX, (std::string("Eluna.class.") + className).c_str());
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_pushstring(L, name);
    lua_pushcclosure(L, coreStubMethod, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
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
    // The documented Player calls the pinned core cannot answer, with the core fact that blocks each one:
    //  GetFreeTalentPoints, SetFreeTalentPoints: Classic 1.60 spends per-spec trait configs and Player.h has no free
    //    talent-point pool (UpdateFields.h keeps PetTalentPoints on PetData only).
    //  AddLifetimeKills, RemoveLifetimeKills, SetLifetimeKills: the counter is ActivePlayerData::LifetimeHonorableKills
    //    and SetUpdateFieldValue is private to the core's entity classes (Object/Updates/UpdateField.h); GetLifetimeKills
    //    reads the fragment, the three writers have no public call.
    //  GetPhaseMaskForSpawn: phasing is PhaseShift phase ids (Phasing/PhasingHandler.h); there is no 32-bit phase mask.
    //  ResetPetTalents: pet talents were removed from the core (the TODO block of scripts/Commands/cs_reset.cpp and no
    //    talent API on Pet).
    //  SendAreaTriggerMessage: Opcodes.h has no SMSG_AREA_TRIGGER_MESSAGE (PrintNotification is SendNotification).
    //  LearnTalent: Player::LearnTalent(uint32 talentId, int32* spellOnCooldown) (Player.h) has no rank parameter;
    //    ranks are trait-config entries, so the documented (talent_id, talentRank) call cannot be answered faithfully.
    for (char const* m : {"GetFreeTalentPoints", "AddLifetimeKills", "RemoveLifetimeKills", "SetLifetimeKills",
                          "SetFreeTalentPoints", "GetPhaseMaskForSpawn", "ResetPetTalents", "SendAreaTriggerMessage",
                          "LearnTalent"})
        addCoreStubMethod(L, "Player", m);
    // Object generic update-field and flag accessors: this core stores typed update-field fragments rather than a
    // flat indexable values array.
    for (char const* m :
         {"GetByteValue", "GetFloatValue", "GetInt32Value", "GetUInt16Value", "GetUInt32Value", "GetUInt64Value",
          "HasFlag", "RemoveFlag", "SetByteValue", "SetFlag", "SetFloatValue", "SetInt16Value", "SetInt32Value",
          "SetUInt16Value", "SetUInt32Value", "SetUInt64Value", "UpdateUInt32Value"})
        addCoreStubMethod(L, "Object", m);
    // WorldObject bitmask phasing: this core uses PhaseShift phase ids rather than a 32-bit phase mask.
    for (char const* m : {"GetPhaseMask", "SetPhaseMask"})
        addCoreStubMethod(L, "WorldObject", m);
    // Map instance script data: InstanceMapScript is database-bound (instance_template.script) with no Lua instance-data
    // object or SaveToDB API (InstanceScript persists boss states and values incrementally through InstanceLockMgr).
    for (char const* m : {"GetInstanceData", "SaveInstanceData"})
        addCoreStubMethod(L, "Map", m);
    // Item random properties/suffixes: ForeverCore uses ItemRandomBonusListId instead of legacy random property/suffix DB2s.
    for (char const* m : {"GetRandomProperty", "GetRandomSuffix", "SetRandomProperty", "SetRandomSuffix"})
        addCoreStubMethod(L, "Item", m);
    // SpellInfo fields/methods absent on this core: SpellPowerEntry has no ManaPerSecondPerLevel, SpellInfo has no
    // Priority or RuneCostID, and SpellModifier has no Lua class.
    for (char const* m : {"GetManaPerSecondPerLevel", "GetPriority", "GetRuneCostID", "IsAffectedBySpellMod"})
        addCoreStubMethod(L, "SpellInfo", m);
    // ElunaProcInfo: HealInfo exposes AbsorbHeal and SetEffectiveHeal but has no SetHeal/ModifyHeal mutator.
    addCoreStubMethod(L, "ElunaProcInfo", "SetHeal");
}

void registerVehicleMethods(lua_State* L)
{
    // The Vehicle class of the docs: every method is a section 7 stub on Classic 1.60.
    for (char const* m : {"AddPassenger", "GetEntry", "GetOwner", "GetPassenger", "IsOnBoard", "RemovePassenger"})
        addStubMethod(L, "Vehicle", m);
}
}  // namespace flu
