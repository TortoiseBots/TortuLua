---@meta

---@class GameObject : WorldObject
local GameObject = {}

---@return any
function GameObject:Despawn() end

---@return any
function GameObject:GetDBTableGUIDLow() end

---@return any
function GameObject:GetDisplayId() end

---@return any
function GameObject:GetGoState() end

---@return any
function GameObject:GetLootRecipient() end

---@return any
function GameObject:GetLootRecipientGroup() end

---@return any
function GameObject:GetLootState() end

---@param questId any
---@return any
function GameObject:HasQuest(questId) end

---@return any
function GameObject:IsActive() end

---@return any
function GameObject:IsDestructible() end

---@return any
function GameObject:IsSpawned() end

---@return any
function GameObject:IsTransport() end

---@param arg1 any
---@return any
function GameObject:RemoveFromWorld(arg1) end

---@return any
function GameObject:Respawn() end

---@deprecated not available (this server)
---@return any
function GameObject:SaveToDB() end

---@param state any
---@return any
function GameObject:SetGoState(state) end

---@param state any
---@return any
function GameObject:SetLootState(state) end

---@param delay any
---@return any
function GameObject:SetRespawnTime(delay) end

---@param arg1 any
---@return any
function GameObject:SetSpawnedByDefault(arg1) end

---@param arg1 any
---@return any
function GameObject:UseDoorOrButton(arg1) end
