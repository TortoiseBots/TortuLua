---@meta

---@class Guild
local Guild = {}

---an explicit rankId the guild has no rank for is refused and nothing is written (the core would otherwise store it and leave the member without a rank)
---@param player any
---@param rankId any
---@return any
function Guild:AddMember(player, rankId) end

---@param player any
---@param arg2 any
---@return any
function Guild:DeleteMember(player, arg2) end

---@return any
function Guild:Disband() end

---@return any
function Guild:GetId() end

---@return any
function Guild:GetInfo() end

---@return any
function Guild:GetLeader() end

---@return any
function Guild:GetLeaderGUID() end

---@return any
function Guild:GetMemberCount() end

---@return any
function Guild:GetMembers() end

---@return any
function Guild:GetMOTD() end

---@return any
function Guild:GetName() end

---@return any
function Guild:SendPacket() end

---@param arg1 any
---@param rankId any
---@return any
function Guild:SendPacketToRanked(arg1, rankId) end

---@deprecated not available (Classic 1.60)
---@return any
function Guild:SetBankTabText() end

---@deprecated not available (this server)
---@return any
function Guild:SetLeader() end

---@param player any
---@param rankId any
---@return any
function Guild:SetMemberRank(player, rankId) end
