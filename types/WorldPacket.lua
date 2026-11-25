---@meta

---@class WorldPacket
local WorldPacket = {}

---only what CreatePacket made or a packet event passes; the events get a read-only copy
---@return any
function WorldPacket:GetOpcode() end

---@return any
function WorldPacket:GetSize() end

---@return any
function WorldPacket:ReadDouble() end

---@return any
function WorldPacket:ReadFloat() end

---@return any
function WorldPacket:ReadString() end

---@param opcode any
---@return any
function WorldPacket:SetOpcode(opcode) end

---@param arg1 any
---@return any
function WorldPacket:WriteDouble(arg1) end

---@param value any
---@return any
function WorldPacket:WriteFloat(value) end

---@param arg1 any
---@return any
function WorldPacket:WriteString(arg1) end
