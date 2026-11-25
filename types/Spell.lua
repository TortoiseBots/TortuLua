---@meta

---@class Spell
local Spell = {}

---@param effIndex any
---@return any
function Spell:CalculateDamage(effIndex) end

---@return any
function Spell:Cancel() end

---@param arg1 any
---@return any
function Spell:Cast(arg1) end

---@return any
function Spell:CleanupTargetList() end

---@return any
function Spell:Finish() end

---@return any
function Spell:GetCaster() end

---@return any
function Spell:GetCastTime() end

---@return any
function Spell:GetDelayMoment() end

---@return any
function Spell:GetDelayStart() end

---@return any
function Spell:GetDuration() end

---@return any
function Spell:GetEntry() end

---@param effIndex any
---@return any
function Spell:GetGameObjectTargetCountForEffect(effIndex) end

---@param effIndex any
---@return any
function Spell:GetItemTargetCountForEffect(effIndex) end

---@return any
function Spell:GetOriginalCaster() end

---@return any
function Spell:GetPowerCost() end

---@return any
function Spell:GetRuneState() end

---@return any
function Spell:GetSpellInfo() end

---@return any
function Spell:GetState() end

---@return any
function Spell:GetTarget() end

---@return any
function Spell:GetTargetDest() end

---@param effIndex any
---@return any
function Spell:GetUnitTargetCountForEffect(effIndex) end

---@return any
function Spell:IsAutoActionResetSpell() end

---@return any
function Spell:IsAutoRepeat() end

---@return any
function Spell:IsChannelActive() end

---@return any
function Spell:IsDeletable() end

---@return any
function Spell:IsIgnoringCooldowns() end

---@return any
function Spell:IsInterruptable() end

---@return any
function Spell:IsNeedSendToClient() end

---@return any
function Spell:IsPositive() end

---@return any
function Spell:IsProcDisabled() end

---@return any
function Spell:IsTriggered() end

---@param spellInfo any
---@return any
function Spell:IsTriggeredByAura(spellInfo) end

---@param arg1 any
---@return any
function Spell:SetAutoRepeat(arg1) end

---@param time any
---@return any
function Spell:SetDelayStart(time) end

---@param arg1 any
---@return any
function Spell:SetExecutedCurrently(arg1) end

---@param arg1 any
---@return any
function Spell:SetReferencedFromCurrent(arg1) end

---@param value any
---@return any
function Spell:SetRuneState(value) end

---@param mod any
---@param value any
---@return any
function Spell:SetSpellValue(mod, value) end

---@param state any
---@return any
function Spell:SetState(state) end

---@return any
function Spell:UpdatePointers() end
