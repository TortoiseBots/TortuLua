---@meta

---@class ElunaQuery
local ElunaQuery = {}

---@param arg1 any
---@return any
function ElunaQuery:GetBool(arg1) end

---@param column any
---@return any
function ElunaQuery:GetColumnCount(column) end

---@param arg1 any
---@return any
function ElunaQuery:GetDouble(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetFloat(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetInt16(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetInt32(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetInt64(arg1) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@param arg5 any
---@param arg6 any
---@param arg7 any
---@return any
function ElunaQuery:GetInt8(arg1, arg2, arg3, arg4, arg5, arg6, arg7) end

---@param column any
---@return any
function ElunaQuery:GetRow(column) end

---@param column any
---@return any
function ElunaQuery:GetRowCount(column) end

---@param arg1 any
---@return any
function ElunaQuery:GetString(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetUInt16(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetUInt32(arg1) end

---@param arg1 any
---@return any
function ElunaQuery:GetUInt64(arg1) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@param arg5 any
---@param arg6 any
---@param arg7 any
---@return any
function ElunaQuery:GetUInt8(arg1, arg2, arg3, arg4, arg5, arg6, arg7) end

---@param arg1 any
---@return any
function ElunaQuery:IsNull(arg1) end

---@return any
function ElunaQuery:NextRow() end
