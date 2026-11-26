// Eluna Group methods of E2 and the documented long tail (docs: Group/index.html). The handle is {kind Group, its guid};
// the group resolves through GroupMgr on every call, and a method on a disbanded group raises "Group no longer exists".
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

// The subgroup indexes the core's counters array holds (MAX_RAID_SUBGROUPS = 8). A binding that passed a larger byte
// would make Group::ChangeMembersGroup / HasFreeSlotSubGroup index past the array, so the argument is checked here.
uint32_t argSubGroup(lua_State* L, int idx, char const* what)
{
    uint32_t const subgroup = argU8(L, idx, what);
    if (subgroup >= 8)
        luaL_error(L, "bad argument #%d to '%s' (subgroup must be 0..7)", idx, what);
    return subgroup;
}

// Group:GetMembers()
// Reading the member list is the world thread's alone (another thread may be changing the group): a handler that runs on
// a map thread gets an error instead of a race.
int gGetMembers(lua_State* L)
{
    requireWorldRead(L, "Group:GetMembers");
    return listOp(L, kGroup, "group.getMembers", "GetMembers");
}
// Group:GetGUID() -> the same 32-hex form as Object:GetGUID (docs: a number; see knowledge/modes/eluna-mode.md).
int gGetGUID(lua_State* L)
{
    requireWorldRead(L, "Group:GetGUID");
    return readOp(L, kGroup, "group.getGuid", "GetGUID");
}
// Group:GetLeaderGUID()
int gGetLeaderGUID(lua_State* L)
{
    requireWorldRead(L, "Group:GetLeaderGUID");
    return readOp(L, kGroup, "group.getLeaderGuid", "GetLeaderGUID");
}
// Group:GetMembersCount()
int gGetMembersCount(lua_State* L)
{
    requireWorldRead(L, "Group:GetMembersCount");
    return readOp(L, kGroup, "group.getMembersCount", "GetMembersCount");
}
// Group:GetMemberGUID(name) -> the member's guid, or nil when no member has that name
int gGetMemberGUID(lua_State* L)
{
    requireWorldRead(L, "Group:GetMemberGUID");
    std::string const name = argStr(L, 2, "name");
    return readOp(L, kGroup, "group.getMemberGuid", "GetMemberGUID", {EventValue(name)});
}
// Group:GetMemberFlags(guid) -> MEMBER_FLAG_* bits (0 when the guid is not a member)
int gGetMemberFlags(lua_State* L)
{
    requireWorldRead(L, "Group:GetMemberFlags");
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readOp(L, kGroup, "group.getMemberFlags", "GetMemberFlags", {EventValue(fl::guidText(guid))});
}
// Group:GetMemberGroup(guid) -> the subgroup, or 9 (MAX_RAID_SUBGROUPS + 1) for a guid that is not a member
int gGetMemberGroup(lua_State* L)
{
    requireWorldRead(L, "Group:GetMemberGroup");
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readOp(L, kGroup, "group.getMemberGroup", "GetMemberGroup", {EventValue(fl::guidText(guid))});
}
// Group:IsRaidGroup(), IsLFGGroup(), IsBGGroup(), IsBFGroup(), IsFull()
int gIsRaidGroup(lua_State* L)
{
    requireWorldRead(L, "Group:IsRaidGroup");
    return readOp(L, kGroup, "group.isRaidGroup", "IsRaidGroup");
}
int gIsLFGGroup(lua_State* L)
{
    requireWorldRead(L, "Group:IsLFGGroup");
    return readOp(L, kGroup, "group.isLfgGroup", "IsLFGGroup");
}
int gIsBGGroup(lua_State* L)
{
    requireWorldRead(L, "Group:IsBGGroup");
    return readOp(L, kGroup, "group.isBgGroup", "IsBGGroup");
}
int gIsBFGroup(lua_State* L)
{
    requireWorldRead(L, "Group:IsBFGroup");
    return readOp(L, kGroup, "group.isBfGroup", "IsBFGroup");
}
int gIsFull(lua_State* L)
{
    requireWorldRead(L, "Group:IsFull");
    return readOp(L, kGroup, "group.isFull", "IsFull");
}
// Group:IsMember(guid), IsLeader(guid), IsAssistant(guid)
int gIsMember(lua_State* L)
{
    requireWorldRead(L, "Group:IsMember");
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readOp(L, kGroup, "group.isMember", "IsMember", {EventValue(fl::guidText(guid))});
}
int gIsLeader(lua_State* L)
{
    requireWorldRead(L, "Group:IsLeader");
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readOp(L, kGroup, "group.isLeader", "IsLeader", {EventValue(fl::guidText(guid))});
}
int gIsAssistant(lua_State* L)
{
    requireWorldRead(L, "Group:IsAssistant");
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readOp(L, kGroup, "group.isAssistant", "IsAssistant", {EventValue(fl::guidText(guid))});
}
// Group:SameSubGroup(player1, player2)
int gSameSubGroup(lua_State* L)
{
    requireWorldRead(L, "Group:SameSubGroup");
    fl::HandleData const& a = objArg(L, 2, kPlayer, "player1");
    fl::HandleData const& b = objArg(L, 3, kPlayer, "player2");
    return readOp(L, kGroup, "group.sameSubGroup", "SameSubGroup",
                  {EventValue(fl::guidText(a.guid)), EventValue(fl::guidText(b.guid))});
}
// Group:HasFreeSlotSubGroup(subGroup)
int gHasFreeSlotSubGroup(lua_State* L)
{
    requireWorldRead(L, "Group:HasFreeSlotSubGroup");
    uint32_t const subgroup = argSubGroup(L, 2, "HasFreeSlotSubGroup");
    return readOp(L, kGroup, "group.hasFreeSlotSubGroup", "HasFreeSlotSubGroup",
                  {EventValue(static_cast<int64_t>(subgroup))});
}
// Group:AddMember(player) -> true when added (docs)
int gAddMember(lua_State* L)
{
    fl::HandleData const& player = objArg(L, 2, kPlayer, "player");
    return readOp(L, kGroup, "group.addMember", "AddMember", {EventValue(player)});
}
// Group:RemoveMember(guid [, method]) -> true when removed (docs)
int gRemoveMember(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    uint64_t const method = optUInt(L, 3, 0);
    if (method > 3)   // the core's RemoveMethod: 0 default, 1 kick, 2 leave, 3 kick from the dungeon finder
        return luaL_error(L, "bad argument #2 to 'RemoveMember' (method must be 0..3)");
    return readOp(L, kGroup, "group.removeMember", "RemoveMember",
                  {EventValue(fl::guidText(guid)), EventValue(static_cast<int64_t>(method))});
}
// Group:ConvertToLFG(), ConvertToRaid(), Disband()
int gConvertToLFG(lua_State* L) { return actionOp(L, kGroup, "group.convertToLfg", "ConvertToLFG"); }
int gConvertToRaid(lua_State* L) { return actionOp(L, kGroup, "group.convertToRaid", "ConvertToRaid"); }
int gDisband(lua_State* L) { return actionOp(L, kGroup, "group.disband", "Disband"); }
// Group:SetLeader(guid)
int gSetLeader(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return actionOp(L, kGroup, "group.setLeader", "SetLeader", {EventValue(fl::guidText(guid))});
}
// Group:SetMemberFlag(target, apply, flag)
int gSetMemberFlag(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    bool const apply = argBool(L, 3, "apply");
    uint32_t const flag = argU8(L, 4, "flag");
    if (flag != 1 && flag != 2 && flag != 4)   // MEMBER_FLAG_ASSISTANT/MAINTANK/MAINASSIST
        return luaL_error(L, "bad argument #3 to 'SetMemberFlag' (flag must be 1, 2 or 4)");
    return actionOp(L, kGroup, "group.setMemberFlag", "SetMemberFlag",
                    {EventValue(fl::guidText(guid)), EventValue(apply), EventValue(static_cast<int64_t>(flag))});
}
// Group:SetMembersGroup(guid, groupID)
int gSetMembersGroup(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    uint32_t const subgroup = argSubGroup(L, 3, "SetMembersGroup");
    return actionOp(L, kGroup, "group.setMembersGroup", "SetMembersGroup",
                    {EventValue(fl::guidText(guid)), EventValue(static_cast<int64_t>(subgroup))});
}
// Group:SetTargetIcon(icon, target, setter); target 0 clears the icon (docs)
int gSetTargetIcon(lua_State* L)
{
    uint32_t const icon = argU8(L, 2, "icon");
    if (icon >= 8)   // TARGET_ICONS_COUNT
        return luaL_error(L, "bad argument #1 to 'SetTargetIcon' (icon must be 0..7)");
    fl::Guid target;
    argGuid(L, 3, target);
    fl::Guid setter;
    argGuid(L, 4, setter);
    return actionOp(L, kGroup, "group.setTargetIcon", "SetTargetIcon",
                    {EventValue(static_cast<int64_t>(icon)), EventValue(fl::guidText(target)),
                     EventValue(fl::guidText(setter))});
}
// Group:SendPacket(packet, ignorePlayersInBg, ignore)
int gSendPacket(lua_State* L)
{
    fl::HandleData const& h = self(L, kGroup);
    sendPacketArg(L, h, 2, "SendPacket", optBool(L, 3, false), optUInt(L, 4, 0));
    return 0;
}
}  // namespace

void registerGroupMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetMembers", fl::safe<gGetMembers>},           {"GetGUID", fl::safe<gGetGUID>},
        {"GetLeaderGUID", fl::safe<gGetLeaderGUID>},     {"GetMembersCount", fl::safe<gGetMembersCount>},
        {"GetMemberGUID", fl::safe<gGetMemberGUID>},     {"GetMemberFlags", fl::safe<gGetMemberFlags>},
        {"GetMemberGroup", fl::safe<gGetMemberGroup>},   {"IsRaidGroup", fl::safe<gIsRaidGroup>},
        {"IsLFGGroup", fl::safe<gIsLFGGroup>},           {"IsBGGroup", fl::safe<gIsBGGroup>},
        {"IsBFGroup", fl::safe<gIsBFGroup>},             {"IsFull", fl::safe<gIsFull>},
        {"IsMember", fl::safe<gIsMember>},               {"IsLeader", fl::safe<gIsLeader>},
        {"IsAssistant", fl::safe<gIsAssistant>},         {"SameSubGroup", fl::safe<gSameSubGroup>},
        {"HasFreeSlotSubGroup", fl::safe<gHasFreeSlotSubGroup>},
        {"AddMember", fl::safe<gAddMember>},             {"RemoveMember", fl::safe<gRemoveMember>},
        {"ConvertToLFG", fl::safe<gConvertToLFG>},       {"ConvertToRaid", fl::safe<gConvertToRaid>},
        {"Disband", fl::safe<gDisband>},                 {"SetLeader", fl::safe<gSetLeader>},
        {"SetMemberFlag", fl::safe<gSetMemberFlag>},     {"SetMembersGroup", fl::safe<gSetMembersGroup>},
        {"SetTargetIcon", fl::safe<gSetTargetIcon>},     {"SendPacket", fl::safe<gSendPacket>},
        {nullptr, nullptr}};
    addMethods(L, "Group", regs);
}
}  // namespace flu
