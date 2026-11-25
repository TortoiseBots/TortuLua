// Guards: a script that never returns, that hides behind pcall or a coroutine, or that hogs the tick.
#include "Support.h"

#include <thread>

using namespace fltest;

namespace
{
// The wall-clock budget cases run on the fake host's frozen clock where every read moves it by 1 ms (a deterministic busy
// host), so a slow or preempted CI machine cannot trip or miss a budget. Spins until that clock moved by `ms`.
std::string spin(int ms)
{
    return "local t = World.clock() while World.clock() - t < " + std::to_string(ms) + " do end ";
}
void useTickClock(Rig& rig)
{
    rig.host.frozen = true;
    rig.host.tickMs = 1;
}
}  // namespace

TEST_CASE("budget: an endless loop dies on the instruction budget")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {60000, 200000}; });
    auto r = rig.run("while true do end");
    CHECK_FALSE(r.ok);
    CHECK(r.budget);
    CHECK(r.error == "budget");
    // the state is fine afterwards and the next call gets a fresh budget
    CHECK(rig.ok("return 1 + 1") == "2");
}

TEST_CASE("budget: an endless loop dies on the time budget")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {30, ~uint64_t(0) / 2}; });
    useTickClock(rig);
    uint64_t t0 = rig.host.nowMs();
    auto r = rig.run("local n = 0 while true do n = n + 1 end");
    uint64_t took = rig.host.nowMs() - t0;
    CHECK(r.budget);
    CHECK(took < 2000);
    CHECK(rig.ok("return 3") == "3");
}

TEST_CASE("budget: pcall inside the script cannot swallow the budget error")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {60000, 300000}; });
    auto r = rig.run("while true do pcall(function() while true do end end) end");
    CHECK(r.budget);
    auto r2 = rig.run("while true do pcall(error, 'x') end");
    CHECK(r2.budget);
    auto r3 = rig.run("local ok, e = pcall(function() while true do end end) while true do end");
    CHECK(r3.budget);
    CHECK(rig.ok("return 5") == "5");
}

TEST_CASE("budget: a coroutine cannot hide the loop either")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {60000, 300000}; });
    CHECK(rig.run("local co = coroutine.wrap(function() while true do end end) co()").budget);
    CHECK(rig.run("local co = coroutine.create(function() while true do end end) while true do coroutine.resume(co) end").budget);
    CHECK(rig.run("local co = coroutine.create(function() while true do pcall(function() while true do end end) end end) "
                  "while true do coroutine.resume(co) end").budget);
    CHECK(rig.ok("return 'alive'") == "alive");
}

TEST_CASE("budget: a handler that loops does not stop the next handler or the server")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {60000, 200000}; });
    rig.ok("hits = 0 Event.on('t', function() while true do end end) Event.on('t', function() hits = hits + 1 end)");
    rig.emit("t");
    CHECK(rig.ok("return hits") == "1");
    CHECK(rig.host.logContains("budget"));
}

TEST_CASE("budget: three breaks in the window disable a handler, an old break does not count")
{
    SECTION("three quick breaks")
    {
        Rig rig([](fl::Config& c) { c.eventBudget = {60000, 100000}; });
        rig.ok("runs = 0 Event.on('t', function() runs = runs + 1 while true do end end)");
        for (int i = 0; i < 5; ++i)
            rig.emit("t");
        CHECK(rig.ok("return runs") == "3");
        CHECK(rig.e().status().disabledHandlers == 1);
        CHECK(rig.host.logContains("disabled after 3 budget breaks"));
    }
    SECTION("breaks spread over more than the window")
    {
        Rig rig([](fl::Config& c) { c.eventBudget = {60000, 100000}; c.breakWindowMs = 1000; });
        rig.ok("runs = 0 Event.on('t', function() runs = runs + 1 while true do end end)");
        for (int i = 0; i < 5; ++i)
        {
            rig.emit("t");
            rig.e().tick();   // past the per-tick window an emit is queued (N4): the tick runs it
            rig.host.advance(2000);
        }
        CHECK(rig.ok("return runs") == "5");
        CHECK(rig.e().status().disabledHandlers == 0);
    }
}

TEST_CASE("budget: the tick cap leaves the rest of the queue for the next tick")
{
    Rig rig([](fl::Config& c) { c.tickBudgetMs = 20; c.eventBudget = {200, 100000000}; });
    useTickClock(rig);
    rig.ok("count = 0 Event.on('slow', function() " + spin(6) + " count = count + 1 end)");
    for (int i = 0; i < 40; ++i)
    {
        fl::QueuedEvent ev;
        ev.name = "slow";
        REQUIRE(rig.e().post(std::move(ev)));
    }
    size_t first = rig.e().tick();
    CHECK(first >= 1);
    CHECK(first < 40);
    CHECK(rig.e().status().queued == 40 - first);
    size_t total = first;
    for (int guard = 0; guard < 100 && rig.e().status().queued; ++guard)
        total += rig.e().tick();
    CHECK(total == 40);
    CHECK(rig.ok("return count") == "40");
}

TEST_CASE("budget: a tick always makes progress even when one item eats the whole cap")
{
    Rig rig([](fl::Config& c) { c.tickBudgetMs = 1; c.eventBudget = {200, 100000000}; });
    useTickClock(rig);
    rig.ok("count = 0 Event.on('slow', function() " + spin(5) + " count = count + 1 end)");
    for (int i = 0; i < 3; ++i)
    {
        fl::QueuedEvent ev;
        ev.name = "slow";
        rig.e().post(std::move(ev));
    }
    CHECK(rig.e().tick() == 1);
    CHECK(rig.e().tick() == 1);
    CHECK(rig.e().tick() == 1);
    CHECK(rig.ok("return count") == "3");
}

TEST_CASE("budget: server.stop style events get the command budget")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {15, 1000000000}; c.commandBudget = {2000, 1000000000}; });
    useTickClock(rig);
    rig.ok("done = 0 Event.on('server.stop', function() " + spin(60) + " done = done + 1 end)");
    rig.emit("server.stop");
    CHECK(rig.ok("return done") == "0");   // normal budget: cut off at 15 ms
    fl::QueuedEvent ev;
    ev.name = "server.stop";
    ev.longBudget = true;
    rig.e().emit(std::move(ev));
    CHECK(rig.ok("return done") == "1");
}
