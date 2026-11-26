---@meta

---@class Group
local Group = {}

---@param player any
---@return any
function Group:AddMember(player) end

---@return any
function Group:ConvertToLFG() end

---@return any
function Group:ConvertToRaid() end

---@return any
function Group:Disband() end

---@return any
function Group:GetGUID() end

---@return any
function Group:GetLeaderGUID() end

---@param arg1 any
---@return any
function Group:GetMemberFlags(arg1) end

---@param arg1 any
---@return any
function Group:GetMemberGroup(arg1) end

---@param name any
---@return any
function Group:GetMemberGUID(name) end

---@return any
function Group:GetMembers() end

---@return any
function Group:GetMembersCount() end

---@param HasFreeSlotSubGroup any
---@return any
function Group:HasFreeSlotSubGroup(HasFreeSlotSubGroup) end

---@param arg1 any
---@return any
function Group:IsAssistant(arg1) end

---@return any
function Group:IsBFGroup() end

---@return any
function Group:IsBGGroup() end

---@return any
function Group:IsFull() end

---@param arg1 any
---@return any
function Group:IsLeader(arg1) end

---@return any
function Group:IsLFGGroup() end

---@param arg1 any
---@return any
function Group:IsMember(arg1) end

---@return any
function Group:IsRaidGroup() end

---@param arg1 any
---@param arg2 any
---@return any
function Group:RemoveMember(arg1, arg2) end

---@param player1 any
---@param player2 any
---@return any
function Group:SameSubGroup(player1, player2) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function Group:SendPacket(arg1, arg2, arg3) end

---@param arg1 any
---@return any
function Group:SetLeader(arg1) end

---@param arg1 any
---@param apply any
---@param flag any
---@return any
function Group:SetMemberFlag(arg1, apply, flag) end

---@param arg1 any
---@param SetMembersGroup any
---@return any
function Group:SetMembersGroup(arg1, SetMembersGroup) end

---@param icon any
---@param arg2 any
---@param arg3 any
---@return any
function Group:SetTargetIcon(icon, arg2, arg3) end
