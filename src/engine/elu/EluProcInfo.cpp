// Eluna ElunaProcInfo bindings (docs: ElunaProcInfo/index.html). Transient handle valid only during the call that
// made it. SetHeal is a core stub in EluStubs.cpp (HealInfo has no Heal mutator on this core).
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
int piOp(lua_State* L, char const* op, char const* display, std::vector<fl::EventValue> args = {})
{
    fl::HandleData const& h = kindArg(L, 1, kProcInfo, display);
    std::vector<fl::EventValue> out;
    if (!host(L).gameCall(h, op, args, out))
        return luaL_error(L, "%s no longer exists", fl::eluClassName(h.kind));
    return pushResults(L, out);
}

int piGetActionTarget(lua_State* L) { return piOp(L, "procinfo.getActionTarget", "GetActionTarget"); }
int piGetActor(lua_State* L) { return piOp(L, "procinfo.getActor", "GetActor"); }
int piGetAttackType(lua_State* L) { return piOp(L, "procinfo.getAttackType", "GetAttackType"); }
int piGetBlock(lua_State* L) { return piOp(L, "procinfo.getBlock", "GetBlock"); }
int piGetDamage(lua_State* L) { return piOp(L, "procinfo.getDamage", "GetDamage"); }
int piGetDamageAbsorb(lua_State* L) { return piOp(L, "procinfo.getDamageAbsorb", "GetDamageAbsorb"); }
int piGetDamageType(lua_State* L) { return piOp(L, "procinfo.getDamageType", "GetDamageType"); }
int piGetEffectiveHeal(lua_State* L) { return piOp(L, "procinfo.getEffectiveHeal", "GetEffectiveHeal"); }
int piGetHeal(lua_State* L) { return piOp(L, "procinfo.getHeal", "GetHeal"); }
int piGetHealAbsorb(lua_State* L) { return piOp(L, "procinfo.getHealAbsorb", "GetHealAbsorb"); }
int piGetHitMask(lua_State* L) { return piOp(L, "procinfo.getHitMask", "GetHitMask"); }
int piGetMap(lua_State* L) { return piOp(L, "procinfo.getMap", "GetMap"); }
int piGetProcSpell(lua_State* L) { return piOp(L, "procinfo.getProcSpell", "GetProcSpell"); }
int piGetResist(lua_State* L) { return piOp(L, "procinfo.getResist", "GetResist"); }
int piGetSchoolMask(lua_State* L) { return piOp(L, "procinfo.getSchoolMask", "GetSchoolMask"); }
int piGetSpellInfo(lua_State* L) { return piOp(L, "procinfo.getSpellInfo", "GetSpellInfo"); }
int piGetSpellPhaseMask(lua_State* L) { return piOp(L, "procinfo.getSpellPhaseMask", "GetSpellPhaseMask"); }
int piGetSpellTypeMask(lua_State* L) { return piOp(L, "procinfo.getSpellTypeMask", "GetSpellTypeMask"); }
int piGetTypeMask(lua_State* L) { return piOp(L, "procinfo.getTypeMask", "GetTypeMask"); }
int piHasDamage(lua_State* L) { return piOp(L, "procinfo.hasDamage", "HasDamage"); }
int piHasHeal(lua_State* L) { return piOp(L, "procinfo.hasHeal", "HasHeal"); }
int piSetAbsorbDamage(lua_State* L)
{
    return piOp(L, "procinfo.setAbsorbDamage", "SetAbsorbDamage",
                {EventValue(static_cast<int64_t>(argUInt(L, 2, "absorb")))});
}
int piSetAbsorbHeal(lua_State* L)
{
    return piOp(L, "procinfo.setAbsorbHeal", "SetAbsorbHeal",
                {EventValue(static_cast<int64_t>(argUInt(L, 2, "absorb")))});
}
int piSetBlockDamage(lua_State* L)
{
    return piOp(L, "procinfo.setBlockDamage", "SetBlockDamage",
                {EventValue(static_cast<int64_t>(argUInt(L, 2, "block")))});
}
int piSetDamage(lua_State* L)
{
    return piOp(L, "procinfo.setDamage", "SetDamage", {EventValue(argInt(L, 2, "damage"))});
}
int piSetEffectiveHeal(lua_State* L)
{
    return piOp(L, "procinfo.setEffectiveHeal", "SetEffectiveHeal",
                {EventValue(static_cast<int64_t>(argUInt(L, 2, "effectiveHeal")))});
}
int piSetResistDamage(lua_State* L)
{
    return piOp(L, "procinfo.setResistDamage", "SetResistDamage",
                {EventValue(static_cast<int64_t>(argUInt(L, 2, "resist")))});
}
}  // namespace

void registerProcInfoMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetActionTarget", fl::safe<piGetActionTarget>},
        {"GetActor", fl::safe<piGetActor>},
        {"GetAttackType", fl::safe<piGetAttackType>},
        {"GetBlock", fl::safe<piGetBlock>},
        {"GetDamage", fl::safe<piGetDamage>},
        {"GetDamageAbsorb", fl::safe<piGetDamageAbsorb>},
        {"GetDamageType", fl::safe<piGetDamageType>},
        {"GetEffectiveHeal", fl::safe<piGetEffectiveHeal>},
        {"GetHeal", fl::safe<piGetHeal>},
        {"GetHealAbsorb", fl::safe<piGetHealAbsorb>},
        {"GetHitMask", fl::safe<piGetHitMask>},
        {"GetMap", fl::safe<piGetMap>},
        {"GetProcSpell", fl::safe<piGetProcSpell>},
        {"GetResist", fl::safe<piGetResist>},
        {"GetSchoolMask", fl::safe<piGetSchoolMask>},
        {"GetSpellInfo", fl::safe<piGetSpellInfo>},
        {"GetSpellPhaseMask", fl::safe<piGetSpellPhaseMask>},
        {"GetSpellTypeMask", fl::safe<piGetSpellTypeMask>},
        {"GetTypeMask", fl::safe<piGetTypeMask>},
        {"HasDamage", fl::safe<piHasDamage>},
        {"HasHeal", fl::safe<piHasHeal>},
        {"SetAbsorbDamage", fl::safe<piSetAbsorbDamage>},
        {"SetAbsorbHeal", fl::safe<piSetAbsorbHeal>},
        {"SetBlockDamage", fl::safe<piSetBlockDamage>},
        {"SetDamage", fl::safe<piSetDamage>},
        {"SetEffectiveHeal", fl::safe<piSetEffectiveHeal>},
        {"SetResistDamage", fl::safe<piSetResistDamage>},
        {nullptr, nullptr}};
    addMethods(L, "ElunaProcInfo", regs);
}
}  // namespace flu
