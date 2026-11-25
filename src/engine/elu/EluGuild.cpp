// Eluna Guild methods of E2 and the documented long tail (docs: Guild/index.html). The handle is {kind Guild, guild id};
// the guild resolves through GuildMgr on every call, so a handle kept after the guild disbands raises "Guild no longer
// exists".
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

// Guild:GetName(), GetMOTD(), GetInfo(), GetId()
int guGetName(lua_State* L)
{
    requireWorldRead(L, "Guild:GetName");
    return readOp(L, kGuild, "guild.getName", "GetName");
}
int guGetMOTD(lua_State* L)
{
    requireWorldRead(L, "Guild:GetMOTD");
    return readOp(L, kGuild, "guild.getMotd", "GetMOTD");
}
int guGetInfo(lua_State* L)
{
    requireWorldRead(L, "Guild:GetInfo");
    return readOp(L, kGuild, "guild.getInfo", "GetInfo");
}
int guGetId(lua_State* L)
{
    requireWorldRead(L, "Guild:GetId");
    return readOp(L, kGuild, "guild.getId", "GetId");
}
// Guild:GetMembers(): the players of the guild that are online and in the world (docs).
// World thread only, like Group:GetMembers (another thread may be changing the guild).
int guGetMembers(lua_State* L)
{
    requireWorldRead(L, "Guild:GetMembers");
    return listOp(L, kGuild, "guild.getMembers", "GetMembers");
}
// Guild:GetMemberCount(): every member, online or not (docs).
int guGetMemberCount(lua_State* L)
{
    requireWorldRead(L, "Guild:GetMemberCount");
    return readOp(L, kGuild, "guild.getMemberCount", "GetMemberCount");
}
// Guild:GetLeaderGUID()
int guGetLeaderGUID(lua_State* L)
{
    requireWorldRead(L, "Guild:GetLeaderGUID");
    return readOp(L, kGuild, "guild.getLeaderGuid", "GetLeaderGUID");
}
// Guild:GetLeader(): the leader when logged in, else nil (docs).
int guGetLeader(lua_State* L)
{
    requireWorldRead(L, "Guild:GetLeader");
    return readHandleOp(L, kGuild, "guild.getLeader", "GetLeader");
}
// Guild:AddMember(player [, rankId]); no rank means the guild's lowest rank (docs).
int guAddMember(lua_State* L)
{
    fl::HandleData const& player = objArg(L, 2, kPlayer, "player");
    bool const hasRank = !lua_isnoneornil(L, 3);
    uint32_t const rank = hasRank ? argU8(L, 3, "rankId") : 0;
    return actionOp(L, kGuild, "guild.addMember", "AddMember",
                    {EventValue(player), EventValue(static_cast<int64_t>(rank)), EventValue(hasRank)});
}
// Guild:DeleteMember(player [, isDisbanding])
int guDeleteMember(lua_State* L)
{
    fl::HandleData const& player = objArg(L, 2, kPlayer, "player");
    bool const isDisbanding = optBool(L, 3, false);
    return actionOp(L, kGuild, "guild.deleteMember", "DeleteMember", {EventValue(player), EventValue(isDisbanding)});
}
// Guild:Disband()
int guDisband(lua_State* L) { return actionOp(L, kGuild, "guild.disband", "Disband"); }
// Guild:SetMemberRank(player, rankId)
int guSetMemberRank(lua_State* L)
{
    fl::HandleData const& player = objArg(L, 2, kPlayer, "player");
    uint32_t const rank = argU8(L, 3, "rankId");
    return actionOp(L, kGuild, "guild.setMemberRank", "SetMemberRank",
                    {EventValue(player), EventValue(static_cast<int64_t>(rank))});
}
// Guild:SetLeader(player)
// The pinned core changes a guild leader only through Guild::HandleSetNewGuildMaster, which needs the old leader's
// session and applies that leader's client permission checks; there is no server-side setter the module may call.
int guSetLeader(lua_State* L)
{
    return luaL_error(L, "SetLeader: not available on this server (the core has no public guild leader setter)");
}
// Guild:SendPacket(packet)
int guSendPacket(lua_State* L)
{
    fl::HandleData const& h = self(L, kGuild);
    sendPacketArg(L, h, 2, "SendPacket");
    return 0;
}
// Guild:SendPacketToRanked(packet, rankId)
int guSendPacketToRanked(lua_State* L)
{
    fl::HandleData const& h = self(L, kGuild);
    uint32_t const rank = argU8(L, 3, "rankId");
    sendPacketToRankArg(L, h, 2, rank, "SendPacketToRanked");
    return 0;
}
}  // namespace

void registerGuildMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetName", fl::safe<guGetName>},               {"GetMOTD", fl::safe<guGetMOTD>},
        {"GetInfo", fl::safe<guGetInfo>},               {"GetId", fl::safe<guGetId>},
        {"GetMembers", fl::safe<guGetMembers>},         {"GetMemberCount", fl::safe<guGetMemberCount>},
        {"GetLeaderGUID", fl::safe<guGetLeaderGUID>},   {"GetLeader", fl::safe<guGetLeader>},
        {"AddMember", fl::safe<guAddMember>},           {"DeleteMember", fl::safe<guDeleteMember>},
        {"Disband", fl::safe<guDisband>},               {"SetLeader", fl::safe<guSetLeader>},
        {"SetMemberRank", fl::safe<guSetMemberRank>},   {"SendPacket", fl::safe<guSendPacket>},
        {"SendPacketToRanked", fl::safe<guSendPacketToRanked>},
        {nullptr, nullptr}};
    addMethods(L, "Guild", regs);
}
}  // namespace flu
