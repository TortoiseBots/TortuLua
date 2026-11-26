---@meta

---@class WorldPacket
local WorldPacket = {}

---only what CreatePacket made or a packet event passes; the events get a read-only copy
---@return any
function WorldPacket:GetOpcode() end

---@return any
function WorldPacket:GetSize() end

---@return any
function WorldPacket:ReadByte() end

---@return any
function WorldPacket:ReadDouble() end

---@return any
function WorldPacket:ReadFloat() end

---@return any
function WorldPacket:ReadGUID() end

---@return any
function WorldPacket:ReadLong() end

---@return any
function WorldPacket:ReadShort() end

---@return any
function WorldPacket:ReadString() end

---@return any
function WorldPacket:ReadUByte() end

---@return any
function WorldPacket:ReadULong() end

---@return any
function WorldPacket:ReadUShort() end

---@param opcode any
---@return any
function WorldPacket:SetOpcode(opcode) end

---@param value any
---@return any
function WorldPacket:WriteByte(value) end

---@param value any
---@return any
function WorldPacket:WriteDouble(value) end

---@param value any
---@return any
function WorldPacket:WriteFloat(value) end

---@param value any
---@return any
function WorldPacket:WriteGUID(value) end

---@param value any
---@return any
function WorldPacket:WriteLong(value) end

---@param value any
---@return any
function WorldPacket:WriteShort(value) end

---@param value any
---@return any
function WorldPacket:WriteString(value) end

---@param value any
---@return any
function WorldPacket:WriteUByte(value) end

---@param value any
---@return any
function WorldPacket:WriteULong(value) end

---@param value any
---@return any
function WorldPacket:WriteUShort(value) end
