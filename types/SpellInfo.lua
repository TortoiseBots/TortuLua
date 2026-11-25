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

---@return any
function SpellInfo:GetActiveIconID() end

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

---@return any
function SpellInfo:GetDispel() end

---@return any
function SpellInfo:GetDmgClass() end

---@return any
function SpellInfo:GetDuration() end

---@return any
function SpellInfo:GetEquippedItemClass() end

---@return any
function SpellInfo:GetEquippedItemInventoryTypeMask() end

---@return any
function SpellInfo:GetEquippedItemSubClassMask() end

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
function SpellInfo:GetManaCostPerlevel() end

---@return any
function SpellInfo:GetManaPerSecond() end

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
function SpellInfo:GetMechanic() end

---@return any
function SpellInfo:GetMinRange() end

---@return any
function SpellInfo:GetPowerType() end

---@return any
function SpellInfo:GetPreventionType() end

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

---@return any
function SpellInfo:GetSpellSpecific() end

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
function SpellInfo:IsAffectedBySpellMods() end

---@return any
function SpellInfo:IsAffectingArea() end

---@return any
function SpellInfo:IsAllowingDeadTarget() end

---@return any
function SpellInfo:IsAutocastable() end

---@return any
function SpellInfo:IsAutoRepeatRangedSpell() end

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
function SpellInfo:IsGroupBuff() end

---@param spellInfo any
---@return any
function SpellInfo:IsHighRankOf(spellInfo) end

---@return any
function SpellInfo:IsMoveAllowedChannel() end

---@return any
function SpellInfo:IsMultiSlotAura() end

---@return any
function SpellInfo:IsNextMeleeSwingSpell() end

---@return any
function SpellInfo:IsPassive() end

---@return any
function SpellInfo:IsPositive() end

---@param effIndex any
---@return any
function SpellInfo:IsPositiveEffect(effIndex) end

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
function SpellInfo:IsSingleTarget() end

---@return any
function SpellInfo:IsStackableWithRanks() end

---@return any
function SpellInfo:IsTargetingArea() end

---@return any
function SpellInfo:NeedsExplicitUnitTarget() end

---@param triggeringSpell any
---@return any
function SpellInfo:NeedsToBeTriggeredByCaster(triggeringSpell) end
