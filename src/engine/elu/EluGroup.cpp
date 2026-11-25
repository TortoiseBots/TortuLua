// Eluna Group methods of E2 (docs: Group/index.html). The handle is {kind Group, its guid}; the group resolves through
// GroupMgr on every call, and a method on a disbanded group raises "Group no longer exists".
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

// Group:GetMembers()
// Reading the member list is the world thread's alone (another thread may be changing the group): a handler that runs on
// a map thread gets an error instead of a race.
int gGetMembers(lua_State* L)
{
    if (!host(L).worldOnlyReadOk())
        return luaL_error(L, "Group:GetMembers is only available on the world thread (server events, timed events, commands), "
                             "not from a map-thread handler");
    return listOp(L, kGroup, "group.getMembers", "GetMembers");
}
// Group:GetLeaderGUID()
int gGetLeaderGUID(lua_State* L) { return readOp(L, kGroup, "group.getLeaderGuid", "GetLeaderGUID"); }
// Group:IsRaidGroup()
int gIsRaidGroup(lua_State* L) { return readOp(L, kGroup, "group.isRaidGroup", "IsRaidGroup"); }
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
        {"GetMembers", fl::safe<gGetMembers>},       {"GetLeaderGUID", fl::safe<gGetLeaderGUID>},
        {"IsRaidGroup", fl::safe<gIsRaidGroup>},     {"AddMember", fl::safe<gAddMember>},
        {"RemoveMember", fl::safe<gRemoveMember>},   {"SendPacket", fl::safe<gSendPacket>},
        {nullptr, nullptr}};
    addMethods(L, "Group", regs);
}
}  // namespace flu
