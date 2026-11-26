---@meta

---@class Guild
local Guild = {}

---an explicit rankId is validated by the core: the member is added at the lowest rank, then moved to rankId only if the guild has that rank; if it has not, the member stays at the lowest rank and a warning is logged (the call is queued, so it cannot return the refusal)
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

---@deprecated not available (Classic 1.60); Classic 1.60 has no guild bank (no client window and GUILD 9/11 never fire), so the core's Guild::SetBankTabText has no tab to name
---@return any
function Guild:SetBankTabText() end

---@deprecated not available (this server); the pinned core changes a guild leader only through Guild::HandleSetNewGuildMaster, which needs the old leader's session and its client permission checks; there is no server-side setter (Guild::_SetLeader is private)
---@return any
function Guild:SetLeader() end

---@param player any
---@param rankId any
---@return any
function Guild:SetMemberRank(player, rankId) end
