// The review-fix round for E0/E1 on the fake world: every finding that has an engine-side behaviour gets a case that
// fails on the old code. The host side (access rules, routing, settings, DB policy) is in test_host_*.cpp.
#include "Support.h"

#include "AccessRules.h"
#include "Settings.h"
#include "engine/GuidText.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace fltest;

namespace
{
// A rig whose engine config is the module's REAL config loading path (flh::settingsFrom), not hand-set budgets.
struct SettingsRig : Rig
{
    explicit SettingsRig(std::map<std::string, std::string> conf)
        : Rig([&](fl::Config& c) { applySettings(c, conf); })
    {
    }
};

struct ElunaRig : Rig
{
    ElunaRig() : Rig([](fl::Config& c) { c.mode = fl::EngineMode::Eluna; c.eventBudget = {0, 0}; c.commandBudget = {0, 0};
                                         c.loadBudget = {0, 0}; c.tickBudgetMs = 0; }) { }
    explicit ElunaRig(std::function<void(fl::Config&)> more)
        : Rig([&](fl::Config& c)
              {
                  c.mode = fl::EngineMode::Eluna;
                  c.eventBudget = {0, 0};
                  c.commandBudget = {0, 0};
                  c.loadBudget = {0, 0};
                  c.tickBudgetMs = 0;
                  more(c);
              })
    {
    }
};

std::string errorOf(Rig& rig, std::string const& code)
{
    fl::CallResult r = rig.run(code);
    return r.ok ? std::string("no error") : r.error;
}

void storePlayer(Rig& rig, fl::HandleData const& p)
{
    rig.ok("RegisterPlayerEvent(3, function(ev, player) stored = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
}

fl::ObjectInfo named(char const* n)
{
    fl::ObjectInfo i;
    i.name = n;
    return i;
}

// Runs `f` on its own thread inside a map scope (the thread a map update would be), and waits for it.
template <typename F>
void onMapThread(uint32_t map, uint32_t instance, F f)
{
    std::thread t([&] {
        flh::MapScope scope(map, instance);
        f();
    });
    t.join();
}
}  // namespace

// ---- finding 1: the module's real settings, budgets really off ----

TEST_CASE("fix 1: eluna mode defaults through the settings loader run with no instruction and no clock budget")
{
    SettingsRig rig({{"ForeverLua.Mode", "eluna"}});
    CHECK(rig.cfg.eventBudget.maxMs == 0);
    CHECK(rig.cfg.eventBudget.maxInstr == 0);
    CHECK(rig.cfg.commandBudget.maxInstr == 0);
    // The old defaulting left 10'000'000 instructions: this loop (60M) was killed with "budget".
    fl::CallResult r = rig.run("local x = 0 for i = 1, 60000000 do x = x + 1 end return x");
    CHECK(r.ok);
    CHECK_FALSE(r.budget);
    // ... and a handler run by an event is unbounded the same way.
    fl::HandleData p = FakeHost::player(1);
    rig.host.add(p, named("A"));
    rig.ok("RegisterPlayerEvent(3, function(ev, player) local x = 0 for i = 1, 30000000 do x = x + 1 end done = x end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    CHECK(rig.ok("return done") == "30000000");
}

TEST_CASE("fix 1: a configured budget applies in eluna mode, each field on its own, and test mode keeps its minimum")
{
    SettingsRig timed({{"ForeverLua.Mode", "eluna"}, {"ForeverLua.EventBudgetMs", "5"}});
    CHECK(timed.cfg.eventBudget.maxMs == 5);
    fl::CallResult cut = timed.run("while true do end");
    CHECK_FALSE(cut.ok);
    CHECK(cut.budget);

    // Instruction budget alone (0 ms = no clock): the hook still counts instructions (the "checked in the hook path" part).
    ElunaRig instr([](fl::Config& c) { c.eventBudget = {0, 5000}; });
    fl::CallResult byInstr = instr.run("while true do end");
    CHECK_FALSE(byInstr.ok);
    CHECK(byInstr.budget);
    // Clock alone (0 instructions = no count limit).
    ElunaRig clock([](fl::Config& c) { c.eventBudget = {3, 0}; });
    fl::CallResult byClock = clock.run("while true do end");
    CHECK_FALSE(byClock.ok);
    CHECK(byClock.budget);

    // Test mode: a 0 in the config is raised to 1 (a 0 ms budget would kill every handler), as before the E0 round.
    SettingsRig test({{"ForeverLua.EventBudgetMs", "0"}, {"ForeverLua.TickBudgetMs", "0"}, {"ForeverLua.CommandBudgetMs", "0"}});
    CHECK(test.cfg.mode == fl::EngineMode::Test);
    CHECK(test.cfg.eventBudget.maxMs == 1);
    CHECK(test.cfg.commandBudget.maxMs == 1);
    CHECK(test.cfg.tickBudgetMs == 1);
    CHECK(test.cfg.eventBudget.maxInstr == 10'000'000);
}

// ---- finding 12 (review, high): nested calls keep the outer budget ----

TEST_CASE("fix 12: a nested event inside a handler does not erase the outer call's budget")
{
    ElunaRig rig([](fl::Config& c) { c.eventBudget = {30, 50'000'000}; });
    fl::HandleData p = FakeHost::player(7);
    rig.host.add(p, named("Nested"));
    std::atomic<int> nested{0};
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const&,
                              std::vector<fl::EventValue>&)
    {
        if (op == "object.getScale" && nested++ == 0)
            rig.e().emitEluna(fl::EluKind::Player, 0, 4, {fl::EventValue(p)});   // a quick inner handler
        return false;
    };
    rig.ok("RegisterPlayerEvent(4, function() inner = 1 end)");
    rig.ok("RegisterPlayerEvent(3, function(ev, player) pcall(player.GetScale, player) while true do end end)");
    auto started = std::chrono::steady_clock::now();
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});   // the old code hung here for good
    auto took = std::chrono::steady_clock::now() - started;
    CHECK(rig.ok("return inner") == "1");
    CHECK(took < std::chrono::seconds(10));
    CHECK(rig.host.logContains("handler #"));   // the outer handler was cut off by its budget and logged
}

// ---- finding 3: handlers cancel themselves, the next one, or everything, mid-dispatch ----

TEST_CASE("fix 3: handlers that cancel themselves, the next handler, or clear everything are safe (ASan)")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(8);
    rig.host.add(p, named("Cancel"));
    // 1. a handler unregisters itself
    rig.ok("selfcalls = 0 selfcancel = RegisterPlayerEvent(3, function() selfcalls = selfcalls + 1 selfcancel() end)");
    // 2. a handler cancels the one registered after it
    rig.ok("second = 0 cancelnext = nil first = RegisterPlayerEvent(3, function() cancelnext() end)\n"
           "cancelnext = RegisterPlayerEvent(3, function() second = second + 1 end)");
    // 3. a handler with shots left clears the whole family
    rig.ok("third = 0 RegisterPlayerEvent(3, function() ClearPlayerEvents() third = third + 1 end)\n"
           "RegisterPlayerEvent(3, function() third = third + 100 end)");
    fl::EventResult r = rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    CHECK(r.calls >= 1);
    CHECK(rig.ok("return selfcalls") == "1");
    CHECK(rig.ok("return second") == "0");     // cancelled before its turn: never ran
    CHECK(rig.ok("return third") == "1");      // the handler after the Clear never ran either
    CHECK(rig.e().status().elunaHandlers == 0);
    // Nothing left: a second dispatch is a no-op, and a fresh registration works again.
    CHECK(rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)}).calls == 0);
    rig.ok("again = 0 RegisterPlayerEvent(3, function() again = again + 1 end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    CHECK(rig.ok("return again") == "1");

    // A one-shot handler that also errors, cancels itself in the same call, and a unique-creature handler that clears its
    // own creature's events.
    fl::HandleData c = FakeHost::creature(9, 0, 0, 55);
    rig.host.add(c, named("C"));
    rig.ok("uc = 0 RegisterUniqueCreatureEvent('" + fl::guidText(c.guid) + "', 0, 4, function()\n"
           "  uc = uc + 1 ClearUniqueCreatureEvents('" + fl::guidText(c.guid) + "', 0) error('after clear') end)");
    fl::EventResult u = rig.e().emitElunaUnique(fl::EluKind::Creature, c.guid, 0, 4, {fl::EventValue(c), fl::EventValue()});
    CHECK(u.calls == 1);
    CHECK(rig.ok("return uc") == "1");
    CHECK(rig.e().status().elunaHandlers == 1);   // only the `again` handler is left
}

// ---- finding 4: the guid keeps its 128 bits ----

TEST_CASE("fix 4: GetGUID carries the high half and every guid helper agrees with the fake core")
{
    ElunaRig rig;
    fl::HandleData pl = FakeHost::player(51, 0);
    fl::HandleData cr = FakeHost::creature(51, 0, 0, 1234);   // the same low counter as the player
    rig.host.add(pl, named("Both"));
    rig.host.add(cr, named("Mob"));
    rig.host.players.push_back(pl);
    rig.ok("RegisterPlayerEvent(3, function(ev, player, creature) P, C = player, creature end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(pl), fl::EventValue(cr)});
    CHECK(rig.ok("return P:GetGUID()") == fl::guidText(pl.guid));
    CHECK(rig.ok("return C:GetGUID()") == fl::guidText(cr.guid));
    CHECK(rig.ok("return tostring(P:GetGUID() == C:GetGUID())") == "false");     // same low, different high
    CHECK(rig.ok("return tostring(P:GetGUIDLow() == C:GetGUIDLow())") == "true");
    CHECK(rig.ok("return GetGUIDType(P:GetGUID()) .. ',' .. GetGUIDType(C:GetGUID())") == "4,9");
    CHECK(rig.ok("return GetGUIDEntry(C:GetGUID()) .. ',' .. C:GetEntry()") == "1234,1234");
    CHECK(rig.ok("return GetGUIDEntry(P:GetGUID())") == "0");
    CHECK(rig.ok("return GetGUIDLow(C:GetGUID())") == "51");
    // The lookup matches the whole guid: the creature's guid is no player, the player's guid finds the player.
    CHECK(rig.ok("return tostring(GetPlayerByGUID(C:GetGUID()))") == "nil");
    CHECK(rig.ok("return GetPlayerByGUID(P:GetGUID()):GetGUID()") == fl::guidText(pl.guid));
    // A bad guid argument is an error naming the argument, not a silent zero.
    CHECK(errorOf(rig, "GetPlayerByGUID('zz')").find("guid") != std::string::npos);
    CHECK(errorOf(rig, "GetGUIDType({})").find("guid") != std::string::npos);

    // The round trip of the pure helpers.
    fl::Guid g{0xDEADBEEF12345678ull, 0x0123456789ABCDEFull};
    fl::Guid back;
    REQUIRE(fl::parseGuidText(fl::guidText(g), back));
    CHECK(back == g);
    CHECK_FALSE(fl::parseGuidText("123", back));
    CHECK_FALSE(fl::parseGuidText(std::string(32, 'g'), back));
}

// ---- finding 5: the access check comes first, against the object's CURRENT map ----

TEST_CASE("fix 5: a map thread never looks up a foreign or stale handle: access is checked first")
{
    ElunaRig rig;
    fl::HandleData pl = FakeHost::player(31, 0);
    rig.host.add(pl, named("Mover"));
    fl::HandleData cr = FakeHost::creature(32, 1, 0, 5);   // lives on map 1
    rig.host.add(cr, named("Elsewhere"));
    storePlayer(rig, pl);
    rig.ok("RegisterPlayerEvent(4, function(ev, player, creature) cre = creature end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 4, {fl::EventValue(pl), fl::EventValue(cr)});
    // On the world thread with the maps idle everything resolves.
    CHECK(errorOf(rig, "return stored:GetLevel()") != "");   // reaches the host (no op: "not available"), not a map error
    CHECK(errorOf(rig, "stored:GetLevel()").find("another map") == std::string::npos);

    // A map-0 thread: its own player is fine; a creature of map 1 is refused WITHOUT a lookup.
    size_t before = rig.host.lookupCalls;
    std::string foreign, mine;
    onMapThread(0, 0, [&] {
        foreign = errorOf(rig, "cre:GetHealth()");
        mine = errorOf(rig, "stored:GetHealth()");
    });
    CHECK(foreign.find("object is on another map") != std::string::npos);
    CHECK(mine.find("another map") == std::string::npos);
    // Exactly one lookup (the own player's); the foreign creature's was never looked up.
    CHECK(rig.host.lookupCalls == before + 1);

    // The player teleports into instance 33: its old handle (map 0) is stale. On the map-0 thread it answers "no longer
    // exists" and the host is not asked at all; on the map-0/33 thread it is gone as well (the handle names map 0).
    rig.host.moveTo(pl, 0, 33);
    before = rig.host.lookupCalls;
    std::string stale, staleOnNew;
    onMapThread(0, 0, [&] { stale = errorOf(rig, "stored:SetHealth(1)"); });
    onMapThread(0, 33, [&] { staleOnNew = errorOf(rig, "stored:SetHealth(1)"); });
    CHECK(stale.find("Player no longer exists") != std::string::npos);
    CHECK(staleOnNew.find("Player no longer exists") != std::string::npos);
    CHECK(rig.host.lookupCalls == before);
    // IsInWorld asks the same rule: a stale handle is "not in the world", answered without touching the object.
    bool inWorld = true;
    onMapThread(0, 0, [&] { inWorld = rig.ok("return tostring(stored:IsInWorld())") == "true"; });
    CHECK_FALSE(inWorld);
    CHECK(rig.host.lookupCalls == before);

    // A fresh handle made on the player's new map works there and answers GetMapId/GetInstanceId from the live object.
    fl::HandleData fresh = FakeHost::player(31, 0, 33);
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(fresh)});
    std::string ok;
    onMapThread(0, 33, [&] { ok = rig.ok("return stored:GetMapId() .. ',' .. stored:GetInstanceId()"); });
    CHECK(ok == "0,33");
    // On the world thread the old (stale) handle still resolves and reports the map the player is on NOW.
    rig.ok("RegisterPlayerEvent(5, function(ev, player) old = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 5, {fl::EventValue(pl)});
    CHECK(rig.ok("return old:GetMapId() .. ',' .. old:GetInstanceId()") == "0,33");
    // A scope that is not any map at all (a worker thread, not the world thread) refuses.
    std::string worker;
    std::thread t([&] { worker = errorOf(rig, "stored:GetHealth()"); });
    t.join();
    CHECK(worker.find("object is on another map") != std::string::npos);
}

// ---- finding 6: RegisterEvent gets the object, and the events leave with it ----

TEST_CASE("fix 6: RegisterEvent callbacks get the object as the 4th argument and die with their object")
{
    ElunaRig rig;
    rig.host.frozen = true;
    fl::HandleData boss = FakeHost::creature(3, 0, 0, 90);
    rig.host.add(boss, named("Boss"));
    rig.ok("RegisterCreatureEvent(90, 1, function(ev, creature) obj = creature end)");
    rig.e().emitEluna(fl::EluKind::Creature, 90, 1, {fl::EventValue(boss), fl::EventValue()});
    rig.ok("args = {} obj:RegisterEvent(function(id, delay, repeats, c) args = {id, delay, repeats, c:GetGUIDLow(), c == obj} end, 10, 2)");
    rig.host.advance(15);
    rig.e().tick();
    CHECK(rig.ok("return tostring(args[2]) .. ',' .. tostring(args[3]) .. ',' .. tostring(args[4]) .. ',' .. tostring(args[5])") ==
          "10,2,3,true");

    // A player's repeating event stops when the player leaves the world: no more shots, no pile-up of timers.
    fl::HandleData pl = FakeHost::player(4, 0);
    rig.host.add(pl, named("Leaver"));
    rig.ok("RegisterPlayerEvent(3, function(ev, player) lp = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(pl)});
    rig.ok("shots = 0 for i = 1, 20 do lp:RegisterEvent(function() shots = shots + 1 end, 10, 0) end");
    CHECK(rig.e().status().timers >= 20);
    rig.host.advance(15);
    rig.e().tick();
    CHECK(rig.ok("return shots") == "20");
    rig.host.remove(pl);                 // logout / left the world
    rig.host.advance(15);
    rig.e().tick();                       // each event looks at its owner before it runs: dropped, not called
    CHECK(rig.ok("return shots") == "20");
    CHECK(rig.e().status().timers == 0);

    // The same through the host's "object gone" hook (logout): removeLuaEventsOf drops them at once.
    fl::HandleData pl2 = FakeHost::player(5, 0);
    rig.host.add(pl2, named("Leaver2"));
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(pl2)});
    rig.ok("lp:RegisterEvent(function() end, 100000, 0) lp:RegisterEvent(function() end, 100000, 0)");
    size_t const timersBefore = rig.e().status().timers;
    CHECK(rig.e().removeLuaEventsOf(pl2) == 2);
    CHECK(rig.e().status().timers == timersBefore - 2);

    // A long-delay event of an object that is gone is found by the periodic owner sweep, before it ever fires.
    fl::HandleData pl3 = FakeHost::player(6, 0);
    rig.host.add(pl3, named("Leaver3"));
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(pl3)});
    rig.ok("lp:RegisterEvent(function() long = 1 end, 600000, 0)");
    size_t const withLong = rig.e().status().timers;
    rig.host.remove(pl3);
    rig.host.advance(1500);
    rig.e().tick();
    CHECK(rig.e().status().timers == withLong - 1);
}

// ---- findings 8: spawn and RunCommand only from the right thread ----

TEST_CASE("fix 8: PerformIngameSpawn and RunCommand run on the right thread or queue to the world thread")
{
    ElunaRig rig;
    rig.host.spawnResult = FakeHost::creature(70, 1, 0, 5);
    // World thread with the maps idle: spawns now, any map; RunCommand runs now.
    CHECK(rig.ok("return PerformIngameSpawn(1, 100, 1, 0, 1, 2, 3, 4):GetGUIDLow()") == "70");
    rig.ok("RunCommand('server info')");
    REQUIRE(rig.host.commands.size() == 1);
    CHECK(rig.host.spawns.size() == 1);

    // A map-0 thread: its own map spawns at once; map 1 is queued (nil now); RunCommand is queued.
    std::string own, other;
    onMapThread(0, 0, [&] {
        own = rig.ok("return tostring(PerformIngameSpawn(1, 100, 0, 0, 1, 2, 3, 4) ~= nil)");
        other = rig.ok("return tostring(PerformIngameSpawn(1, 101, 1, 0, 1, 2, 3, 4))");
        rig.ok("RunCommand('reload all')");
    });
    CHECK(own == "true");
    CHECK(other == "nil");
    CHECK(rig.host.spawns.size() == 2);        // only the own-map one
    CHECK(rig.host.commands.size() == 1);      // not yet
    CHECK(rig.host.deferred.size() == 2);
    // The world thread drains after the maps: both run there.
    CHECK(rig.host.deferred.drain() == 2);
    CHECK(rig.host.spawns.size() == 3);
    REQUIRE(rig.host.commands.size() == 2);
    CHECK(rig.host.commands[1] == "reload all");
    // The maps are updating (no scope, world thread): the same queueing applies.
    rig.host.rules.setMapsIdle(false);
    rig.ok("RunCommand('during maps')");
    CHECK(rig.host.commands.size() == 2);
    rig.host.rules.setMapsIdle(true);
    rig.host.deferred.drain();
    CHECK(rig.host.commands.size() == 3);
}

// ---- finding 16: docs signatures ----

TEST_CASE("fix 16: optional arguments follow the docs pages (GetPower, SetPower, DealDamage)")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(41), t = FakeHost::creature(42);
    rig.host.add(p, named("A"));
    rig.host.add(t, named("T"));
    storePlayer(rig, p);
    rig.ok("RegisterPlayerEvent(4, function(ev, player, target) tgt = target end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 4, {fl::EventValue(p), fl::EventValue(t)});
    std::map<std::string, std::vector<fl::EventValue>> seen;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const& args,
                              std::vector<fl::EventValue>& out)
    {
        seen[op] = args;
        out.clear();
        if (op == "unit.getPower")
            out.emplace_back(int64_t(9));
        return true;
    };
    CHECK(rig.ok("return stored:GetPower()") == "9");                 // Unit:GetPower([type]), -1 = current
    CHECK(std::get<int64_t>(seen["unit.getPower"][0].v) == -1);
    rig.ok("stored:GetPower(3)");
    CHECK(std::get<int64_t>(seen["unit.getPower"][0].v) == 3);
    rig.ok("stored:SetPower(50)");                                    // Unit:SetPower(amount [, type])
    REQUIRE(seen["unit.setPower"].size() == 2);
    CHECK(std::get<int64_t>(seen["unit.setPower"][0].v) == 50);
    CHECK(std::get<int64_t>(seen["unit.setPower"][1].v) == -1);
    CHECK(errorOf(rig, "stored:SetPower()").find("number expected") != std::string::npos);   // the amount is required
    rig.ok("stored:DealDamage(tgt, 100)");                            // Unit:DealDamage(target, damage[, durabilityloss[, school[, spell]]])
    REQUIRE(seen["unit.dealDamage"].size() == 5);
    CHECK(std::get<int64_t>(seen["unit.dealDamage"][1].v) == 100);
    CHECK(std::get<bool>(seen["unit.dealDamage"][2].v) == true);      // durabilityloss default true
    CHECK(std::get<int64_t>(seen["unit.dealDamage"][3].v) == 7);      // school default MAX_SPELL_SCHOOL
    CHECK(std::get<int64_t>(seen["unit.dealDamage"][4].v) == 0);      // spell default 0
    rig.ok("stored:SetHealth(33)");
    CHECK(errorOf(rig, "stored:SetHealth()").find("number expected") != std::string::npos);
}

// ---- finding 17: the listening check takes no Lua lock ----

TEST_CASE("fix 17: asking whether anyone listens does not wait for a running handler")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(61);
    rig.host.add(p, named("Busy"));
    std::mutex m;
    std::condition_variable cv;
    bool inside = false, release = false;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const&,
                              std::vector<fl::EventValue>& out)
    {
        if (op != "object.getScale")
            return false;
        std::unique_lock<std::mutex> lock(m);
        inside = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
        out.clear();
        return true;
    };
    rig.ok("RegisterPlayerEvent(3, function(ev, player) player:GetScale() end) RegisterCreatureEvent(5, 7, function() end)");
    std::thread runner([&] {
        flh::MapScope scope(0, 0);   // a map thread: its handler reaches its own player, so it really enters the host call
        rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    });
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return inside; });
    }
    // The handler holds the Lua lock and is stuck. The hooks' check must still answer at once.
    auto answer = std::async(std::launch::async, [&] {
        return rig.e().listeningEluna(fl::EluKind::Creature, 5, 7) && !rig.e().listeningEluna(fl::EluKind::Creature, 6, 7) &&
               !rig.e().listeningEluna(fl::EluKind::Creature, 5, 8) &&
               !rig.e().listeningElunaUnique(fl::EluKind::Creature, fl::Guid{1, 2}, 0, 7);
    });
    bool const ready = answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    cv.notify_all();
    runner.join();
    REQUIRE(ready);
    CHECK(answer.get());
}

// ---- finding 20: a copied metatable is not a handle ----

TEST_CASE("fix 20: a foreign userdata given a handle metatable is refused, not read as a handle")
{
    ElunaRig rig([](fl::Config& c) { c.openStdLibs = true; });
    fl::HandleData p = FakeHost::player(71);
    rig.host.add(p, named("Real"));
    storePlayer(rig, p);
    // io.stdout is a 16-byte userdata; with the handle metatable it would be read as a 40-byte HandleData.
    fl::CallResult r = rig.run("debug.setmetatable(io.stdout, debug.getmetatable(stored)) return io.stdout:GetGUID()");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("Eluna object") != std::string::npos);
    // A genuine handle still works, and so does a handle passed as an argument.
    CHECK(rig.ok("return stored:GetGUIDLow()") == "71");
}

// ---- finding 21: query getters ----

TEST_CASE("fix 21: the typed query getters wrap like the C types and GetString turns a double into text")
{
    ElunaRig rig;
    fl::DbRows rows;
    rows.columns = {"a", "b", "c", "d"};
    rows.rows = {{int64_t(300), int64_t(-129), 2.5, int64_t(-2147483649ll)}};
    rig.host.rawResults["SELECT 1"] = rows;
    rig.ok("q = WorldDBQuery('SELECT 1')");
    CHECK(rig.ok("return q:GetUInt8(0)") == "44");           // 300 as a uint8, not clamped to 255
    CHECK(rig.ok("return q:GetInt8(1)") == "127");           // -129 wraps
    CHECK(rig.ok("return q:GetInt32(3)") == "2147483647");   // -2147483649 wraps
    CHECK(rig.ok("return type(q:GetString(2)) .. q:GetString(2)") == "string2.5");
    rows.rows = {{int64_t(-128), int64_t(-32768), int64_t(-2147483648ll), int64_t(0)}};
    rig.host.rawResults["SELECT 2"] = rows;
    rig.ok("q2 = WorldDBQuery('SELECT 2')");
    CHECK(rig.ok("return q2:GetInt8(0) .. ',' .. q2:GetInt16(1) .. ',' .. q2:GetInt32(2)") == "-128,-32768,-2147483648");
}

// ---- finding 26: a ref released at the depth cap ----

TEST_CASE("fix 26: removing a timed event at the nesting cap still releases its callback")
{
    ElunaRig rig;
    rig.host.frozen = true;
    fl::HandleData p = FakeHost::player(81);
    rig.host.add(p, named("Deep"));
    rig.ok("probe = setmetatable({}, {__mode = 'k'}) weak = setmetatable({}, {__mode = 'v'})\n"
           "do local token = {} weak[1] = token tid = CreateLuaEvent(function() return token end, 100000, 0) end\n"
           "depth = 0 RegisterPlayerEvent(3, function(ev, player)\n"
           "  depth = depth + 1\n"
           "  if depth < 8 then player:GetScale() else RemoveEventById(tid) end\n"
           "end)");
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const&,
                              std::vector<fl::EventValue>&)
    {
        if (op == "object.getScale")
            rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
        return false;
    };
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    CHECK(rig.ok("return depth") == "8");
    rig.e().tick();   // the deferred release runs at depth 0
    rig.ok("for i = 1, 50 do collectgarbage('step', 100000) end");
    CHECK(rig.ok("return tostring(weak[1])") == "nil");
}

// ---- finding 27: where() reads the running coroutine ----

TEST_CASE("fix 27: the script:line of a failed query comes from the running coroutine")
{
    ElunaRig rig;
    rig.host.rawFail = true;
    rig.ok("local co = coroutine.create(function()\n\n  WorldDBQuery('SELECT 1')\nend) assert(coroutine.resume(co))");
    CHECK(rig.host.logContains("test:3: world query failed"));
}

// ---- finding 30: loader ----

TEST_CASE("fix 30: a symlink out of the folder is refused with its path, and folders sort by component")
{
    ElunaRig rig;
    rig.dir.write("a/init.lua", "order = (order or '') .. 'a ' ");
    rig.dir.write("a-x/z.lua", "order = (order or '') .. 'a-x ' ");
    rig.dir.write("a/deep/q.lua", "order = (order or '') .. 'a/deep ' ");
    std::error_code ec;
    std::filesystem::create_symlink("/etc/hostname", std::filesystem::path(rig.dir.path()) / "escape.lua", ec);
    REQUIRE_FALSE(ec);
    fl::LoadReport rep = rig.e().load();
    CHECK(rig.ok("return order") == "a a/deep a-x ");     // "a" < "a/deep" < "a-x", not "a" < "a-x" < "a/deep"
    bool named = false;
    for (auto const& e : rep.errors)
        named = named || (e.find("escape.lua") != std::string::npos && e.find("outside the script folder") != std::string::npos);
    CHECK(named);
}

// ---- finding 29 (and review, medium): a failed reload does not tell the old state it closed ----

TEST_CASE("fix 29: the close callback runs only when the reload is applied")
{
    ElunaRig rig;
    rig.dir.write("ok.lua", "x = 1");
    REQUIRE(rig.e().load().errors.empty());
    int closed = 0;
    // A candidate with a syntax error: not applied, the callback never runs, the old state is intact.
    rig.dir.write("bad.lua", "this is not lua");
    fl::LoadReport failed = rig.e().reload([&] { ++closed; });
    CHECK_FALSE(failed.applied);
    CHECK(closed == 0);
    CHECK(rig.ok("return x") == "1");
    rig.dir.remove("bad.lua");
    fl::LoadReport good = rig.e().reload([&]
    {
        ++closed;
        CHECK(rig.ok("return x") == "1");   // runs with the OLD state still live
    });
    CHECK(good.applied);
    CHECK(closed == 1);
}

// ---- finding 22: one SQL/string cap everywhere ----

TEST_CASE("fix 22: string arguments up to 256 KB are accepted, like the SQL cap of LuaDb")
{
    ElunaRig rig;
    rig.host.rawResults["x"] = fl::DbRows{};
    CHECK(errorOf(rig, "SendWorldMessage(string.rep('x', 200 * 1024))") == "no error");
    CHECK(errorOf(rig, "SendWorldMessage(string.rep('x', 300 * 1024))").find("longer than 256 KB") != std::string::npos);
}
