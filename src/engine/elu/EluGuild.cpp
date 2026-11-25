// Eluna Guild methods of E2 (docs: Guild/index.html). The handle is {kind Guild, guild id}; the guild resolves through
// GuildMgr on every call, so a handle kept after the guild disbands raises "Guild no longer exists".
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
// Guild:GetName()
int guGetName(lua_State* L) { return readOp(L, kGuild, "guild.getName", "GetName"); }
// Guild:GetMembers(): the players of the guild that are online and in the world (docs).
// World thread only, like Group:GetMembers (another thread may be changing the guild).
int guGetMembers(lua_State* L)
{
    if (!host(L).worldOnlyReadOk())
        return luaL_error(L, "Guild:GetMembers is only available on the world thread (server events, timed events, commands), "
                             "not from a map-thread handler");
    return listOp(L, kGuild, "guild.getMembers", "GetMembers");
}
// Guild:GetLeaderGUID()
int guGetLeaderGUID(lua_State* L) { return readOp(L, kGuild, "guild.getLeaderGuid", "GetLeaderGUID"); }
// Guild:SendPacket(packet)
int guSendPacket(lua_State* L)
{
    fl::HandleData const& h = self(L, kGuild);
    sendPacketArg(L, h, 2, "SendPacket");
    return 0;
}
}  // namespace

void registerGuildMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetName", fl::safe<guGetName>},       {"GetMembers", fl::safe<guGetMembers>},
        {"GetLeaderGUID", fl::safe<guGetLeaderGUID>}, {"SendPacket", fl::safe<guSendPacket>},
        {nullptr, nullptr}};
    addMethods(L, "Guild", regs);
}
}  // namespace flu
