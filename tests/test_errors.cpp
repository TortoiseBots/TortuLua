// Guards: one failing script taking down the call chain, the thread or the process.
#include "Support.h"

#include <stdexcept>

using namespace fltest;

TEST_CASE("errors: a failing handler is logged and the next handler still runs")
{
    Rig rig;
    rig.ok("later = 0 "
           "Event.on('t', function() error('boom one') end) "
           "Event.on('t', function() local x = nil; return x.field end) "
           "Event.on('t', function() later = later + 1 end)");
    rig.emit("t");
    CHECK(rig.ok("return later") == "1");
    CHECK(rig.host.logContains("boom one"));
    CHECK(rig.host.logContains("stack traceback"));
    CHECK(rig.host.logCount(fl::LogLevel::Error) == 2);
    auto st = rig.e().status();
    REQUIRE(st.lastErrors.size() == 2);
    CHECK(st.lastErrors[0].find("boom one") != std::string::npos);
}

TEST_CASE("errors: odd error values do not crash the error path")
{
    Rig rig;
    auto a = rig.run("error({code = 1})");
    CHECK_FALSE(a.ok);
    CHECK(a.error.find("error object is a table") != std::string::npos);
    auto b = rig.run("error()");
    CHECK_FALSE(b.ok);
    auto c = rig.run("error(setmetatable({}, {__tostring = function() return 'custom' end}))");
    CHECK_FALSE(c.ok);
    CHECK(c.error == "custom");
    auto d = rig.run("local t = setmetatable({}, {__tostring = function() error('inside tostring') end}) error(t)");
    CHECK_FALSE(d.ok);
    CHECK(rig.ok("return 'alive'") == "alive");
}

TEST_CASE("errors: the last five errors are kept for .lua status")
{
    Rig rig;
    for (int i = 0; i < 8; ++i)
        rig.run("error('e" + std::to_string(i) + "')");
    auto st = rig.e().status();
    REQUIRE(st.lastErrors.size() == 5);
    CHECK(st.lastErrors.front().find("e3") != std::string::npos);
    CHECK(st.lastErrors.back().find("e7") != std::string::npos);
    CHECK(rig.e().statusText().find("e7") != std::string::npos);
}

TEST_CASE("errors: a C++ exception in a binding becomes a Lua error the script can catch")
{
    Rig rig;
    rig.host.throwOnSelect = true;
    CHECK(rig.ok("local ok, err = pcall(Db.select, 'world', 'creature_template', {'entry'}, function() end) "
                 "return tostring(ok) .. '|' .. tostring(err)").find("false|") == 0);
    CHECK(rig.ok("local ok, err = pcall(Db.select, 'world', 'creature_template', {'entry'}, function() end) return err")
              .find("select exploded") != std::string::npos);
    // and an uncaught one only ends that call
    auto r = rig.run("Db.select('world', 'creature_template', {'entry'}, function() end)");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("select exploded") != std::string::npos);
    rig.host.throwOnSelect = false;
    CHECK(rig.ok("return tostring(Db.select('world', 'creature_template', {'entry'}, function() end))") == "true");
    CHECK(rig.ok("return 'server alive'") == "server alive");
}

TEST_CASE("errors: a failing timer is logged and the other timers run")
{
    Rig rig;
    rig.ok("ran = 0 Timer.after(50, function() error('timer boom') end) Timer.after(50, function() ran = ran + 1 end)");
    rig.host.advance(60);
    rig.e().tick();
    CHECK(rig.ok("return ran") == "1");
    CHECK(rig.host.logContains("timer boom"));
}

TEST_CASE("errors: a syntax error in a script file is reported and the other files still load")
{
    Rig rig;
    rig.dir.write("init.lua", "order = 'init'");
    rig.dir.write("a_bad.lua", "this is ( not lua");
    rig.dir.write("b_good.lua", "order = order .. ',b'");
    rig.dir.write("c_runtime.lua", "error('runtime failure in c')");
    rig.dir.write("d_good.lua", "order = order .. ',d'");
    rig.dir.write("notes.txt", "not a script");
    fl::LoadReport rep = rig.e().load();
    CHECK(rep.loaded == std::vector<std::string>{"init.lua", "b_good.lua", "d_good.lua"});
    REQUIRE(rep.errors.size() == 2);
    CHECK(rep.errors[0].find("a_bad.lua") == 0);
    CHECK(rep.errors[1].find("runtime failure in c") != std::string::npos);
    CHECK(rig.ok("return order") == "init,b,d");
}

TEST_CASE("errors: a missing script folder is reported, not fatal")
{
    Rig rig([](fl::Config& c) { c.scriptPath = "/nonexistent/forever-lua-folder"; });
    fl::LoadReport rep = rig.e().load();
    CHECK(rep.folderMissing);
    CHECK(rep.errors.size() == 1);
    CHECK(rig.ok("return 1") == "1");
}

TEST_CASE("errors: invalid arguments to the API are Lua errors")
{
    Rig rig;
    for (char const* bad : {"Event.on()", "Event.on('t')", "Event.on('bad name!', function() end)",
                            "Event.on('t', function() end, {entry = 'x'})", "Timer.after('x', function() end)",
                            "Timer.after(10)", "Timer.every(100, function() end, 0)", "Timer.after(-5, function() end)",
                            "Cmd.register('tt', function() end)", "World.log('loud', 'x')"})
    {
        auto r = rig.run(bad);
        INFO(bad << " -> " << r.error);
        CHECK_FALSE(r.ok);
    }
    CHECK(rig.ok("return 'ok'") == "ok");
}
