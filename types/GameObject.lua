---@meta

---@class GameObject : WorldObject
local GameObject = {}

---@return any
function GameObject:Despawn() end

---@return any
function GameObject:GetDBTableGUIDLow() end

---@return any
function GameObject:GetGoState() end

---@return any
function GameObject:GetLootState() end

---@return any
function GameObject:IsSpawned() end

---@return any
function GameObject:Respawn() end

---@param state any
---@return any
function GameObject:SetGoState(state) end

---@param state any
---@return any
function GameObject:SetLootState(state) end

---@param arg1 any
---@return any
function GameObject:UseDoorOrButton(arg1) end
