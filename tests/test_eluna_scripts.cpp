// The Eluna compatibility scripts of tests/eluna/: our own examples, run unchanged on the fake world. They use only
// the published Eluna API, so the same files are the live selftest material (docs/E0-E1-NOTES.md).
#include "Support.h"

#include "engine/StateSet.h"

#include <string>
#include <vector>

using namespace fltest;

namespace
{
struct ScriptRig : Rig
{
    ScriptRig() : Rig([](fl::Config& c)
                 {
                     c.mode = fl::EngineMode::Eluna;
                     c.scriptPath = FL_TEST_ELUNA_DIR;
                     c.eventBudget = {0, 0};
                     c.commandBudget = {0, 0};
                     c.loadBudget = {0, 0};
                     c.tickBudgetMs = 0;
                 })
    {
    }
};

struct Capture
{
    std::vector<std::pair<std::string, std::vector<fl::EventValue>>> calls;
    void attach(FakeHost& host)
    {
        host.onGameCall = [this, &host](fl::HandleData const&, std::string const& op,
                                        std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
        {
            calls.emplace_back(op, args);
            auto it = host.gameResults.find(op);
            if (it == host.gameResults.end())
                return false;
            out = it->second;
            return true;
        };
    }
    size_t count(std::string const& op) const
    {
        size_t n = 0;
        for (auto const& c : calls)
            n += c.first == op;
        return n;
    }
    std::vector<fl::EventValue> args(std::string const& op, size_t which = 0) const
    {
        size_t n = 0;
        for (auto const& c : calls)
            if (c.first == op)
            {
                if (n++ == which)
                    return c.second;
            }
        return {};
    }
};

std::string textOf(fl::EventValue const& v)
{
    if (auto const* s = std::get_if<std::string>(&v.v))
        return *s;
    return {};
}
}  // namespace

TEST_CASE("eluna scripts: the E1 compatibility scripts run unchanged on the fake world")
{
    ScriptRig rig;
    rig.host.frozen = true;
    fl::HandleData player = FakeHost::player(100);
    fl::HandleData innkeeper = FakeHost::creature(200);
    fl::HandleData boss = FakeHost::creature(300);
    fl::HandleData item;
    item.kind = fl::ObjKind::Item;
    item.guid.lo = 400;
    fl::ObjectInfo pinfo;
    pinfo.name = "Alice";
    fl::ObjectInfo cinfo;
    cinfo.name = "Innkeeper";
    fl::ObjectInfo binfo;
    binfo.name = "Boss";
    fl::ObjectInfo iinfo;
    iinfo.name = "Teleporter";
    rig.host.add(player, pinfo);
    rig.host.add(innkeeper, cinfo);
    rig.host.add(boss, binfo);
    rig.host.add(item, iinfo);
    fl::HandleData quest;
    quest.kind = fl::ObjKind::Quest;
    quest.guid.lo = 100;
    fl::ObjectInfo qinfo;
    qinfo.entry = 100;
    rig.host.add(quest, qinfo);

    fl::DbRows table;
    table.columns = {"entry", "name"};
    table.rows = {{int64_t(1), std::string("First")}, {int64_t(2), std::string("Second")}};
    rig.host.rawResults["SELECT entry, name FROM creature_template ORDER BY entry"] = table;

    for (char const* op : {"gossip.addItem", "gossip.sendMenu", "gossip.complete", "player.teleport",
                           "player.broadcast", "player.modifyMoney", "unit.castSpell", "item.getEntry",
                           "item.getItemLink", "guild.getName", "quest.getId", "quest.getLevel", "quest.getTitle",
                           "quest.isDaily"})
        rig.host.gameResults[op] = {};
    rig.host.gameResults["quest.getId"] = {fl::EventValue(int64_t(100))};
    rig.host.gameResults["quest.getLevel"] = {fl::EventValue(int64_t(12))};
    rig.host.gameResults["quest.getTitle"] = {fl::EventValue("The Test Quest")};
    rig.host.gameResults["quest.isDaily"] = {fl::EventValue(true)};

    Capture cap;
    cap.attach(rig.host);
    fl::LoadReport rep = rig.e().load();
    std::string loadErrors;
    for (auto const& e : rep.errors)
        loadErrors += e + "\n";
    INFO("load errors: " << loadErrors);
    REQUIRE(rep.errors.empty());

    // db_lookup.lua ran at load time.
    CHECK(rig.ok("return rows") == "2");
    CHECK(rig.ok("return lastname") == "Second");
    CHECK(rig.ok("return tostring(missing)") == "nil");

    // hello_npc.lua: gossip hello builds the menu, select teleports.
    fl::EventResult hello = rig.e().emitEluna(fl::EluKind::CreatureGossip, 90001, 1,
                                              {fl::EventValue(player), fl::EventValue(innkeeper)});
    CHECK_FALSE(hello.cancel);   // the handler built the menu itself: it does not ask for the default action
    CHECK(cap.count("gossip.addItem") == 1);
    CHECK(cap.count("gossip.sendMenu") == 1);
    rig.e().emitEluna(fl::EluKind::CreatureGossip, 90001, 2,
                      {fl::EventValue(player), fl::EventValue(innkeeper), fl::EventValue(int64_t(1)),
                       fl::EventValue(int64_t(1)), fl::EventValue(std::string()), fl::EventValue(int64_t(0))});
    CHECK(cap.count("player.teleport") == 1);
    REQUIRE(cap.args("player.teleport").size() == 5);
    CHECK(std::get<int64_t>(cap.args("player.teleport")[0].v) == 1);

    // login_welcome.lua and require_lib.lua greet on login.
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(player)});
    size_t const broadcasts = cap.count("player.broadcast");
    CHECK(broadcasts == 2);
    CHECK(textOf(cap.args("player.broadcast", 0)[0]) == "Welcome, Alice!");
    CHECK(textOf(cap.args("player.broadcast", 1)[0]) == "Hello from the library, Alice");
    rig.e().emitEluna(fl::EluKind::Player, 0, 30, {fl::EventValue(player)});
    CHECK(cap.count("player.broadcast") == 3);

    // kill_reward.lua: money on a kill, doubled experience.
    rig.e().emitEluna(fl::EluKind::Player, 0, 7, {fl::EventValue(player), fl::EventValue(boss)});
    REQUIRE(cap.args("player.modifyMoney").size() == 1);
    CHECK(std::get<int64_t>(cap.args("player.modifyMoney")[0].v) == 1000);
    fl::EventResult xp = rig.e().emitEluna(fl::EluKind::Player, 0, 12,
                                          {fl::EventValue(player), fl::EventValue(int64_t(100)), fl::EventValue(boss)});
    CHECK(xp.hasValue);
    CHECK(xp.value == 200);

    // chat_filter.lua: the text is replaced.
    fl::EventResult chat = rig.e().emitEluna(fl::EluKind::Player, 0, 18,
                                            {fl::EventValue(player), fl::EventValue(std::string("hi")),
                                             fl::EventValue(int64_t(1)), fl::EventValue(int64_t(0))});
    CHECK(chat.cancel);
    CHECK(chat.text == "[filtered] hi");

    // boss_phases.lua: a one-shot cast after 500 ms, CLEARED on death.
    rig.e().emitEluna(fl::EluKind::Creature, 90002, 1, {fl::EventValue(boss), fl::EventValue(player)});
    rig.host.advance(600);
    rig.e().tick();
    CHECK(cap.count("unit.castSpell") == 1);
    rig.e().emitEluna(fl::EluKind::Creature, 90002, 4, {fl::EventValue(boss), fl::EventValue(player)});
    rig.e().emitEluna(fl::EluKind::Creature, 90002, 1, {fl::EventValue(boss), fl::EventValue(player)});
    rig.e().emitEluna(fl::EluKind::Creature, 90002, 4, {fl::EventValue(boss), fl::EventValue(player)});
    rig.host.advance(600);
    rig.e().tick();
    CHECK(cap.count("unit.castSpell") == 1);   // the second one was removed before it fired

    // timed_announcer.lua: every 60 s, three times, then removed.
    for (int i = 0; i < 5; ++i)
    {
        rig.host.advance(60000);
        rig.e().tick();
    }
    CHECK(rig.ok("return announced") == "3");
    CHECK(rig.host.worldMessages.size() == 3);

    // item_teleporter.lua: item USE teleports (the gossip select needs core hook C3 and is not exercised here).
    rig.e().emitEluna(fl::EluKind::Item, 90003, 2,
                      {fl::EventValue(player), fl::EventValue(item), fl::EventValue()});
    CHECK(cap.count("player.teleport") == 2);
    // The item USE and the gossip select still need core hooks: registering the script logged the gap once (spec section 5).
    CHECK(rig.host.logContains("item USE needs core hook C3"));
}

TEST_CASE("eluna scripts: the E2 and E3 compatibility scripts run unchanged on the fake world")
{
    ScriptRig rig;
    rig.host.frozen = true;
    fl::HandleData player = FakeHost::player(101);
    fl::ObjectInfo pinfo;
    pinfo.name = "Bob";
    rig.host.add(player, pinfo);
    rig.host.players.push_back(player);
    fl::HandleData quest;
    quest.kind = fl::ObjKind::Quest;
    quest.guid.lo = 100;
    fl::ObjectInfo qinfo;
    qinfo.entry = 100;
    rig.host.add(quest, qinfo);
    rig.host.gameResults["guild.getName"] = {fl::EventValue("The Guild")};
    rig.host.gameResults["quest.getId"] = {fl::EventValue(int64_t(100))};
    rig.host.gameResults["quest.getLevel"] = {fl::EventValue(int64_t(12))};
    rig.host.gameResults["quest.getTitle"] = {fl::EventValue("The Test Quest")};
    rig.host.gameResults["quest.isDaily"] = {fl::EventValue(true)};
    fl::LoadReport rep = rig.e().load();
    std::string loadErrors;
    for (auto const& e : rep.errors)
        loadErrors += e + "\n";
    INFO("load errors: " << loadErrors);
    REQUIRE(rep.errors.empty());

    // group_events.lua: the docs argument lists reach the handler.
    rig.e().emitEluna(fl::EluKind::Group, 0, 1,
                      {fl::EventValue(fl::HandleData{}), fl::EventValue(std::string("0000000000000000000000000000002A"))});
    CHECK(rig.ok("return group_added") == "0000000000000000000000000000002A");
    rig.e().emitEluna(fl::EluKind::Group, 0, 3,
                      {fl::EventValue(fl::HandleData{}), fl::EventValue(std::string("0000000000000000000000000000002A")),
                       fl::EventValue(int64_t(1)), fl::EventValue(std::string("0000000000000000000000000000002B")),
                       fl::EventValue(std::string("bye"))});
    CHECK(rig.ok("return group_removed[2] .. ',' .. group_removed[3]") == "1,bye");
    rig.e().emitEluna(fl::EluKind::Group, 0, 5, {fl::EventValue(fl::HandleData{})});
    CHECK(rig.ok("return tostring(group_disbanded)") == "true");
    rig.e().emitEluna(fl::EluKind::Guild, 0, 1,
                      {fl::EventValue(fl::HandleData{}), fl::EventValue(player), fl::EventValue(int64_t(4))});
    CHECK(rig.ok("return tostring(guild_joined[1]) .. ',' .. guild_joined[2]") == "101,4");
    rig.e().emitEluna(fl::EluKind::Guild, 0, 3,
                      {fl::EventValue(fl::HandleData{}), fl::EventValue(std::string("new motd"))});
    CHECK(rig.ok("return guild_motd") == "new motd");

    // quest_info.lua ran at load time against the fake quest 100.
    CHECK(rig.ok("return quest_title .. ',' .. quest_level .. ',' .. tostring(quest_daily)") ==
          "The Test Quest,12,true");

    // async_lookup.lua: the rows arrive on a later tick.
    bool found = false;
    for (auto const& r : rig.host.asyncRequests)
        if (r.sql.find("SELECT entry FROM creature_template") != std::string::npos)
        {
            fl::DbRows rows;
            rows.columns = {"entry"};
            rows.rows = {{int64_t(1)}, {int64_t(2)}};
            rig.host.completeAsync(r.id, rows);
            found = true;
        }
    REQUIRE(found);
    rig.e().tick();
    CHECK(rig.ok("return async_rows") == "3");

    // persistent_data.lua: 1 at the first load, 2 after a reload of the same file.
    CHECK(rig.ok("return visits_now()") == "1");
    CHECK(rig.e().reload().applied);
    CHECK(rig.ok("return visits_now()") == "2");

    // custom_command.lua: the command answers through the engine, and its text is the help line.
    std::vector<fl::Engine::LuaCommand> cmds = rig.e().luaCommands();
    size_t index = 0;
    for (; index < cmds.size(); ++index)
        if (cmds[index].name == "luahello")
            break;
    REQUIRE(index < cmds.size());
    fl::CommandResult r = rig.e().runLuaCommand(index, "there", 0, std::nullopt);
    CHECK(r.status == fl::CommandStatus::Ok);
    REQUIRE(r.lines.size() == 1);
    CHECK(r.lines[0] == "hello there");
}

TEST_CASE("eluna scripts: the E4 and E5 compatibility scripts run unchanged on the fake world")
{
    fltest::FakeHost host;
    fl::Config cfg = testConfig(FL_TEST_ELUNA_DIR);
    cfg.mode = fl::EngineMode::Eluna;
    cfg.eventBudget = {0, 0};
    cfg.commandBudget = {0, 0};
    cfg.loadBudget = {0, 0};
    cfg.tickBudgetMs = 0;
    cfg.slowHandlerMs = 0;
    cfg.mapStates = 1;
    // The folder's E2 script (quest_info.lua) runs at load: give the fake world its quest 100.
    fl::HandleData quest;
    quest.kind = fl::ObjKind::Quest;
    quest.guid.lo = 100;
    host.add(quest, fl::ObjectInfo{"", 100, 0, 0, 0, 0, true});
    host.gameResults["quest.getId"] = {fl::EventValue(int64_t(100))};
    host.gameResults["quest.getLevel"] = {fl::EventValue(int64_t(12))};
    host.gameResults["quest.getTitle"] = {fl::EventValue(std::string("The Test Quest"))};
    host.gameResults["quest.isDaily"] = {fl::EventValue(true)};
    fl::StateSet states(host, cfg);
    REQUIRE(states.start());
    fl::LoadReport const rep = states.load();
    std::string loadErrors;
    for (auto const& e : rep.errors)
        loadErrors += e + "\n";
    INFO("load errors: " << loadErrors);
    REQUIRE(rep.errors.empty());

    // map_states.lua: the world state knows it is the world; the map state of map 7 knows its map.
    fl::Engine& world = *states.world();
    REQUIRE(world.runChunk("return tostring(world_state_seen)").result == "true");
    REQUIRE(world.runChunk("return tostring(compat_mode)").result == "false");
    REQUIRE(world.runChunk("return GetStateMapId()").result == "-1");
    REQUIRE(states.openMap(7, 0) != nullptr);
    fl::Engine* map = states.mapState(7, 0);
    REQUIRE(map != nullptr);
    REQUIRE(map->runChunk("return map_state_seen").result == "7");
    REQUIRE(map->runChunk("return GetStateMapId()").result == "7");

    fl::HandleData p = fl::HandleData{};
    p.kind = fl::ObjKind::Player;
    p.guid = FakeHost::makeGuid(FakeHost::kTypePlayer, 12);
    p.mapId = 7;
    host.add(p, fl::ObjectInfo{"Mapper", 0, 0, 0, 0, 0, true});
    host.players = {p};
    fl::HandleData mapHandle;
    mapHandle.kind = fl::ObjKind::Map;
    mapHandle.mapId = 7;
    states.emit(fl::EluKind::Map, 7, 3, {fl::EventValue(), fl::EventValue(mapHandle),
                                         fl::EventValue(int64_t(50))}, mapHandle);
    REQUIRE(map->runChunk("return map_ticks").result == "1");
    REQUIRE(map->runChunk("return map_players").result == "1");
    REQUIRE(map->runChunk("return state_map_id").result == "7");

    // packets.lua: the value round-trip, and the packet event gets a copy in the world state.
    REQUIRE(world.runChunk("return packet_ok .. ':' .. packet_long .. ':' .. packet_text").result ==
            "5:70000:forever");
    REQUIRE(world.runChunk("return packet_size").result == "13");
    REQUIRE(world.runChunk("return packet_opcode").result == "153");
    world.emitPacketEvent(0x99, 5, {9}, fl::EventValue());
    REQUIRE(world.runChunk("return packet_seen").result == "5:9:nil");

    // classic_stubs.lua: the documented error texts, and the BG registration.
    CHECK(world.runChunk("return stub_error").result.find("GetOwnerHalaa: not available on Classic 1.60") !=
          std::string::npos);
    CHECK(world.runChunk("return vehicle_error").result.find("RegisterVehicleEvent: not available on Classic 1.60") !=
          std::string::npos);
    CHECK(world.runChunk("return taxi_error").result.find("AddTaxiPath: not available on this server") !=
          std::string::npos);
    REQUIRE(world.runChunk("return tostring(bg_registered)").result == "true");
}
