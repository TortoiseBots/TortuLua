---@meta

---@class SpellInfo
local SpellInfo = {}

---@return any
function SpellInfo:CalcCastTime() end

---@return any
function SpellInfo:CanBeUsedInCombat() end

---@param auraSpellInfo any
---@return any
function SpellInfo:CanDispelAura(auraSpellInfo) end

---@param auraSpellInfo any
---@return any
function SpellInfo:CanPierceImmuneAura(auraSpellInfo) end

---@param auraSpellInfo any
---@return any
function SpellInfo:CanSpellProvideImmunityAgainstAura(auraSpellInfo) end

---@param caster any
---@param target any
---@return any
function SpellInfo:CheckTarget(caster, target) end

---@param target any
---@return any
function SpellInfo:CheckTargetCreatureType(target) end

---@param effIndex any
---@return any
function SpellInfo:EffectIsAreaAuraEffect(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:EffectIsAura(effIndex) end

---@param effIndex any
---@param aura any
---@return any
function SpellInfo:EffectIsAuraType(effIndex, aura) end

---@param effIndex any
---@return any
function SpellInfo:EffectIsEffect(effIndex) end

---@param effIndex any
---@param effect any
---@return any
function SpellInfo:EffectIsEffectType(effIndex, effect) end

---@param effIndex any
---@return any
function SpellInfo:EffectIsTargetingArea(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:EffectIsUnitOwnedAuraEffect(effIndex) end

---@return any
function SpellInfo:GetActiveIconID() end

---@return any
function SpellInfo:GetAllEffectsMechanicMask() end

---@return any
function SpellInfo:GetAllowedMechanicMask() end

---@return any
function SpellInfo:GetAreaGroupId() end

---@return any
function SpellInfo:GetAttackType() end

---@return any
function SpellInfo:GetAttributes() end

---@return any
function SpellInfo:GetAttributesCu() end

---@return any
function SpellInfo:GetAttributesEx() end

---@return any
function SpellInfo:GetAttributesEx2() end

---@return any
function SpellInfo:GetAttributesEx3() end

---@return any
function SpellInfo:GetAttributesEx4() end

---@return any
function SpellInfo:GetAttributesEx5() end

---@return any
function SpellInfo:GetAttributesEx6() end

---@return any
function SpellInfo:GetAttributesEx7() end

---@return any
function SpellInfo:GetAuraInterruptFlags() end

---@return any
function SpellInfo:GetAuraState() end

---@return any
function SpellInfo:GetBaseLevel() end

---@return any
function SpellInfo:GetCasterAuraSpell() end

---@return any
function SpellInfo:GetCasterAuraState() end

---@return any
function SpellInfo:GetCasterAuraStateNot() end

---@return any
function SpellInfo:GetCategory() end

---@return any
function SpellInfo:GetCategoryRecoveryTime() end

---@return any
function SpellInfo:GetChannelInterruptFlags() end

---@param triggered any
---@return any
function SpellInfo:GetDiminishingReturnsGroupForSpell(triggered) end

---@param triggered any
---@return any
function SpellInfo:GetDiminishingReturnsGroupType(triggered) end

---@param triggered any
---@return any
function SpellInfo:GetDiminishingReturnsLimitDuration(triggered) end

---@param triggered any
---@return any
function SpellInfo:GetDiminishingReturnsMaxLevel(triggered) end

---@return any
function SpellInfo:GetDispel() end

---@return any
function SpellInfo:GetDmgClass() end

---@return any
function SpellInfo:GetDuration() end

---@param effIndex any
---@return any
function SpellInfo:GetEffectAmplitude(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectApplyAuraName(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectBasePoints(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectBonusMultiplier(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectCalcRadius(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectCalcValue(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectChainTarget(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectDamageMultiplier(effIndex) end

---returns ClassicSpellValue DieSides (vanilla damaging/healing/absorbing effects; 0 for others)
---@param effIndex any
---@return any
function SpellInfo:GetEffectDieSides(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectHasRadius(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectImplicitTargetType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectIndex(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectItemType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectMechanic(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectMechanicMask(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectMiscValue(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectMiscValueB(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectPointsPerComboPoint(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectProvidedTargetMask(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectRealPointsPerLevel(effIndex) end

---@param effIndex any
---@param maskIndex any
---@return any
function SpellInfo:GetEffectSpellClassMask(effIndex, maskIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetACheckType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetADirectionAngle(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetADirectionType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetAIsArea(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetAObjectType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetAReferenceType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetASelectionCategory(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetATarget(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBCheckType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBDirectionAngle(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBDirectionType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBIsArea(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBObjectType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBReferenceType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBSelectionCategory(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTargetBTarget(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectTriggerSpell(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectUsedTargetObjectType(effIndex) end

---@param effIndex any
---@return any
function SpellInfo:GetEffectValueMultiplier(effIndex) end

---@return any
function SpellInfo:GetEquippedItemClass() end

---@return any
function SpellInfo:GetEquippedItemInventoryTypeMask() end

---@return any
function SpellInfo:GetEquippedItemSubClassMask() end

---@return any
function SpellInfo:GetExcludeCasterAuraSpell() end

---@return any
function SpellInfo:GetExcludeTargetAuraSpell() end

---@return any
function SpellInfo:GetExplicitTargetMask() end

---@return any
function SpellInfo:GetFacingCasterFlags() end

---@return any
function SpellInfo:GetId() end

---@return any
function SpellInfo:GetInterruptFlags() end

---@return any
function SpellInfo:GetManaCost() end

---@return any
function SpellInfo:GetManaCostPercentage() end

---@return any
function SpellInfo:GetManaCostPerlevel() end

---@return any
function SpellInfo:GetManaPerSecond() end

---@deprecated not available (this core)
---@return any
function SpellInfo:GetManaPerSecondPerLevel() end

---@return any
function SpellInfo:GetMaxAffectedTargets() end

---@return any
function SpellInfo:GetMaxDuration() end

---@return any
function SpellInfo:GetMaxLevel() end

---@return any
function SpellInfo:GetMaxRange() end

---@return any
function SpellInfo:GetMaxTargetLevel() end

---@return any
function SpellInfo:GetMaxTicks() end

---@return any
function SpellInfo:GetMechanic() end

---@return any
function SpellInfo:GetMinRange() end

---@return any
function SpellInfo:GetPowerType() end

---@return any
function SpellInfo:GetPreventionType() end

---@deprecated not available (this core)
---@return any
function SpellInfo:GetPriority() end

---@return any
function SpellInfo:GetProcChance() end

---@return any
function SpellInfo:GetProcCharges() end

---@return any
function SpellInfo:GetProcFlags() end

---@return any
function SpellInfo:GetRank() end

---@param index any
---@return any
function SpellInfo:GetReagent(index) end

---@param index any
---@return any
function SpellInfo:GetReagentCount(index) end

---@return any
function SpellInfo:GetRecoveryTime() end

---@return any
function SpellInfo:GetRequiresSpellFocus() end

---@deprecated not available (this core)
---@return any
function SpellInfo:GetRuneCostID() end

---@return any
function SpellInfo:GetSchoolMask() end

---@return any
function SpellInfo:GetSpeed() end

---@param index any
---@return any
function SpellInfo:GetSpellFamilyFlags(index) end

---@return any
function SpellInfo:GetSpellFamilyName() end

---@return any
function SpellInfo:GetSpellIconID() end

---@return any
function SpellInfo:GetSpellLevel() end

---@param effectMask any
---@return any
function SpellInfo:GetSpellMechanicMaskByEffectMask(effectMask) end

---@return any
function SpellInfo:GetSpellSpecific() end

---@param index any
---@return any
function SpellInfo:GetSpellVisual(index) end

---@return any
function SpellInfo:GetStackAmount() end

---@return any
function SpellInfo:GetStances() end

---@return any
function SpellInfo:GetStancesNot() end

---@return any
function SpellInfo:GetStartRecoveryCategory() end

---@return any
function SpellInfo:GetStartRecoveryTime() end

---@return any
function SpellInfo:GetTargetAuraSpell() end

---@return any
function SpellInfo:GetTargetAuraState() end

---@return any
function SpellInfo:GetTargetAuraStateNot() end

---@return any
function SpellInfo:GetTargetCreatureType() end

---@return any
function SpellInfo:GetTargets() end

---@param index any
---@return any
function SpellInfo:GetTotem(index) end

---@param index any
---@return any
function SpellInfo:GetTotemCategory(index) end

---@return any
function SpellInfo:HasAreaAuraEffect() end

---@param aura any
---@return any
function SpellInfo:HasAura(aura) end

---@param effect any
---@return any
function SpellInfo:HasEffect(effect) end

---@return any
function SpellInfo:HasInitialAggro() end

---@return any
function SpellInfo:HasOnlyDamageEffects() end

---@return any
function SpellInfo:IsAbilityLearnedWithProfession() end

---@param skillType any
---@return any
function SpellInfo:IsAbilityOfSkillType(skillType) end

---@param familyName any
---@param flag0 any
---@param flag1 any
---@param flag2 any
---@return any
function SpellInfo:IsAffected(familyName, flag0, flag1, flag2) end

---@deprecated not available (this core)
---@return any
function SpellInfo:IsAffectedBySpellMod() end

---@return any
function SpellInfo:IsAffectedBySpellMods() end

---@return any
function SpellInfo:IsAffectingArea() end

---@return any
function SpellInfo:IsAllowingDeadTarget() end

---@param spellInfo any
---@return any
function SpellInfo:IsAuraExclusiveBySpecificPerCasterWith(spellInfo) end

---@param spellInfo any
---@return any
function SpellInfo:IsAuraExclusiveBySpecificWith(spellInfo) end

---@return any
function SpellInfo:IsAutocastable() end

---@return any
function SpellInfo:IsAutoRepeatRangedSpell() end

---@return any
function SpellInfo:IsBreakingStealth() end

---@return any
function SpellInfo:IsChanneled() end

---@return any
function SpellInfo:IsCooldownStartedOnEvent() end

---@return any
function SpellInfo:IsDeathPersistent() end

---@param spellInfo any
---@return any
function SpellInfo:IsDifferentRankOf(spellInfo) end

---@return any
function SpellInfo:IsExplicitDiscovery() end

---@return any
function SpellInfo:IsGroupBuff() end

---@param spellInfo any
---@return any
function SpellInfo:IsHighRankOf(spellInfo) end

---@param item any
---@return any
function SpellInfo:IsItemFitToSpellRequirements(item) end

---@return any
function SpellInfo:IsLootCrafting() end

---@return any
function SpellInfo:IsMoveAllowedChannel() end

---@return any
function SpellInfo:IsMultiSlotAura() end

---@return any
function SpellInfo:IsNextMeleeSwingSpell() end

---@return any
function SpellInfo:IsPassive() end

---@return any
function SpellInfo:IsPassiveStackableWithRanks() end

---@return any
function SpellInfo:IsPositive() end

---@param effIndex any
---@return any
function SpellInfo:IsPositiveEffect(effIndex) end

---@return any
function SpellInfo:IsPrimaryProfession() end

---@return any
function SpellInfo:IsPrimaryProfessionFirstRank() end

---@return any
function SpellInfo:IsProfession() end

---@return any
function SpellInfo:IsProfessionOrRiding() end

---@return any
function SpellInfo:IsRangedWeaponSpell() end

---@return any
function SpellInfo:IsRanked() end

---@param spellInfo any
---@return any
function SpellInfo:IsRankOf(spellInfo) end

---@return any
function SpellInfo:IsRequiringDeadTarget() end

---@return any
function SpellInfo:IsSelfCast() end

---@return any
function SpellInfo:IsSingleTarget() end

---@return any
function SpellInfo:IsStackableOnOneSlotWithDifferentCasters() end

---@return any
function SpellInfo:IsStackableWithRanks() end

---@return any
function SpellInfo:IsTargetingArea() end

---@return any
function SpellInfo:NeedsComboPoints() end

---@return any
function SpellInfo:NeedsExplicitUnitTarget() end

---@param triggeringSpell any
---@return any
function SpellInfo:NeedsToBeTriggeredByCaster(triggeringSpell) end

---@param aurEff any
---@return any
function SpellInfo:SpellCancelsAuraEffect(aurEff) end
