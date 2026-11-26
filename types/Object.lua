---@meta

---@class Object
local Object = {}

---@return any
function Object:GetEntry() end

---returns all 128 bits as a 32-digit hex string (the docs type is a 64-bit number); the type, entry and map live in the high half
---@return any
function Object:GetGUID() end

---@return any
function Object:GetGUIDLow() end

---@return any
function Object:GetScale() end

---@return any
function Object:GetTypeId() end

---@return any
function Object:IsInWorld() end

---@return any
function Object:IsPlayer() end

---@param scale any
---@return any
function Object:SetScale(scale) end

---@return any
function Object:ToCorpse() end

---@return any
function Object:ToCreature() end

---@return any
function Object:ToGameObject() end

---@return any
function Object:ToPlayer() end

---@return any
function Object:ToUnit() end
