// Bot API B1 (the maintainers' bot API specification sections 3-6): the null provider, every getter, the name-ordered lists and
// lookups, RegisterBotEvent in a map state, all nine events with their argument order and exactly once, stale/relog/
// removed handles, the test-mode names, the snapshot rebuild policy and the cross-thread snapshot swap.
#include "Support.h"

#include "FakeBotProvider.h"
#include "engine/BotProvider.h"
#include "engine/GuidText.h"
#include "engine/StateSet.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace fltest;

namespace
{
void elunaBots(fl::Config& c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    c.slowHandlerMs = 0;
}

std::string guidTextOf(fl::HandleData const& h) { return fl::guidText(h.guid); }

fl::HandleData groupHandle(uint64_t low)
{
    fl::HandleData g;
    g.kind = fl::ObjKind::Group;
    g.guid = fl::Guid{0, low};
    return g;
}

fl::GroupMemberInfo member(fl::HandleData const& h, bool isBot, bool alive, bool inWorld = true)
{
    fl::GroupMemberInfo m;
    m.guid = h.guid;
    m.isBot = isBot;
    m.alive = alive;
    m.inWorld = inWorld;
    return m;
}

// The Lua into the C++ tests: one handler per bot event that records the arguments it received.
char const* kRecorder =
    "log = {} "
    "local function add(s) log[#log+1] = s end "
    "RegisterBotEvent(1, function(e, b) add(e .. ' added ' .. b:GetName()) end) "
    "RegisterBotEvent(2, function(e, b) add(e .. ' removed ' .. tostring(b:IsValid())) end) "
    "RegisterBotEvent(3, function(e, b, t, s, v) add(e .. ' given ' .. b:GetName() .. ' ' .. t .. ' ' .. s .. ' ' .. v) end) "
    "RegisterBotEvent(4, function(e, b, t, s, v) add(e .. ' done ' .. b:GetName() .. ' ' .. t .. ' ' .. s .. ' ' .. v) end) "
    "RegisterBotEvent(5, function(e, b, t, s, v, c, m) add(e .. ' failed ' .. b:GetName() .. ' ' .. t .. ' ' .. s .. ' ' .. "
    "v .. ' ' .. c .. ' ' .. m) end) "
    "RegisterBotEvent(6, function(e, b) add(e .. ' died ' .. b:GetName()) end) "
    "RegisterBotEvent(7, function(e, g, bots) local names = {} "
    "for _, b in ipairs(bots) do names[#names+1] = b:GetName() end "
    "add(e .. ' wipe ' .. tostring(g) .. ' ' .. #bots .. ' ' .. table.concat(names, ',')) end) "
    "RegisterBotEvent(8, function(e, l, p, s, leg) add(e .. ' step ' .. tostring(l) .. ' ' .. p .. ' ' .. s .. ' ' .. leg) end) "
    "RegisterBotEvent(9, function(e, l, p, s, w) add(e .. ' stopped ' .. tostring(l) .. ' ' .. p .. ' ' .. s .. ' ' .. w) end)";

std::string logText(Rig& rig) { return rig.ok("return table.concat(log, '|')"); }
}  // namespace

TEST_CASE("bots: no provider means every bot global raises bots not built")
{
    BotProviderGuard guard(nullptr);
    Rig rig(elunaBots);
    CHECK(rig.ok("return tostring(IsBotApiAvailable())") == "false");
    for (char const* code : {"GetBots()", "GetBotByName('x')", "GetBotByGUID(1)", "GetBotsOfOwner(1)", "GetBotsOfMaster(1)",
                             "GetBotsInGroup(1)", "RegisterBotEvent(1, function() end)", "ClearBotEvents()"})
    {
        fl::CallResult const r = rig.run(code);
        INFO("chunk: " << code << "\nerror: " << r.error);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("bots not built") != std::string::npos);
    }
}

TEST_CASE("bots: every getter reads the last snapshot")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    fl::HandleData const me = FakeHost::player(7);
    rig.host.add(me, {"Alpha", 0, 0, 0, 0, 0, true});
    fl::HandleData const master = FakeHost::player(100);
    rig.host.add(master, {"Master", 0, 0, 0, 0, 0, true});

    fl::BotInfo bot = FakeBotProvider::bot(7, "Alpha", 42);
    bot.level = 62;
    bot.cls = 8;
    bot.role = "healer";
    bot.roleForced = true;
    bot.spec = "Holy";
    bot.hasEngine = true;
    bot.engineState = "combat";
    bot.alive = false;
    bot.inCombat = true;
    bot.master = master;
    bot.commander = FakeHost::player(101);
    bot.group = groupHandle(500);
    bot.lastOrder = fl::OrderInfo{4, "follow", "ok", fl::OrderState::Done, "", ""};
    provider.next.bots = {bot};
    rig.e().pumpBots();

    std::string const got = rig.ok(
        "local b = GetBotByName('Alpha') "
        "local seq, verb, result = b:GetLastOrder() "
        "return table.concat({b:GetName(), b:GetLevel(), b:GetClass(), b:GetRole(), tostring(b:IsRoleForced()), b:GetSpec(), "
        "tostring(b:HasEngine()), b:GetEngineState(), tostring(b:IsAlive()), tostring(b:IsInCombat()), "
        "tostring(b:GetGUID() == '" +
        rig.host.guidToString(me.guid) +
        "'), tostring(b:IsValid()), tostring(b:GetMaster()), tostring(b:GetCommander()), tostring(b:GetGroup()), "
        "b:GetPlayer():GetName(), tostring(seq), verb, result, tostring(b:GetRun())}, '|')");
    CHECK(got == "Alpha|62|8|healer|true|Holy|true|combat|false|true|true|true|" +
                     rig.host.guidToString(master.guid) + "|" + rig.host.guidToString(FakeHost::player(101).guid) + "|" +
                     rig.host.guidToString(groupHandle(500).guid) + "|Alpha|4|follow|ok|nil");

    // GetRun(): nil for a bot in no run, the six fields otherwise.
    bot.hasRun = true;
    bot.run = fl::RunInfo{master, "deadmines", "running", 3, 9, "waiting"};
    provider.next.bots = {bot};
    rig.ok("return #GetBots()");   // a read keeps the next snapshot due
    rig.e().pumpBots();
    CHECK(rig.ok("local r = GetBotByName('Alpha'):GetRun() return table.concat({tostring(r.leader), r.plan, r.state, r.step, "
                 "r.steps, r.why}, '|')") == rig.host.guidToString(master.guid) + "|deadmines|running|3|9|waiting");
}

TEST_CASE("bots: the lists are in name order and the lookups answer")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    fl::HandleData const master = FakeHost::player(100);
    rig.host.add(master, {"Master", 0, 0, 0, 0, 0, true});
    rig.host.playersByName["Master"] = master;
    rig.host.playerOwners[{master.guid.hi, master.guid.lo}] = 42;
    fl::HandleData const group = groupHandle(500);
    rig.host.add(group, {"Party", 0, 0, 0, 0, 0, true});
    rig.host.gameResults["player.getGroup"] = {fl::EventValue(group)};

    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha", 42);
    alpha.master = master;
    alpha.group = group;
    fl::BotInfo beta = FakeBotProvider::bot(8, "Beta", 42);
    beta.commander = master;
    beta.group = group;
    fl::BotInfo gamma = FakeBotProvider::bot(9, "Gamma", 0);
    for (uint64_t low : {uint64_t(7), uint64_t(8), uint64_t(9)})
        rig.host.add(FakeHost::player(low), {"Bot" + std::to_string(low), 0, 0, 0, 0, 0, true});
    provider.next.bots = {gamma, beta, alpha};   // the provider's order is not the name order
    provider.next.groups = {{group, {member(master, false, true), member(alpha.player, true, true),
                                     member(beta.player, true, true)}}};
    rig.e().pumpBots();

    CHECK(rig.ok("local n = {} for _, b in ipairs(GetBots()) do n[#n+1] = b:GetName() end return table.concat(n, ',')") ==
          "Alpha,Beta,Gamma");
    CHECK(rig.ok("return GetBotByName('Beta'):GetName()") == "Beta");
    CHECK(rig.ok("return tostring(GetBotByName('beta'))") == "nil");
    CHECK(rig.ok("return GetBotByGUID(" + std::to_string(8) + "):GetName()") == "Beta");   // the legacy low counter
    CHECK(rig.ok("return GetBotByGUID('" + guidTextOf(FakeHost::player(8)) + "'):GetName()") == "Beta");
    CHECK(rig.ok("local n = {} for _, b in ipairs(GetBotsOfOwner(GetPlayerByName('Master'))) do n[#n+1] = b:GetName() end "
                 "return table.concat(n, ',')") == "Alpha,Beta");
    CHECK(rig.ok("local n = {} for _, b in ipairs(GetBotsOfMaster(GetPlayerByName('Master'))) do n[#n+1] = b:GetName() end "
                 "return table.concat(n, ',')") == "Alpha,Beta");
    CHECK(rig.ok("local n = {} for _, b in ipairs(GetBotsInGroup(GetPlayerByName('Master'):GetGroup())) do "
                 "n[#n+1] = b:GetName() end return table.concat(n, ',')") == "Alpha,Beta");
    CHECK(rig.ok("local n = {} for _, b in ipairs(GetBotsInGroup(GetPlayerByName('Master'))) do n[#n+1] = b:GetName() end "
                 "return table.concat(n, ',')") == "Alpha,Beta");
    // A player the module does not own gets no bot; an ungrouped bot is in no list.
    rig.host.playerOwners[{master.guid.hi, master.guid.lo}] = 0;
    CHECK(rig.ok("return #GetBotsOfOwner(GetPlayerByName('Master'))") == "0");
    CHECK(rig.ok("return #GetBotsInGroup(GetBotByName('Gamma'):GetPlayer())") == "0");
}

// MORE_TESTS

TEST_CASE("bots: all nine events, with the spec's argument order, exactly once")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    rig.host.frozen = true;
    rig.ok(kRecorder);
    rig.host.add(FakeHost::player(100), {"Master", 0, 0, 0, 0, 0, true});

    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha");
    fl::BotInfo beta = FakeBotProvider::bot(8, "Beta");
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha|1 added Beta");

    // BOT_DIED once per death edge, never again while it stays dead.
    alpha.alive = false;
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha|1 added Beta|6 died Alpha");
    rig.ok("log = {}");
    rig.e().pumpBots();
    CHECK(logText(rig) == "");

    // ORDER_GIVEN (ticket 0: not from Lua) then ORDER_DONE.
    beta.lastOrder = fl::OrderInfo{5, "follow", "", fl::OrderState::Pending, "", ""};
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "3 given Beta 0 5 follow");
    rig.ok("log = {}");
    beta.lastOrder = fl::OrderInfo{5, "follow", "ok", fl::OrderState::Done, "", ""};
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "4 done Beta 0 5 follow");

    // A pending order replaced by the next one ends `lost` first; the new one is given and fails at once.
    rig.ok("log = {}");
    beta.lastOrder = fl::OrderInfo{6, "attack", "", fl::OrderState::Pending, "", ""};
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "3 given Beta 0 6 attack");
    rig.ok("log = {}");
    beta.alive = true;
    beta.lastOrder = fl::OrderInfo{7, "attack", "fail:expired", fl::OrderState::Failed, "expired", "the mob was gone"};
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "5 failed Beta 0 6 attack lost a later order replaced it before the outcome arrived|"
                          "3 given Beta 0 7 attack|5 failed Beta 0 7 attack expired the mob was gone");

    // A ticket still pending after 30 s ends `lost`, exactly once.
    rig.ok("log = {}");
    beta.lastOrder = fl::OrderInfo{8, "hold", "", fl::OrderState::Pending, "", ""};
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(logText(rig) == "3 given Beta 0 8 hold");
    rig.host.advance(29'000);
    rig.e().pumpBots();
    CHECK(logText(rig) == "3 given Beta 0 8 hold");
    rig.host.advance(1'500);
    rig.e().pumpBots();
    CHECK(logText(rig) == "3 given Beta 0 8 hold|5 failed Beta 0 8 hold lost no outcome within 30 s");
    rig.e().pumpBots();
    CHECK(logText(rig) == "3 given Beta 0 8 hold|5 failed Beta 0 8 hold lost no outcome within 30 s");

    // WIPE: every in-world member of a group holding a bot is dead (it fires on the edge, once).
    rig.ok("log = {}");
    fl::HandleData const master = FakeHost::player(100);
    fl::HandleData const group = groupHandle(500);
    provider.next.groups = {{group, {member(master, false, false), member(alpha.player, true, false),
                                     member(beta.player, true, false)}}};
    alpha.alive = false;
    beta.alive = false;
    provider.next.bots = {alpha, beta};   // the snapshot is a copy: change the bots first, then hand them over
    rig.e().pumpBots();
    CHECK(logText(rig) == "6 died Beta|7 wipe Guid-0-500 2 Alpha,Beta");
    rig.ok("log = {}");
    rig.e().pumpBots();
    CHECK(logText(rig) == "");

    // RUN_STEP and RUN_STOPPED from the ring, in ring order; `done` and `stopped` are the two state words.
    rig.ok("log = {}");
    fl::RunEventInfo step;
    step.kind = fl::RunEventKind::Step;
    step.leader = master;
    step.plan = "deadmines";
    step.step = 2;
    step.leg = 1;
    fl::RunEventInfo done;
    done.kind = fl::RunEventKind::Done;
    done.leader = master;
    done.plan = "deadmines";
    done.why = "all bosses dead";
    fl::RunEventInfo stop;
    stop.kind = fl::RunEventKind::Stop;
    stop.leader = master;
    stop.plan = "deadmines";
    stop.why = "wipe";
    provider.runRing = {step, done, stop};
    rig.e().pumpBots();
    CHECK(logText(rig) == "8 step " + rig.host.guidToString(master.guid) + " deadmines 2 1|" + "9 stopped " +
                              rig.host.guidToString(master.guid) + " deadmines done all bosses dead|" + "9 stopped " +
                              rig.host.guidToString(master.guid) + " deadmines stopped wipe");

    // BOT_REMOVED: the handle it hands out never resolves.
    rig.ok("log = {}");
    rig.ok("ClearBotEvents(2) removed = nil RegisterBotEvent(2, function(e, b) removed = b log[#log+1] = 'kept' end)");
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(logText(rig) == "kept");
    CHECK(rig.ok("return tostring(removed:IsValid())") == "false");
    CHECK(rig.ok("return tostring(removed:GetGUID() == '" + rig.host.guidToString(beta.player.guid) + "')") == "true");
    fl::CallResult const gone = rig.run("return removed:GetName()");
    CHECK_FALSE(gone.ok);
    CHECK(gone.error.find("Bot no longer exists") != std::string::npos);
    // A relog revives a stale handle, never a BOT_REMOVED one.
    provider.next.bots = {alpha, beta};
    rig.e().pumpBots();
    CHECK(rig.ok("return tostring(removed:IsValid())") == "false");
}

TEST_CASE("bots: WIPE needs every in-world member dead")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    rig.ok(kRecorder);
    fl::HandleData const master = FakeHost::player(100);
    fl::HandleData const group = groupHandle(500);
    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha");
    alpha.alive = false;   // already dead at first sight: no BOT_DIED (see the next case)

    // A living in-world human is a member: the group holding a dead bot does not wipe.
    provider.next.bots = {alpha};
    provider.next.groups = {{group, {member(master, false, true), member(alpha.player, true, false)}}};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha");

    // A dead member who is not in the world is ignored: that group does wipe.
    fl::HandleData const group2 = groupHandle(501);
    fl::HandleData const ghost = FakeHost::player(101);
    provider.next.groups = {{group, {member(master, false, true), member(alpha.player, true, false)}},
                            {group2, {member(ghost, false, false), member(alpha.player, true, false)}}};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha|7 wipe Guid-0-501 1 Alpha");

    // The human dies too: the first group wipes now, once; the second one does not repeat.
    rig.ok("log = {}");
    provider.next.groups = {{group, {member(master, false, false), member(alpha.player, true, false)}},
                            {group2, {member(ghost, false, false), member(alpha.player, true, false)}}};
    rig.e().pumpBots();
    CHECK(logText(rig) == "7 wipe Guid-0-500 1 Alpha");
}

TEST_CASE("bots: a bot already dead when first seen gets no BOT_DIED")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    rig.ok(kRecorder);
    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha");
    alpha.alive = false;
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha");   // first sight: BOT_ADDED only
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha");   // still dead, still no death edge
    // Gone and back (a relog) is a new first sight: still no BOT_DIED.
    provider.next.bots.clear();
    rig.e().pumpBots();
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha|2 removed false|1 added Alpha");
}

TEST_CASE("bots: available() false empties the hub, and true announces the bots again")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha");
    rig.ok(kRecorder);
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(logText(rig) == "1 added Alpha");
    CHECK(fl::botHub().snapshot()->bots.size() == 1);

    // The module's available() goes false (its OnShutdown): the API is off and the hub keeps no bot and no diff state.
    provider.isAvailable = false;
    rig.e().pumpBots();
    CHECK(rig.ok("return tostring(IsBotApiAvailable())") == "false");
    CHECK(fl::botHub().snapshot()->bots.empty());
    CHECK(logText(rig) == "1 added Alpha");   // the clear emits nothing

    // It comes back: the bot is a first sight again, so BOT_ADDED once more.
    provider.isAvailable = true;
    rig.e().pumpBots();
    CHECK(rig.ok("return #GetBots()") == "1");
    CHECK(logText(rig) == "1 added Alpha|1 added Alpha");
}

TEST_CASE("bots: the B2 defaults answer not-supported, one answer per request")
{
    struct B1Only final : fl::BotProvider
    {
        bool available() override { return true; }
        void snapshot(fl::BotSnapshot&) override { }
        void takeRunEvents(std::vector<fl::RunEventInfo>&) override { }
    } provider;
    BotProviderGuard guard(&provider);

    fl::OrderRequest first;
    first.ticket = 11;
    first.verb = "follow";
    fl::OrderRequest second;
    second.ticket = 12;
    second.verb = "stay";
    std::vector<fl::OrderVerdict> verdicts;
    provider.submit({first, second}, verdicts);
    REQUIRE(verdicts.size() == 2);
    CHECK(verdicts[0].ticket == 11);
    CHECK(verdicts[1].ticket == 12);
    for (fl::OrderVerdict const& verdict : verdicts)
    {
        CHECK_FALSE(verdict.accepted);
        CHECK(verdict.seq == 0);
        CHECK(verdict.code == "not-supported");
    }

    std::vector<std::string> results;
    provider.outcomes({7, 8, 9}, results);
    CHECK(results == std::vector<std::string>{"not-supported", "not-supported", "not-supported"});
}

TEST_CASE("bots: RegisterBotEvent is refused in a map state, and a map state reads the snapshot")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    FakeHost host;
    TempDir dir;
    fl::Config cfg = testConfig(dir.path());
    elunaBots(cfg);
    cfg.mapStates = 1;
    fl::StateSet states(host, cfg);
    REQUIRE(states.start());
    provider.next.bots = {FakeBotProvider::bot(7, "Alpha")};
    states.world()->pumpBots();

    fl::Engine* map = states.openMap(1, 0);
    REQUIRE(map != nullptr);
    // Reads are the last immutable snapshot, from the map's state and thread.
    CHECK(map->runChunk("return #GetBots()").result == "1");
    CHECK(map->runChunk("return GetBots()[1]:GetName()").result == "Alpha");
    CHECK(map->runChunk("return tostring(GetBots()[1]:IsValid())").result == "true");
    // Registration is the world state's only (it emits in the world state's handlers).
    fl::CallResult const refused = map->runChunk("RegisterBotEvent(1, function() end)");
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.find("world state") != std::string::npos);
    states.shutdown();
}

TEST_CASE("bots: test mode reaches the bot events through Event.on")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig;
    rig.ok("log = {} "
           "Event.on('bot.added', function(b) log[#log+1] = 'added ' .. b:kind() .. ' ' .. b:guid() end) "
           "Event.on('bot.order', function(b, t, s, v) log[#log+1] = 'order ' .. b:kind() .. ' ' .. t .. ' ' .. s .. ' ' .. v end) "
           "Event.on('bot.wipe', function(g, bots) log[#log+1] = 'wipe ' .. #bots .. ' ' .. bots[1]:guid() end)");
    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha");
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(rig.ok("return log[1]") == "added bot " + rig.host.guidToString(alpha.player.guid));
    alpha.lastOrder = fl::OrderInfo{3, "stay", "", fl::OrderState::Pending, "", ""};
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(rig.ok("return log[2]") == "order bot 0 3 stay");
    fl::HandleData const group = groupHandle(500);
    provider.next.groups = {{group, {member(alpha.player, true, false)}}};
    alpha.alive = false;
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(rig.ok("return log[3]") == "wipe 1 " + rig.host.guidToString(alpha.player.guid));
}

TEST_CASE("bots: the snapshot is rebuilt every tick with a handler or a recent read, else once a second")
{
    SECTION("nobody listening, nobody reading")
    {
        FakeBotProvider provider;
        BotProviderGuard guard(&provider);
        Rig rig;
        rig.host.frozen = true;
        provider.next.bots = {FakeBotProvider::bot(7, "Alpha")};
        rig.e().pumpBots();
        CHECK(provider.snapshots == 1);
        rig.e().pumpBots();
        CHECK(provider.snapshots == 1);   // not due yet: the read below is what a script would do
        rig.host.advance(1'000);
        rig.e().pumpBots();
        CHECK(provider.snapshots == 2);
        CHECK(provider.runDrains == 3);   // the ring is drained every tick, handler or not
    }
    SECTION("a read keeps it fresh for 60 s")
    {
        FakeBotProvider provider;
        BotProviderGuard guard(&provider);
        Rig rig(elunaBots);
        rig.host.frozen = true;
        provider.next.bots = {FakeBotProvider::bot(7, "Alpha")};
        rig.e().pumpBots();
        CHECK(provider.snapshots == 1);
        rig.ok("return #GetBots()");   // a read
        rig.host.advance(900);         // below the idle second, inside the read window
        rig.e().pumpBots();
        CHECK(provider.snapshots == 2);
        rig.host.advance(61'000);      // the window is over, the idle second is long past
        rig.e().pumpBots();
        CHECK(provider.snapshots == 3);
    }
    SECTION("a handler means every tick")
    {
        FakeBotProvider provider;
        BotProviderGuard guard(&provider);
        Rig rig(elunaBots);
        rig.host.frozen = true;
        rig.ok("RegisterBotEvent(1, function() end)");
        provider.next.bots = {FakeBotProvider::bot(7, "Alpha")};
        rig.e().pumpBots();
        rig.e().pumpBots();
        CHECK(provider.snapshots == 2);
    }
}

TEST_CASE("bots: a snapshot swap while a map state reads (TSan)")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    FakeHost host;
    TempDir dir;
    fl::Config cfg = testConfig(dir.path());
    elunaBots(cfg);
    cfg.mapStates = 1;
    fl::StateSet states(host, cfg);
    REQUIRE(states.start());
    fl::Engine* map = states.openMap(1, 0);
    REQUIRE(map != nullptr);
    provider.next.bots = {FakeBotProvider::bot(7, "Alpha")};
    states.world()->pumpBots();

    std::atomic<bool> done{false};
    std::thread pump([&]
    {
        for (int i = 0; i < 200; ++i)
        {
            provider.next.bots.clear();
            for (int k = 0; k <= i % 4; ++k)
                provider.next.bots.push_back(FakeBotProvider::bot(7 + static_cast<uint64_t>(k), "Bot" + std::to_string(k)));
            states.world()->pumpBots();
        }
        done.store(true, std::memory_order_release);
    });
    int reads = 0;
    while (!done.load(std::memory_order_acquire))
    {
        fl::CallResult const r =
            map->runChunk("local n = 0 for _, b in ipairs(GetBots()) do if b:IsValid() then n = n + 1 end end return n");
        REQUIRE(r.ok);
        ++reads;
    }
    pump.join();
    CHECK(reads > 0);
    states.shutdown();
}
