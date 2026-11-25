// Db.select is asynchronous: no database call may run on the world thread, and every answer (rows, failure, timeout)
// arrives in a callback on a later tick. Review, 2026-11-20.
#include "Support.h"

#include <chrono>

using namespace fltest;

namespace
{
uint64_t realMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

TEST_CASE("db: rows arrive in the callback on a later tick, never inside Db.select")
{
    Rig rig;
    fl::DbRows rows;
    rows.columns = {"entry", "name"};
    rows.rows = {{fl::DbValue(int64_t(25)), fl::DbValue(std::string("Kobold"))}};
    rig.host.defaultRows = rows;
    rig.ok("got = 'none' Db.select('world', 'creature_template', {'entry', 'name'}, function(r) "
           "got = r[1].entry .. r[1].name end)");
    CHECK(rig.ok("return got") == "none");   // the Lua call returned before any database work was visible
    rig.e().tick();
    CHECK(rig.ok("return got") == "25Kobold");
    CHECK(rig.host.dbRequests.empty());
}

TEST_CASE("db: a stalled database never blocks the world thread; the client timeout answers it")
{
    Rig rig([](fl::Config& c) { c.dbTimeoutMs = 100; c.eventBudget = {20, 10'000'000}; });
    rig.host.frozen = true;   // the clock only moves with advance()
    rig.host.stalled = true;  // the request is accepted and never answered: a wedged pool
    rig.ok("hits = 0 got = 'none' Event.on('t', function() hits = hits + 1 end)");
    uint64_t t0 = realMs();
    CHECK(rig.ok("return tostring(Db.select('world', 'creature_template', {'entry'}, function(r, e) "
                 "got = tostring(r) .. '|' .. tostring(e) end))") == "true");
    CHECK(realMs() - t0 < 500);   // the call did not wait for the database
    // ticks keep working while the query is stuck
    for (int i = 0; i < 3; ++i)
    {
        uint64_t t = realMs();
        rig.e().tick();
        CHECK(realMs() - t < 200);
    }
    rig.emit("t");
    CHECK(rig.ok("return hits") == "1");
    CHECK(rig.ok("return got") == "none");
    // the client-side timeout (MAX_EXECUTION_TIME only binds MySQL) fires from the clock, not from the driver
    rig.host.advance(101);
    uint64_t t1 = realMs();
    rig.e().tick();
    CHECK(realMs() - t1 < 200);
    CHECK(rig.ok("return got") == "nil|timeout");
    CHECK(rig.host.cancelled == 1);
}

TEST_CASE("db: too many requests in flight are refused with busy, and shutdown drops them")
{
    Rig rig([](fl::Config& c) { c.maxPendingSelects = 2; c.dbTimeoutMs = 60'000; });
    rig.host.frozen = true;
    rig.host.stalled = true;
    CHECK(rig.ok("return tostring(Db.select('world', 'creature_template', {'entry'}, function() end))") == "true");
    CHECK(rig.ok("return tostring(Db.select('world', 'creature_template', {'entry'}, function() end))") == "true");
    CHECK(rig.ok("local ok, err = Db.select('world', 'creature_template', {'entry'}, function() end) "
                 "return tostring(ok) .. '|' .. tostring(err)") == "nil|busy");
    rig.host.stalled = false;   // the two stuck requests get cancelled at shutdown
    rig.ok("Event.on('t', function() end)");
    rig.e().shutdown();
    CHECK(rig.host.cancelled == 2);
    CHECK(rig.e().status().running == false);
}

TEST_CASE("db: a failing query calls back with the reason and a broken callback does not stop the next one")
{
    Rig rig;
    rig.host.failSelect = true;
    CHECK(rig.dbSelect("'world', 'creature_template', {'entry'}") == "nil|query_failed");
    rig.host.failSelect = false;
    rig.ok("first = 'not run' second = 'not run' ");
    rig.ok("Db.select('world', 'creature_template', {'entry'}, function() error('callback boom') end) "
           "Db.select('world', 'creature_template', {'entry'}, function() second = 'ran' end)");
    rig.e().tick();
    CHECK(rig.ok("return second") == "ran");
    CHECK(rig.e().status().running);
    CHECK(rig.host.logContains("callback boom"));
}

TEST_CASE("db: a select issued while a reload loads is delivered in the adopted state")
{
    Rig rig;
    rig.dir.write("init.lua", "got = 'none' "
                              "Db.select('world', 'creature_template', {'entry'}, function(rows) got = type(rows) end)");
    fl::LoadReport rep = rig.e().reload();
    REQUIRE(rep.errors.empty());
    CHECK(rig.ok("return got") == "none");
    rig.e().tick();
    CHECK(rig.ok("return got") == "table");
}

TEST_CASE("db: a select after a reload does not reuse the id of the select the reload left in flight")
{
    Rig rig;
    rig.host.manualSelect = true;
    rig.dir.write("init.lua", "reload_got = 'none' after_got = 'none' "
                              "Db.select('world', 'creature_template', {'entry'}, function(rows, err) "
                              "reload_got = rows and rows[1].entry or tostring(err) end)");
    fl::LoadReport rep = rig.e().reload();
    REQUIRE(rep.errors.empty());
    CHECK(rig.ok("return reload_got") == "none");   // the reload-time select is still in flight
    rig.ok("Db.select('world', 'creature', {'guid'}, function(rows, err) "
           "after_got = rows and rows[1].guid or tostring(err) end)");
    REQUIRE(rig.host.dbRequests.size() == 2);
    // Both requests are in flight: the second one must not share the id of the first (adoption kept the old counter).
    CHECK(rig.host.dbRequests[0].id != rig.host.dbRequests[1].id);

    fl::DbRows during, after;
    during.columns = {"entry"};
    during.rows = {{fl::DbValue(int64_t(111))}};
    after.columns = {"guid"};
    after.rows = {{fl::DbValue(int64_t(222))}};
    rig.host.answerSelect(rig.host.dbRequests[0].id, during);
    rig.host.answerSelect(rig.host.dbRequests[1].id, after);
    rig.e().tick();
    CHECK(rig.ok("return tostring(reload_got) .. '|' .. tostring(after_got)") == "111|222");
}
