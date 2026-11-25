// Eluna Player gossip methods of E1 (docs: Player/index.html Gossip*) and the gossip event payloads. The menu itself is
// the core's GossipMenu (PlayerMenu), built by the host from these calls with sender/action and a synthetic option id.
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

int gMenuAddItem(lua_State* L)
{
    std::vector<EventValue> args{
        EventValue(static_cast<int64_t>(argUInt(L, 2, "icon"))), EventValue(argStr(L, 3, "msg")),
        EventValue(static_cast<int64_t>(argUInt(L, 4, "sender"))),
        EventValue(static_cast<int64_t>(argUInt(L, 5, "intid"))), EventValue(optBool(L, 6, false))};
    args.emplace_back(lua_isnoneornil(L, 7) ? EventValue() : EventValue(argStr(L, 7, "popup")));
    args.emplace_back(EventValue(static_cast<int64_t>(optUInt(L, 8, 0))));
    return actionOp(L, kPlayer, "gossip.addItem", "GossipMenuAddItem", args);
}
int gSendMenu(lua_State* L)
{
    std::vector<EventValue> args{EventValue(static_cast<int64_t>(argUInt(L, 2, "npc_text")))};
    args.emplace_back(objArg(L, 3, kObject, "sender"));
    if (!lua_isnoneornil(L, 4))
        args.emplace_back(static_cast<int64_t>(argUInt(L, 4, "menu_id")));
    else
        args.emplace_back(static_cast<int64_t>(0));
    return actionOp(L, kPlayer, "gossip.sendMenu", "GossipSendMenu", args);
}
int gComplete(lua_State* L) { return actionOp(L, kPlayer, "gossip.complete", "GossipComplete"); }
int gClearMenu(lua_State* L) { return actionOp(L, kPlayer, "gossip.clear", "GossipClearMenu"); }
int gAddQuests(lua_State* L)
{
    fl::HandleData const& source = objArg(L, 2, kWorldObject, "source");
    return actionOp(L, kPlayer, "gossip.addQuests", "GossipAddQuests", {EventValue(source)});
}
}  // namespace

void registerGossipMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GossipMenuAddItem", fl::safe<gMenuAddItem>},
        {"GossipSendMenu", fl::safe<gSendMenu>},
        {"GossipComplete", fl::safe<gComplete>},
        {"GossipClearMenu", fl::safe<gClearMenu>},
        {"GossipAddQuests", fl::safe<gAddQuests>},
        {nullptr, nullptr}};
    addMethods(L, "Player", regs);
}
}  // namespace flu
