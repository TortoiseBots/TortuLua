// Eluna SpellInfo of the long tail (docs: SpellInfo/index.html, Global/GetSpellInfo.html). A SpellInfo handle holds the
// spell id in `guid.lo` and nothing else: it names core data that lives for the whole server run, so it never goes
// stale (docs/LONGTAIL-NOTES.md). An id the core does not know has no handle at all (GetSpellInfo answers nil).
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
int siRead(lua_State* L, char const* op, char const* display, std::vector<fl::EventValue> args = {})
{
    fl::HandleData const& h = kindArg(L, 1, kSpellInfo, display);
    std::vector<fl::EventValue> out;
    if (!host(L).gameCall(h, op, args, out))
        // A handle is only made for an id the core answered (GetSpellInfo/Aura/Spell:GetSpellInfo): nothing can vanish.
        return luaL_error(L, "%s: no such spell entry", display);
    return pushResults(L, out);
}
// A SpellInfo argument of a method (IsRankOf and the like): the handle's id, the host looks the entry up again.
int64_t otherSpellId(lua_State* L, int idx, char const* what)
{
    return static_cast<int64_t>(kindArg(L, idx, kSpellInfo, what).guid.lo);
}

// ---- scalar getters ----
int siGetId(lua_State* L) { return siRead(L, "spellinfo.getId", "GetId"); }
int siGetCategory(lua_State* L) { return siRead(L, "spellinfo.getCategory", "GetCategory"); }
int siGetDispel(lua_State* L) { return siRead(L, "spellinfo.getDispel", "GetDispel"); }
int siGetMechanic(lua_State* L) { return siRead(L, "spellinfo.getMechanic", "GetMechanic"); }
int siGetSchoolMask(lua_State* L) { return siRead(L, "spellinfo.getSchoolMask", "GetSchoolMask"); }
int siGetAttributes(lua_State* L) { return siRead(L, "spellinfo.getAttributes", "GetAttributes"); }
int siGetAttributesCu(lua_State* L) { return siRead(L, "spellinfo.getAttributesCu", "GetAttributesCu"); }
int siGetAttributesEx(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx", "GetAttributesEx"); }
int siGetAttributesEx2(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx2", "GetAttributesEx2"); }
int siGetAttributesEx3(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx3", "GetAttributesEx3"); }
int siGetAttributesEx4(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx4", "GetAttributesEx4"); }
int siGetAttributesEx5(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx5", "GetAttributesEx5"); }
int siGetAttributesEx6(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx6", "GetAttributesEx6"); }
int siGetAttributesEx7(lua_State* L) { return siRead(L, "spellinfo.getAttributesEx7", "GetAttributesEx7"); }
int siGetDuration(lua_State* L) { return siRead(L, "spellinfo.getDuration", "GetDuration"); }
int siGetMaxDuration(lua_State* L) { return siRead(L, "spellinfo.getMaxDuration", "GetMaxDuration"); }
int siGetRecoveryTime(lua_State* L) { return siRead(L, "spellinfo.getRecoveryTime", "GetRecoveryTime"); }
int siGetCategoryRecoveryTime(lua_State* L)
{
    return siRead(L, "spellinfo.getCategoryRecoveryTime", "GetCategoryRecoveryTime");
}
int siGetMaxRange(lua_State* L) { return siRead(L, "spellinfo.getMaxRange", "GetMaxRange"); }
int siGetMinRange(lua_State* L) { return siRead(L, "spellinfo.getMinRange", "GetMinRange"); }
int siGetSpeed(lua_State* L) { return siRead(L, "spellinfo.getSpeed", "GetSpeed"); }
int siGetStackAmount(lua_State* L) { return siRead(L, "spellinfo.getStackAmount", "GetStackAmount"); }
int siGetMaxAffectedTargets(lua_State* L)
{
    return siRead(L, "spellinfo.getMaxAffectedTargets", "GetMaxAffectedTargets");
}
int siGetSpellFamilyName(lua_State* L) { return siRead(L, "spellinfo.getSpellFamilyName", "GetSpellFamilyName"); }
int siGetExplicitTargetMask(lua_State* L)
{
    return siRead(L, "spellinfo.getExplicitTargetMask", "GetExplicitTargetMask");
}
int siGetAuraState(lua_State* L) { return siRead(L, "spellinfo.getAuraState", "GetAuraState"); }
int siGetSpellSpecific(lua_State* L) { return siRead(L, "spellinfo.getSpellSpecific", "GetSpellSpecific"); }
int siGetBaseLevel(lua_State* L) { return siRead(L, "spellinfo.getBaseLevel", "GetBaseLevel"); }
int siGetSpellLevel(lua_State* L) { return siRead(L, "spellinfo.getSpellLevel", "GetSpellLevel"); }
int siGetMaxLevel(lua_State* L) { return siRead(L, "spellinfo.getMaxLevel", "GetMaxLevel"); }
int siGetMaxTargetLevel(lua_State* L) { return siRead(L, "spellinfo.getMaxTargetLevel", "GetMaxTargetLevel"); }
int siGetDmgClass(lua_State* L) { return siRead(L, "spellinfo.getDmgClass", "GetDmgClass"); }
int siGetPowerType(lua_State* L) { return siRead(L, "spellinfo.getPowerType", "GetPowerType"); }
int siGetProcChance(lua_State* L) { return siRead(L, "spellinfo.getProcChance", "GetProcChance"); }
int siGetProcCharges(lua_State* L) { return siRead(L, "spellinfo.getProcCharges", "GetProcCharges"); }
int siGetProcFlags(lua_State* L) { return siRead(L, "spellinfo.getProcFlags", "GetProcFlags"); }
int siGetRank(lua_State* L) { return siRead(L, "spellinfo.getRank", "GetRank"); }
int siGetTargets(lua_State* L) { return siRead(L, "spellinfo.getTargets", "GetTargets"); }
int siGetTargetCreatureType(lua_State* L)
{
    return siRead(L, "spellinfo.getTargetCreatureType", "GetTargetCreatureType");
}
int siGetManaCost(lua_State* L) { return siRead(L, "spellinfo.getManaCost", "GetManaCost"); }
int siGetManaCostPerlevel(lua_State* L)
{
    return siRead(L, "spellinfo.getManaCostPerlevel", "GetManaCostPerlevel");
}
int siGetManaPerSecond(lua_State* L) { return siRead(L, "spellinfo.getManaPerSecond", "GetManaPerSecond"); }
int siGetInterruptFlags(lua_State* L) { return siRead(L, "spellinfo.getInterruptFlags", "GetInterruptFlags"); }
int siGetAuraInterruptFlags(lua_State* L)
{
    return siRead(L, "spellinfo.getAuraInterruptFlags", "GetAuraInterruptFlags");
}
int siGetChannelInterruptFlags(lua_State* L)
{
    return siRead(L, "spellinfo.getChannelInterruptFlags", "GetChannelInterruptFlags");
}
int siGetStartRecoveryCategory(lua_State* L)
{
    return siRead(L, "spellinfo.getStartRecoveryCategory", "GetStartRecoveryCategory");
}
int siGetStartRecoveryTime(lua_State* L)
{
    return siRead(L, "spellinfo.getStartRecoveryTime", "GetStartRecoveryTime");
}
int siGetPreventionType(lua_State* L) { return siRead(L, "spellinfo.getPreventionType", "GetPreventionType"); }
int siGetAttackType(lua_State* L) { return siRead(L, "spellinfo.getAttackType", "GetAttackType"); }
int siGetActiveIconID(lua_State* L) { return siRead(L, "spellinfo.getActiveIconID", "GetActiveIconID"); }
int siGetSpellIconID(lua_State* L) { return siRead(L, "spellinfo.getSpellIconID", "GetSpellIconID"); }
int siGetRequiresSpellFocus(lua_State* L)
{
    return siRead(L, "spellinfo.getRequiresSpellFocus", "GetRequiresSpellFocus");
}
int siGetAreaGroupId(lua_State* L) { return siRead(L, "spellinfo.getAreaGroupId", "GetAreaGroupId"); }
int siGetCasterAuraSpell(lua_State* L) { return siRead(L, "spellinfo.getCasterAuraSpell", "GetCasterAuraSpell"); }
int siGetCasterAuraState(lua_State* L) { return siRead(L, "spellinfo.getCasterAuraState", "GetCasterAuraState"); }
int siGetCasterAuraStateNot(lua_State* L)
{
    return siRead(L, "spellinfo.getCasterAuraStateNot", "GetCasterAuraStateNot");
}
int siGetTargetAuraSpell(lua_State* L) { return siRead(L, "spellinfo.getTargetAuraSpell", "GetTargetAuraSpell"); }
int siGetTargetAuraState(lua_State* L) { return siRead(L, "spellinfo.getTargetAuraState", "GetTargetAuraState"); }
int siGetTargetAuraStateNot(lua_State* L)
{
    return siRead(L, "spellinfo.getTargetAuraStateNot", "GetTargetAuraStateNot");
}
int siGetFacingCasterFlags(lua_State* L)
{
    return siRead(L, "spellinfo.getFacingCasterFlags", "GetFacingCasterFlags");
}
int siGetEquippedItemClass(lua_State* L)
{
    return siRead(L, "spellinfo.getEquippedItemClass", "GetEquippedItemClass");
}
int siGetEquippedItemSubClassMask(lua_State* L)
{
    return siRead(L, "spellinfo.getEquippedItemSubClassMask", "GetEquippedItemSubClassMask");
}
int siGetEquippedItemInventoryTypeMask(lua_State* L)
{
    return siRead(L, "spellinfo.getEquippedItemInventoryTypeMask", "GetEquippedItemInventoryTypeMask");
}
int siGetStances(lua_State* L) { return siRead(L, "spellinfo.getStances", "GetStances"); }
int siGetStancesNot(lua_State* L) { return siRead(L, "spellinfo.getStancesNot", "GetStancesNot"); }
int siCalcCastTime(lua_State* L) { return siRead(L, "spellinfo.calcCastTime", "CalcCastTime"); }

// ---- indexed getters (the docs' own bounds are checked here; the host checks them again) ----
int siGetSpellFamilyFlags(lua_State* L)
{
    // The 96-bit family flags: index 0..2 (docs).
    return siRead(L, "spellinfo.getSpellFamilyFlags", "GetSpellFamilyFlags",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}
int siGetTotem(lua_State* L)
{
    // Two totem slots at this core (docs: 0-1).
    return siRead(L, "spellinfo.getTotem", "GetTotem", {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}
int siGetTotemCategory(lua_State* L)
{
    return siRead(L, "spellinfo.getTotemCategory", "GetTotemCategory",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}
int siGetReagent(lua_State* L)
{
    // MAX_SPELL_REAGENTS is 8 at this core (docs: 0-MAX_SPELL_REAGENTS-1).
    return siRead(L, "spellinfo.getReagent", "GetReagent", {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}
int siGetReagentCount(lua_State* L)
{
    return siRead(L, "spellinfo.getReagentCount", "GetReagentCount",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}

// ---- predicates ----
int siHasEffect(lua_State* L)
{
    return siRead(L, "spellinfo.hasEffect", "HasEffect", {EventValue(static_cast<int64_t>(argUInt(L, 2, "effect")))});
}
int siHasAura(lua_State* L)
{
    return siRead(L, "spellinfo.hasAura", "HasAura", {EventValue(static_cast<int64_t>(argUInt(L, 2, "aura")))});
}
int siHasAreaAuraEffect(lua_State* L) { return siRead(L, "spellinfo.hasAreaAuraEffect", "HasAreaAuraEffect"); }
int siHasOnlyDamageEffects(lua_State* L)
{
    return siRead(L, "spellinfo.hasOnlyDamageEffects", "HasOnlyDamageEffects");
}
int siHasInitialAggro(lua_State* L) { return siRead(L, "spellinfo.hasInitialAggro", "HasInitialAggro"); }
int siIsPassive(lua_State* L) { return siRead(L, "spellinfo.isPassive", "IsPassive"); }
int siIsAutocastable(lua_State* L) { return siRead(L, "spellinfo.isAutocastable", "IsAutocastable"); }
int siIsStackableWithRanks(lua_State* L) { return siRead(L, "spellinfo.isStackableWithRanks", "IsStackableWithRanks"); }
int siIsChanneled(lua_State* L) { return siRead(L, "spellinfo.isChanneled", "IsChanneled"); }
int siIsPositive(lua_State* L) { return siRead(L, "spellinfo.isPositive", "IsPositive"); }
int siIsPositiveEffect(lua_State* L)
{
    return siRead(L, "spellinfo.isPositiveEffect", "IsPositiveEffect",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siIsAffectingArea(lua_State* L) { return siRead(L, "spellinfo.isAffectingArea", "IsAffectingArea"); }
int siIsTargetingArea(lua_State* L) { return siRead(L, "spellinfo.isTargetingArea", "IsTargetingArea"); }
int siNeedsExplicitUnitTarget(lua_State* L)
{
    return siRead(L, "spellinfo.needsExplicitUnitTarget", "NeedsExplicitUnitTarget");
}
int siIsDeathPersistent(lua_State* L) { return siRead(L, "spellinfo.isDeathPersistent", "IsDeathPersistent"); }
int siIsRequiringDeadTarget(lua_State* L)
{
    return siRead(L, "spellinfo.isRequiringDeadTarget", "IsRequiringDeadTarget");
}
int siIsAllowingDeadTarget(lua_State* L)
{
    return siRead(L, "spellinfo.isAllowingDeadTarget", "IsAllowingDeadTarget");
}
int siCanBeUsedInCombat(lua_State* L) { return siRead(L, "spellinfo.canBeUsedInCombat", "CanBeUsedInCombat"); }
int siIsCooldownStartedOnEvent(lua_State* L)
{
    return siRead(L, "spellinfo.isCooldownStartedOnEvent", "IsCooldownStartedOnEvent");
}
int siIsRanked(lua_State* L) { return siRead(L, "spellinfo.isRanked", "IsRanked"); }
int siIsSingleTarget(lua_State* L) { return siRead(L, "spellinfo.isSingleTarget", "IsSingleTarget"); }
int siIsMultiSlotAura(lua_State* L) { return siRead(L, "spellinfo.isMultiSlotAura", "IsMultiSlotAura"); }
int siIsGroupBuff(lua_State* L) { return siRead(L, "spellinfo.isGroupBuff", "IsGroupBuff"); }
int siIsMoveAllowedChannel(lua_State* L) { return siRead(L, "spellinfo.isMoveAllowedChannel", "IsMoveAllowedChannel"); }
int siIsNextMeleeSwingSpell(lua_State* L)
{
    return siRead(L, "spellinfo.isNextMeleeSwingSpell", "IsNextMeleeSwingSpell");
}
int siIsRangedWeaponSpell(lua_State* L) { return siRead(L, "spellinfo.isRangedWeaponSpell", "IsRangedWeaponSpell"); }
int siIsAutoRepeatRangedSpell(lua_State* L)
{
    return siRead(L, "spellinfo.isAutoRepeatRangedSpell", "IsAutoRepeatRangedSpell");
}
int siIsAffectedBySpellMods(lua_State* L)
{
    return siRead(L, "spellinfo.isAffectedBySpellMods", "IsAffectedBySpellMods");
}

// ---- methods that take another SpellInfo ----
int siNeedsToBeTriggeredByCaster(lua_State* L)
{
    return siRead(L, "spellinfo.needsToBeTriggeredByCaster", "NeedsToBeTriggeredByCaster",
                  {EventValue(otherSpellId(L, 2, "triggeringSpell"))});
}
int siIsRankOf(lua_State* L)
{
    return siRead(L, "spellinfo.isRankOf", "IsRankOf", {EventValue(otherSpellId(L, 2, "spellInfo"))});
}
int siIsDifferentRankOf(lua_State* L)
{
    return siRead(L, "spellinfo.isDifferentRankOf", "IsDifferentRankOf", {EventValue(otherSpellId(L, 2, "spellInfo"))});
}
int siIsHighRankOf(lua_State* L)
{
    return siRead(L, "spellinfo.isHighRankOf", "IsHighRankOf", {EventValue(otherSpellId(L, 2, "spellInfo"))});
}
int siCanDispelAura(lua_State* L)
{
    return siRead(L, "spellinfo.canDispelAura", "CanDispelAura", {EventValue(otherSpellId(L, 2, "auraSpellInfo"))});
}
int siCanPierceImmuneAura(lua_State* L)
{
    return siRead(L, "spellinfo.canPierceImmuneAura", "CanPierceImmuneAura",
                  {EventValue(otherSpellId(L, 2, "auraSpellInfo"))});
}
int siCanSpellProvideImmunityAgainstAura(lua_State* L)
{
    return siRead(L, "spellinfo.canSpellProvideImmunityAgainstAura", "CanSpellProvideImmunityAgainstAura",
                  {EventValue(otherSpellId(L, 2, "auraSpellInfo"))});
}
}  // namespace

void registerSpellInfoMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetId", fl::safe<siGetId>},
        {"GetCategory", fl::safe<siGetCategory>},
        {"GetDispel", fl::safe<siGetDispel>},
        {"GetMechanic", fl::safe<siGetMechanic>},
        {"GetSchoolMask", fl::safe<siGetSchoolMask>},
        {"GetAttributes", fl::safe<siGetAttributes>},
        {"GetAttributesCu", fl::safe<siGetAttributesCu>},
        {"GetAttributesEx", fl::safe<siGetAttributesEx>},
        {"GetAttributesEx2", fl::safe<siGetAttributesEx2>},
        {"GetAttributesEx3", fl::safe<siGetAttributesEx3>},
        {"GetAttributesEx4", fl::safe<siGetAttributesEx4>},
        {"GetAttributesEx5", fl::safe<siGetAttributesEx5>},
        {"GetAttributesEx6", fl::safe<siGetAttributesEx6>},
        {"GetAttributesEx7", fl::safe<siGetAttributesEx7>},
        {"GetDuration", fl::safe<siGetDuration>},
        {"GetMaxDuration", fl::safe<siGetMaxDuration>},
        {"GetRecoveryTime", fl::safe<siGetRecoveryTime>},
        {"GetCategoryRecoveryTime", fl::safe<siGetCategoryRecoveryTime>},
        {"GetMaxRange", fl::safe<siGetMaxRange>},
        {"GetMinRange", fl::safe<siGetMinRange>},
        {"GetSpeed", fl::safe<siGetSpeed>},
        {"GetStackAmount", fl::safe<siGetStackAmount>},
        {"GetMaxAffectedTargets", fl::safe<siGetMaxAffectedTargets>},
        {"GetSpellFamilyName", fl::safe<siGetSpellFamilyName>},
        {"GetExplicitTargetMask", fl::safe<siGetExplicitTargetMask>},
        {"GetAuraState", fl::safe<siGetAuraState>},
        {"GetSpellSpecific", fl::safe<siGetSpellSpecific>},
        {"GetBaseLevel", fl::safe<siGetBaseLevel>},
        {"GetSpellLevel", fl::safe<siGetSpellLevel>},
        {"GetMaxLevel", fl::safe<siGetMaxLevel>},
        {"GetMaxTargetLevel", fl::safe<siGetMaxTargetLevel>},
        {"GetDmgClass", fl::safe<siGetDmgClass>},
        {"GetPowerType", fl::safe<siGetPowerType>},
        {"GetProcChance", fl::safe<siGetProcChance>},
        {"GetProcCharges", fl::safe<siGetProcCharges>},
        {"GetProcFlags", fl::safe<siGetProcFlags>},
        {"GetRank", fl::safe<siGetRank>},
        {"GetTargets", fl::safe<siGetTargets>},
        {"GetTargetCreatureType", fl::safe<siGetTargetCreatureType>},
        {"GetManaCost", fl::safe<siGetManaCost>},
        {"GetManaCostPerlevel", fl::safe<siGetManaCostPerlevel>},
        {"GetManaPerSecond", fl::safe<siGetManaPerSecond>},
        {"GetInterruptFlags", fl::safe<siGetInterruptFlags>},
        {"GetAuraInterruptFlags", fl::safe<siGetAuraInterruptFlags>},
        {"GetChannelInterruptFlags", fl::safe<siGetChannelInterruptFlags>},
        {"GetStartRecoveryCategory", fl::safe<siGetStartRecoveryCategory>},
        {"GetStartRecoveryTime", fl::safe<siGetStartRecoveryTime>},
        {"GetPreventionType", fl::safe<siGetPreventionType>},
        {"GetAttackType", fl::safe<siGetAttackType>},
        {"GetActiveIconID", fl::safe<siGetActiveIconID>},
        {"GetSpellIconID", fl::safe<siGetSpellIconID>},
        {"GetRequiresSpellFocus", fl::safe<siGetRequiresSpellFocus>},
        {"GetAreaGroupId", fl::safe<siGetAreaGroupId>},
        {"GetCasterAuraSpell", fl::safe<siGetCasterAuraSpell>},
        {"GetCasterAuraState", fl::safe<siGetCasterAuraState>},
        {"GetCasterAuraStateNot", fl::safe<siGetCasterAuraStateNot>},
        {"GetTargetAuraSpell", fl::safe<siGetTargetAuraSpell>},
        {"GetTargetAuraState", fl::safe<siGetTargetAuraState>},
        {"GetTargetAuraStateNot", fl::safe<siGetTargetAuraStateNot>},
        {"GetFacingCasterFlags", fl::safe<siGetFacingCasterFlags>},
        {"GetEquippedItemClass", fl::safe<siGetEquippedItemClass>},
        {"GetEquippedItemSubClassMask", fl::safe<siGetEquippedItemSubClassMask>},
        {"GetEquippedItemInventoryTypeMask", fl::safe<siGetEquippedItemInventoryTypeMask>},
        {"GetStances", fl::safe<siGetStances>},
        {"GetStancesNot", fl::safe<siGetStancesNot>},
        {"CalcCastTime", fl::safe<siCalcCastTime>},
        {"GetSpellFamilyFlags", fl::safe<siGetSpellFamilyFlags>},
        {"GetTotem", fl::safe<siGetTotem>},
        {"GetTotemCategory", fl::safe<siGetTotemCategory>},
        {"GetReagent", fl::safe<siGetReagent>},
        {"GetReagentCount", fl::safe<siGetReagentCount>},
        {"HasEffect", fl::safe<siHasEffect>},
        {"HasAura", fl::safe<siHasAura>},
        {"HasAreaAuraEffect", fl::safe<siHasAreaAuraEffect>},
        {"HasOnlyDamageEffects", fl::safe<siHasOnlyDamageEffects>},
        {"HasInitialAggro", fl::safe<siHasInitialAggro>},
        {"IsPassive", fl::safe<siIsPassive>},
        {"IsAutocastable", fl::safe<siIsAutocastable>},
        {"IsStackableWithRanks", fl::safe<siIsStackableWithRanks>},
        {"IsChanneled", fl::safe<siIsChanneled>},
        {"IsPositive", fl::safe<siIsPositive>},
        {"IsPositiveEffect", fl::safe<siIsPositiveEffect>},
        {"IsAffectingArea", fl::safe<siIsAffectingArea>},
        {"IsTargetingArea", fl::safe<siIsTargetingArea>},
        {"NeedsExplicitUnitTarget", fl::safe<siNeedsExplicitUnitTarget>},
        {"IsDeathPersistent", fl::safe<siIsDeathPersistent>},
        {"IsRequiringDeadTarget", fl::safe<siIsRequiringDeadTarget>},
        {"IsAllowingDeadTarget", fl::safe<siIsAllowingDeadTarget>},
        {"CanBeUsedInCombat", fl::safe<siCanBeUsedInCombat>},
        {"IsCooldownStartedOnEvent", fl::safe<siIsCooldownStartedOnEvent>},
        {"IsRanked", fl::safe<siIsRanked>},
        {"IsSingleTarget", fl::safe<siIsSingleTarget>},
        {"IsMultiSlotAura", fl::safe<siIsMultiSlotAura>},
        {"IsGroupBuff", fl::safe<siIsGroupBuff>},
        {"IsMoveAllowedChannel", fl::safe<siIsMoveAllowedChannel>},
        {"IsNextMeleeSwingSpell", fl::safe<siIsNextMeleeSwingSpell>},
        {"IsRangedWeaponSpell", fl::safe<siIsRangedWeaponSpell>},
        {"IsAutoRepeatRangedSpell", fl::safe<siIsAutoRepeatRangedSpell>},
        {"IsAffectedBySpellMods", fl::safe<siIsAffectedBySpellMods>},
        {"NeedsToBeTriggeredByCaster", fl::safe<siNeedsToBeTriggeredByCaster>},
        {"IsRankOf", fl::safe<siIsRankOf>},
        {"IsDifferentRankOf", fl::safe<siIsDifferentRankOf>},
        {"IsHighRankOf", fl::safe<siIsHighRankOf>},
        {"CanDispelAura", fl::safe<siCanDispelAura>},
        {"CanPierceImmuneAura", fl::safe<siCanPierceImmuneAura>},
        {"CanSpellProvideImmunityAgainstAura", fl::safe<siCanSpellProvideImmunityAgainstAura>},
        {nullptr, nullptr}};
    addMethods(L, "SpellInfo", regs);
}
}  // namespace flu
