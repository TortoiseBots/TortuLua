// Eluna Spell and Aura of the long tail (docs: Spell/index.html, Aura/index.html). A spell or aura handle is not looked
// up in the world: it carries {caster guid, spell/aura id in `mapId`, cast id in `instanceId`} and is only meaningful
// inside the call that produced it (docs/E0-E1-NOTES.md). The host keeps the transient object behind `instanceId` and
// answers every method; a handle from an earlier call raises "Spell/Aura no longer exists".
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
// A spell/aura handle: the kind check only, no world lookup (there is no store to look the cast up in).
fl::HandleData const& spellSelf(lua_State* L, unsigned kinds, char const* method)
{
    return kindArg(L, 1, kinds, method);
}

int callOn(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<fl::EventValue> args)
{
    fl::HandleData const& h = spellSelf(L, kinds, display);
    std::vector<fl::EventValue> out;
    if (!host(L).gameCall(h, op, args, out))
        // A transient object that the running call no longer owns (docs/E2-E3-NOTES.md): the host resolved nothing.
        return luaL_error(L, "%s no longer exists", fl::eluClassName(h.kind));
    return pushResults(L, out);
}

int spellOp(lua_State* L, char const* op, char const* display, std::vector<fl::EventValue> args = {})
{
    return callOn(L, kSpell, op, display, std::move(args));
}
int auraOp(lua_State* L, char const* op, char const* display, std::vector<fl::EventValue> args = {})
{
    return callOn(L, kAura, op, display, std::move(args));
}
// A SpellInfo argument (Spell:IsTriggeredByAura): the handle's id, the host looks the entry up again.
int64_t spellInfoId(lua_State* L, int idx, char const* what)
{
    return static_cast<int64_t>(kindArg(L, idx, kSpellInfo, what).guid.lo);
}

// ---- Spell ----
int sGetEntry(lua_State* L) { return spellOp(L, "spell.getEntry", "GetEntry"); }
int sGetCaster(lua_State* L) { return spellOp(L, "spell.getCaster", "GetCaster"); }
int sGetOriginalCaster(lua_State* L) { return spellOp(L, "spell.getOriginalCaster", "GetOriginalCaster"); }
int sCancel(lua_State* L) { return spellOp(L, "spell.cancel", "Cancel"); }
int sGetDuration(lua_State* L) { return spellOp(L, "spell.getDuration", "GetDuration"); }
int sGetCastTime(lua_State* L) { return spellOp(L, "spell.getCastTime", "GetCastTime"); }
int sGetDelayStart(lua_State* L) { return spellOp(L, "spell.getDelayStart", "GetDelayStart"); }
int sGetDelayMoment(lua_State* L) { return spellOp(L, "spell.getDelayMoment", "GetDelayMoment"); }
int sSetDelayStart(lua_State* L)
{
    return spellOp(L, "spell.setDelayStart", "SetDelayStart", {EventValue(argI64(L, 2, "time"))});
}
int sGetPowerCost(lua_State* L) { return spellOp(L, "spell.getPowerCost", "GetPowerCost"); }
int sGetSpellInfo(lua_State* L) { return spellOp(L, "spell.getSpellInfo", "GetSpellInfo"); }
int sGetState(lua_State* L) { return spellOp(L, "spell.getState", "GetState"); }
int sSetState(lua_State* L)
{
    // The core's SpellState: SPELL_STATE_NULL .. SPELL_STATE_IDLE (0..5). The docs' generic uint32 range would cast an
    // out-of-enum value into the state machine, so the core's own bound is checked here.
    int32_t const state = argInt(L, 2, "state");
    if (state < 0 || state > 5)
        return luaL_error(L, "state %I is not a spell state (0..5)", static_cast<lua_Integer>(state));
    return spellOp(L, "spell.setState", "SetState", {EventValue(state)});
}
int sGetTarget(lua_State* L) { return spellOp(L, "spell.getTarget", "GetTarget"); }
int sGetTargetDest(lua_State* L) { return spellOp(L, "spell.getTargetDest", "GetTargetDest"); }
int sIsAutoRepeat(lua_State* L) { return spellOp(L, "spell.isAutoRepeat", "IsAutoRepeat"); }
int sSetAutoRepeat(lua_State* L)
{
    return spellOp(L, "spell.setAutoRepeat", "SetAutoRepeat", {EventValue(optBool(L, 2, false))});
}
int sCast(lua_State* L)
{
    return spellOp(L, "spell.cast", "Cast", {EventValue(optBool(L, 2, false))});
}
int sFinish(lua_State* L) { return spellOp(L, "spell.finish", "Finish"); }
int sIsPositive(lua_State *L) { return spellOp(L, "spell.isPositive", "IsPositive"); }
int sIsTriggered(lua_State* L) { return spellOp(L, "spell.isTriggered", "IsTriggered"); }
int sIsChannelActive(lua_State* L) { return spellOp(L, "spell.isChannelActive", "IsChannelActive"); }
int sIsIgnoringCooldowns(lua_State* L) { return spellOp(L, "spell.isIgnoringCooldowns", "IsIgnoringCooldowns"); }
int sIsInterruptable(lua_State* L) { return spellOp(L, "spell.isInterruptable", "IsInterruptable"); }
int sIsNeedSendToClient(lua_State* L) { return spellOp(L, "spell.isNeedSendToClient", "IsNeedSendToClient"); }
int sIsProcDisabled(lua_State* L) { return spellOp(L, "spell.isProcDisabled", "IsProcDisabled"); }
int sIsDeletable(lua_State* L) { return spellOp(L, "spell.isDeletable", "IsDeletable"); }
int sIsAutoActionResetSpell(lua_State* L)
{
    return spellOp(L, "spell.isAutoActionResetSpell", "IsAutoActionResetSpell");
}
int sIsTriggeredByAura(lua_State* L)
{
    return spellOp(L, "spell.isTriggeredByAura", "IsTriggeredByAura",
                   {EventValue(spellInfoId(L, 2, "spellInfo"))});
}
int sSetExecutedCurrently(lua_State* L)
{
    return spellOp(L, "spell.setExecutedCurrently", "SetExecutedCurrently", {EventValue(optBool(L, 2, false))});
}
int sSetReferencedFromCurrent(lua_State* L)
{
    return spellOp(L, "spell.setReferencedFromCurrent", "SetReferencedFromCurrent",
                   {EventValue(optBool(L, 2, false))});
}
int sGetRuneState(lua_State* L) { return spellOp(L, "spell.getRuneState", "GetRuneState"); }
int sSetRuneState(lua_State* L)
{
    return spellOp(L, "spell.setRuneState", "SetRuneState", {EventValue(static_cast<int64_t>(argU8(L, 2, "value")))});
}
int sSetSpellValue(lua_State* L)
{
    return spellOp(L, "spell.setSpellValue", "SetSpellValue",
                   {EventValue(static_cast<int64_t>(argInt(L, 2, "mod"))), EventValue(argInt(L, 3, "value"))});
}
int sUpdatePointers(lua_State* L) { return spellOp(L, "spell.updatePointers", "UpdatePointers"); }
int sCleanupTargetList(lua_State* L) { return spellOp(L, "spell.cleanupTargetList", "CleanupTargetList"); }
int sCalculateDamage(lua_State* L)
{
    return spellOp(L, "spell.calculateDamage", "CalculateDamage",
                   {EventValue(static_cast<int64_t>(argU8(L, 2, "effIndex")))});
}
int sGetUnitTargetCountForEffect(lua_State* L)
{
    return spellOp(L, "spell.getUnitTargetCountForEffect", "GetUnitTargetCountForEffect",
                   {EventValue(static_cast<int64_t>(argU8(L, 2, "effIndex")))});
}
int sGetGameObjectTargetCountForEffect(lua_State* L)
{
    return spellOp(L, "spell.getGameObjectTargetCountForEffect", "GetGameObjectTargetCountForEffect",
                   {EventValue(static_cast<int64_t>(argU8(L, 2, "effIndex")))});
}
int sGetItemTargetCountForEffect(lua_State* L)
{
    return spellOp(L, "spell.getItemTargetCountForEffect", "GetItemTargetCountForEffect",
                   {EventValue(static_cast<int64_t>(argU8(L, 2, "effIndex")))});
}

// ---- Aura ----
int aGetAuraId(lua_State* L) { return auraOp(L, "aura.getAuraId", "GetAuraId"); }
// The docs list GetEntry on Aura (spec section 4) as well: the aura's entry is the spell that created it.
int aGetEntry(lua_State* L) { return auraOp(L, "aura.getEntry", "GetEntry"); }
int aGetCaster(lua_State* L) { return auraOp(L, "aura.getCaster", "GetCaster"); }
int aGetCasterGUID(lua_State* L) { return auraOp(L, "aura.getCasterGuid", "GetCasterGUID"); }
int aGetCasterLevel(lua_State* L) { return auraOp(L, "aura.getCasterLevel", "GetCasterLevel"); }
int aGetDuration(lua_State* L) { return auraOp(L, "aura.getDuration", "GetDuration"); }
int aSetDuration(lua_State* L)
{
    return auraOp(L, "aura.setDuration", "SetDuration", {EventValue(argInt(L, 2, "duration"))});
}
int aGetMaxDuration(lua_State* L) { return auraOp(L, "aura.getMaxDuration", "GetMaxDuration"); }
int aSetMaxDuration(lua_State* L)
{
    return auraOp(L, "aura.setMaxDuration", "SetMaxDuration", {EventValue(argInt(L, 2, "duration"))});
}
int aGetStackAmount(lua_State* L) { return auraOp(L, "aura.getStackAmount", "GetStackAmount"); }
int aSetStackAmount(lua_State* L)
{
    // The docs' 0..255 range is the core's uint8 stack count.
    return auraOp(L, "aura.setStackAmount", "SetStackAmount",
                  {EventValue(static_cast<int64_t>(argU8(L, 2, "amount")))});
}
int aGetOwner(lua_State* L) { return auraOp(L, "aura.getOwner", "GetOwner"); }
int aGetSpellInfo(lua_State* L) { return auraOp(L, "aura.getSpellInfo", "GetSpellInfo"); }
int aRemove(lua_State* L) { return auraOp(L, "aura.remove", "Remove"); }
}  // namespace

void registerSpellMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetEntry", fl::safe<sGetEntry>},
        {"GetCaster", fl::safe<sGetCaster>},
        {"GetOriginalCaster", fl::safe<sGetOriginalCaster>},
        {"Cancel", fl::safe<sCancel>},
        {"Cast", fl::safe<sCast>},
        {"Finish", fl::safe<sFinish>},
        {"GetDuration", fl::safe<sGetDuration>},
        {"GetCastTime", fl::safe<sGetCastTime>},
        {"GetDelayStart", fl::safe<sGetDelayStart>},
        {"GetDelayMoment", fl::safe<sGetDelayMoment>},
        {"SetDelayStart", fl::safe<sSetDelayStart>},
        {"GetPowerCost", fl::safe<sGetPowerCost>},
        {"GetSpellInfo", fl::safe<sGetSpellInfo>},
        {"GetState", fl::safe<sGetState>},
        {"SetState", fl::safe<sSetState>},
        {"GetTarget", fl::safe<sGetTarget>},
        {"GetTargetDest", fl::safe<sGetTargetDest>},
        {"IsAutoRepeat", fl::safe<sIsAutoRepeat>},
        {"SetAutoRepeat", fl::safe<sSetAutoRepeat>},
        {"IsPositive", fl::safe<sIsPositive>},
        {"IsTriggered", fl::safe<sIsTriggered>},
        {"IsChannelActive", fl::safe<sIsChannelActive>},
        {"IsIgnoringCooldowns", fl::safe<sIsIgnoringCooldowns>},
        {"IsInterruptable", fl::safe<sIsInterruptable>},
        {"IsNeedSendToClient", fl::safe<sIsNeedSendToClient>},
        {"IsProcDisabled", fl::safe<sIsProcDisabled>},
        {"IsDeletable", fl::safe<sIsDeletable>},
        {"IsAutoActionResetSpell", fl::safe<sIsAutoActionResetSpell>},
        {"IsTriggeredByAura", fl::safe<sIsTriggeredByAura>},
        {"SetExecutedCurrently", fl::safe<sSetExecutedCurrently>},
        {"SetReferencedFromCurrent", fl::safe<sSetReferencedFromCurrent>},
        {"GetRuneState", fl::safe<sGetRuneState>},
        {"SetRuneState", fl::safe<sSetRuneState>},
        {"SetSpellValue", fl::safe<sSetSpellValue>},
        {"UpdatePointers", fl::safe<sUpdatePointers>},
        {"CleanupTargetList", fl::safe<sCleanupTargetList>},
        {"CalculateDamage", fl::safe<sCalculateDamage>},
        {"GetUnitTargetCountForEffect", fl::safe<sGetUnitTargetCountForEffect>},
        {"GetGameObjectTargetCountForEffect", fl::safe<sGetGameObjectTargetCountForEffect>},
        {"GetItemTargetCountForEffect", fl::safe<sGetItemTargetCountForEffect>},
        {nullptr, nullptr}};
    addMethods(L, "Spell", regs);
}

void registerAuraMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetAuraId", fl::safe<aGetAuraId>},
        {"GetEntry", fl::safe<aGetEntry>},
        {"GetCaster", fl::safe<aGetCaster>},
        {"GetCasterGUID", fl::safe<aGetCasterGUID>},
        {"GetCasterLevel", fl::safe<aGetCasterLevel>},
        {"GetDuration", fl::safe<aGetDuration>},
        {"SetDuration", fl::safe<aSetDuration>},
        {"GetMaxDuration", fl::safe<aGetMaxDuration>},
        {"SetMaxDuration", fl::safe<aSetMaxDuration>},
        {"GetStackAmount", fl::safe<aGetStackAmount>},
        {"SetStackAmount", fl::safe<aSetStackAmount>},
        {"GetOwner", fl::safe<aGetOwner>},
        {"GetSpellInfo", fl::safe<aGetSpellInfo>},
        {"Remove", fl::safe<aRemove>},
        {nullptr, nullptr}};
    addMethods(L, "Aura", regs);
}
}  // namespace flu
