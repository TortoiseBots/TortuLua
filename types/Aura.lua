---@meta

---@class Aura
local Aura = {}

---@return any
function Aura:GetAuraId() end

---@return any
function Aura:GetCaster() end

---@return any
function Aura:GetCasterGUID() end

---@return any
function Aura:GetCasterLevel() end

---@return any
function Aura:GetDuration() end

---@return any
function Aura:GetEntry() end

---@return any
function Aura:GetMaxDuration() end

---@return any
function Aura:GetOwner() end

---@return any
function Aura:GetSpellInfo() end

---@return any
function Aura:GetStackAmount() end

---@return any
function Aura:Remove() end

---@param duration any
---@return any
function Aura:SetDuration(duration) end

---@param duration any
---@return any
function Aura:SetMaxDuration(duration) end

---@param amount any
---@return any
function Aura:SetStackAmount(amount) end
