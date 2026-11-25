// Guards: a reload that leaves old timers, handlers or queued events behind, or leaks.
#include "Support.h"

#include <thread>

using namespace fltest;

TEST_CASE("reload: handlers, timers, commands and queued events of the old state are dropped")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("Event.on('t', function() end) Timer.every(100, function() end) Cmd.register('tt', function(c) c:reply('old') end, 0)");
    fl::QueuedEvent e;
    e.name = "t";
    rig.e().post(std::move(e));
    auto before = rig.e().status();
    CHECK(before.handlers == 1);
    CHECK(before.timers == 1);
    CHECK(before.queued == 1);

    fl::LoadReport rep = rig.e().reload();    // empty script folder
    CHECK(rep.errors.empty());
    auto after = rig.e().status();
    CHECK(after.handlers == 0);
    CHECK(after.timers == 0);
    CHECK(after.queued == 0);
    CHECK(after.reloads == 1);
    CHECK_FALSE(rig.e().listening());
    CHECK(rig.e().runCommand("tt", "", 0).status == fl::CommandStatus::NoHandler);
    rig.host.advance(1000);
    CHECK(rig.e().tick() == 0);
}

TEST_CASE("reload: new script content replaces the old, and globals of the old state are gone")
{
    Rig rig;
    rig.dir.write("init.lua", "version = 1 old_only = true Event.on('t', function() hits = (hits or 0) + 1 end)");
    rig.e().load();
    CHECK(rig.ok("return version") == "1");
    rig.dir.write("init.lua", "version = 2");
    fl::LoadReport rep = rig.e().reload();
    CHECK(rep.errors.empty());
    CHECK(rig.ok("return version .. tostring(old_only) .. tostring(hits)") == "2nilnil");
    rig.emit("t");   // the old handler is gone
    CHECK(rig.ok("return tostring(hits)") == "nil");
}

TEST_CASE("reload with errors: nothing is applied, the old state stays, the report lists the errors")
{
    Rig rig;
    rig.dir.write("init.lua", "loaded = 'init'");
    rig.dir.write("b.lua", "loaded = loaded .. ',b'");
    rig.e().load();
    rig.dir.write("b.lua", "loaded = loaded .. ',b' error('b is broken now')");
    rig.dir.write("c.lua", "loaded = loaded .. ',c'");
    fl::LoadReport rep = rig.e().reload();
    CHECK_FALSE(rep.applied);
    CHECK(rep.loaded == std::vector<std::string>{"init.lua", "c.lua"});
    REQUIRE(rep.errors.size() == 2);
    CHECK(rep.errors[0].find("b.lua") == 0);
    CHECK(rep.errors[0].find("b is broken now") != std::string::npos);
    CHECK(rig.ok("return loaded") == "init,b");   // the old state, untouched
    CHECK(rig.e().running());
    // fix it and reload again
    rig.dir.write("b.lua", "loaded = loaded .. ',B'");
    rep = rig.e().reload();
    CHECK(rep.errors.empty());
    CHECK(rep.applied);
    CHECK(rig.ok("return loaded") == "init,B,c");
}

TEST_CASE("reload: timers registered by scripts exist once, not twice, after a reload")
{
    Rig rig;
    rig.host.frozen = true;
    rig.dir.write("init.lua", "ticks = 0 Timer.every(100, function() ticks = ticks + 1 end)");
    rig.e().load();
    rig.host.advance(100);
    rig.e().tick();
    CHECK(rig.ok("return ticks") == "1");
    CHECK(rig.e().status().timers == 1);
    rig.e().reload();
    CHECK(rig.e().status().timers == 1);
    CHECK(rig.ok("return ticks") == "0");
    rig.host.advance(100);
    rig.e().tick();
    CHECK(rig.ok("return ticks") == "1");     // one timer, firing once per interval
    rig.host.advance(100);
    rig.e().tick();
    CHECK(rig.ok("return ticks") == "2");
}

TEST_CASE("reload: a timer that was already due when the reload came does not fire in the new state")
{
    Rig rig;
    rig.host.frozen = true;
    rig.ok("fired = 0 Timer.after(50, function() fired = fired + 1 end)");
    rig.host.advance(500);
    rig.e().reload();
    rig.e().tick();
    CHECK(rig.ok("return tostring(fired)") == "nil");
}

TEST_CASE("reload: the SQL schema cache is cleared")
{
    Rig rig;
    rig.ok("Db.select('world', 'creature_template', {'entry'}, function() end)");
    rig.ok("Db.select('world', 'creature_template', {'entry'}, function() end)");
    CHECK(rig.host.schemaCalls == 1);
    // a new snapshot object, as a real host builds one: the engine's cached copy must not see it before the reload
    auto fresh = std::make_shared<fl::SchemaSnapshot>(*rig.host.schemas["world"]);
    fresh->addTable("brand_new", {"id"});
    rig.host.schemas["world"] = fresh;
    CHECK(rig.ok("return tostring(Db.has('world', 'brand_new'))") == "false");   // cached snapshot
    rig.e().reload();
    CHECK(rig.ok("return tostring(Db.has('world', 'brand_new'))") == "true");
    CHECK(rig.host.schemaCalls == 2);
}

TEST_CASE("reload: from another thread or from inside Lua is refused and changes nothing")
{
    Rig rig;
    rig.ok("Event.on('t', function() end)");
    fl::LoadReport rep;
    std::thread t([&] { rep = rig.e().reload(); });
    t.join();
    REQUIRE(rep.errors.size() == 1);
    CHECK(rep.errors[0].find("refused") != std::string::npos);
    CHECK(rig.e().status().handlers == 1);
    CHECK(rig.e().status().wrongThreadRefused >= 1);

    // inside Lua: the host fires the reload from a hook in the middle of a binding
    rig.host.onSelect = [&] { rep = rig.e().reload(); };
    rig.ok("Db.select('world', 'creature_template', {'entry'}, function() end)");
    REQUIRE(rep.errors.size() == 1);
    CHECK(rep.errors[0].find("refused") != std::string::npos);
    CHECK(rig.e().status().handlers == 1);
}

TEST_CASE("reload: fifty reloads do not grow the state")
{
    Rig rig;
    rig.dir.write("init.lua", "data = {} for i = 1, 2000 do data[i] = tostring(i) end Timer.every(100, function() end) Event.on('x', function() end)");
    rig.e().load();
    size_t first = rig.e().status().memoryUsed;
    for (int i = 0; i < 50; ++i)
        REQUIRE(rig.e().reload().errors.empty());
    size_t last = rig.e().status().memoryUsed;
    CHECK(last < first + 64 * 1024);
    CHECK(rig.e().status().reloads == 50);
}
