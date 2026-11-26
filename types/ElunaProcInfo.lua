---@meta

---@class ElunaProcInfo
local ElunaProcInfo = {}

---@return any
function ElunaProcInfo:GetActionTarget() end

---@return any
function ElunaProcInfo:GetActor() end

---@return any
function ElunaProcInfo:GetAttackType() end

---@return any
function ElunaProcInfo:GetBlock() end

---@return any
function ElunaProcInfo:GetDamage() end

---@return any
function ElunaProcInfo:GetDamageAbsorb() end

---@return any
function ElunaProcInfo:GetDamageType() end

---@return any
function ElunaProcInfo:GetEffectiveHeal() end

---@return any
function ElunaProcInfo:GetHeal() end

---@return any
function ElunaProcInfo:GetHealAbsorb() end

---@return any
function ElunaProcInfo:GetHitMask() end

---@return any
function ElunaProcInfo:GetMap() end

---@return any
function ElunaProcInfo:GetProcSpell() end

---@return any
function ElunaProcInfo:GetResist() end

---@return any
function ElunaProcInfo:GetSchoolMask() end

---@return any
function ElunaProcInfo:GetSpellInfo() end

---@return any
function ElunaProcInfo:GetSpellPhaseMask() end

---@return any
function ElunaProcInfo:GetSpellTypeMask() end

---@return any
function ElunaProcInfo:GetTypeMask() end

---@return any
function ElunaProcInfo:HasDamage() end

---@return any
function ElunaProcInfo:HasHeal() end

---@param absorb any
---@return any
function ElunaProcInfo:SetAbsorbDamage(absorb) end

---@param absorb any
---@return any
function ElunaProcInfo:SetAbsorbHeal(absorb) end

---@param block any
---@return any
function ElunaProcInfo:SetBlockDamage(block) end

---@param damage any
---@return any
function ElunaProcInfo:SetDamage(damage) end

---@param effectiveHeal any
---@return any
function ElunaProcInfo:SetEffectiveHeal(effectiveHeal) end

---@deprecated not available (this core)
---@return any
function ElunaProcInfo:SetHeal() end

---@param resist any
---@return any
function ElunaProcInfo:SetResistDamage(resist) end
