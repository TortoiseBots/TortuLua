---@meta

---@class AuraEffect
local AuraEffect = {}

---@param caster any
---@return any
function AuraEffect:CalculateAmount(caster) end

---@param caster any
---@param arg2 any
---@param arg3 any
---@return any
function AuraEffect:CalculatePeriodic(caster, arg2, arg3) end

---@return any
function AuraEffect:CanBeRecalculated() end

---@param newAmount any
---@param arg2 any
---@param arg3 any
---@return any
function AuraEffect:ChangeAmount(newAmount, arg2, arg3) end

---@return any
function AuraEffect:GetAmount() end

---@return any
function AuraEffect:GetAmplitude() end

---@return any
function AuraEffect:GetAuraType() end

---@return any
function AuraEffect:GetBase() end

---@return any
function AuraEffect:GetBaseAmount() end

---@return any
function AuraEffect:GetCaster() end

---@return any
function AuraEffect:GetCasterGUID() end

---@return any
function AuraEffect:GetEffIndex() end

---@return any
function AuraEffect:GetId() end

---@return any
function AuraEffect:GetPeriodicTimer() end

---@return any
function AuraEffect:GetRemainingTicks() end

---@return any
function AuraEffect:GetSpellInfo() end

---@return any
function AuraEffect:GetTargetList() end

---@return any
function AuraEffect:GetTickNumber() end

---@return any
function AuraEffect:GetTotalTicks() end

---@return any
function AuraEffect:RecalculateAmount() end

---@param amount any
---@return any
function AuraEffect:SetAmount(amount) end

---@param arg1 any
---@return any
function AuraEffect:SetCanBeRecalculated(arg1) end

---@param timer any
---@return any
function AuraEffect:SetPeriodicTimer(timer) end
