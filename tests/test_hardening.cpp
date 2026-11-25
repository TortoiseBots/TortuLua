// Guards from the R1 security reviews (independent reviews): every case here hangs, runs far over its budget or fails on the
// R1 engine. The ctest case has a hard timeout, so a regression cannot block CI.
#include "Support.h"

#include "engine/Coords.h"
#include "engine/Json.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

using namespace fltest;

namespace
{
uint64_t realMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

// ---- finding 1: __gc finalizers run with the hooks off ----
TEST_CASE("hardening: setmetatable refuses a __gc key, so no finalizer can loop outside the budget")
{
    Rig rig;
    auto r = rig.run("setmetatable({}, {__gc = function() while true do end end})");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("__gc") != std::string::npos);
    // a plain metatable still works, and so does getmetatable on it
    CHECK(rig.ok("local mt = {__index = {v = 3}} local t = setmetatable({}, mt) return t.v .. tostring(getmetatable(t) == mt)") == "3true");
    // the state closes (a finalizer loop would hang the close)
    rig.run("setmetatable({}, {__gc = function() while true do end end})");
    CHECK(rig.e().reload().errors.empty());
    rig.e().shutdown();
}

TEST_CASE("hardening: the metatables of the command context and of strings are locked")
{
    Rig rig;
    rig.ok("Cmd.register('tt', function(ctx) ctx:reply(type(getmetatable(ctx)) .. '|' .. tostring(pcall(setmetatable, ctx, {}))) end, 0)");
    auto r = rig.e().runCommand("tt", "", 0);
    REQUIRE(r.lines.size() == 1);
    CHECK(r.lines[0] == "string|false");
    CHECK(rig.ok("return type(getmetatable('')) .. '|' .. tostring(pcall(setmetatable, '', {}))") == "string|false");
    // collectable garbage in the command path cannot be given a finalizer through the context table
    CHECK(rig.ok("return ('x'):rep(3)") == "xxx");
}

// ---- finding 2: single C calls that never return ----
TEST_CASE("hardening: string.rep with an empty string or a huge result returns or fails at once")
{
    Rig rig;
    uint64_t t0 = realMs();
    CHECK(rig.ok("return #string.rep('', 2^40)") == "0");
    CHECK(rig.ok("return #string.rep('', 2^40, '')") == "0");
    auto r = rig.run("return #string.rep('x', 2^26)");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("too large") != std::string::npos);
    CHECK_FALSE(rig.run("return #string.rep('ab', 2^30, 'cd')").ok);
    CHECK(rig.ok("return #string.rep('ab', 1000, ',')") == "2999");
    CHECK(realMs() - t0 < 5000);
}

TEST_CASE("hardening: table.move and table.sort refuse huge ranges")
{
    Rig rig;
    uint64_t t0 = realMs();
    auto r = rig.run("table.move({}, 1, 2^40, 2)");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("too many") != std::string::npos);
    CHECK(rig.ok("local t = {1, 2, 3} table.move(t, 1, 3, 2) return t[1] .. t[2] .. t[3] .. t[4]") == "1123");
    CHECK_FALSE(rig.run("local t = {} for i = 1, 300000 do t[i] = -i end table.sort(t)").ok);
    CHECK(rig.ok("local t = {3, 1, 2} table.sort(t) return t[1] .. t[2] .. t[3]") == "123");
    CHECK(realMs() - t0 < 5000);
}

TEST_CASE("hardening: pattern matching has a step limit (no exponential backtracking)")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {200, 100'000'000}; });
    uint64_t t0 = realMs();
    for (char const* code : {"return string.find(('a'):rep(40), ('a*'):rep(30) .. 'b')",
                             "return string.match(('a'):rep(40), ('a*'):rep(30) .. 'b')",
                             "return string.gsub(('a'):rep(40), ('a*'):rep(30) .. 'b', 'x')",
                             "for w in string.gmatch(('a'):rep(40), ('a*'):rep(30) .. 'b') do end"})
    {
        INFO(code);
        auto r = rig.run(code);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("pattern too complex") != std::string::npos);
    }
    CHECK(realMs() - t0 < 8000);
    // ordinary patterns still work on a large subject
    CHECK(rig.ok("local s = ('word '):rep(5000) local n = 0 for w in s:gmatch('%a+') do n = n + 1 end return n") == "5000");
    CHECK(rig.ok("return select(2, (('abc'):rep(1000)):gsub('b', 'X'))") == "1000");
}

TEST_CASE("hardening: load refuses a huge chunk; collectgarbage is limited to count and step")
{
    Rig rig;
    CHECK(rig.ok("local f, err = load(('x = 1 '):rep(300000)) return tostring(f) .. '|' .. tostring(err)") == "nil|chunk too large");
    CHECK(rig.ok("return type(collectgarbage('count'))") == "number");
    CHECK(rig.ok("collectgarbage('step') return 'ok'") == "ok");
    for (char const* opt : {"stop", "collect", "incremental", "generational", "setpause"})
    {
        INFO(opt);
        CHECK_FALSE(rig.run(std::string("collectgarbage('") + opt + "')").ok);
    }
    // a script that stops the collector cannot starve later calls near the cap
    CHECK(rig.ok("return 1") == "1");
}

// ---- finding 3: the deadline is checked before every C function, not only every 1000 instructions ----
TEST_CASE("hardening: a loop of C calls hits the deadline within a few calls")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {20, ~uint64_t(0) / 2}; });
    rig.host.frozen = true;
    rig.host.tickMs = 1;   // every clock read costs 1 ms: the number of Lua-side calls before the break is deterministic
    auto r = rig.run("n = 0 while true do n = n + 1 local _ = string.len('x') end");
    CHECK(r.budget);
    rig.host.tickMs = 0;
    int64_t n = std::stoll(rig.ok("return n"));
    CHECK(n < 100);   // the R1 hook looked at the clock once per 1000 instructions: about 200 calls first
}

// ---- finding 4: Json.encode output is unbounded ----
TEST_CASE("hardening: Json.encode refuses an exponentially shared table")
{
    Rig rig;
    uint64_t t0 = realMs();
    auto r = rig.run("local a = {} for i = 1, 30 do a = {a, a} end local s, err = Json.encode(a) return tostring(s) .. '|' .. tostring(err)");
    REQUIRE(r.ok);
    CHECK(r.result.find("nil|") == 0);
    CHECK(realMs() - t0 < 5000);
    CHECK(rig.ok("return Json.encode({1, 2, 3})") == "[1,2,3]");
}

// ---- finding 5: the GM bridge deny list is beaten by command abbreviations ----
TEST_CASE("hardening: the bridge refuses abbreviations and extensions of denied or forwarded roots")
{
    Rig rig;
    for (char const* line : {"acc set gmlevel X 3 -1", "serv shut 0", "ser shutdown 0", "lu reload", "l reload", "rb list",
                             ".account create a b", "ACC create", "bnet create", "t units #1", "accounts", "luax reload"})
    {
        INFO(line);
        auto r = rig.run(std::string("local ok, out = Gm.run([==[") + line + "]==]) return tostring(ok) .. '|' .. out[1]");
        REQUIRE(r.ok);
        CHECK(r.result.find("false|refused") == 0);
    }
    CHECK(rig.host.gmLines.empty());
    CHECK(rig.ok("local ok = Gm.run('go xyz 1 2 3') return tostring(ok)") == "true");
    CHECK(rig.ok("local ok = Gm.run('additem 25') return tostring(ok)") == "true");
}

// ---- finding 6: one event can run all its handlers in one tick ----
TEST_CASE("hardening: one event with many slow handlers spills over to the next ticks, in order")
{
    Rig rig([](fl::Config& c) { c.tickBudgetMs = 5; c.eventBudget = {1000, 100'000'000}; });
    rig.host.frozen = true;
    rig.host.tickMs = 1;
    rig.ok("order = {} for i = 1, 10 do Event.on('slow', function() order[#order + 1] = i end) end");
    rig.emit("slow");
    size_t first = size_t(std::stoll(rig.ok("return #order")));
    CHECK(first < 10);
    CHECK(first >= 1);
    for (int i = 0; i < 50; ++i)
        rig.e().tick();
    CHECK(rig.ok("return table.concat(order, ',')") == "1,2,3,4,5,6,7,8,9,10");
}

// ---- finding 7: handle methods go through the std::exception wrapper ----
TEST_CASE("hardening: a host exception inside a handle method becomes a Lua error that pcall catches")
{
    Rig rig;
    rig.host.add(FakeHost::creature(7), {"Boss", 100, 1, 2, 3, 0, true});
    rig.ok("h = nil Event.on('t', function(c) h = c end)");
    fl::QueuedEvent ev;
    ev.name = "t";
    ev.args.emplace_back(FakeHost::creature(7));
    rig.e().emit(std::move(ev));
    rig.host.throwOnLookup = true;
    CHECK(rig.ok("local ok, err = pcall(function() return h:name() end) return tostring(ok) .. '|' .. tostring(err)").find("false|") == 0);
    CHECK(rig.ok("local ok, err = pcall(function() return h:name() end) return tostring(err)").find("lookup exploded") != std::string::npos);
    CHECK(rig.ok("local ok, err = pcall(function() return h:valid() end) return tostring(ok)") == "false");
    rig.host.throwOnLookup = false;
    CHECK(rig.ok("return h:name()") == "Boss");
}

// ---- finding 8 and the auth database ----
TEST_CASE("hardening: Db.select carries a server-side time limit and refuses the auth database")
{
    Rig rig;
    rig.ok("Db.select('world', 'creature_template', {'entry'}, function() end)");
    REQUIRE(rig.host.queries.size() == 1);
    CHECK(rig.host.queries[0].find("MAX_EXECUTION_TIME(") != std::string::npos);
    rig.host.schemas["auth"] = std::make_shared<fl::SchemaSnapshot>();
    rig.host.schemas["auth"]->addTable("account", {"id", "username", "salt", "verifier"});
    auto before = rig.host.queries.size();
    CHECK(rig.ok("local rows, err = Db.select('auth', 'account', {'salt'}, function() end) return tostring(rows) .. '|' .. tostring(err)") ==
          "nil|bad_db");
    CHECK(rig.ok("local ok, err = Db.has('auth', 'account') return tostring(ok) .. '|' .. tostring(err)") == "nil|bad_db");
    CHECK(rig.host.queries.size() == before);
}

TEST_CASE("hardening: a result with a huge cell is refused, not copied into Lua")
{
    Rig rig([](fl::Config& c) { c.memoryCap = 16u * 1024 * 1024; });
    rig.host.bigCellBytes = 8u * 1024 * 1024;
    CHECK(rig.dbSelect("'world', 'creature_template', {'name'}") == "nil|too_large");
    CHECK(rig.e().status().memoryUsed < 4u * 1024 * 1024);
}

// ---- review 2: a failed reload keeps the known-good state ----
TEST_CASE("hardening: a reload with a broken script keeps the old handlers, timers, commands and globals")
{
    Rig rig;
    rig.host.frozen = true;
    rig.dir.write("init.lua", "version = 1 Event.on('t', function() hit = (hit or 0) + 1 end) Timer.every(100, function() ticks = (ticks or 0) + 1 end) "
                              "Cmd.register('tt', function(c) c:reply('v1') end, 0)");
    rig.e().load();
    rig.dir.write("init.lua", "version = 2 Cmd.register('tt', function(c) c:reply('v2') end, 0)");
    rig.dir.write("z.lua", "error('z is broken')");
    fl::LoadReport rep = rig.e().reload();
    REQUIRE_FALSE(rep.errors.empty());
    CHECK(rep.errors[0].find("z is broken") != std::string::npos);
    CHECK(rig.ok("return version") == "1");
    CHECK(rig.e().status().handlers == 1);
    CHECK(rig.e().status().timers == 1);
    CHECK(rig.e().runCommand("tt", "", 0).lines == std::vector<std::string>{"v1"});
    rig.emit("t");
    CHECK(rig.ok("return hit") == "1");
    rig.host.advance(100);
    rig.e().tick();
    CHECK(rig.ok("return ticks") == "1");
    CHECK(rig.e().status().reloads == 0);
    // fixing the script and reloading again applies it
    rig.dir.remove("z.lua");
    CHECK(rig.e().reload().errors.empty());
    CHECK(rig.ok("return version") == "2");
    CHECK(rig.e().runCommand("tt", "", 0).lines == std::vector<std::string>{"v2"});
    CHECK(rig.e().status().reloads == 1);
}

// ---- N1: Map:height and Map:los never hand a coordinate outside the terrain grid to the core ----
TEST_CASE("hardening: coordinates outside the terrain grid are refused before any core call")
{
    // Map:height and Map:los run this check before the map lookup (src/bind/BindWorld.cpp). GetGrid computes
    // (int)(32 - x/533.33) without a bounds check: a NaN reads TerrainInfo[INT_MIN], and -20000 indexes grid 69 of
    // 64 (an out-of-bounds write). 0/0, math.huge and -20000 are the cases the review used.
    float nan = std::nanf("");
    float inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(fl::validMapCoord(nan));
    CHECK_FALSE(fl::validMapCoord(inf));
    CHECK_FALSE(fl::validMapCoord(-inf));
    CHECK_FALSE(fl::validMapCoord(-20000.0f));
    CHECK_FALSE(fl::validMapCoord(20000.0f));
    CHECK_FALSE(fl::validMapCoord(0.0f, nan, 0.0f));
    CHECK_FALSE(fl::validMapCoord(0.0f, 0.0f, nan));
    CHECK_FALSE(fl::validMapCoord(1.0f, 2.0f, inf));
    // the bounds themselves: MAP_HALFSIZE - 0.5
    CHECK(fl::validMapCoord(0.0f, 0.0f, 0.0f));
    CHECK(fl::validMapCoord(17066.0f, -17066.0f, -500.0f));
    CHECK(fl::validMapCoord(17066.166f));
    CHECK_FALSE(fl::validMapCoord(17066.7f));
    // a double that overflows a float becomes an infinity, so it is refused as well
    CHECK_FALSE(fl::validMapCoord(static_cast<float>(1e40)));
}

// ---- N2/N5/N6: one allocation of 4 MiB or more fails at once, whatever the memory cap ----
TEST_CASE("hardening: a single allocation over 4 MiB is refused even under a large memory cap")
{
    // The host allocator refuses the part, not only the total: one 64 MB string (or a 64 MB table part) would be a
    // single malloc that the kernel serves while the world thread waits, and the memory cap alone lets it through.
    Rig rig([](fl::Config& c) { c.memoryCap = 128u * 1024 * 1024; c.eventBudget = {20000, 2000000000}; });
    uint64_t t0 = realMs();
    auto r = rig.run("local a = ('x'):rep(1024) for i = 1, 20 do a = a .. a end return #a");
    CHECK_FALSE(r.ok);
    CHECK(r.memory);   // LUA_ERRMEM from the allocator, not a crash or a hang
    CHECK(rig.e().status().memoryUsed <= rig.cfg.memoryCap);
    auto t = rig.run("local t = {} for i = 1, 1 << 22 do t[i] = true end return #t");
    CHECK_FALSE(t.ok);
    CHECK(t.memory);
    CHECK(rig.ok("return #('x'):rep(3 * 1024 * 1024)") == std::to_string(3u * 1024 * 1024));
    CHECK(realMs() - t0 < 5000);
}

// ---- N2: the subject and the pattern of the string functions are capped ----
TEST_CASE("hardening: pattern subjects are capped at 1 MiB and patterns at 512 bytes")
{
    Rig rig;
    uint64_t t0 = realMs();
    for (char const* code : {"return ('x'):rep(2^20 + 1):find('x')",
                             "return ('x'):rep(2^20 + 1):match('x')",
                             "return ('x'):rep(2^20 + 1):gsub('x', 'y')",
                             "for w in ('x'):rep(2^20 + 1):gmatch('x') do end"})
    {
        INFO(code);
        auto r = rig.run(code);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("subject too large") != std::string::npos);
    }
    auto p = rig.run("return ('abc'):find(('a'):rep(513))");
    CHECK_FALSE(p.ok);
    CHECK(p.error.find("pattern too long") != std::string::npos);
    CHECK_FALSE(rig.run("return ('abc'):gsub(('a'):rep(513), 'x')").ok);
    // exactly at the caps the calls still work
    CHECK(rig.ok("return table.concat({('word '):rep(200000):find('word', 999996)}, ',')") == "999996,999999");
    CHECK(rig.ok("return select(2, (('a'):rep(1024 * 1024)):gsub('a', 'b'))") == std::to_string(1024 * 1024));
    CHECK(realMs() - t0 < 10000);
}

// ---- N2: table.insert / table.remove cannot loop to a script __len ----
TEST_CASE("hardening: table.insert and table.remove use the raw length, never a script __len")
{
    Rig rig;
    uint64_t t0 = realMs();
    for (char const* code : {"local t = setmetatable({}, {__len = function() return 2^40 end}) table.insert(t, 1)",
                             "local t = setmetatable({}, {__len = function() return 2^40 end}) table.insert(t, 1, 'x')",
                             "local t = setmetatable({}, {__len = function() return 2^40 end}) table.remove(t)"})
    {
        INFO(code);
        auto r = rig.run(code);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("raw length") != std::string::npos);
    }
    // a __len that agrees with the raw length (a read-only wrapper) still works
    CHECK(rig.ok("local t = setmetatable({1, 2, 3}, {__len = function() return 3 end}) table.insert(t, 2, 9) "
                 "return t[1] .. t[2] .. t[3] .. t[4]") == "1923");
    CHECK(rig.ok("local t = {1, 2, 3} table.remove(t, 1) return table.concat(t, ',')") == "2,3");
    // the element cap is the sort cap
    CHECK_FALSE(rig.run("local t = {} for i = 1, 100001 do t[i] = i end table.insert(t, 1, 0)").ok);
    CHECK(realMs() - t0 < 8000);
}

// ---- N2 (review, high): the step limit must cover the scans inside the matching loops ----
TEST_CASE("hardening: the pattern step limit covers bracket classes, %b, expansions and plain finds")
{
    // 20 ms of call budget = 2,000,000 pattern steps. The subjects and patterns are built in an untimed chunk first:
    // building 1 MiB under the sanitizers can itself cost the budget, and then the clock would end the chunk with
    // "budget" instead of the C call being cut by its own counter.
    Rig rig([](fl::Config& c) { c.eventBudget = {20, ~uint64_t(0) / 2}; });
    fl::CallResult prep = rig.e().runChunk("subject = ('a'):rep(2^20) parens = ('('):rep(2^16) "
                                           "class = '^[' .. ('b'):rep(400) .. 'a]*$' "
                                           "minimal = ('a-'):rep(200) .. 'b' plain = ('a'):rep(400) .. 'b'",
                                           "=prep", true);   // command budget: building 1 MiB under the sanitizers costs more than 20 ms
    REQUIRE(prep.ok);
    uint64_t t0 = realMs();
    for (char const* code : {"return string.find(parens, '%b()')",
                             "return string.find(subject, class)",
                             "return string.find(subject, minimal)",
                             "return string.find(subject, plain, 1, true)"})
    {
        INFO(code);
        auto r = rig.run(code);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("pattern too complex") != std::string::npos);
    }
    CHECK(realMs() - t0 < 15000);
    // ordinary matching at the caps still works (its own budget: 20 ms is for the adversarial cases above)
    Rig plain;   // default budgets (5 s / 500 M steps)
    CHECK(plain.ok("return select(2, ('a'):rep(2^16):gsub('a', 'x'))") == "65536");
    CHECK(plain.ok("return string.find(('('):rep(1000) .. ')', '%b()')") == "1000");
}
// ---- N6: Json.encode counts the escaped output against its cap while it writes ----
TEST_CASE("hardening: Json.encode stops a single long string at the output cap")
{
    Rig rig;
    uint64_t t0 = realMs();
    // 1 MiB of byte 1 escapes to 6 MiB of text: before the fix the cap was checked only on entry to the next value,
    // so one string built all six megabytes in a loop no hook can interrupt.
    auto r = rig.run("local s = ('\\1'):rep(1024 * 1024) local o, err = Json.encode(s) "
                     "return tostring(o) .. '|' .. tostring(err)");
    REQUIRE(r.ok);
    CHECK(r.result.find("nil|result too large") == 0);
    CHECK(rig.ok("return Json.encode(('x'):rep(1000))") == std::string("\"") + std::string(1000, 'x') + "\"");
    CHECK(rig.ok("return Json.encode({a = 1, b = {2, 3}})") == "{\"a\":1,\"b\":[2,3]}");
    CHECK(realMs() - t0 < 2000);
}

// ---- N5: between two clock reads the VM can still do one huge memcmp, so the hook gets denser on a large heap ----
TEST_CASE("hardening: a large heap shrinks the instruction step between clock reads")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {20, ~uint64_t(0) / 2}; });
    rig.host.frozen = true;
    rig.host.tickMs = 1;   // one clock read = 1 ms: the number of loop iterations reveals the hook step
    REQUIRE(rig.run("n = 0 while true do n = n + 1 end").budget);
    int64_t small = std::stoll(rig.ok("return n"));
    // 3 MiB on the heap: a single string comparison between two hooks is a 3 MiB memcmp
    rig.ok("keep = ('x'):rep(3 * 1024 * 1024)");
    REQUIRE(rig.run("n = 0 while true do n = n + 1 end").budget);
    int64_t big = std::stoll(rig.ok("return n"));
    CHECK(small > 2000);        // ~21 clock reads per 1000 instructions
    CHECK(big * 4 < small);     // ... and ten times as many clock reads past 2 MiB
}

// ---- N3: server.stop was queued behind a pending event and then dropped at shutdown ----
TEST_CASE("hardening: finishPending drains the queue, so server.stop still fires after an overrun tick")
{
    Rig rig([](fl::Config& c) { c.tickBudgetMs = 1; c.eventBudget = {1000, 100'000'000}; c.shutdownBudgetMs = 5000; });
    rig.host.frozen = true;
    rig.host.tickMs = 1;
    rig.ok("stopped = false order = {} "
           "for i = 1, 10 do Event.on('slow', function() order[#order + 1] = i end) end "
           "Event.on('server.stop', function() stopped = true end)");
    rig.emit("slow");   // runs a few handlers, leaves the rest pending
    CHECK(rig.ok("return #order") != "10");
    rig.emit("server.stop");   // queued: an event is still pending
    rig.e().finishPending();
    CHECK(rig.ok("return tostring(stopped)") == "true");
    CHECK(rig.ok("return #order") == "10");   // and the pending handlers ran too, in order
    CHECK(rig.ok("return table.concat(order, ',')") == "1,2,3,4,5,6,7,8,9,10");
}

TEST_CASE("hardening: the shutdown drain is bounded by shutdownBudgetMs")
{
    Rig rig([](fl::Config& c) { c.eventBudget = {20, 1'000'000}; c.shutdownBudgetMs = 150; });
    rig.host.frozen = false;   // real clock: the bound is wall clock
    rig.ok("for i = 1, 40 do Event.on('slow', function() while true do end end) end");
    rig.emit("slow");   // queued: the owner-thread emit runs at most one handler per deadline
    uint64_t t0 = realMs();
    size_t calls = rig.e().finishPending();
    uint64_t took = realMs() - t0;
    CHECK(calls < 40);    // each handler burns its 20 ms budget: 40 of them would be 800 ms
    CHECK(took < 700);    // ... the drain stops at shutdownBudgetMs (150 ms) instead
    CHECK(rig.e().status().running);
}

// ---- N4: every emit in one tick shared nothing, so N chat lines cost N budgets ----
TEST_CASE("hardening: all emits of one tick share one Lua deadline; past it they are queued")
{
    Rig rig([](fl::Config& c) { c.tickBudgetMs = 10; c.eventBudget = {1000, 100'000'000}; });
    rig.host.frozen = true;
    rig.host.tickMs = 3;   // every clock read costs 3 ms: the window is spent after a few handlers
    rig.ok("hits = 0 Event.on('chat', function() hits = hits + 1 end)");
    rig.e().beginTick();
    for (int i = 0; i < 20; ++i)
        rig.emit("chat");
    int64_t first = std::stoll(rig.ok("return hits"));
    CHECK(first >= 1);
    CHECK(first < 20);   // before the fix each emit got a fresh 10 ms and all 20 ran
    CHECK(rig.e().status().queued == size_t(20 - first));
    rig.e().tick();      // the next tick runs the rest
    CHECK(std::stoll(rig.ok("return hits")) > first);
}

// ---- N7: a reload loads a second state, and the hooks of that load must not dispatch into the running one ----
TEST_CASE("hardening: no event dispatches into the old state while a reload loads the candidate")
{
    Rig rig;
    rig.ok("Event.on('inner', function() World.log('info', 'old-state-dispatch') end)");
    rig.dir.write("init.lua", "Gm.run('go xyz 1 2 3')");
    rig.host.onGm = [&] { rig.emit("inner"); };   // a world-thread hook fires from inside the candidate's load
    fl::LoadReport rep = rig.e().reload();
    CHECK(rep.errors.empty());
    CHECK_FALSE(rig.host.logContains("old-state-dispatch"));
}

// ---- N7: the bridge denies pdump and ban by default, like the other roots ----
TEST_CASE("hardening: the bridge denies pdump, ban and their abbreviations by default")
{
    Rig rig;
    for (char const* line : {"pdump write /tmp/pdump", "pd write /tmp/pdump", "ban account someone 0",
                             "ban character someone 0", "ba account someone"})
    {
        INFO(line);
        auto r = rig.run(std::string("local ok, out = Gm.run([==[") + line + "]==]) return tostring(ok) .. '|' .. out[1]");
        REQUIRE(r.ok);
        CHECK(r.result.find("false|refused") == 0);
    }
    CHECK(rig.host.gmLines.empty());
}

// ---- N7: collectgarbage("step", n) is clamped, so one call cannot run a whole cycle ----
TEST_CASE("hardening: collectgarbage step is clamped to 64 KB of work")
{
    Rig rig;
    // "step", 0 returns to a cycle boundary; then ~20 MB becomes garbage and one step with 2^53 KB is asked for. The
    // unclamped call (a 1e12-byte debt) walks the whole heap, finishes the cycle and reclaims it - the same as the
    // refused "collect". Clamped to 64 KB of work, the call cannot finish and leaves the memory for later steps.
    auto r = rig.run("collectgarbage('step', 0) "
                     "local keep = {} for i = 1, 1000 do keep[i] = ('x'):rep(20000) end "
                     "local before = collectgarbage('count') keep = nil "
                     "local done = collectgarbage('step', 2^53) "
                     "return tostring(done) .. '|' .. tostring(collectgarbage('count') > before / 2)");
    INFO(r.error);
    REQUIRE(r.ok);
    CHECK(r.result == "false|true");
    CHECK(rig.ok("collectgarbage('step', 64) collectgarbage('step') return 'ok'") == "ok");
}
