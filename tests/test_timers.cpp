// Guards: timers firing early, twice, forever, in bursts or after they were cancelled.
#include "Support.h"

#include "engine/Timers.h"

using namespace fltest;

TEST_CASE("Timers table: ordering, repeats and cancel (no Lua)")
{
    fl::Timers t;
    uint64_t a = t.add(1000, 100, 0, 0, 1);
    uint64_t b = t.add(1000, 50, 50, 3, 2);
    fl::TimerEntry e;
    CHECK_FALSE(t.popDue(1049, e));
    REQUIRE(t.popDue(1050, e));
    CHECK(e.id == b);
    CHECK_FALSE(t.popDue(1050, e));
    REQUIRE(t.popDue(1100, e));    // b again (due 1100, id order after a? a is due 1100 too, a was added first)
    CHECK((e.id == a || e.id == b));
    int fired = 1;
    uint64_t now = 1100;
    while (t.popDue(now, e)) ++fired;
    now = 2000;
    while (t.popDue(now, e)) ++fired;
    CHECK(fired == 3);    // after the pops of b (1050) and a (1100): b at 1100 and 1150 and the one popped above
    CHECK(t.size() == 0);
    uint64_t c = t.add(0, 10, 0, 0, 9);
    int ref = 0;
    CHECK(t.cancel(c, &ref));
    CHECK(ref == 9);
    CHECK_FALSE(t.cancel(c, &ref));
}

TEST_CASE("timers: after fires once, not early, with a 50 ms minimum")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("n = 0 Timer.after(1, function() n = n + 1 end)");
    rig.host.advance(49);
    rig.e().tick();
    CHECK(rig.ok("return n") == "0");
    rig.host.advance(2);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
    rig.host.advance(1000);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
    CHECK(rig.e().status().timers == 0);
}

TEST_CASE("timers: every fires each interval and a counted every stops after its count")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("inf, three = 0, 0 Timer.every(100, function() inf = inf + 1 end) Timer.every(100, function() three = three + 1 end, 3)");
    for (int i = 0; i < 6; ++i)
    {
        rig.host.advance(100);
        rig.e().tick();
    }
    CHECK(rig.ok("return inf .. ',' .. three") == "6,3");
    CHECK(rig.e().status().timers == 1);
}

TEST_CASE("timers: cancel before it is due, from inside its own callback, and of a neighbour in the same tick")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("log = {} "
           "early = Timer.after(100, function() log[#log+1] = 'early' end) "
           "Timer.after(60, function() Timer.cancel(early) end) "
           "self = Timer.every(100, function() log[#log+1] = 'self' Timer.cancel(self) end) "
           "a = Timer.after(200, function() log[#log+1] = 'a' Timer.cancel(b) end) "
           "b = Timer.after(200, function() log[#log+1] = 'b' end)");
    rig.host.advance(100);
    rig.e().tick();
    rig.host.advance(100);
    rig.e().tick();
    rig.host.advance(500);
    rig.e().tick();
    CHECK(rig.ok("return table.concat(log, ',')") == "self,a");
    CHECK(rig.e().status().timers == 0);
    CHECK(rig.ok("return tostring(Timer.cancel(12345))") == "false");
}

TEST_CASE("timers: a long stall fires an every timer once, not in a burst")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("n = 0 Timer.every(100, function() n = n + 1 end)");
    rig.host.advance(10000);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
    rig.host.advance(100);
    rig.e().tick();
    CHECK(rig.ok("return n") == "2");
}

TEST_CASE("timers: a looping timer is cut off by the budget and cancelled after three breaks")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {60000, 100000}; });
    rig.host.frozen = true;
    rig.ok("n = 0 Timer.every(50, function() n = n + 1 while true do end end)");
    for (int i = 0; i < 6; ++i)
    {
        rig.host.advance(50);
        rig.e().tick();
    }
    CHECK(rig.ok("return n") == "3");
    CHECK(rig.e().status().timers == 0);
    CHECK(rig.host.logContains("cancelled after 3 budget breaks"));
}

TEST_CASE("timers: the timer limit is enforced")
{
    Rig rig([](fl::Config& c) { c.maxTimers = 4; });
    rig.host.frozen = true;
    CHECK(rig.run("for i = 1, 4 do Timer.after(1000, function() end) end").ok);
    auto r = rig.run("Timer.after(1000, function() end)");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("too many") != std::string::npos);
}

TEST_CASE("timers: a timer can add timers and tick() keeps the due ones in order")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("log = {} Timer.after(50, function() log[#log+1] = 'a' Timer.after(50, function() log[#log+1] = 'c' end) end) "
           "Timer.after(100, function() log[#log+1] = 'b' end)");
    rig.host.advance(60);
    rig.e().tick();
    rig.host.advance(60);
    rig.e().tick();
    rig.host.advance(60);
    rig.e().tick();
    CHECK(rig.ok("return table.concat(log, ',')") == "a,b,c");
}
