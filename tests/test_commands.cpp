// Guards: a forwarded command running for a caller without the security level, the GM bridge running .lua or other
// forbidden commands, and tail parsing of "#id" requests.
#include "Support.h"

#include "engine/CommandTail.h"

#include <thread>

using namespace fltest;

TEST_CASE("tail parsing: words, quotes and the #id")
{
    auto p = fl::parseTail("units #123 near  \"two words\" 'single' #x #456");
    CHECK(p.error.empty());
    CHECK(p.args == std::vector<std::string>{"units", "near", "two words", "'single'", "#x", "#456"});
    REQUIRE(p.id.has_value());
    CHECK(*p.id == "123");

    p = fl::parseTail("");
    CHECK(p.args.empty());
    CHECK_FALSE(p.id.has_value());

    p = fl::parseTail("\"#77\" q");
    CHECK_FALSE(p.id.has_value());        // a quoted token is never an id
    CHECK(p.args == std::vector<std::string>{"#77", "q"});

    p = fl::parseTail("say \"a \\\"quoted\\\" \\\\ word\"");
    CHECK(p.args == std::vector<std::string>{"say", "a \"quoted\" \\ word"});

    CHECK_FALSE(fl::parseTail("\"unterminated").error.empty());
    CHECK_FALSE(fl::parseTail("a\x01 b").error.empty());
    CHECK_FALSE(fl::parseTail("a\nb").error.empty());
    CHECK(fl::parseTail("#" + std::string(41, 'a')).args.size() == 1);    // too long for an id: stays an argument
    p = fl::parseTail("q floor #ta1x");
    REQUIRE(p.id.has_value());
    CHECK(*p.id == "ta1x");                                               // the agent's ids are letters and digits
    p = fl::parseTail("#a-b q");
    CHECK_FALSE(p.id.has_value());                                        // other characters: an ordinary argument
    CHECK_FALSE(fl::parseTail("#").id.has_value());
}

TEST_CASE("command root: trims, strips the dot, lower-cases")
{
    CHECK(fl::commandRoot("  .LUA reload") == "lua");
    CHECK(fl::commandRoot("!tt x") == "tt");
    CHECK(fl::commandRoot("tt") == "tt");
    CHECK(fl::commandRoot("") == "");
    CHECK(fl::commandRoot(".") == "");
}

TEST_CASE("bridge guard: refuses the deny list, forwarded roots, control characters and nesting")
{
    std::set<std::string> deny = {"lua", "server", "account", "rbac", "bnetaccount"};
    std::set<std::string> fwd = {"tt"};
    for (char const* bad : {".lua reload", "lua status", " .LUA reload", "!Lua x", ".server shutdown 1", ".account create a b",
                            ".rbac list", ".bnetaccount create", ".tt units #1", ".tt", "", ".", ".gm on\n.lua reload",
                            ".gm on\r.lua reload", ".gm\x01on", ".lua.reload", ".gm;lua reload"})
    {
        INFO("line: " << bad);
        CHECK_FALSE(fl::bridgeRefusal(bad, deny, fwd, 0).empty());
    }
    for (char const* good : {".gm on", ".instance unbind all", "reset talents", ".go xyz 1 2 3", ".additem 25"})
    {
        INFO("line: " << good);
        CHECK(fl::bridgeRefusal(good, deny, fwd, 0).empty());
    }
    CHECK_FALSE(fl::bridgeRefusal(".gm on", deny, fwd, 1).empty());   // depth is capped at 1
    CHECK_FALSE(fl::bridgeRefusal(std::string(2000, 'a'), deny, fwd, 0).empty());
}

TEST_CASE("commands: only configured roots can be registered")
{
    Rig rig;
    CHECK(rig.ok("local ok, why = Cmd.register('tt', function() end, 0) return tostring(ok) .. tostring(why)") == "truenil");
    CHECK(rig.ok("local ok, why = Cmd.register('quit', function() end, 0) return tostring(ok) .. '|' .. why") ==
          "false|root not configured");
    CHECK(rig.ok("local ok = Cmd.register('TT', function() end, 0) return tostring(ok)") == "true");   // case-insensitive
    CHECK(rig.e().runCommand("quit", "", 9).status == fl::CommandStatus::NoHandler);
}

TEST_CASE("commands: a forwarded root enforces minSecurity, and the handler does not run when it is not met")
{
    Rig rig;
    rig.ok("runs = 0 Cmd.register('tt', function(ctx) runs = runs + 1 ctx:reply('ran') end, 2)");
    auto low = rig.e().runCommand("tt", "x", 1);
    CHECK(low.status == fl::CommandStatus::Denied);
    CHECK(rig.ok("return runs") == "0");
    CHECK_FALSE(low.lines.empty());
    auto exact = rig.e().runCommand("tt", "x", 2);
    CHECK(exact.status == fl::CommandStatus::Ok);
    CHECK(exact.lines == std::vector<std::string>{"ran"});
    CHECK(rig.e().runCommand("tt", "x", 4).status == fl::CommandStatus::Ok);
    CHECK(rig.ok("return runs") == "2");
    // the console has security 0 in the host: refused when minSecurity is above it
    CHECK(rig.e().runCommand("tt", "x", 0).status == fl::CommandStatus::Denied);
}

TEST_CASE("commands: ctx carries player, security, tail, args and #id, and replies come back as lines")
{
    Rig rig;
    rig.host.add(FakeHost::player(3), {"Tester", 0, 0, 0, 0, 0, true});
    rig.ok("Cmd.register('tt', function(ctx) "
           "ctx:reply(tostring(ctx.player and ctx.player:name())) "
           "ctx:reply(ctx.security .. '|' .. ctx.tail .. '|' .. #ctx.args .. '|' .. ctx.args[1] .. '|' .. ctx.args[2] .. '|' .. tostring(ctx.id)) "
           "ctx:reply('three') end, 0)");
    auto r = rig.e().runCommand("tt", "units \"big boss\" #42", 3, FakeHost::player(3));
    REQUIRE(r.status == fl::CommandStatus::Ok);
    CHECK(r.lines == std::vector<std::string>{"Tester", "3|units \"big boss\" #42|2|units|big boss|42", "three"});
    auto console = rig.e().runCommand("tt", "a b", 4);
    CHECK(console.lines[0] == "nil");
    CHECK(console.lines[1] == "4|a b|2|a|b|nil");
}

TEST_CASE("commands: a failing handler answers with the error and the server keeps going")
{
    Rig rig;
    rig.ok("Cmd.register('tt', function(ctx) ctx:reply('before') error('cmd broke') end, 0)");
    auto r = rig.e().runCommand("tt", "", 0);
    CHECK(r.status == fl::CommandStatus::Error);
    CHECK(r.error.find("cmd broke") != std::string::npos);
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0] == "before");
    CHECK(r.lines[1].find("cmd broke") != std::string::npos);
    CHECK(rig.ok("return 1") == "1");
}

TEST_CASE("commands: a looping command is cut off by the command budget")
{
    Rig rig([](fl::Config& c) { c.commandBudget = {60000, 200000}; });
    rig.ok("Cmd.register('tt', function() while true do end end, 0)");
    auto r = rig.e().runCommand("tt", "", 0);
    CHECK(r.status == fl::CommandStatus::Error);
    CHECK(r.error == "budget");
    CHECK(rig.e().runCommand("tt", "", 0).status == fl::CommandStatus::Error);   // commands are never disabled, only cut off
}

TEST_CASE("commands: malformed tails are refused before Lua runs; reply lines are capped")
{
    Rig rig;
    rig.ok("ran = false Cmd.register('tt', function(ctx) ran = true for i = 1, 500 do ctx:reply('line' .. i) end end, 0)");
    CHECK(rig.e().runCommand("tt", "\"open", 0).status == fl::CommandStatus::Refused);
    CHECK(rig.ok("return tostring(ran)") == "false");
    auto r = rig.e().runCommand("tt", "", 0);
    CHECK(r.lines.size() == rig.cfg.maxReplyLines);
}

TEST_CASE("commands: a later Cmd.register replaces the handler")
{
    Rig rig;
    rig.ok("Cmd.register('tt', function(c) c:reply('v1') end, 0) Cmd.register('tt', function(c) c:reply('v2') end, 0)");
    CHECK(rig.e().runCommand("tt", "", 0).lines == std::vector<std::string>{"v2"});
}

TEST_CASE("Gm.run: the bridge refuses .lua and other forbidden roots without calling the host")
{
    Rig rig;
    for (char const* line : {".lua reload", ".LUA status", " lua reload", ".server shutdown 1", ".account create x y", ".tt units #1",
                             ".rbac list", ".bnetaccount create", ".gm on\n.lua reload"})
    {
        INFO(line);
        std::string code = std::string("local ok, out = Gm.run([==[") + line + "]==]) return tostring(ok) .. '|' .. out[1]";
        auto r = rig.run(code);
        REQUIRE(r.ok);
        CHECK(r.result.find("false|refused") == 0);
    }
    CHECK(rig.host.gmLines.empty());
    CHECK(rig.ok("local ok, out = Gm.run('.gm on') return tostring(ok) .. '|' .. out[1]") == "true|ran: .gm on");
    CHECK(rig.host.gmLines == std::vector<std::string>{".gm on"});
}

TEST_CASE("Gm.run: a nested command call from the host side is refused (depth 1, no re-entry)")
{
    Rig rig;
    rig.ok("Cmd.register('tt', function(c) c:reply('should not run') end, 0)");
    fl::CommandStatus nested = fl::CommandStatus::Ok;
    rig.host.onGm = [&] { nested = rig.e().runCommand("tt", "", 9).status; };
    rig.ok("Gm.run('.gm on')");
    CHECK(nested == fl::CommandStatus::Refused);
}

TEST_CASE("commands: from another thread the call is refused")
{
    Rig rig;
    rig.ok("Cmd.register('tt', function(c) c:reply('x') end, 0)");
    fl::CommandStatus s = fl::CommandStatus::Ok;
    std::thread t([&] { s = rig.e().runCommand("tt", "", 9).status; });
    t.join();
    CHECK(s == fl::CommandStatus::Refused);
}

TEST_CASE("runScript: runs one script of the folder again, refuses bad names and other threads")
{
    fltest::Rig rig;
    rig.dir.write("probe.lua", "runs = (runs or 0) + 1");
    CHECK(rig.e().runScript("probe").ok);
    CHECK(rig.e().runScript("probe").ok);
    CHECK(rig.ok("return runs") == "2");
    fl::CallResult bad = rig.e().runScript("../probe");
    CHECK_FALSE(bad.ok);
    CHECK(rig.e().runScript("missing").error.find("not found") != std::string::npos);
    rig.dir.write("boom.lua", "error('boom')");
    CHECK(rig.e().runScript("boom").error.find("boom") != std::string::npos);
    fl::CallResult other;
    std::thread t([&] { other = rig.e().runScript("probe"); });
    t.join();
    CHECK_FALSE(other.ok);
    CHECK(rig.ok("return runs") == "2");
}
