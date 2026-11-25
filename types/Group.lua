---@meta

---@class Group
local Group = {}

---@param player any
---@return any
function Group:AddMember(player) end

---@return any
function Group:GetLeaderGUID() end

---@return any
function Group:GetMembers() end

---@return any
function Group:IsRaidGroup() end

---@param arg1 any
---@param arg2 any
---@return any
function Group:RemoveMember(arg1, arg2) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function Group:SendPacket(arg1, arg2, arg3) end
