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
int siGetAllEffectsMechanicMask(lua_State* L)
{
    return siRead(L, "spellinfo.getAllEffectsMechanicMask", "GetAllEffectsMechanicMask");
}
int siGetAllowedMechanicMask(lua_State* L)
{
    return siRead(L, "spellinfo.getAllowedMechanicMask", "GetAllowedMechanicMask");
}
int siGetExcludeCasterAuraSpell(lua_State* L)
{
    return siRead(L, "spellinfo.getExcludeCasterAuraSpell", "GetExcludeCasterAuraSpell");
}
int siGetExcludeTargetAuraSpell(lua_State* L)
{
    return siRead(L, "spellinfo.getExcludeTargetAuraSpell", "GetExcludeTargetAuraSpell");
}
int siGetManaCostPercentage(lua_State* L)
{
    return siRead(L, "spellinfo.getManaCostPercentage", "GetManaCostPercentage");
}
int siGetMaxTicks(lua_State* L) { return siRead(L, "spellinfo.getMaxTicks", "GetMaxTicks"); }
int siGetDiminishingReturnsGroupForSpell(lua_State* L)
{
    return siRead(L, "spellinfo.getDiminishingReturnsGroupForSpell", "GetDiminishingReturnsGroupForSpell",
                  {EventValue(argBool(L, 2, "triggered"))});
}
int siGetDiminishingReturnsGroupType(lua_State* L)
{
    return siRead(L, "spellinfo.getDiminishingReturnsGroupType", "GetDiminishingReturnsGroupType",
                  {EventValue(argBool(L, 2, "triggered"))});
}
int siGetDiminishingReturnsLimitDuration(lua_State* L)
{
    return siRead(L, "spellinfo.getDiminishingReturnsLimitDuration", "GetDiminishingReturnsLimitDuration",
                  {EventValue(argBool(L, 2, "triggered"))});
}
int siGetDiminishingReturnsMaxLevel(lua_State* L)
{
    return siRead(L, "spellinfo.getDiminishingReturnsMaxLevel", "GetDiminishingReturnsMaxLevel",
                  {EventValue(argBool(L, 2, "triggered"))});
}
int siGetSpellMechanicMaskByEffectMask(lua_State* L)
{
    return siRead(L, "spellinfo.getSpellMechanicMaskByEffectMask", "GetSpellMechanicMaskByEffectMask",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effectMask")))});
}
int siGetSpellVisual(lua_State* L)
{
    return siRead(L, "spellinfo.getSpellVisual", "GetSpellVisual",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "index")))});
}

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
int siIsAuraExclusiveBySpecificWith(lua_State* L)
{
    return siRead(L, "spellinfo.isAuraExclusiveBySpecificWith", "IsAuraExclusiveBySpecificWith",
                  {EventValue(otherSpellId(L, 2, "spellInfo"))});
}
int siIsAuraExclusiveBySpecificPerCasterWith(lua_State* L)
{
    return siRead(L, "spellinfo.isAuraExclusiveBySpecificPerCasterWith", "IsAuraExclusiveBySpecificPerCasterWith",
                  {EventValue(otherSpellId(L, 2, "spellInfo"))});
}

// ---- target / item / skill / profession / aura effect checks ----
int siCheckTarget(lua_State* L)
{
    return siRead(L, "spellinfo.checkTarget", "CheckTarget",
                  {EventValue(objArg(L, 2, kWorldObject, "caster")),
                   EventValue(objArg(L, 3, kWorldObject, "target"))});
}
int siCheckTargetCreatureType(lua_State* L)
{
    return siRead(L, "spellinfo.checkTargetCreatureType", "CheckTargetCreatureType",
                  {EventValue(objArg(L, 2, kUnit, "target"))});
}
int siIsAbilityLearnedWithProfession(lua_State* L)
{
    return siRead(L, "spellinfo.isAbilityLearnedWithProfession", "IsAbilityLearnedWithProfession");
}
int siIsAbilityOfSkillType(lua_State* L)
{
    return siRead(L, "spellinfo.isAbilityOfSkillType", "IsAbilityOfSkillType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skillType")))});
}
int siIsAffected(lua_State* L)
{
    return siRead(L, "spellinfo.isAffected", "IsAffected",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "familyName"))),
                   EventValue(static_cast<int64_t>(argUInt(L, 3, "flag0"))),
                   EventValue(static_cast<int64_t>(argUInt(L, 4, "flag1"))),
                   EventValue(static_cast<int64_t>(argUInt(L, 5, "flag2")))});
}
int siIsBreakingStealth(lua_State* L) { return siRead(L, "spellinfo.isBreakingStealth", "IsBreakingStealth"); }
int siIsExplicitDiscovery(lua_State* L) { return siRead(L, "spellinfo.isExplicitDiscovery", "IsExplicitDiscovery"); }
int siIsItemFitToSpellRequirements(lua_State* L)
{
    return siRead(L, "spellinfo.isItemFitToSpellRequirements", "IsItemFitToSpellRequirements",
                  {EventValue(objArg(L, 2, kItem, "item"))});
}
int siIsLootCrafting(lua_State* L) { return siRead(L, "spellinfo.isLootCrafting", "IsLootCrafting"); }
int siIsPassiveStackableWithRanks(lua_State* L)
{
    return siRead(L, "spellinfo.isPassiveStackableWithRanks", "IsPassiveStackableWithRanks");
}
int siIsPrimaryProfession(lua_State* L) { return siRead(L, "spellinfo.isPrimaryProfession", "IsPrimaryProfession"); }
int siIsPrimaryProfessionFirstRank(lua_State* L)
{
    return siRead(L, "spellinfo.isPrimaryProfessionFirstRank", "IsPrimaryProfessionFirstRank");
}
int siIsProfession(lua_State* L) { return siRead(L, "spellinfo.isProfession", "IsProfession"); }
int siIsProfessionOrRiding(lua_State* L)
{
    return siRead(L, "spellinfo.isProfessionOrRiding", "IsProfessionOrRiding");
}
int siIsStackableOnOneSlotWithDifferentCasters(lua_State* L)
{
    return siRead(L, "spellinfo.isStackableOnOneSlotWithDifferentCasters",
                  "IsStackableOnOneSlotWithDifferentCasters");
}
int siNeedsComboPoints(lua_State* L) { return siRead(L, "spellinfo.needsComboPoints", "NeedsComboPoints"); }
int siSpellCancelsAuraEffect(lua_State* L)
{
    return siRead(L, "spellinfo.spellCancelsAuraEffect", "SpellCancelsAuraEffect",
                  {EventValue(static_cast<int64_t>(kindArg(L, 2, kAuraEffect, "aurEff").instanceId))});
}

// ---- per-effect readers and predicates ----
int siEffectIsAreaAuraEffect(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsAreaAuraEffect", "EffectIsAreaAuraEffect",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siEffectIsAura(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsAura", "EffectIsAura",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siEffectIsAuraType(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsAuraType", "EffectIsAuraType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex"))),
                   EventValue(static_cast<int64_t>(argUInt(L, 3, "aura")))});
}
int siEffectIsEffect(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsEffect", "EffectIsEffect",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siEffectIsEffectType(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsEffectType", "EffectIsEffectType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex"))),
                   EventValue(static_cast<int64_t>(argUInt(L, 3, "effect")))});
}
int siEffectIsTargetingArea(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsTargetingArea", "EffectIsTargetingArea",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siEffectIsUnitOwnedAuraEffect(lua_State* L)
{
    return siRead(L, "spellinfo.effectIsUnitOwnedAuraEffect", "EffectIsUnitOwnedAuraEffect",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectAmplitude(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectAmplitude", "GetEffectAmplitude",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectApplyAuraName(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectApplyAuraName", "GetEffectApplyAuraName",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectBasePoints(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectBasePoints", "GetEffectBasePoints",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectBonusMultiplier(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectBonusMultiplier", "GetEffectBonusMultiplier",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectCalcRadius(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectCalcRadius", "GetEffectCalcRadius",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectCalcValue(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectCalcValue", "GetEffectCalcValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectChainTarget(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectChainTarget", "GetEffectChainTarget",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectDamageMultiplier(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectDamageMultiplier", "GetEffectDamageMultiplier",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectHasRadius(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectHasRadius", "GetEffectHasRadius",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectImplicitTargetType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectImplicitTargetType", "GetEffectImplicitTargetType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectIndex(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectIndex", "GetEffectIndex",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectItemType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectItemType", "GetEffectItemType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectMechanic(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectMechanic", "GetEffectMechanic",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectMechanicMask(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectMechanicMask", "GetEffectMechanicMask",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectMiscValue(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectMiscValue", "GetEffectMiscValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectMiscValueB(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectMiscValueB", "GetEffectMiscValueB",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectPointsPerComboPoint(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectPointsPerComboPoint", "GetEffectPointsPerComboPoint",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectProvidedTargetMask(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectProvidedTargetMask", "GetEffectProvidedTargetMask",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectRealPointsPerLevel(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectRealPointsPerLevel", "GetEffectRealPointsPerLevel",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectSpellClassMask(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectSpellClassMask", "GetEffectSpellClassMask",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex"))),
                   EventValue(static_cast<int64_t>(argUInt(L, 3, "maskIndex")))});
}
int siGetEffectTargetACheckType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetACheckType", "GetEffectTargetACheckType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetADirectionAngle(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetADirectionAngle", "GetEffectTargetADirectionAngle",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetADirectionType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetADirectionType", "GetEffectTargetADirectionType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetAIsArea(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetAIsArea", "GetEffectTargetAIsArea",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetAObjectType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetAObjectType", "GetEffectTargetAObjectType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetAReferenceType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetAReferenceType", "GetEffectTargetAReferenceType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetASelectionCategory(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetASelectionCategory", "GetEffectTargetASelectionCategory",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetATarget(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetATarget", "GetEffectTargetATarget",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBCheckType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBCheckType", "GetEffectTargetBCheckType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBDirectionAngle(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBDirectionAngle", "GetEffectTargetBDirectionAngle",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBDirectionType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBDirectionType", "GetEffectTargetBDirectionType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBIsArea(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBIsArea", "GetEffectTargetBIsArea",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBObjectType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBObjectType", "GetEffectTargetBObjectType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBReferenceType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBReferenceType", "GetEffectTargetBReferenceType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBSelectionCategory(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBSelectionCategory", "GetEffectTargetBSelectionCategory",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTargetBTarget(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTargetBTarget", "GetEffectTargetBTarget",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectTriggerSpell(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectTriggerSpell", "GetEffectTriggerSpell",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectType", "GetEffectType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectUsedTargetObjectType(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectUsedTargetObjectType", "GetEffectUsedTargetObjectType",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
}
int siGetEffectValueMultiplier(lua_State* L)
{
    return siRead(L, "spellinfo.getEffectValueMultiplier", "GetEffectValueMultiplier",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "effIndex")))});
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
        {"CheckTarget", fl::safe<siCheckTarget>},
        {"CheckTargetCreatureType", fl::safe<siCheckTargetCreatureType>},
        {"EffectIsAreaAuraEffect", fl::safe<siEffectIsAreaAuraEffect>},
        {"EffectIsAura", fl::safe<siEffectIsAura>},
        {"EffectIsAuraType", fl::safe<siEffectIsAuraType>},
        {"EffectIsEffect", fl::safe<siEffectIsEffect>},
        {"EffectIsEffectType", fl::safe<siEffectIsEffectType>},
        {"EffectIsTargetingArea", fl::safe<siEffectIsTargetingArea>},
        {"EffectIsUnitOwnedAuraEffect", fl::safe<siEffectIsUnitOwnedAuraEffect>},
        {"GetAllEffectsMechanicMask", fl::safe<siGetAllEffectsMechanicMask>},
        {"GetAllowedMechanicMask", fl::safe<siGetAllowedMechanicMask>},
        {"GetDiminishingReturnsGroupForSpell", fl::safe<siGetDiminishingReturnsGroupForSpell>},
        {"GetDiminishingReturnsGroupType", fl::safe<siGetDiminishingReturnsGroupType>},
        {"GetDiminishingReturnsLimitDuration", fl::safe<siGetDiminishingReturnsLimitDuration>},
        {"GetDiminishingReturnsMaxLevel", fl::safe<siGetDiminishingReturnsMaxLevel>},
        {"GetEffectAmplitude", fl::safe<siGetEffectAmplitude>},
        {"GetEffectApplyAuraName", fl::safe<siGetEffectApplyAuraName>},
        {"GetEffectBasePoints", fl::safe<siGetEffectBasePoints>},
        {"GetEffectBonusMultiplier", fl::safe<siGetEffectBonusMultiplier>},
        {"GetEffectCalcRadius", fl::safe<siGetEffectCalcRadius>},
        {"GetEffectCalcValue", fl::safe<siGetEffectCalcValue>},
        {"GetEffectChainTarget", fl::safe<siGetEffectChainTarget>},
        {"GetEffectDamageMultiplier", fl::safe<siGetEffectDamageMultiplier>},
        {"GetEffectHasRadius", fl::safe<siGetEffectHasRadius>},
        {"GetEffectImplicitTargetType", fl::safe<siGetEffectImplicitTargetType>},
        {"GetEffectIndex", fl::safe<siGetEffectIndex>},
        {"GetEffectItemType", fl::safe<siGetEffectItemType>},
        {"GetEffectMechanic", fl::safe<siGetEffectMechanic>},
        {"GetEffectMechanicMask", fl::safe<siGetEffectMechanicMask>},
        {"GetEffectMiscValue", fl::safe<siGetEffectMiscValue>},
        {"GetEffectMiscValueB", fl::safe<siGetEffectMiscValueB>},
        {"GetEffectPointsPerComboPoint", fl::safe<siGetEffectPointsPerComboPoint>},
        {"GetEffectProvidedTargetMask", fl::safe<siGetEffectProvidedTargetMask>},
        {"GetEffectRealPointsPerLevel", fl::safe<siGetEffectRealPointsPerLevel>},
        {"GetEffectSpellClassMask", fl::safe<siGetEffectSpellClassMask>},
        {"GetEffectTargetACheckType", fl::safe<siGetEffectTargetACheckType>},
        {"GetEffectTargetADirectionAngle", fl::safe<siGetEffectTargetADirectionAngle>},
        {"GetEffectTargetADirectionType", fl::safe<siGetEffectTargetADirectionType>},
        {"GetEffectTargetAIsArea", fl::safe<siGetEffectTargetAIsArea>},
        {"GetEffectTargetAObjectType", fl::safe<siGetEffectTargetAObjectType>},
        {"GetEffectTargetAReferenceType", fl::safe<siGetEffectTargetAReferenceType>},
        {"GetEffectTargetASelectionCategory", fl::safe<siGetEffectTargetASelectionCategory>},
        {"GetEffectTargetATarget", fl::safe<siGetEffectTargetATarget>},
        {"GetEffectTargetBCheckType", fl::safe<siGetEffectTargetBCheckType>},
        {"GetEffectTargetBDirectionAngle", fl::safe<siGetEffectTargetBDirectionAngle>},
        {"GetEffectTargetBDirectionType", fl::safe<siGetEffectTargetBDirectionType>},
        {"GetEffectTargetBIsArea", fl::safe<siGetEffectTargetBIsArea>},
        {"GetEffectTargetBObjectType", fl::safe<siGetEffectTargetBObjectType>},
        {"GetEffectTargetBReferenceType", fl::safe<siGetEffectTargetBReferenceType>},
        {"GetEffectTargetBSelectionCategory", fl::safe<siGetEffectTargetBSelectionCategory>},
        {"GetEffectTargetBTarget", fl::safe<siGetEffectTargetBTarget>},
        {"GetEffectTriggerSpell", fl::safe<siGetEffectTriggerSpell>},
        {"GetEffectType", fl::safe<siGetEffectType>},
        {"GetEffectUsedTargetObjectType", fl::safe<siGetEffectUsedTargetObjectType>},
        {"GetEffectValueMultiplier", fl::safe<siGetEffectValueMultiplier>},
        {"GetExcludeCasterAuraSpell", fl::safe<siGetExcludeCasterAuraSpell>},
        {"GetExcludeTargetAuraSpell", fl::safe<siGetExcludeTargetAuraSpell>},
        {"GetManaCostPercentage", fl::safe<siGetManaCostPercentage>},
        {"GetMaxTicks", fl::safe<siGetMaxTicks>},
        {"GetSpellMechanicMaskByEffectMask", fl::safe<siGetSpellMechanicMaskByEffectMask>},
        {"GetSpellVisual", fl::safe<siGetSpellVisual>},
        {"IsAbilityLearnedWithProfession", fl::safe<siIsAbilityLearnedWithProfession>},
        {"IsAbilityOfSkillType", fl::safe<siIsAbilityOfSkillType>},
        {"IsAffected", fl::safe<siIsAffected>},
        {"IsAuraExclusiveBySpecificPerCasterWith", fl::safe<siIsAuraExclusiveBySpecificPerCasterWith>},
        {"IsAuraExclusiveBySpecificWith", fl::safe<siIsAuraExclusiveBySpecificWith>},
        {"IsBreakingStealth", fl::safe<siIsBreakingStealth>},
        {"IsExplicitDiscovery", fl::safe<siIsExplicitDiscovery>},
        {"IsItemFitToSpellRequirements", fl::safe<siIsItemFitToSpellRequirements>},
        {"IsLootCrafting", fl::safe<siIsLootCrafting>},
        {"IsPassiveStackableWithRanks", fl::safe<siIsPassiveStackableWithRanks>},
        {"IsPrimaryProfession", fl::safe<siIsPrimaryProfession>},
        {"IsPrimaryProfessionFirstRank", fl::safe<siIsPrimaryProfessionFirstRank>},
        {"IsProfession", fl::safe<siIsProfession>},
        {"IsProfessionOrRiding", fl::safe<siIsProfessionOrRiding>},
        {"IsStackableOnOneSlotWithDifferentCasters", fl::safe<siIsStackableOnOneSlotWithDifferentCasters>},
        {"NeedsComboPoints", fl::safe<siNeedsComboPoints>},
        {"SpellCancelsAuraEffect", fl::safe<siSpellCancelsAuraEffect>},
        {nullptr, nullptr}};
    addMethods(L, "SpellInfo", regs);
}
}  // namespace flu
