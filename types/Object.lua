---@meta

---@class Object
local Object = {}

---@deprecated not available (this core)
---@return any
function Object:GetByteValue() end

---@return any
function Object:GetEntry() end

---@deprecated not available (this core)
---@return any
function Object:GetFloatValue() end

---returns all 128 bits as a 32-digit hex string (the docs type is a 64-bit number); the type, entry and map live in the high half
---@return any
function Object:GetGUID() end

---@return any
function Object:GetGUIDLow() end

---@deprecated not available (this core)
---@return any
function Object:GetInt32Value() end

---@return any
function Object:GetScale() end

---@return any
function Object:GetTypeId() end

---@deprecated not available (this core)
---@return any
function Object:GetUInt16Value() end

---@param index any
---@return any
function Object:GetUInt32Value(index) end

---@deprecated not available (this core)
---@return any
function Object:GetUInt64Value() end

---@param index any
---@param flag any
---@return any
function Object:HasFlag(index, flag) end

---@return any
function Object:IsInWorld() end

---@return any
function Object:IsPlayer() end

---@deprecated not available (this core)
---@return any
function Object:RemoveFlag() end

---@deprecated not available (this core)
---@return any
function Object:SetByteValue() end

---@deprecated not available (this core)
---@return any
function Object:SetFlag() end

---@deprecated not available (this core)
---@return any
function Object:SetFloatValue() end

---@deprecated not available (this core)
---@return any
function Object:SetInt16Value() end

---@deprecated not available (this core)
---@return any
function Object:SetInt32Value() end

---@param scale any
---@return any
function Object:SetScale(scale) end

---@deprecated not available (this core)
---@return any
function Object:SetUInt16Value() end

---@deprecated not available (this core)
---@return any
function Object:SetUInt32Value() end

---@deprecated not available (this core)
---@return any
function Object:SetUInt64Value() end

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

---@deprecated not available (this core)
---@return any
function Object:UpdateUInt32Value() end
