// Guards: a script that eats memory until the server would be killed.
#include "Support.h"

#include <lua.h>

using namespace fltest;

TEST_CASE("memory cap: a runaway allocation returns LUA_ERRMEM and the state survives")
{
    Rig rig([](fl::Config& c) { c.memoryCap = 6u * 1024 * 1024; c.eventBudget = {20000, 2000000000}; });
    auto r = rig.run("local t = {} for i = 1, 100000000 do t[i] = string.rep('x', 64) .. i end");
    CHECK_FALSE(r.ok);
    CHECK(r.memory);
    CHECK(r.status == LUA_ERRMEM);
    CHECK(rig.e().status().memoryUsed <= rig.cfg.memoryCap);
    // the garbage is unreachable now: after a collection the state works as before
    CHECK(rig.ok("for i = 1, 20 do collectgarbage('step', 100000) end return 'fine'") == "fine");
    CHECK(rig.ok("return 2 * 21") == "42");
    CHECK(rig.e().status().memoryUsed < rig.cfg.memoryCap / 2);
}

TEST_CASE("memory cap: one giant string is refused too")
{
    Rig rig([](fl::Config& c) { c.memoryCap = 4u * 1024 * 1024; });
    // string.rep refuses a result over its own cap (4 MB); under the cap the memory cap refuses the allocation
    auto r = rig.run("local t = {} for i = 1, 10 do t[i] = string.rep('x', 1024 * 1024 + i) end");
    CHECK_FALSE(r.ok);
    CHECK(r.memory);
    auto r2 = rig.run("return #string.rep('abc', 1 << 40)");   // larger than any size: also an error, not a crash
    CHECK_FALSE(r2.ok);
}

TEST_CASE("memory cap: a script can catch the error with pcall and go on")
{
    Rig rig([](fl::Config& c) { c.memoryCap = 4u * 1024 * 1024; });
    CHECK(rig.ok("local ok = pcall(string.rep, 'x', 32 * 1024 * 1024) return tostring(ok)") == "false");
    CHECK(rig.ok("return 'after'") == "after");
}

TEST_CASE("memory cap: deep recursion is a Lua error, not a crash")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {20000, 2000000000}; });
    auto r = rig.run("local function f(n) return 1 + f(n + 1) end return f(1)");
    CHECK_FALSE(r.ok);
    CHECK(rig.ok("return 1") == "1");
}

TEST_CASE("memory: closing the state gives every byte back")
{
    Rig rig;
    rig.ok("t = {} for i = 1, 10000 do t[i] = {i, tostring(i)} end Timer.every(100, function() end) Event.on('x', function() end)");
    CHECK(rig.e().status().memoryUsed > 100 * 1024);
    rig.e().shutdown();
    CHECK(rig.e().status().memoryUsed == 0);
    CHECK_FALSE(rig.e().running());
}
