// Bot API B2 (the maintainers' bot API specification sections 3-6): the order calls, their tickets, the ORDER_GIVEN/DONE/FAILED
// events, the per-script rate cap, the stale-handle rule, a B1-only provider, the outbox, the lost deadline, a reload
// (with the orders it cancels at the hand-off, in both interleavings), a provider that goes away (with open orders, or
// inside submit(), and in the same tick as a reload) and the test-mode permissions. Plus the shipped selftest script
// (scripts/selftest/bot_spread_on_low_hp.lua) as a fixture.
#include "Support.h"

#include "FakeBotProvider.h"
#include "engine/BotProvider.h"
#include "engine/GuidText.h"
#include "engine/StateSet.h"

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
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

// The three order events, as the spec's argument table spells them: the bot may be nil (a party-level call).
char const* kOrderLog =
    "log = {} "
    "local function add(s) log[#log+1] = s end "
    "RegisterBotEvent(3, function(e, b, t, s, v) add('given ' .. tostring(b and b:GetName()) .. ' ' .. t .. ' ' .. s .. ' ' .. v) end) "
    "RegisterBotEvent(4, function(e, b, t, s, v) add('done ' .. tostring(b and b:GetName()) .. ' ' .. t .. ' ' .. s .. ' ' .. v) end) "
    "RegisterBotEvent(5, function(e, b, t, s, v, c, m) add('failed ' .. tostring(b and b:GetName()) .. ' ' .. t .. ' ' .. s .. "
    "' ' .. v .. ' ' .. c .. ' ' .. m) end)";

// The same three events without resolving the bot: while the provider is gone a bot handle answers "bots not built", so
// the ticket, the seq, the verb and the code/message are what a script can read (the provider-loss test).
char const* kOrderLogTicket =
    "log = {} "
    "local function add(s) log[#log+1] = s end "
    "RegisterBotEvent(3, function(e, b, t, s, v) add('given ' .. t .. ' ' .. s .. ' ' .. v) end) "
    "RegisterBotEvent(4, function(e, b, t, s, v) add('done ' .. t .. ' ' .. s .. ' ' .. v) end) "
    "RegisterBotEvent(5, function(e, b, t, s, v, c, m) add('failed ' .. t .. ' ' .. s .. ' ' .. v .. ' ' .. c .. ' ' .. m) end)";

// The message ORDER_FAILED carries when the domain drop cancelled an order before the hand-off (BotHub.cpp).
char const* kCancelledMessage = "the state that gave the order was reloaded or closed before the hand-off";
// The message ORDER_FAILED carries when the provider went away with the order still open (BotHub.cpp).
char const* kUnavailableMessage = "the bot provider became unavailable";

std::string logText(Rig& rig) { return rig.ok("return table.concat(log, '|')"); }

std::string join(std::vector<std::string> const& words)
{
    std::string out;
    for (std::string const& word : words)
        out += (out.empty() ? "" : "|") + word;
    return out;
}

size_t logMatching(FakeHost& host, std::string const& needle)
{
    size_t count = 0;
    for (auto const& line : host.logs)
        if (line.second.find(needle) != std::string::npos)
            ++count;
    return count;
}

std::string readFile(std::string const& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// One bot of the fake world, its party of two bots and a creature target.
struct Party
{
    fl::HandleData group;
    fl::HandleData master;
    fl::BotInfo alpha;
    fl::BotInfo bravo;

    void build(Rig& rig, FakeBotProvider& provider, bool withGroup = true)
    {
        group = groupHandle(500);
        master = FakeHost::player(100);
        alpha = FakeBotProvider::bot(7, "Alpha");
        bravo = FakeBotProvider::bot(8, "Bravo");
        rig.host.add(master, {"Master", 0, 0, 0, 0, 0, true});
        rig.host.playersByName["Master"] = master;
        if (withGroup)
        {
            alpha.group = group;
            bravo.group = group;
            rig.host.add(group, {"Party", 0, 0, 0, 0, 0, true});
            provider.next.groups = {{group, {member(master, false, true), member(alpha.player, true, true),
                                             member(bravo.player, true, true)}}};
        }
        provider.next.bots = {alpha, bravo};
        rig.e().pumpBots();
    }
};
}  // namespace

TEST_CASE("bot orders: every verb reaches the provider with the spec's arguments")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    fl::HandleData const mob = FakeHost::creature(900, 0, 0, 1234);
    rig.host.add(mob, {"Mob", 1234, 0, 0, 0, 0, true});
    std::string const mobText = fl::guidText(mob.guid);
    std::string const alphaText = fl::guidText(party.alpha.player.guid);
    std::string const bravoText = fl::guidText(party.bravo.player.guid);

    // One call, one pump: the request the provider received, its fields checked against the spec's table.
    auto check = [&](std::string const& expr, char const* verb, std::string const& arg,
                     std::vector<std::string> words = {})
    {
        provider.submitted.clear();
        INFO("call: " << expr);
        // One token per call: 20 in a burst, then one per 100 ms (all these calls are one script's bucket).
        rig.host.advance(100);
        std::string const answered = rig.ok("__ticket = " + expr + " return tostring(__ticket)");
        CHECK(answered != "nil");
        CHECK(provider.submitted.empty());   // nothing reaches the provider before the pump
        rig.e().pumpBots();
        REQUIRE(provider.submitted.size() == 1);
        fl::OrderRequest const& order = provider.submitted.back();
        INFO("verb=" << order.verb << " arg=" << order.arg << " words=" << join(order.words));
        CHECK(order.verb == verb);
        CHECK(order.arg == arg);
        CHECK(order.words == words);
        CHECK(order.seq == 0);              // the bot engine assigns the seq, the verdict carries it
        CHECK(order.script == "chunk");     // the rate bucket: the calling script's data namespace
        CHECK(order.ticket == std::stoull(answered));
        CHECK(order.bot.kind == fl::ObjKind::Bot);
    };

    check("GetBotByName('Alpha'):Follow()", "follow", "");
    check("GetBotByName('Alpha'):Follow(GetBotByName('Bravo'):GetPlayer())", "follow", bravoText);
    check("GetBotByName('Alpha'):Stay()", "stay", "");
    check("GetBotByName('Alpha'):Come(10.5, -2.25, 3)", "come", "10.500,-2.250,3.000");
    check("GetBotByName('Alpha'):Attack('" + mobText + "')", "attack", mobText);
    check("GetBotByName('Alpha'):Attack(GetPlayerByName('Master'))", "attack", fl::guidText(party.master.guid));
    check("GetBotByName('Alpha'):Pull('" + mobText + "')", "pull", mobText);
    check("GetBotByName('Alpha'):Pull('" + mobText + "', 5, 'ranged')", "pull", mobText, {"5", "ranged"});
    check("GetBotByName('Alpha'):EndPull()", "endpull", "");
    check("GetBotByName('Alpha'):Hold()", "hold", "");
    check("GetBotByName('Alpha'):Free()", "free", "");
    check("GetBotByName('Alpha'):Assist(GetBotByName('Bravo'):GetPlayer())", "assist", bravoText);
    check("GetBotByName('Alpha'):Release()", "release", "");
    check("GetBotByName('Alpha'):Stop()", "stop", "");
    check("GetBotByName('Alpha'):Flee()", "flee", "");
    check("GetBotByName('Alpha'):Guard()", "guard", "");
    check("GetBotByName('Alpha'):Stance('tank')", "stance", "tank");
    check("GetBotByName('Alpha'):Engage()", "engage", "");
    check("GetBotByName('Alpha'):Engage(12)", "engage", "12");
    check("GetBotByName('Alpha'):Rebuff()", "rebuff", "");
    check("GetBotByName('Alpha'):SetToggle('aoe', true)", "aoe", "on");
    check("GetBotByName('Alpha'):SetToggle('loot', false)", "loot", "off");
    check("GetBotByName('Alpha'):CorpseRun()", "corpserun", "");
    check("GetBotByName('Alpha'):CrowdControl('" + mobText + "', 'moon')", "cc", mobText, {"moon"});
    check("GetBotByName('Alpha'):CrowdControl('" + mobText + "', 'star', 'humanoid')", "cc", mobText,
          {"star", "humanoid"});
    check("GetBotByName('Alpha'):ClearCrowdControl()", "cc-clear", "");
    check("GetBotByName('Alpha'):StartRun('deadmines')", "startrun", "deadmines");
    check("GetBotByName('Alpha'):StartRun('deadmines', 3)", "startrun", "deadmines", {"3"});
    check("GetBotByName('Alpha'):StopRun('wipe')", "stoprun", "wipe");
    check("GetBotByName('Alpha'):StopRun()", "stoprun", "");
    check("GetBotByName('Alpha'):PauseRun()", "pauserun", "");
    check("GetBotByName('Alpha'):ResumeRun()", "resumerun", "");

    // The two globals: the interrupt hands every candidate over (the first is the request's bot), place names no bot.
    check("BotsInterrupt({GetBotByName('Alpha'), GetBotByName('Bravo')}, '" + mobText + "')", "interrupt", mobText,
          {alphaText, bravoText});
    CHECK(provider.submitted.back().bot.guid == party.alpha.player.guid);
    check("BotCommanderPlace(GetPlayerByName('Master'), 'place spread tight here')", "place",
          fl::guidText(party.master.guid), {"place", "spread", "tight", "here"});
    CHECK(provider.submitted.back().bot.guid == fl::Guid());
    check("BotCommanderPlace(GetPlayerByName('Master'), {'rally', 'here'})", "place",
          fl::guidText(party.master.guid), {"rally", "here"});
}

TEST_CASE("bot orders: a script in the world state orders a bot on another map without touching its Player")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    // The target is a player standing on map 1 while the world thread runs: the engine's access rule refuses to resolve
    // that Player, exactly as a live command found ("object is on another map"). The order still travels: it carries the
    // guid and the provider resolves it on the world thread at its next tick.
    fl::HandleData const far = FakeHost::player(101, 1);
    rig.host.add(far, {"Far", 0, 0, 0, 0, 0, true});
    rig.host.playersByName["Far"] = far;
    rig.ok("__far = GetPlayerByName('Far') __bot = GetBotByName('Alpha')");
    // The maps update now (a command's moment): the world state may not resolve the far Player ...
    rig.host.rules.setMapsIdle(false);
    fl::CallResult const read = rig.run("return __far:GetName()");
    CHECK_FALSE(read.ok);
    CHECK(read.error.find("object is on another map") != std::string::npos);
    // ... and the order still goes out: it never touches that Player, only its guid.
    provider.submitted.clear();
    CHECK(rig.ok("return tostring(__bot:Attack(__far))") != "nil");
    rig.e().pumpBots();
    rig.host.rules.setMapsIdle(true);
    REQUIRE(provider.submitted.size() == 1);
    CHECK(provider.submitted[0].verb == "attack");
    CHECK(provider.submitted[0].arg == fl::guidText(far.guid));
}

TEST_CASE("bot orders: the ticket is order_given then order_done, exactly once")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);

    std::string const ticket = rig.ok("return tostring(GetBotByName('Alpha'):Follow())");
    rig.e().pumpBots();
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 follow");   // the fake's first seq

    provider.outcomeBySeq[101] = "ok";
    rig.e().pumpBots();
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 follow|done Alpha " + ticket + " 101 follow");
    // A finished ticket ends once: the poll no longer asks, and no event repeats.
    size_t const asked = provider.askedSeqs.size();
    rig.e().pumpBots();
    CHECK(provider.askedSeqs.size() == asked);
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 follow|done Alpha " + ticket + " 101 follow");
    // The last-order diff never speaks for a seq a script's ticket owns.
    fl::BotInfo done = party.alpha;
    done.lastOrder = fl::OrderInfo{101, "follow", "ok", fl::OrderState::Done, "", ""};
    provider.next.bots = {done, party.bravo};
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "2");
}

TEST_CASE("bot orders: a refusal at the hand-off is order_failed with its code and seq 0")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);

    // A provider that refuses (the bot module's pre-flight codes travel unchanged): no GIVEN, one FAILED with seq 0.
    std::string const ticket =
        rig.ok("return tostring(GetBotByName('Bravo'):Attack('" + fl::guidText(FakeHost::creature(900).guid) + "'))");
    fl::OrderVerdict refused;
    refused.ticket = std::stoull(ticket);
    refused.accepted = false;
    refused.code = "no-engine";
    refused.message = "the bot has no running engine";
    provider.nextVerdicts = {refused};
    rig.e().pumpBots();
    CHECK(logText(rig) == "failed Bravo " + ticket + " 0 attack no-engine the bot has no running engine");
    // The ticket is gone: no outcome is ever asked for it.
    rig.e().pumpBots();
    CHECK(provider.askedSeqs.empty());
    CHECK(rig.ok("return #log") == "1");

    // An accepted order with a negative seq is a provider bug, and the ticket says so instead of hanging.
    std::string const second = rig.ok("return tostring(GetBotByName('Bravo'):Stay())");
    fl::OrderVerdict broken;
    broken.ticket = std::stoull(second);
    broken.accepted = true;
    broken.seq = -1;
    provider.nextVerdicts = {broken};
    rig.e().pumpBots();
    CHECK(rig.ok("return log[2]") == "failed Bravo " + second + " 0 stay failed the bot provider answered a negative sequence");

    // A synchronous verb (the provider ran it at the hand-off, `place` and the run verbs): seq 0, GIVEN and DONE at once.
    std::string const third = rig.ok("return tostring(GetBotByName('Alpha'):PauseRun())");
    fl::OrderVerdict synchronous;
    synchronous.ticket = std::stoull(third);
    synchronous.accepted = true;
    provider.nextVerdicts = {synchronous};
    rig.e().pumpBots();
    CHECK(rig.ok("return log[4]") == "done Alpha " + third + " 0 pauserun");
    CHECK(rig.ok("return log[3]") == "given Alpha " + third + " 0 pauserun");
    CHECK(provider.askedSeqs.empty());   // a synchronous ticket never asks for an outcome
}

TEST_CASE("bot orders: the interrupt hands the candidates over and the verdict names the pick")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);

    std::string const ticket = rig.ok("return tostring(BotsInterrupt({GetBotByName('Alpha'), GetBotByName('Bravo')}, '"
                                      + fl::guidText(FakeHost::creature(900).guid) + "'))");
    rig.e().pumpBots();
    REQUIRE(provider.submitted.size() == 1);
    CHECK(provider.submitted[0].bot.guid == party.alpha.player.guid);   // the probe
    CHECK(provider.submitted[0].words.size() == 2);

    // The fake echoes the probe back in its verdict (no pick was canned): the ORDER events name Alpha, the request's bot.
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 interrupt");
    provider.outcomeBySeq[101] = "ok";
    rig.e().pumpBots();
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 interrupt|done Alpha " + ticket + " 101 interrupt");

    // The pick travels in the verdict.
    provider.submitted.clear();
    rig.ok("__t = BotsInterrupt({GetBotByName('Alpha'), GetBotByName('Bravo')}, '"
           + fl::guidText(FakeHost::creature(900).guid) + "')");
    fl::OrderVerdict picked;
    picked.ticket = std::stoull(rig.ok("return tostring(__t)"));
    picked.accepted = true;
    picked.seq = 200;
    picked.bot.kind = fl::ObjKind::Bot;
    picked.bot.guid = party.bravo.player.guid;
    provider.nextVerdicts = {picked};
    rig.ok("log = {}");
    rig.e().pumpBots();
    CHECK(logText(rig) == "given Bravo " + std::to_string(picked.ticket) + " 200 interrupt");
}

TEST_CASE("bot orders: BotsInterrupt dedupes its candidates and caps them at 40")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    std::string const target = fl::guidText(FakeHost::creature(900).guid);
    std::string const alphaText = fl::guidText(party.alpha.player.guid);

    // The same handle 50 times is one candidate: the request's words are the deduped list, not the table (the words are a
    // C++ heap vector outside the Lua memory limit, which is what the bound below is for).
    std::string repeated;
    for (int i = 0; i < 50; ++i)
        repeated += (i == 0 ? "" : ",") + std::string("GetBotByName('Alpha')");
    CHECK(rig.ok("return tostring(BotsInterrupt({" + repeated + "}, '" + target + "'))") != "nil");
    rig.e().pumpBots();
    REQUIRE(provider.submitted.size() == 1);
    CHECK(provider.submitted[0].words == std::vector<std::string>{alphaText});
    provider.submitted.clear();

    // 41 distinct candidates (one more than a raid) is `bad-argument`: nothing is queued.
    std::vector<fl::BotInfo> crowd;
    for (int i = 0; i <= 40; ++i)
        crowd.push_back(FakeBotProvider::bot(200 + static_cast<uint64_t>(i), "Bot" + std::to_string(i)));
    provider.next.bots = crowd;
    rig.e().pumpBots();
    auto candidates = [](int count)
    {
        std::string list;
        for (int i = 0; i < count; ++i)
            list += (i == 0 ? "" : ",") + std::string("GetBotByName('Bot") + std::to_string(i) + "')";
        return list;
    };
    rig.ok("__t, __code, __msg = BotsInterrupt({" + candidates(41) + "}, '" + target + "')");
    CHECK(rig.ok("return tostring(__t)") == "nil");
    CHECK(rig.ok("return __code") == "bad-argument");
    CHECK(rig.ok("return __msg") == "BotsInterrupt: at most 40 distinct candidate bots are allowed (a raid)");
    CHECK(provider.submitted.empty());

    // Exactly 40 (a raid) is still taken, with all 40 candidates in the request.
    CHECK(rig.ok("return tostring(BotsInterrupt({" + candidates(40) + "}, '" + target + "'))") != "nil");
    rig.e().pumpBots();
    REQUIRE(provider.submitted.size() == 1);
    CHECK(provider.submitted[0].words.size() == 40);
    CHECK(provider.submitted[0].words.front() == fl::guidText(crowd[0].player.guid));
    CHECK(provider.submitted[0].bot.guid == crowd[0].player.guid);   // the first candidate is the probe
}

TEST_CASE("bot orders: the rate cap refuses the 21st order in a second and recovers")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    rig.host.frozen = true;   // the tick clock: a token refills only with advance(), never from wall time
    Party party;
    party.build(rig, provider);

    // The burst: 20 orders in one script, all tickets (the pump has not run, so the outbox holds them).
    CHECK(rig.ok("__n = 0 for i = 1, 20 do if GetBotByName('Alpha'):Stay() then __n = __n + 1 end end return __n") == "20");
    rig.ok("__t, __code, __msg = GetBotByName('Alpha'):Stay()");
    CHECK(rig.ok("return tostring(__t)") == "nil");
    CHECK(rig.ok("return __code") == "rate-limited");
    CHECK(rig.ok("return __msg") ==
          "the script is over its order rate (a burst of 20, then one order every 100 ms): nothing was queued");
    // One token refills every 100 ms.
    rig.host.advance(100);
    CHECK(rig.ok("return tostring(GetBotByName('Alpha'):Stay())") != "nil");
    rig.ok("__t = GetBotByName('Alpha'):Stay()");
    CHECK(rig.ok("return tostring(__t)") == "nil");
    rig.e().pumpBots();
    CHECK(provider.submitted.size() == 21);

    // A long idle banks no more than one burst: 10 s of idleness is 100 tokens' worth of refill, but the bucket is capped
    // at the burst of 20, so 20 orders pass and the 21st is refused again.
    rig.host.advance(10'000);
    CHECK(rig.ok("__n = 0 for i = 1, 20 do if GetBotByName('Alpha'):Stay() then __n = __n + 1 end end return __n") == "20");
    rig.ok("__idleT, __idleCode = GetBotByName('Alpha'):Stay()");
    CHECK(rig.ok("return tostring(__idleT)") == "nil");
    CHECK(rig.ok("return __idleCode") == "rate-limited");
}

TEST_CASE("bot orders: every script file has its own rate bucket")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.dir.write("a_spender.lua",
                  "local bot = GetBotByName('Alpha') "
                  "spender = 0 "
                  "for i = 1, 20 do if bot:Stay() then spender = spender + 1 end end "
                  "spenderCode = select(2, bot:Stay())");
    rig.dir.write("b_other.lua", "otherTicket = GetBotByName('Alpha'):Stay()");
    fl::LoadReport const report = rig.e().load();
    REQUIRE(report.errors.empty());
    CHECK(rig.ok("return spender") == "20");
    CHECK(rig.ok("return tostring(spenderCode)") == "rate-limited");
    CHECK(rig.ok("return tostring(otherTicket)") != "nil");   // its own bucket: the first leak does not starve it
}

TEST_CASE("bot orders: a stale handle raises, a relog revives it and a removed one never does")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok("__bot = GetBotByName('Alpha')");

    // The bot leaves the snapshot: every order method raises (section 5).
    provider.next.bots = {party.bravo};
    rig.e().pumpBots();
    for (char const* call : {"return __bot:Stay()", "return __bot:Flee()", "return __bot:StartRun('deadmines')"})
    {
        fl::CallResult const r = rig.run(call);
        INFO("call: " << call << "\nerror: " << r.error);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("Bot no longer exists") != std::string::npos);
    }
    CHECK(provider.submitted.empty());
    // A relog revives the handle (it is only stale, not removed).
    provider.next.bots = {party.alpha, party.bravo};
    rig.e().pumpBots();
    CHECK(rig.ok("return tostring(__bot:Stay())") != "nil");
    // A BOT_REMOVED handle never resolves, even after the bot is back.
    rig.ok("removed = nil RegisterBotEvent(2, function(e, b) removed = b end)");
    provider.next.bots = {party.bravo};
    rig.e().pumpBots();
    provider.next.bots = {party.alpha, party.bravo};
    rig.e().pumpBots();
    fl::CallResult const gone = rig.run("return removed:Stay()");
    CHECK_FALSE(gone.ok);
    CHECK(gone.error.find("Bot no longer exists") != std::string::npos);
}

TEST_CASE("bot orders: a provider without B2 answers not-supported to every order")
{
    struct B1Only final : fl::BotProvider
    {
        fl::BotSnapshot next;
        bool available() override { return true; }
        void snapshot(fl::BotSnapshot& out) override { out = next; }
        void takeRunEvents(std::vector<fl::RunEventInfo>&) override { }
    } provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    provider.next.bots = {FakeBotProvider::bot(7, "Alpha")};
    rig.e().pumpBots();
    rig.ok(kOrderLog);

    std::string const ticket = rig.ok("return tostring(GetBotByName('Alpha'):Flee())");
    rig.e().pumpBots();
    CHECK(logText(rig) == "failed Alpha " + ticket + " 0 flee not-supported this bot provider does not support orders");
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "1");
}

TEST_CASE("bot orders: the outbox is full at 1024 and says so")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    rig.host.frozen = true;   // the tick clock: the token checks below are exact, not wall-clock dependent
    Party party;
    party.build(rig, provider);
    // 1024 queued orders, at most 20 per second and 100 ms per token: the clock is advanced for the refills, and the
    // pump never runs, so nothing leaves the outbox and no ticket reaches its deadline.
    size_t posted = 0;
    while (posted < fl::BotHub::kOutboxMax)
    {
        rig.ok("__t, __code = GetBotByName('Alpha'):Stay()");
        if (rig.ok("return tostring(__t)") == "nil")
        {
            REQUIRE(rig.ok("return __code") == "rate-limited");
            rig.host.advance(100);
            continue;
        }
        ++posted;
    }
    CHECK(posted == fl::BotHub::kOutboxMax);
    rig.host.advance(100);   // the rate check comes first: one token, then the outbox is what refuses
    rig.ok("__t, __code, __msg = GetBotByName('Alpha'):Stay()");
    CHECK(rig.ok("return tostring(__t)") == "nil");
    CHECK(rig.ok("return __code") == "queue-full");
    CHECK(rig.ok("return __msg") == "the order outbox is full (1024 orders wait for the world tick)");
    // A refusal spends no token: the queue-full call refilled the one token above and left it. The pump drains the
    // outbox, and the next call (same clock, no advance) is taken, not `rate-limited`.
    rig.e().pumpBots();
    CHECK(provider.submitted.size() == fl::BotHub::kOutboxMax);
    rig.ok("__afterT = GetBotByName('Alpha'):Stay()");
    CHECK(rig.ok("return tostring(__afterT)") != "nil");
    // Then the refill cycle carries on as before.
    rig.host.advance(100);
    CHECK(rig.ok("return tostring(GetBotByName('Alpha'):Stay())") != "nil");
}

TEST_CASE("bot orders: the open-ticket cap refuses the 4097th ticket and freeing one takes the next")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    rig.host.frozen = true;   // the tick clock: the rate refills are exact
    Party party;
    party.build(rig, provider);

    // The rate cap is per script file, so 32 script files fill one state's ticket table at 32 orders per 100 ms: 640 in
    // their first burst, then 32 per refill round for 108 rounds, 4096 tickets in 10.8 s (inside the 30 s `lost`).
    constexpr int kFillers = 32;
    constexpr int kRounds = 108;   // 32 * 20 + 32 * 108 = 4096
    for (int n = 1; n <= kFillers; ++n)
        rig.dir.write("fill" + std::to_string(n) + ".lua",
                      "function burst" + std::to_string(n) + "() local posted = 0 "
                      "for i = 1, 20 do if GetBotByName('Alpha'):Stay() then posted = posted + 1 end end "
                      "return posted end");
    fl::LoadReport const report = rig.e().load();
    REQUIRE(report.errors.empty());
    auto burst = [&](int n)
    {
        return std::stoull(rig.ok("return tostring(burst" + std::to_string(n) + "())"));
    };

    size_t posted = 0;
    for (int n = 1; n <= kFillers; ++n)
    {
        posted += burst(n);
        if (n % 16 == 0)
            rig.e().pumpBots();   // the outbox stays under its own 1024: 16 files' 20 orders are 320
    }
    rig.e().pumpBots();
    CHECK(posted == kFillers * fl::BotHub::kOrderBurst);
    for (int round = 0; round < kRounds; ++round)
    {
        rig.host.advance(100);
        for (int n = 1; n <= kFillers; ++n)
            posted += burst(n);
        rig.e().pumpBots();   // accepted orders leave the outbox; no outcome ever arrives, so the tickets stay open
    }
    CHECK(posted == fl::BotHub::kOpenTicketsMax);
    CHECK(provider.submitted.size() == fl::BotHub::kOpenTicketsMax);

    // The next ticket is refused with the documented code, and the refusal spends no token.
    rig.host.advance(100);
    rig.ok("__t, __code, __msg = GetBotByName('Alpha'):Stay()");
    CHECK(rig.ok("return tostring(__t)") == "nil");
    CHECK(rig.ok("return __code") == "queue-full");
    CHECK(rig.ok("return __msg") == "this state has 4096 order tickets open");

    // An outcome ends one ticket (`ok`), and the freed slot takes the next order.
    provider.outcomeBySeq[101] = "ok";   // the fake's first seq
    rig.e().pumpBots();
    CHECK(rig.ok("return tostring(GetBotByName('Alpha'):Stay())") != "nil");
    rig.e().pumpBots();
    CHECK(provider.submitted.size() == fl::BotHub::kOpenTicketsMax + 1);
}

TEST_CASE("bot orders: a ticket still running after 30 s ends lost, exactly once")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);
    std::string const ticket = rig.ok("return tostring(GetBotByName('Alpha'):Hold())");
    rig.e().pumpBots();
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 hold");
    rig.host.advance(29'000);
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "1");
    rig.host.advance(1'500);
    rig.e().pumpBots();
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 hold|failed Alpha " + ticket +
                              " 101 hold lost no outcome within 30 s");
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "2");
    // A late outcome finds no ticket and invents no second end.
    provider.outcomeBySeq[101] = "ok";
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "2");
}

TEST_CASE("bot orders: a reload cancels the old script's queued orders and keeps the new ones")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);
    std::string const oldTicket = rig.ok("return tostring(GetBotByName('Alpha'):Stay())");
    rig.dir.write("second.lua", std::string(kOrderLog) + " newTicket = GetBotByName('Alpha'):Flee()");
    fl::LoadReport const report = rig.e().reload();
    REQUIRE(report.applied);
    std::string const newTicket = rig.ok("return tostring(newTicket)");
    CHECK(newTicket != oldTicket);
    rig.e().pumpBots();
    REQUIRE(provider.submitted.size() == 1);   // the old ticket's order never reached the provider
    CHECK(provider.submitted[0].ticket == std::stoull(newTicket));
    CHECK(provider.submitted[0].verb == "flee");
    // The order that was still queued when the reload ran is reported cancelled, exactly once, before the new one goes
    // out: the old ticket has no other end.
    CHECK(logText(rig) == "failed Alpha " + oldTicket + " 0 stay cancelled " + kCancelledMessage +
                              "|given Alpha " + newTicket + " 101 flee");
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "2");
}

TEST_CASE("bot orders: a reload before the hand-off cancels the queued order before the provider sees it")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);
    std::string const ticket = rig.ok("return tostring(GetBotByName('Alpha'):Stay())");

    // The domain drop a map thread runs on a reload or a close, between the post and the world thread's pump.
    fl::botHub().dropOrders(rig.e().orderDomain());
    rig.e().pumpBots();

    // The order never reaches the provider, and the ticket still ends exactly once: ORDER_FAILED `cancelled`.
    CHECK(provider.submits == 0);
    CHECK(provider.submitted.empty());
    CHECK(logText(rig) == "failed Alpha " + ticket + " 0 stay cancelled " + kCancelledMessage);
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "1");
}

TEST_CASE("bot orders: a reload during submit keeps the accepted order's ticket and its terminal event")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLog);
    std::string const ticket = rig.ok("return tostring(GetBotByName('Alpha'):Stay())");
    provider.outcomeBySeq[101] = "ok";   // the fake's first seq: done at the same pump's outcome poll

    // The world thread is inside submit() (holding no hub lock) when the map thread drops the domain.
    std::mutex gate;
    std::condition_variable cv;
    bool inSubmit = false, release = false;
    provider.onSubmit = [&]
    {
        std::unique_lock<std::mutex> lock(gate);
        inSubmit = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
    };
    uint64_t const domain = rig.e().orderDomain();
    std::thread pump([&] { rig.e().pumpBots(); });
    {
        std::unique_lock<std::mutex> lock(gate);
        cv.wait(lock, [&] { return inSubmit; });
    }
    fl::botHub().dropOrders(domain);   // the reload: the old state's domain goes away mid-submit
    {
        std::lock_guard<std::mutex> lock(gate);
        release = true;
    }
    cv.notify_all();
    pump.join();
    provider.onSubmit = nullptr;

    // Committed before the drop: the ticket survives, so the script gets GIVEN and exactly one terminal event.
    REQUIRE(provider.submitted.size() == 1);
    CHECK(provider.submitted[0].ticket == std::stoull(ticket));
    CHECK(logText(rig) == "given Alpha " + ticket + " 101 stay|done Alpha " + ticket + " 101 stay");
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "2");
}

TEST_CASE("bot orders: a map state's script orders through the world pump")
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
    states.world()->runChunk(kOrderLog);

    // The map state's thread posts (the outbox is the one way in), the world thread hands it over and emits.
    fl::CallResult const posted = map->runChunk("ticket = GetBots()[1]:Guard()");
    REQUIRE(posted.ok);
    CHECK(provider.submitted.empty());
    states.world()->pumpBots();
    REQUIRE(provider.submitted.size() == 1);
    CHECK(provider.submitted[0].verb == "guard");
    CHECK(states.world()->runChunk("return tostring(GetBots()[1])").result != "nil");
    CHECK(states.world()->runChunk("return #log").result == "1");
    states.shutdown();
}

TEST_CASE("bot orders: test mode has the bot events and refuses orders")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig;   // test mode
    for (char const* call : {"BotsInterrupt({}, 1)", "BotCommanderPlace(1, 'place here')"})
    {
        rig.ok(std::string("__a, __b, __c = ") + call);
        INFO("call: " << call);
        CHECK(rig.ok("return tostring(__a)") == "nil");
        CHECK(rig.ok("return __b") == "test-mode");
        CHECK(rig.ok("return __c") == "orders are not available in test mode");
    }
    // The B1 surface of test mode is unchanged: no bot globals, no Bot class (the events are all there is).
    CHECK(rig.ok("return tostring(GetBots)") == "nil");
    CHECK(rig.ok("return tostring(GetBotByName)") == "nil");
    CHECK(rig.ok("return tostring(Follow)") == "nil");
    // And the bot events still arrive (B1), with the generic handle.
    rig.ok("log = {} Event.on('bot.order', function(b, t, s, v) log[#log+1] = v .. ' ' .. t end)");
    fl::BotInfo alpha = FakeBotProvider::bot(7, "Alpha");
    alpha.lastOrder = fl::OrderInfo{3, "stay", "", fl::OrderState::Pending, "", ""};
    provider.next.bots = {alpha};
    rig.e().pumpBots();
    CHECK(rig.ok("return log[1]") == "stay 0");
}

TEST_CASE("bot orders: the spread selftest script orders the party once and logs every outcome")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    // Both bots attack one creature at 25 % health.
    fl::HandleData const victim = FakeHost::creature(900, 0, 0, 1234);
    rig.host.add(victim, {"Mob", 1234, 0, 0, 0, 0, true});
    for (fl::BotInfo const& bot : {party.alpha, party.bravo})
        rig.host.add(bot.player, {bot.name, 0, 0, 0, 0, 0, true});
    rig.host.gameResults["unit.getVictim"] = {fl::EventValue(victim)};
    rig.host.gameResults["unit.getHealthPct"] = {fl::EventValue(25.0)};   // below the script's 30 %

    // The shipped script, loaded by the engine's own loader.
    std::string const script = readFile(FL_TEST_SELFTEST_DIR "/bot_spread_on_low_hp.lua");
    REQUIRE(!script.empty());
    rig.dir.write("bot_spread_on_low_hp.lua", script);
    fl::LoadReport const report = rig.e().load();
    REQUIRE(report.errors.empty());

    rig.host.advance(500);
    rig.e().tick();   // the 500 ms timer: one Flee per party bot
    CHECK(logMatching(rig.host, "Bot spread selftest: victim") == 1);
    provider.outcomeBySeq[101] = "ok";
    provider.outcomeBySeq[102] = "ok";
    rig.e().pumpBots();   // the orders go out: ORDER_GIVEN
    rig.e().pumpBots();   // the outcomes: ORDER_DONE
    CHECK(logMatching(rig.host, "Bot spread selftest: ordered Alpha ticket ") == 1);
    CHECK(logMatching(rig.host, "Bot spread selftest: ordered Bravo ticket ") == 1);
    CHECK(logMatching(rig.host, "Bot spread selftest: ticket ") == 2);
    CHECK(logMatching(rig.host, "done seq ") == 2);
    CHECK(logMatching(rig.host, "failed") == 0);
    // The next tick sees the same victim and orders nothing again (one spread per drop).
    rig.host.advance(500);
    rig.e().tick();
    rig.e().pumpBots();
    CHECK(logMatching(rig.host, "Bot spread selftest: ordered ") == 2);
}

TEST_CASE("bot orders: a map state posting while the world pumps (TSan)")
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

    std::atomic<bool> done{false};
    std::thread pump([&]
    {
        for (int i = 0; i < 200; ++i)
            states.world()->pumpBots();
        done.store(true, std::memory_order_release);
    });
    int posted = 0;
    while (!done.load(std::memory_order_acquire))
    {
        fl::CallResult const r = map->runChunk("local b = GetBots()[1] if b then return tostring(b:Stay()) end return 'none'");
        REQUIRE(r.ok);
        if (r.result != "nil" && r.result != "none")
            ++posted;
    }
    pump.join();
    states.shutdown();
    CHECK(posted > 0);
}

TEST_CASE("bot orders: provider unavailability ends every open order once, with `unavailable`")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLogTicket);

    // One ticket the provider accepted (running: its outcome and its 30 s deadline are still owed) ...
    std::string const running = rig.ok("return tostring(GetBotByName('Alpha'):Hold())");
    rig.e().pumpBots();
    REQUIRE(logText(rig) == "given " + running + " 101 hold");
    // ... and one still in the outbox, which the provider has never seen.
    std::string const queued = rig.ok("return tostring(GetBotByName('Alpha'):Stay())");
    int const outcomeCalls = provider.outcomeCalls;

    // The bot engine goes away (its OnShutdown): no order may disappear without its terminal event.
    provider.isAvailable = false;
    rig.e().pumpBots();
    REQUIRE(rig.ok("return #log") == "3");   // the GIVEN above, then one failed per open ticket
    std::string const text = logText(rig);
    CHECK(text.find("failed " + running + " 101 hold unavailable " + kUnavailableMessage) != std::string::npos);
    CHECK(text.find("failed " + queued + " 0 stay unavailable " + kUnavailableMessage) != std::string::npos);
    // Exactly once each: one "failed" and one "unavailable" per open ticket, nothing else.
    CHECK(rig.ok("return select(2, table.concat(log, '|'):gsub('failed', ''))") == "2");
    CHECK(rig.ok("return select(2, table.concat(log, '|'):gsub(' unavailable ', ''))") == "2");
    // The events went out before the hub state was forgotten: nothing is left to end twice.
    CHECK(fl::botHub().snapshot()->bots.empty());
    CHECK(provider.outcomeCalls == outcomeCalls);   // a dead provider is never polled

    // The state is already cleared: the next tick drops nothing twice and emits nothing.
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "3");

    // Recovery cannot resurrect a ticket: the old seq's late outcome and the returning provider emit nothing new.
    provider.isAvailable = true;
    provider.outcomeBySeq[101] = "ok";
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "3");
    CHECK(rig.ok("return #GetBots()") == "2");
}

TEST_CASE("bot orders: a provider that answers nothing ends failed, or unavailable if it went away inside submit")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLogTicket);

    // Available but silent: the provider did not answer the hand-off (a provider bug), so the ticket says `failed`.
    std::string const silent = rig.ok("return tostring(GetBotByName('Alpha'):Stay())");
    provider.answerNothing = true;
    rig.e().pumpBots();
    CHECK(logText(rig) == "failed " + silent + " 0 stay failed the bot provider did not answer this order");

    // Gone inside submit(): the same missing verdict means the provider is unavailable, not at fault: `unavailable`.
    rig.ok("log = {}");
    provider.answerNothing = false;
    std::string const gone = rig.ok("return tostring(GetBotByName('Bravo'):Hold())");
    provider.onSubmit = [&]
    {
        provider.isAvailable = false;
        provider.answerNothing = true;
    };
    rig.e().pumpBots();
    provider.onSubmit = nullptr;
    provider.answerNothing = false;
    CHECK(logText(rig) == "failed " + gone + " 0 hold unavailable " + kUnavailableMessage);
    // The ticket is gone: the next pump's clear() has nothing left to end a second time.
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "1");
}

TEST_CASE("bot orders: a reload and provider loss in the same tick end the order unavailable, not cancelled")
{
    FakeBotProvider provider;
    BotProviderGuard guard(&provider);
    Rig rig(elunaBots);
    Party party;
    party.build(rig, provider);
    rig.ok(kOrderLogTicket);

    // A ticket the provider accepted (its outcome is still owed) ...
    std::string const running = rig.ok("return tostring(GetBotByName('Alpha'):Hold())");
    rig.e().pumpBots();
    REQUIRE(logText(rig) == "given " + running + " 101 hold");
    // ... and one still queued when the state reloads; the provider goes away before the pump can report either.
    std::string const queued = rig.ok("return tostring(GetBotByName('Alpha'):Stay())");
    fl::botHub().dropOrders(rig.e().orderDomain());
    provider.isAvailable = false;
    rig.e().pumpBots();

    // The drop marked the queued order cancelled, but the provider is gone: its one terminal event is `unavailable`
    // (the reason every ticket the provider leaves open gets), never `cancelled`.
    std::string const text = logText(rig);
    CHECK(text.find("failed " + queued + " 0 stay unavailable " + kUnavailableMessage) != std::string::npos);
    CHECK(text.find("cancelled") == std::string::npos);
    CHECK(text.find("failed " + running + " 101 hold unavailable " + kUnavailableMessage) != std::string::npos);
    CHECK(rig.ok("return select(2, table.concat(log, '|'):gsub(' unavailable ', ''))") == "2");
    // Drained once: the next tick emits nothing.
    rig.e().pumpBots();
    CHECK(rig.ok("return #log") == "3");
}
