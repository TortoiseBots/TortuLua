// E2: group, guild and map/instance events, the Item/Group/Guild/Quest/Spell/Aura classes and the E2 globals, on the
// fake world. The event argument lists are the docs ones; the live adapters (ScriptMgr hooks) are core code exercised by
// the game-side build and the live selftest (docs/E2-E3-NOTES.md).
#include "Support.h"

#include "EluRouter.h"
#include "engine/Elu.h"
#include "engine/GuidText.h"

#include <lua.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

using namespace fltest;

namespace
{
fl::Config elunaConfig(fl::Config c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    return c;
}

struct ElunaRig : Rig
{
    flh::EluRouter router;
    ElunaRig()
        : Rig([](fl::Config& c) { c = elunaConfig(c); }), router([this](fl::EluKind, std::optional<flh::MapKey>) { return unowned(e()); })
    {
    }
};

fl::HandleData groupHandle(uint64_t id)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Group;
    h.guid.hi = 1;
    h.guid.lo = id;
    return h;
}
fl::HandleData guildHandle(uint64_t id)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Guild;
    h.guid.lo = id;
    return h;
}
fl::HandleData questHandle(uint32_t id)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Quest;
    h.guid.lo = id;
    return h;
}
fl::HandleData mapHandle(uint32_t mapId, uint32_t instanceId)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Map;
    h.mapId = mapId;
    h.instanceId = instanceId;
    return h;
}
fl::HandleData spellHandle(uint64_t caster, uint32_t spellId, uint32_t castId)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Spell;
    h.guid = FakeHost::makeGuid(FakeHost::kTypePlayer, caster);
    h.mapId = spellId;
    h.instanceId = castId;
    return h;
}

// A handle made visible to the scripts of the state (a Spell/Aura/Packet has no producer in E2).
void setGlobal(ElunaRig& rig, char const* name, fl::HandleData const& h)
{
    lua_State* L = rig.e().state();
    int top = lua_gettop(L);
    fl::eluPushHandle(L, h);
    lua_setglobal(L, name);
    lua_settop(L, top);
}

std::string errorOf(Rig& rig, std::string const& code)
{
    fl::CallResult r = rig.run(code);
    return r.ok ? std::string("no error") : r.error;
}

void addObject(ElunaRig& rig, fl::HandleData const& h, std::string const& name, uint32_t entry = 0)
{
    fl::ObjectInfo info;
    info.name = name;
    info.entry = entry;
    rig.host.add(h, info);
}

fl::DbRows oneRow(std::vector<fl::DbValue> values, std::vector<std::string> columns = {"value"})
{
    fl::DbRows rows;
    rows.columns = std::move(columns);
    rows.rows.push_back(std::move(values));
    return rows;
}
}  // namespace

TEST_CASE("e2 group events: docs arguments, cancel function, ClearGroupEvents, C10 gap")
{
    ElunaRig rig;
    fl::HandleData g = groupHandle(5);
    fl::HandleData p = FakeHost::player(9);
    addObject(rig, g, "Party");
    addObject(rig, p, "Member");

    rig.ok("group_add, group_remove, group_disband = nil, nil, 0\n"
           "local cancel = RegisterGroupEvent(1, function(ev, group, guid) group_add = {ev, tostring(group), guid} end)\n"
           "RegisterGroupEvent(3, function(ev, group, guid, method, kicker, reason)\n"
           "  group_remove = {ev, method, kicker, reason} end)\n"
           "RegisterGroupEvent(5, function(ev, group) group_disband = group_disband + 1 end)\n"
           "RegisterGroupEvent(6, function() end)   -- on_create: needs C10\n"
           "local cancel_accept = RegisterGroupEvent(7, function() end)   -- on_member_accept: needs C10\n"
           "assert(type(cancel) == 'function')\n"
           "assert(type(cancel_accept) == 'function')\n"
           "cancel()\n"
           "cancel_accept()");

    rig.router.group(1, {fl::EventValue(g), fl::EventValue("0000000000000000000000000000000B")});
    rig.router.group(3, {fl::EventValue(g), fl::EventValue("0000000000000000000000000000000B"), fl::EventValue(1),
                         fl::EventValue("0000000000000000000000000000000C"), fl::EventValue("bye")});
    rig.router.group(5, {fl::EventValue(g)});
    // The cancelled handler did not run.
    CHECK(rig.ok("return tostring(group_add)") == "nil");
    CHECK(rig.ok("return group_remove[1] .. ',' .. group_remove[2] .. ',' .. group_remove[4]") == "3,1,bye");
    CHECK(rig.ok("return group_disband") == "1");
    CHECK(rig.host.logContains("event group CREATE needs core hook C10"));
    CHECK(rig.host.logContains("event group MEMBER_ACCEPT needs core hook C10"));
    size_t const logsBeforeRepeat = rig.host.logs.size();
    rig.ok("RegisterGroupEvent(7, function() end)");
    CHECK(rig.host.logs.size() == logsBeforeRepeat);

    rig.ok("ClearGroupEvents()");
    rig.router.group(5, {fl::EventValue(g)});
    CHECK(rig.ok("return group_disband") == "1");
}

TEST_CASE("e2 guild events: docs arguments, money returns, classic bank stubs")
{
    ElunaRig rig;
    fl::HandleData g = guildHandle(7);
    fl::HandleData p = FakeHost::player(3);
    addObject(rig, g, "Knights");
    addObject(rig, p, "Leader");
    rig.host.gameResults["guild.getName"] = {fl::EventValue("Knights")};

    rig.ok("names, motd, add, withdraw, deposit = {}, nil, nil, 0, 0\n"
           "RegisterGuildEvent(1, function(ev, guild, player, rank) add = {ev, guild:GetName(), rank} end)\n"
           "RegisterGuildEvent(3, function(ev, guild, text) motd = {ev, text} end)\n"
           "RegisterGuildEvent(5, function(ev, guild, leader, name) names[#names+1] = name end)\n"
           "RegisterGuildEvent(7, function(ev, guild, player, amount, isRepair)\n"
           "  withdraw = amount return 500 end)\n"
           "RegisterGuildEvent(8, function(ev, guild, player, amount) deposit = amount return 250 end)\n"
           "RegisterGuildEvent(9, function() end)   -- guild bank: not on Classic 1.60");

    rig.router.guild(1, {fl::EventValue(g), fl::EventValue(p), fl::EventValue(3)});
    rig.router.guild(3, {fl::EventValue(g), fl::EventValue("new motd")});
    rig.router.guild(5, {fl::EventValue(g), fl::EventValue(p), fl::EventValue("Knights")});
    fl::EventResult r = rig.router.guild(7, {fl::EventValue(g), fl::EventValue(p), fl::EventValue(int64_t(100)),
                                             fl::EventValue(false)});
    CHECK(r.hasValue);
    CHECK(r.value == 500);
    CHECK(rig.router.guild(8, {fl::EventValue(g), fl::EventValue(p), fl::EventValue(int64_t(50))}).value == 250);
    CHECK(rig.ok("return add[1] .. ',' .. add[2] .. ',' .. add[3]") == "1,Knights,3");
    CHECK(rig.ok("return motd[2]") == "new motd");
    CHECK(rig.ok("return withdraw .. ',' .. deposit") == "100,50");
    CHECK(rig.ok("return names[1]") == "Knights");
    CHECK(rig.host.logContains("not available on Classic 1.60"));
}

TEST_CASE("e2 map events: the map id and the server map events (instance maps are a core hook gap, see the review tests)")
{
    ElunaRig rig;
    fl::HandleData map = mapHandle(1, 33);
    fl::HandleData p = FakeHost::player(4, 1, 33);
    addObject(rig, map, "Testmap");
    addObject(rig, mapHandle(0, 0), "Elwynn");
    addObject(rig, p, "Visitor");

    rig.ok("map_init, server_create, map_update = 0, 0, nil\n"
           "RegisterMapEvent(1, 1, function(ev, data, m) map_init = {ev, tostring(data), m:GetMapId() .. ':' .. m:GetInstanceId()} end)\n"
           "RegisterServerEvent(17, function(ev, m) server_create = m:GetMapId() end)\n"
           "RegisterMapEvent(1, 2, function() end)    -- LOAD: needs C11\n"
           "RegisterMapEvent(1, 3, function(ev, data, m, diff) map_update = diff end)");
    rig.router.mapEvent(1, 33, true, 1, 17, {fl::EventValue(), fl::EventValue(map)}, {fl::EventValue(map)});
    CHECK(rig.ok("return map_init[1] .. ',' .. map_init[2] .. ',' .. map_init[3]") == "1,nil,1:33");
    CHECK(rig.ok("return server_create") == "1");
    CHECK(rig.host.logContains("event map LOAD needs core hook C11"));

    rig.ok("RegisterMapEvent(0, 1, function(ev, data, m) world_init = m:GetMapId() end)");
    // A world map's own map-id handler runs.
    rig.ok("world_init = -1");
    rig.router.mapEvent(0, 0, false, 1, 17, {fl::EventValue(), fl::EventValue(mapHandle(0, 0))},
                        {fl::EventValue(mapHandle(0, 0))});
    CHECK(rig.ok("return world_init") == "0");
    rig.ok("local none = RegisterMapEvent(1, 1, function() end) assert(none == nil)");   // docs: returns nothing
}

TEST_CASE("e2 Item methods: count, owner, slots, link and a gone item")
{
    ElunaRig rig;
    fl::HandleData item;
    item.kind = fl::ObjKind::Item;
    item.guid = FakeHost::makeGuid(FakeHost::kTypeItem, 55, 2000);
    fl::HandleData owner = FakeHost::player(8);
    addObject(rig, item, "Sword", 2000);
    addObject(rig, owner, "Owner");

    rig.host.gameResults["item.getCount"] = {fl::EventValue(int64_t(3))};
    rig.host.gameResults["item.setCount"] = {};
    rig.host.gameResults["item.getEntry"] = {fl::EventValue(int64_t(2000))};
    rig.host.gameResults["item.getItemLink"] = {fl::EventValue("|cffffffff|Hitem:2000|h[x]|h|r")};
    rig.host.gameResults["item.getOwner"] = {fl::EventValue(owner)};
    rig.host.gameResults["item.isEquipped"] = {fl::EventValue(true)};
    rig.host.gameResults["item.getBagSlot"] = {fl::EventValue(int64_t(1))};
    rig.host.gameResults["item.getSlot"] = {fl::EventValue(int64_t(7))};
    setGlobal(rig, "item", item);
    rig.ok("count = item:GetCount() owner = item:GetOwner() equipped = item:IsEquipped()\n"
           "bag = item:GetBagSlot() slot = item:GetSlot() link = item:GetItemLink()");
    CHECK(rig.ok("return count .. ',' .. owner:GetGUIDLow() .. ',' .. tostring(equipped) .. ',' .. bag .. ',' .. slot") ==
          "3,8,true,1,7");
    CHECK(rig.ok("return link") == "|cffffffff|Hitem:2000|h[x]|h|r");
    CHECK(rig.ok("return item:GetEntry()") == "2000");

    rig.ok("item:SetCount(5)");
    CHECK(rig.host.gameCalls.back() == "item.setCount");
    CHECK(std::count(rig.host.gameCalls.begin(), rig.host.gameCalls.end(), "item.setCount") == 1);

    rig.host.remove(item);
    std::string err = errorOf(rig, "return item:GetCount()");
    CHECK(err.find("Item no longer exists") != std::string::npos);
}

TEST_CASE("e2 Group methods: members, leader, raid, add/remove member, send packet")
{
    ElunaRig rig;
    fl::HandleData g = groupHandle(5);
    fl::HandleData a = FakeHost::player(1);
    fl::HandleData b = FakeHost::player(2);
    addObject(rig, g, "Party");
    addObject(rig, a, "A");
    addObject(rig, b, "B");

    rig.host.gameResults["group.getMembers"] = {fl::EventValue(a), fl::EventValue(b)};
    rig.host.gameResults["group.getLeaderGuid"] = {fl::EventValue(fl::guidText(a.guid))};
    rig.host.gameResults["group.isRaidGroup"] = {fl::EventValue(true)};
    rig.host.gameResults["group.addMember"] = {fl::EventValue(true)};
    rig.host.gameResults["group.removeMember"] = {fl::EventValue(true)};
    setGlobal(rig, "group", g);
    setGlobal(rig, "member", b);

    rig.ok("packet = CreatePacket(0x40, 0) packet:WriteUByte(9)\n"
           "members = group:GetMembers() leader = group:GetLeaderGUID()\n"
           "raid = group:IsRaidGroup() added = group:AddMember(member)\n"
           "removed = group:RemoveMember(member:GetGUID(), 1)\n"
           "group:SendPacket(packet, true, member:GetGUIDLow())");
    CHECK(rig.ok("return #members .. ',' .. members[1]:GetGUIDLow() .. ',' .. members[2]:GetGUIDLow()") == "2,1,2");
    CHECK(rig.ok("return leader") == fl::guidText(a.guid));
    CHECK(rig.ok("return tostring(raid) .. ',' .. tostring(added) .. ',' .. tostring(removed)") == "true,true,true");
    // E5: SendPacket sends the packet value itself (not through a gameCall op).
    REQUIRE(rig.host.packets.size() == 1);
    CHECK(rig.host.packets.back().target.kind == fl::ObjKind::Group);
    CHECK(rig.host.packets.back().opcode == 0x40);
    CHECK(rig.host.packets.back().ignorePlayersInBg);
    CHECK(rig.host.packets.back().ignore == 2);

    rig.host.remove(g);
    CHECK(errorOf(rig, "return group:GetMembers()").find("Group no longer exists") != std::string::npos);
}

TEST_CASE("e2 Guild methods: name, members, leader, send packet")
{
    ElunaRig rig;
    fl::HandleData g = guildHandle(9);
    fl::HandleData p = FakeHost::player(2);
    addObject(rig, g, "Knights");
    addObject(rig, p, "Leader");

    rig.host.gameResults["guild.getName"] = {fl::EventValue("Knights of the Test")};
    rig.host.gameResults["guild.getMembers"] = {fl::EventValue(p)};
    rig.host.gameResults["guild.getLeaderGuid"] = {fl::EventValue(fl::guidText(p.guid))};
    setGlobal(rig, "guild", g);
    rig.ok("packet = CreatePacket(0x41, 1)\n"
           "name = guild:GetName() members = guild:GetMembers() leader = guild:GetLeaderGUID()\n"
           "guild:SendPacket(packet)");
    CHECK(rig.ok("return name") == "Knights of the Test");
    CHECK(rig.ok("return #members .. ',' .. members[1]:GetGUIDLow()") == "1,2");
    CHECK(rig.ok("return leader") == fl::guidText(p.guid));
    REQUIRE(rig.host.packets.size() == 1);
    CHECK(rig.host.packets.back().target.kind == fl::ObjKind::Guild);
    CHECK(rig.host.packets.back().opcode == 0x41);

    rig.host.remove(g);
    CHECK(errorOf(rig, "return guild:GetName()").find("Guild no longer exists") != std::string::npos);
}

TEST_CASE("e2 Group long tail: guid, counts, member queries, flags and subgroups")
{
    ElunaRig rig;
    fl::HandleData g = groupHandle(5);
    fl::HandleData a = FakeHost::player(1);
    fl::HandleData b = FakeHost::player(2);
    addObject(rig, g, "Party");
    addObject(rig, a, "A");
    addObject(rig, b, "B");

    auto text = [](fl::EventValue const& v) {
        auto const* s = std::get_if<std::string>(&v.v);
        return s ? *s : std::string();
    };
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const& args,
                              std::vector<fl::EventValue>& out) {
        if (op == "group.getGuid")
            out = {fl::EventValue(fl::guidText(g.guid))};
        else if (op == "group.getMembersCount")
            out = {fl::EventValue(int64_t(3))};
        else if (op == "group.getMemberGuid")
        {
            if (text(args[0]) == "A")
                out = {fl::EventValue(fl::guidText(a.guid))};
        }
        else if (op == "group.getMemberFlags")
            out = {fl::EventValue(int64_t(1))};
        else if (op == "group.getMemberGroup")
            out = {fl::EventValue(int64_t(2))};
        else if (op == "group.hasFreeSlotSubGroup" || op == "group.isAssistant" || op == "group.isLeader" ||
                 op == "group.isMember" || op == "group.sameSubGroup" || op == "group.isLfgGroup")
            out = {fl::EventValue(true)};
        else if (op == "group.isBgGroup" || op == "group.isBfGroup" || op == "group.isFull")
            out = {fl::EventValue(false)};
        else
            return false;
        return true;
    };
    setGlobal(rig, "group", g);
    setGlobal(rig, "a", a);
    setGlobal(rig, "b", b);

    rig.ok("guid = group:GetGUID() count = group:GetMembersCount() member = group:GetMemberGUID('A') "
           "missing = group:GetMemberGUID('Nobody') flags = group:GetMemberFlags(a:GetGUID()) "
           "subgroup = group:GetMemberGroup(a:GetGUID()) free = group:HasFreeSlotSubGroup(3) "
           "assistant = group:IsAssistant(a:GetGUID()) leader = group:IsLeader(a:GetGUID()) "
           "member2 = group:IsMember(a:GetGUID()) same = group:SameSubGroup(a, b) "
           "lfg = group:IsLFGGroup() bg = group:IsBGGroup() bf = group:IsBFGroup() full = group:IsFull()");
    CHECK(rig.ok("return guid") == fl::guidText(g.guid));
    CHECK(rig.ok("return count") == "3");
    CHECK(rig.ok("return member") == fl::guidText(a.guid));
    CHECK(rig.ok("return tostring(missing)") == "nil");
    CHECK(rig.ok("return flags") == "1");
    CHECK(rig.ok("return subgroup") == "2");
    CHECK(rig.ok("return tostring(free)") == "true");
    CHECK(rig.ok("return tostring(assistant) .. ',' .. tostring(leader) .. ',' .. tostring(member2) .. ',' .. "
                 "tostring(same)") == "true,true,true,true");
    CHECK(rig.ok("return tostring(lfg) .. ',' .. tostring(bg) .. ',' .. tostring(bf) .. ',' .. tostring(full)") ==
          "true,false,false,false");

    // The subgroup index bounds the core's array.
    CHECK(errorOf(rig, "return group:HasFreeSlotSubGroup(8)").find("0..7") != std::string::npos);

    // A map-thread handler must not walk a membership that the world thread may be changing.
    rig.host.worldReadOk = false;
    CHECK(errorOf(rig, "return group:GetMembersCount()").find("only available on the world thread") != std::string::npos);
    CHECK(errorOf(rig, "return group:GetMemberFlags(a:GetGUID())").find("only available on the world thread") !=
          std::string::npos);
    CHECK(errorOf(rig, "return group:SameSubGroup(a, b)").find("only available on the world thread") != std::string::npos);
    CHECK(errorOf(rig, "return group:IsFull()").find("only available on the world thread") != std::string::npos);
    CHECK(errorOf(rig, "return group:GetLeaderGUID()").find("only available on the world thread") != std::string::npos);
    rig.host.worldReadOk = true;

    rig.host.remove(g);
    CHECK(errorOf(rig, "return group:GetGUID()").find("Group no longer exists") != std::string::npos);
    CHECK(errorOf(rig, "return group:GetMembersCount()").find("Group no longer exists") != std::string::npos);
}

TEST_CASE("e2 Group writes: each write is dispatched with its documented arguments")
{
    ElunaRig rig;
    fl::HandleData g = groupHandle(5);
    fl::HandleData a = FakeHost::player(1);
    fl::HandleData b = FakeHost::player(2);
    addObject(rig, g, "Party");
    addObject(rig, a, "A");
    addObject(rig, b, "B");

    std::string op;
    std::vector<fl::EventValue> args;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& name, std::vector<fl::EventValue> const& a,
                              std::vector<fl::EventValue>&) {
        op = name;
        args = a;
        return true;
    };
    setGlobal(rig, "group", g);
    setGlobal(rig, "a", a);
    setGlobal(rig, "b", b);
    auto call = [&](char const* lua) {
        op.clear();
        args.clear();
        rig.ok(lua);
        return op;
    };
    auto text = [](fl::EventValue const& v) {
        auto const* s = std::get_if<std::string>(&v.v);
        return s ? *s : std::string();
    };
    auto number = [](fl::EventValue const& v) {
        auto const* i = std::get_if<int64_t>(&v.v);
        return i ? *i : int64_t(-999);
    };
    auto flag = [](fl::EventValue const& v) {
        auto const* b = std::get_if<bool>(&v.v);
        return b && *b;
    };

    SECTION("ConvertToLFG")
    {
        CHECK(call("group:ConvertToLFG()") == "group.convertToLfg");
    }
    SECTION("ConvertToRaid")
    {
        CHECK(call("group:ConvertToRaid()") == "group.convertToRaid");
    }
    SECTION("SetLeader")
    {
        CHECK(call("group:SetLeader(b:GetGUID())") == "group.setLeader");
        CHECK(text(args[0]) == fl::guidText(b.guid));
    }
    SECTION("SetMemberFlag")
    {
        CHECK(call("group:SetMemberFlag(a:GetGUID(), true, 1)") == "group.setMemberFlag");
        CHECK(text(args[0]) == fl::guidText(a.guid));
        CHECK(flag(args[1]));
        CHECK(number(args[2]) == 1);
        // The flag set is the documented one.
        CHECK(errorOf(rig, "return group:SetMemberFlag(a:GetGUID(), true, 3)").find("1, 2 or 4") != std::string::npos);
    }
    SECTION("SetMembersGroup")
    {
        CHECK(call("group:SetMembersGroup(a:GetGUID(), 2)") == "group.setMembersGroup");
        CHECK(text(args[0]) == fl::guidText(a.guid));
        CHECK(number(args[1]) == 2);
        // The subgroup index bounds the core's array.
        CHECK(errorOf(rig, "return group:SetMembersGroup(a:GetGUID(), 8)").find("0..7") != std::string::npos);
    }
    SECTION("SetTargetIcon")
    {
        CHECK(call("group:SetTargetIcon(7, b:GetGUID(), a:GetGUID())") == "group.setTargetIcon");
        CHECK(number(args[0]) == 7);
        CHECK(text(args[1]) == fl::guidText(b.guid));
        CHECK(text(args[2]) == fl::guidText(a.guid));
        // The icon index bounds the core's array.
        CHECK(errorOf(rig, "return group:SetTargetIcon(8, 0, 0)").find("0..7") != std::string::npos);
    }
    SECTION("Disband")
    {
        CHECK(call("group:Disband()") == "group.disband");
    }
}

TEST_CASE("e2 Group write: a group disbanded before the call refuses the write and dispatches nothing")
{
    ElunaRig rig;
    fl::HandleData g = groupHandle(5);
    addObject(rig, g, "Party");
    setGlobal(rig, "group", g);
    rig.host.remove(g);
    CHECK(errorOf(rig, "return group:Disband()").find("Group no longer exists") != std::string::npos);
    CHECK(rig.host.gameCalls.empty());
}

TEST_CASE("e2 Guild long tail: id, info, motd, leader, count and a ranked send")
{
    ElunaRig rig;
    fl::HandleData g = guildHandle(9);
    fl::HandleData a = FakeHost::player(1);
    fl::HandleData b = FakeHost::player(2);
    addObject(rig, g, "Knights");
    addObject(rig, a, "Leader");
    addObject(rig, b, "Member");

    bool leaderOnline = true;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const& args,
                              std::vector<fl::EventValue>& out) {
        if (op == "guild.getId")
            out = {fl::EventValue(int64_t(9))};
        else if (op == "guild.getMotd")
            out = {fl::EventValue("Be nice")};
        else if (op == "guild.getInfo")
            out = {fl::EventValue("Recruiting")};
        else if (op == "guild.getMemberCount")
            out = {fl::EventValue(int64_t(12))};
        else if (op == "guild.getLeader")
        {
            if (leaderOnline)
                out = {fl::EventValue(a)};
        }
        else
            return false;
        return true;
    };
    setGlobal(rig, "guild", g);
    setGlobal(rig, "a", a);
    setGlobal(rig, "b", b);

    rig.ok("id = guild:GetId() motd = guild:GetMOTD() info = guild:GetInfo() count = guild:GetMemberCount() "
           "leader = guild:GetLeader() packet = CreatePacket(0x42, 0) packet:WriteUByte(3) "
           "guild:SendPacketToRanked(packet, 2)");
    CHECK(rig.ok("return id") == "9");
    CHECK(rig.ok("return motd") == "Be nice");
    CHECK(rig.ok("return info") == "Recruiting");
    CHECK(rig.ok("return count") == "12");
    CHECK(rig.ok("return leader:GetGUIDLow()") == "1");
    REQUIRE(rig.host.packets.size() == 1);
    CHECK(rig.host.packets.back().target.kind == fl::ObjKind::Guild);
    CHECK(rig.host.packets.back().opcode == 0x42);
    CHECK(rig.host.packets.back().rank == 2);
    CHECK(rig.host.packets.back().toRank == 1);

    leaderOnline = false;
    CHECK(rig.ok("return tostring(guild:GetLeader())") == "nil");

    rig.host.worldReadOk = false;
    CHECK(errorOf(rig, "return guild:GetMemberCount()").find("only available on the world thread") != std::string::npos);
    CHECK(errorOf(rig, "return guild:GetLeader()").find("only available on the world thread") != std::string::npos);
    CHECK(errorOf(rig, "return guild:GetName()").find("only available on the world thread") != std::string::npos);
    CHECK(errorOf(rig, "return guild:GetId()").find("only available on the world thread") != std::string::npos);
    rig.host.worldReadOk = true;

    rig.host.remove(g);
    CHECK(errorOf(rig, "return guild:GetId()").find("Guild no longer exists") != std::string::npos);
}

TEST_CASE("e2 Guild writes: each membership write is dispatched with its documented arguments")
{
    ElunaRig rig;
    fl::HandleData g = guildHandle(9);
    fl::HandleData a = FakeHost::player(1);
    fl::HandleData b = FakeHost::player(2);
    addObject(rig, g, "Knights");
    addObject(rig, a, "Leader");
    addObject(rig, b, "Member");

    std::string op;
    std::vector<fl::EventValue> args;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& name, std::vector<fl::EventValue> const& a,
                              std::vector<fl::EventValue>&) {
        op = name;
        args = a;
        return true;
    };
    setGlobal(rig, "guild", g);
    setGlobal(rig, "a", a);
    setGlobal(rig, "b", b);
    auto call = [&](char const* lua) {
        op.clear();
        args.clear();
        rig.ok(lua);
        return op;
    };
    auto text = [](fl::EventValue const& v) {
        auto const* s = std::get_if<std::string>(&v.v);
        return s ? *s : std::string();
    };
    auto number = [](fl::EventValue const& v) {
        auto const* i = std::get_if<int64_t>(&v.v);
        return i ? *i : int64_t(-999);
    };
    auto flag = [](fl::EventValue const& v) {
        auto const* b = std::get_if<bool>(&v.v);
        return b && *b;
    };
    auto handleText = [](fl::EventValue const& v) {
        auto const* h = std::get_if<fl::HandleData>(&v.v);
        return h ? fl::guidText(h->guid) : std::string();
    };

    SECTION("AddMember with a rank")
    {
        CHECK(call("guild:AddMember(a, 3)") == "guild.addMember");
        CHECK(handleText(args[0]) == fl::guidText(a.guid));
        CHECK(number(args[1]) == 3);
        CHECK(flag(args[2]));
    }
    SECTION("AddMember without a rank")
    {
        CHECK(call("guild:AddMember(b)") == "guild.addMember");
        CHECK(handleText(args[0]) == fl::guidText(b.guid));
        CHECK(number(args[1]) == 0);
        CHECK(!flag(args[2]));   // no rank: the core assigns the lowest
    }
    SECTION("DeleteMember")
    {
        CHECK(call("guild:DeleteMember(a, true)") == "guild.deleteMember");
        CHECK(handleText(args[0]) == fl::guidText(a.guid));
        CHECK(flag(args[1]));
    }
    SECTION("SetMemberRank")
    {
        CHECK(call("guild:SetMemberRank(b, 2)") == "guild.setMemberRank");
        CHECK(handleText(args[0]) == fl::guidText(b.guid));
        CHECK(number(args[1]) == 2);
    }
    SECTION("Disband")
    {
        CHECK(call("guild:Disband()") == "guild.disband");
    }
    SECTION("SetLeader is a documented stub")
    {
        // The core changes a guild leader only from the old leader's client session.
        CHECK(errorOf(rig, "return guild:SetLeader(a)").find("not available on this server") != std::string::npos);
        CHECK(rig.host.gameCalls.empty());
    }
}

TEST_CASE("e2 Guild write: a guild disbanded before the call refuses the write and dispatches nothing")
{
    ElunaRig rig;
    fl::HandleData g = guildHandle(9);
    fl::HandleData a = FakeHost::player(1);
    addObject(rig, g, "Knights");
    addObject(rig, a, "Leader");
    setGlobal(rig, "guild", g);
    setGlobal(rig, "a", a);
    rig.host.remove(g);
    CHECK(errorOf(rig, "return guild:AddMember(a, 1)").find("Guild no longer exists") != std::string::npos);
    CHECK(rig.host.gameCalls.empty());
}

TEST_CASE("e2 Quest methods: id, level, title, daily, and a quest that is gone")
{
    ElunaRig rig;
    fl::HandleData q = questHandle(100);
    addObject(rig, q, "A quest");
    rig.host.gameResults["quest.getId"] = {fl::EventValue(int64_t(100))};
    rig.host.gameResults["quest.getLevel"] = {fl::EventValue(int64_t(12))};
    rig.host.gameResults["quest.getTitle"] = {fl::EventValue("The Test Quest")};
    rig.host.gameResults["quest.isDaily"] = {fl::EventValue(true)};
    setGlobal(rig, "quest", q);
    rig.ok("id = quest:GetId() level = quest:GetLevel() title = quest:GetTitle() daily = quest:IsDaily()");
    CHECK(rig.ok("return id .. ',' .. level .. ',' .. title .. ',' .. tostring(daily)") == "100,12,The Test Quest,true");

    rig.host.remove(q);
    CHECK(errorOf(rig, "return quest:GetId()").find("Quest no longer exists") != std::string::npos);
}

TEST_CASE("e2 Spell and Aura: transient handles live for the call that produced them")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(6);
    addObject(rig, p, "Caster");
    rig.host.onGameCall = [&rig](fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const&,
                                 std::vector<fl::EventValue>& out)
    {
        if (!rig.host.transientActive)
            return false;   // the call that produced the object is over
        if (self.kind == fl::ObjKind::Player && op == "unit.addAura")
        {
            fl::HandleData aura;
            aura.kind = fl::ObjKind::Aura;
            aura.guid = self.guid;
            aura.mapId = 123;
            aura.instanceId = 7;
            out.push_back(fl::EventValue(aura));
            return true;
        }
        if (self.kind == fl::ObjKind::Aura)
        {
            if (op == "aura.getAuraId")
                out.push_back(fl::EventValue(int64_t(123)));
            else if (op == "aura.getDuration")
                out.push_back(fl::EventValue(int64_t(5000)));
            else if (op == "aura.setDuration")
                out.push_back(fl::EventValue());
            return true;
        }
        if (self.kind == fl::ObjKind::Spell)
        {
            if (op == "spell.getEntry")
                out.push_back(fl::EventValue(int64_t(4242)));
            else if (op == "spell.getDuration")
                out.push_back(fl::EventValue(int64_t(1500)));
            return true;
        }
        return false;
    };

    // Aura: Unit:AddAura returns one; the methods work inside the handler, then the handle is dead.
    rig.ok("stored_aura = nil\n"
           "RegisterPlayerEvent(3, function(ev, player)\n"
           "  local unit = player\n"
           "  local aura = unit:AddAura(123)\n"
           "  stored_aura = aura\n"
           "  aura_id = aura:GetAuraId()\n"
           "  duration = aura:GetDuration()\n"
           "  aura:SetDuration(1000)\n"
           "end)");
    rig.host.transientActive = true;
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    CHECK(rig.ok("return aura_id") == "123");
    CHECK(rig.ok("return duration") == "5000");
    CHECK(rig.host.endLuaCalls >= 1);
    CHECK(errorOf(rig, "return stored_aura:GetDuration()").find("Aura no longer exists") != std::string::npos);

    // Spell: the object the spell event passes; the same lifetime rule.
    rig.ok("stored_spell = nil\n"
           "RegisterPlayerEvent(5, function(ev, player, spell, skip)\n"
           "  stored_spell = spell\n"
           "  spell_entry = spell:GetEntry()\n"
           "  spell_duration = spell:GetDuration()\n"
           "end)");
    rig.host.transientActive = true;
    rig.e().emitEluna(fl::EluKind::Player, 0, 5, {fl::EventValue(p), fl::EventValue(spellHandle(6, 4242, 1)),
                                                  fl::EventValue(false)});
    CHECK(rig.ok("return spell_entry .. ',' .. spell_duration") == "4242,1500");
    CHECK(errorOf(rig, "return stored_spell:GetEntry()").find("Spell no longer exists") != std::string::npos);
}

TEST_CASE("e2 globals: guild lookups, saves, kick, ban, mail, vendors, game events")
{
    ElunaRig rig;
    fl::HandleData g = guildHandle(12);
    fl::HandleData p = FakeHost::player(2);
    addObject(rig, p, "Victim");
    rig.host.players.push_back(p);
    addObject(rig, g, "Knights");
    rig.host.guildsByName["Knights"] = g;
    rig.host.guildsByLeaderLow[2] = g;
    rig.host.gameResults["guild.getName"] = {fl::EventValue("Knights")};
    rig.host.banResult = 3;
    rig.host.mailItemGuids = {900, 901};
    rig.host.activeEvents = {1, 5};

    rig.ok("by_name = GetGuildByName('Knights')\n"
           "by_leader = GetGuildByLeaderGUID(2)\n"
           "SaveAllPlayers() Kick(GetPlayerByGUID('" + fl::guidText(p.guid) + "'))\n"
           "ban = Ban(0, 'cheater', 3600, 'why', 'tester')\n"
           "g1, g2 = SendMail('subject', 'body', 2, 3, 41, 0, 100, 0, 200, 2)\n"
           "AddVendorItem(7, 200, 5, 60, 0) VendorRemoveItem(7, 200) VendorRemoveAllItems(7)\n"
           "events = GetActiveGameEvents() active = IsGameEventActive(5) inactive = IsGameEventActive(9)\n"
           "StartGameEvent(1, true) StopGameEvent(1)");

    CHECK(rig.ok("return by_name:GetName()") == "Knights");
    CHECK(rig.ok("return by_leader:GetName()") == "Knights");
    CHECK(rig.host.saveAllCalls == 1);
    CHECK(rig.host.kicked.size() == 1);
    CHECK(rig.host.bans.size() == 1);
    CHECK(rig.host.bans[0].mode == 0);
    CHECK(rig.host.bans[0].duration == 3600);
    CHECK(rig.host.bans[0].reason == "why");
    CHECK(rig.ok("return tostring(ban)") == "3");
    rig.host.banResult = -1;
    CHECK(rig.ok("return tostring(Ban(1, 'nobody', 60))") == "nil");
    CHECK(rig.host.bans.size() == 2);
    CHECK(rig.host.mails.size() == 1);
    CHECK(rig.host.mails[0].subject == "subject");
    CHECK(rig.host.mails[0].receiver == 2);
    CHECK(rig.host.mails[0].items.size() == 1);
    CHECK(rig.host.mails[0].items[0].first == 200);
    CHECK(rig.host.mails[0].items[0].second == 2);
    CHECK(rig.ok("return g1 .. ',' .. g2") == "900,901");
    CHECK(rig.host.vendorCalls.size() == 3);
    CHECK(rig.host.vendorCalls[0].op == 0);
    CHECK(rig.host.vendorCalls[0].maxCount == 5);
    CHECK(rig.host.vendorCalls[1].op == 1);
    CHECK(rig.host.vendorCalls[2].op == 2);
    CHECK(rig.ok("return #events .. ',' .. events[1] .. ',' .. events[2]") == "2,1,5");
    CHECK(rig.ok("return tostring(active) .. ',' .. tostring(inactive)") == "true,false");
    CHECK(rig.host.startedEvents.size() == 1);
    CHECK(rig.host.startedEvents[0].second);
    CHECK(rig.host.stoppedEvents.size() == 1);

    // A guild that is gone answers nil.
    rig.host.guildsByName.clear();
    CHECK(rig.ok("return tostring(GetGuildByName('Knights'))") == "nil");

    // SendMail with no item and no mail guid answers nil.
    rig.host.mailItemGuids.clear();
    CHECK(rig.ok("return tostring(SendMail('s', 'b', 2))") == "nil");
}

TEST_CASE("e2 packet registration: the family registers and clears (E5 wires the dispatch)")
{
    ElunaRig rig;
    rig.ok("local c = RegisterPacketEvent(0x100, 5, function(ev, packet, player) end)\n"
           "assert(type(c) == 'function')\n"
           "assert(type(ClearPacketEvents) == 'function')\n"
           "ClearPacketEvents(0x100)");
    CHECK(rig.e().status().elunaHandlers == 0);
}
