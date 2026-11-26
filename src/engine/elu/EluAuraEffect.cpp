// Eluna AuraEffect bindings (docs: AuraEffect/index.html). Transient handle valid only during the call that made it.
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
int aeOp(lua_State* L, char const* op, char const* display, std::vector<fl::EventValue> args = {})
{
    fl::HandleData const& h = kindArg(L, 1, kAuraEffect, display);
    std::vector<fl::EventValue> out;
    if (!host(L).gameCall(h, op, args, out))
        return luaL_error(L, "%s no longer exists", fl::eluClassName(h.kind));
    return pushResults(L, out);
}

int aeCalculateAmount(lua_State* L)
{
    return aeOp(L, "auraeffect.calculateAmount", "CalculateAmount",
                {EventValue(objArg(L, 2, kUnit, "caster"))});
}
int aeCalculatePeriodic(lua_State* L)
{
    return aeOp(L, "auraeffect.calculatePeriodic", "CalculatePeriodic",
                {EventValue(objArg(L, 2, kUnit, "caster")), EventValue(optBool(L, 3, false)),
                 EventValue(optBool(L, 4, false))});
}
int aeCanBeRecalculated(lua_State* L) { return aeOp(L, "auraeffect.canBeRecalculated", "CanBeRecalculated"); }
int aeChangeAmount(lua_State* L)
{
    return aeOp(L, "auraeffect.changeAmount", "ChangeAmount",
                {EventValue(argInt(L, 2, "newAmount")), EventValue(optBool(L, 3, true)),
                 EventValue(optBool(L, 4, false))});
}
int aeGetAmount(lua_State* L) { return aeOp(L, "auraeffect.getAmount", "GetAmount"); }
int aeGetAmplitude(lua_State* L) { return aeOp(L, "auraeffect.getAmplitude", "GetAmplitude"); }
int aeGetAuraType(lua_State* L) { return aeOp(L, "auraeffect.getAuraType", "GetAuraType"); }
int aeGetBase(lua_State* L) { return aeOp(L, "auraeffect.getBase", "GetBase"); }
int aeGetBaseAmount(lua_State* L) { return aeOp(L, "auraeffect.getBaseAmount", "GetBaseAmount"); }
int aeGetCaster(lua_State* L) { return aeOp(L, "auraeffect.getCaster", "GetCaster"); }
int aeGetCasterGUID(lua_State* L) { return aeOp(L, "auraeffect.getCasterGuid", "GetCasterGUID"); }
int aeGetEffIndex(lua_State* L) { return aeOp(L, "auraeffect.getEffIndex", "GetEffIndex"); }
int aeGetId(lua_State* L) { return aeOp(L, "auraeffect.getId", "GetId"); }
int aeGetPeriodicTimer(lua_State* L) { return aeOp(L, "auraeffect.getPeriodicTimer", "GetPeriodicTimer"); }
int aeGetRemainingTicks(lua_State* L) { return aeOp(L, "auraeffect.getRemainingTicks", "GetRemainingTicks"); }
int aeGetSpellInfo(lua_State* L) { return aeOp(L, "auraeffect.getSpellInfo", "GetSpellInfo"); }
int aeGetTargetList(lua_State* L)
{
    fl::HandleData const& h = kindArg(L, 1, kAuraEffect, "GetTargetList");
    std::vector<fl::EventValue> out;
    if (!host(L).gameCall(h, "auraeffect.getTargetList", {}, out))
        return luaL_error(L, "%s no longer exists", fl::eluClassName(h.kind));
    std::vector<fl::HandleData> list;
    list.reserve(out.size());
    for (auto const& v : out)
        if (auto const* o = std::get_if<fl::HandleData>(&v.v))
            list.push_back(*o);
    return pushHandleTable(L, list);
}
int aeGetTickNumber(lua_State* L) { return aeOp(L, "auraeffect.getTickNumber", "GetTickNumber"); }
int aeGetTotalTicks(lua_State* L) { return aeOp(L, "auraeffect.getTotalTicks", "GetTotalTicks"); }
int aeRecalculateAmount(lua_State* L) { return aeOp(L, "auraeffect.recalculateAmount", "RecalculateAmount"); }
int aeSetAmount(lua_State* L)
{
    return aeOp(L, "auraeffect.setAmount", "SetAmount", {EventValue(argInt(L, 2, "amount"))});
}
int aeSetCanBeRecalculated(lua_State* L)
{
    return aeOp(L, "auraeffect.setCanBeRecalculated", "SetCanBeRecalculated", {EventValue(optBool(L, 2, true))});
}
int aeSetPeriodicTimer(lua_State* L)
{
    return aeOp(L, "auraeffect.setPeriodicTimer", "SetPeriodicTimer", {EventValue(argInt(L, 2, "timer"))});
}
}  // namespace

void registerAuraEffectMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"CalculateAmount", fl::safe<aeCalculateAmount>},
        {"CalculatePeriodic", fl::safe<aeCalculatePeriodic>},
        {"CanBeRecalculated", fl::safe<aeCanBeRecalculated>},
        {"ChangeAmount", fl::safe<aeChangeAmount>},
        {"GetAmount", fl::safe<aeGetAmount>},
        {"GetAmplitude", fl::safe<aeGetAmplitude>},
        {"GetAuraType", fl::safe<aeGetAuraType>},
        {"GetBase", fl::safe<aeGetBase>},
        {"GetBaseAmount", fl::safe<aeGetBaseAmount>},
        {"GetCaster", fl::safe<aeGetCaster>},
        {"GetCasterGUID", fl::safe<aeGetCasterGUID>},
        {"GetEffIndex", fl::safe<aeGetEffIndex>},
        {"GetId", fl::safe<aeGetId>},
        {"GetPeriodicTimer", fl::safe<aeGetPeriodicTimer>},
        {"GetRemainingTicks", fl::safe<aeGetRemainingTicks>},
        {"GetSpellInfo", fl::safe<aeGetSpellInfo>},
        {"GetTargetList", fl::safe<aeGetTargetList>},
        {"GetTickNumber", fl::safe<aeGetTickNumber>},
        {"GetTotalTicks", fl::safe<aeGetTotalTicks>},
        {"RecalculateAmount", fl::safe<aeRecalculateAmount>},
        {"SetAmount", fl::safe<aeSetAmount>},
        {"SetCanBeRecalculated", fl::safe<aeSetCanBeRecalculated>},
        {"SetPeriodicTimer", fl::safe<aeSetPeriodicTimer>},
        {nullptr, nullptr}};
    addMethods(L, "AuraEffect", regs);
}
}  // namespace flu
