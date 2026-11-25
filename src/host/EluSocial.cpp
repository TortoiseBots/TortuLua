// Group and Guild events of E2: the GroupScript and GuildScript hooks of the pinned core. One script object of each
// family is enough (the hooks are global); the adapter only builds the docs argument lists and hands them to the
// EluRouter, which fires the Group/Guild families on the world thread.
#include "EluRouter.h"
#include "GameAccess.h"
#include "Module.h"

#include "engine/GuidText.h"

#include "Group.h"
#include "Guild.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
using fl::EluKind;
using fl::EventResult;
using fl::EventValue;
using fl::HandleData;

flh::EluRouter const& router()
{
    static flh::EluRouter const r(&flh::eventEngine);
    return r;
}

EventValue txt(std::string const& s) { return EventValue(s); }
EventValue num(int64_t v) { return EventValue(v); }
EventValue flag(bool b) { return EventValue(b); }
EventValue groupValue(Group const* g)
{
    if (!g)
        return EventValue();
    HandleData h;
    h.kind = fl::ObjKind::Group;
    h.guid = flh::fromGuid(g->GetGUID());
    return EventValue(h);
}
EventValue guildValue(Guild const* g)
{
    if (!g)
        return EventValue();
    HandleData h;
    h.kind = fl::ObjKind::Guild;
    h.guid.lo = g->GetId();
    return EventValue(h);
}
EventValue playerValue(Player* p) { return p && p->IsInWorld() ? EventValue(flh::handleFor(p)) : EventValue(); }
EventValue guidValue(ObjectGuid const& g) { return EventValue(fl::guidText(flh::fromGuid(g))); }

class forever_lua_group_elu : public GroupScript
{
public:
    forever_lua_group_elu() : GroupScript("forever_lua_group_elu") { }

    void OnAddMember(Group* group, ObjectGuid guid) override
    {
        if (router().listening(EluKind::Group, 0, 1))
            router().group(1, {groupValue(group), guidValue(guid)});
    }
    void OnInviteMember(Group* group, ObjectGuid guid) override
    {
        if (router().listening(EluKind::Group, 0, 2))
            router().group(2, {groupValue(group), guidValue(guid)});
    }
    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod method, ObjectGuid kicker, char const* reason) override
    {
        if (!router().listening(EluKind::Group, 0, 3))
            return;
        router().group(3, {groupValue(group), guidValue(guid), num(static_cast<int64_t>(method)), guidValue(kicker),
                           reason ? EventValue(std::string(reason)) : EventValue()});
    }
    void OnChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid oldLeaderGuid) override
    {
        if (router().listening(EluKind::Group, 0, 4))
            router().group(4, {groupValue(group), guidValue(newLeaderGuid), guidValue(oldLeaderGuid)});
    }
    void OnDisband(Group* group) override
    {
        if (router().listening(EluKind::Group, 0, 5))
            router().group(5, {groupValue(group)});
    }
};

class forever_lua_guild_elu : public GuildScript
{
public:
    forever_lua_guild_elu() : GuildScript("forever_lua_guild_elu") { }

    void OnAddMember(Guild* guild, Player* player, uint8 plRank) override
    {
        if (router().listening(EluKind::Guild, 0, 1))
            router().guild(1, {guildValue(guild), playerValue(player), num(plRank)});
    }
    void OnRemoveMember(Guild* guild, ObjectGuid guid, bool isDisbanding, bool /*isKicked*/) override
    {
        if (!router().listening(EluKind::Guild, 0, 2))
            return;
        // The docs argument is the player; a removed member may be offline, then it is nil.
        Player* player = ObjectAccessor::FindPlayer(guid);
        (void)guid;
        router().guild(2, {guildValue(guild), playerValue(player), flag(isDisbanding)});
    }
    void OnMOTDChanged(Guild* guild, std::string const& newMotd) override
    {
        if (router().listening(EluKind::Guild, 0, 3))
            router().guild(3, {guildValue(guild), txt(newMotd)});
    }
    void OnInfoChanged(Guild* guild, std::string const& newInfo) override
    {
        if (router().listening(EluKind::Guild, 0, 4))
            router().guild(4, {guildValue(guild), txt(newInfo)});
    }
    void OnCreate(Guild* guild, Player* leader, std::string const& name) override
    {
        if (router().listening(EluKind::Guild, 0, 5))
            router().guild(5, {guildValue(guild), playerValue(leader), txt(name)});
    }
    void OnDisband(Guild* guild) override
    {
        if (router().listening(EluKind::Guild, 0, 6))
            router().guild(6, {guildValue(guild)});
    }
    void OnMemberWitdrawMoney(Guild* guild, Player* player, uint64& amount, bool isRepair) override
    {
        if (!router().listening(EluKind::Guild, 0, 7))
            return;
        EventResult r = router().guild(7, {guildValue(guild), playerValue(player), num(static_cast<int64_t>(amount)),
                                          flag(isRepair)});
        if (r.hasValue)
            amount = static_cast<uint64>(std::max<int64_t>(0, r.value));
    }
    void OnMemberDepositMoney(Guild* guild, Player* player, uint64& amount) override
    {
        if (!router().listening(EluKind::Guild, 0, 8))
            return;
        EventResult r = router().guild(8, {guildValue(guild), playerValue(player), num(static_cast<int64_t>(amount))});
        if (r.hasValue)
            amount = static_cast<uint64>(std::max<int64_t>(0, r.value));
    }
    // 9 ITEM_MOVE and 11 BANK_EVENT never fire on Classic 1.60 (no guild bank): the registrations log that once.
    void OnEvent(Guild* guild, uint8 eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2,
                 uint8 newRank) override
    {
        if (router().listening(EluKind::Guild, 0, 10))
            router().guild(10, {guildValue(guild), num(eventType), num(static_cast<int64_t>(playerGuid1)),
                                num(static_cast<int64_t>(playerGuid2)), num(newRank)});
    }
};
}  // namespace

void AddSC_forever_lua_social()
{
    new forever_lua_group_elu();
    new forever_lua_guild_elu();
}
