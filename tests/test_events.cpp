// Guards: map-thread events reaching Lua off the world thread, a flooded queue, bots flooding handlers.
#include "Support.h"

#include <atomic>
#include <thread>

using namespace fltest;

namespace
{
fl::QueuedEvent ev(std::string name)
{
    fl::QueuedEvent e;
    e.name = std::move(name);
    return e;
}
}  // namespace

TEST_CASE("events: a full queue drops events and counts them")
{
    Rig rig([](fl::Config& c) { c.queueCapacity = 10; });
    rig.ok("n = 0 Event.on('t', function() n = n + 1 end)");
    int accepted = 0;
    for (int i = 0; i < 25; ++i)
        accepted += rig.e().post(ev("t"));
    CHECK(accepted == 10);
    auto st = rig.e().status();
    CHECK(st.queued == 10);
    CHECK(st.queueDropped == 15);
    rig.e().tick();
    CHECK(rig.ok("return n") == "10");
    CHECK(rig.e().statusText().find("dropped 15") != std::string::npos);
}

TEST_CASE("events: nobody listening means nothing is queued and nothing counts as dropped")
{
    Rig rig([](fl::Config& c) { c.queueCapacity = 2; });
    CHECK_FALSE(rig.e().listening());
    for (int i = 0; i < 10; ++i)
        CHECK_FALSE(rig.e().post(ev("player.login")));
    CHECK(rig.e().status().queued == 0);
    CHECK(rig.e().status().queueDropped == 0);
    rig.ok("Event.on('other', function() end)");
    CHECK(rig.e().listening());
    CHECK(rig.e().listening("other"));
    CHECK_FALSE(rig.e().listening("player.login"));
}

TEST_CASE("events: from another thread an event is queued and runs on the owner thread at the next tick")
{
    Rig rig;
    rig.ok("n = 0 Event.on('creature.death', function(snap) n = n + 1 end)");
    std::thread t([&] { rig.e().emit(ev("creature.death")); });
    t.join();
    CHECK(rig.ok("return n") == "0");   // not run on the map thread
    CHECK(rig.e().status().queued == 1);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
}

TEST_CASE("events: many producer threads and a draining world thread lose nothing but counted drops")
{
    Rig rig([](fl::Config& c) { c.queueCapacity = 500; });
    rig.ok("n = 0 Event.on('t', function() n = n + 1 end)");
    constexpr int kThreads = 6, kEach = 400;
    std::atomic<int> accepted{0};
    std::vector<std::thread> producers;
    for (int i = 0; i < kThreads; ++i)
        producers.emplace_back([&] {
            for (int k = 0; k < kEach; ++k)
                accepted += rig.e().post(ev("t"));
        });
    size_t handled = 0;
    for (int spin = 0; spin < 20000; ++spin)
        handled += rig.e().tick();
    for (auto& p : producers)
        p.join();
    while (size_t k = rig.e().tick())
        handled += k;
    auto st = rig.e().status();
    CHECK(handled == static_cast<size_t>(accepted.load()));
    CHECK(static_cast<uint64_t>(accepted.load()) + st.queueDropped == static_cast<uint64_t>(kThreads) * kEach);
    CHECK(rig.ok("return n") == std::to_string(handled));
}

TEST_CASE("events: the entry filter and the bot filter")
{
    Rig rig;
    rig.ok("log = {} "
           "Event.on('creature.death', function() log[#log+1] = 'any' end) "
           "Event.on('creature.death', function() log[#log+1] = 'e100' end, {entry = 100}) "
           "Event.on('player.login', function() log[#log+1] = 'humans' end) "
           "Event.on('player.login', function() log[#log+1] = 'all' end, {bots = true})");
    auto death = [&](uint32_t entry) {
        fl::QueuedEvent e = ev("creature.death");
        e.hasEntry = true;
        e.entry = entry;
        rig.e().emit(std::move(e));
    };
    death(100);
    death(200);
    rig.ok("log[#log+1] = '|'");
    CHECK(rig.ok("return table.concat(log, ',')") == "any,e100,any,|");
    rig.ok("log = {}");

    fl::QueuedEvent human = ev("player.login");
    rig.e().emit(std::move(human));
    fl::QueuedEvent bot = ev("player.login");
    bot.isBot = true;
    rig.e().emit(std::move(bot));
    CHECK(rig.ok("return table.concat(log, ',')") == "humans,all,all");   // bots only reach {bots = true}
    // an event without an entry never matches an entry filter
    rig.ok("log = {}");
    rig.e().emit(ev("creature.death"));
    CHECK(rig.ok("return table.concat(log, ',')") == "any");
}

TEST_CASE("events: arguments reach Lua as values, tables and handles")
{
    Rig rig;
    rig.host.add(FakeHost::creature(7), {"Boar", 4242, 1, 2, 3, 0.5f, true});
    rig.ok("Event.on('creature.death', function(snap, c, k, flag, num, text, none) "
           "seen = {snap.entry, snap.x, snap.where.map, c:name(), c:valid(), tostring(k), flag, num, text, tostring(none)} end)");
    auto snap = std::make_shared<fl::EventTable>();
    snap->fields.push_back({"entry", fl::EventValue(4242)});
    snap->fields.push_back({"x", fl::EventValue(1.5)});
    auto where = std::make_shared<fl::EventTable>();
    where->fields.push_back({"map", fl::EventValue(389)});
    snap->fields.push_back({"where", fl::EventValue(where)});
    rig.emit("creature.death", {fl::EventValue(snap), fl::EventValue(FakeHost::creature(7)),
                                fl::EventValue(), fl::EventValue(true), fl::EventValue(int64_t(9)), fl::EventValue("hi"),
                                fl::EventValue()});
    CHECK(rig.ok("return table.concat({tostring(seen[1]), tostring(seen[2]), tostring(seen[3]), seen[4], tostring(seen[5]), "
                 "seen[6], tostring(seen[7]), tostring(seen[8]), seen[9], seen[10]}, ',')") ==
          "4242,1.5,389,Boar,true,nil,true,9,hi,nil");
}

TEST_CASE("events: Event.off stops a handler, also from inside another handler")
{
    Rig rig;
    rig.ok("n1, n2 = 0, 0 "
           "id2 = nil "
           "id1 = Event.on('t', function() n1 = n1 + 1 Event.off(id2) end) "
           "id2 = Event.on('t', function() n2 = n2 + 1 end)");
    rig.emit("t");
    CHECK(rig.ok("return n1 .. ',' .. n2") == "1,0");   // handler 2 was removed before its turn
    CHECK(rig.ok("return tostring(Event.off(id1)) .. tostring(Event.off(id1)) .. tostring(Event.off(999))") == "truefalsefalse");
    rig.emit("t");
    CHECK(rig.ok("return n1") == "1");
    CHECK_FALSE(rig.e().listening());
    CHECK(rig.e().status().handlers == 0);
}

TEST_CASE("events: a handler removing itself or adding a handler while running is safe")
{
    Rig rig;
    rig.ok("n = 0 added = 0 "
           "me = Event.on('t', function() n = n + 1 Event.off(me) Event.on('t', function() added = added + 1 end) end)");
    rig.emit("t");
    CHECK(rig.ok("return n .. ',' .. added") == "1,0");   // the new handler waits for the next event
    rig.emit("t");
    CHECK(rig.ok("return n .. ',' .. added") == "1,1");
}

TEST_CASE("events: an emit that happens while Lua is running is queued, never nested")
{
    Rig rig;
    rig.ok("depth = 0 maxdepth = 0 hits = 0 "
           "Event.on('inner', function() hits = hits + 1 end) "
           "Event.on('outer', function() Db.select('world', 'creature_template', {'entry'}, function() end) end)");
    rig.host.onSelect = [&] { rig.e().emit(ev("inner")); };   // the host fires a hook in the middle of a binding
    rig.emit("outer");
    CHECK(rig.ok("return hits") == "0");
    CHECK(rig.e().status().queued == 1);
    rig.e().tick();
    CHECK(rig.ok("return hits") == "1");
}

TEST_CASE("events: the handler limit is enforced")
{
    Rig rig([](fl::Config& c) { c.maxHandlers = 3; });
    CHECK(rig.run("for i = 1, 3 do Event.on('t', function() end) end").ok);
    auto r = rig.run("Event.on('t', function() end)");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("too many") != std::string::npos);
}

TEST_CASE("events: finishPending runs the handlers a long emit() left over (server.stop at shutdown)")
{
    Rig rig([](fl::Config& c) { c.tickBudgetMs = 5; c.eventBudget = {1000, 100'000'000}; });
    rig.host.frozen = true;
    rig.host.tickMs = 1;
    rig.ok("order = {} for i = 1, 10 do Event.on('stop', function() order[#order + 1] = i end) end");
    fl::QueuedEvent ev;
    ev.name = "stop";
    ev.longBudget = true;
    rig.e().emit(std::move(ev));
    CHECK(rig.ok("return #order") != "10");   // the 25 ms tick cap spilled the rest
    CHECK(rig.e().finishPending() > 0);
    CHECK(rig.ok("return table.concat(order, ',')") == "1,2,3,4,5,6,7,8,9,10");
    CHECK(rig.e().finishPending() == 0);
}
