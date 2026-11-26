// E0: the eluna mode. The toolkit tables are gone, the Eluna classes cover the handles, the folder loads recursively,
// require resolves modules, every error carries script:line, the budgets are off by default and can be turned on, and
// a nested Eluna event is dropped at depth 8.
#include "Support.h"

#include <atomic>

using namespace fltest;

namespace
{
fl::Config elunaConfig(fl::Config c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    return c;
}
}  // namespace

TEST_CASE("eluna mode: the Eluna API is installed and the toolkit tables are not")
{
    Rig eluna([](fl::Config& c) { c = elunaConfig(c); });
    CHECK(eluna.ok("return type(RegisterPlayerEvent)") == "function");
    CHECK(eluna.ok("return type(RegisterCreatureEvent)") == "function");
    CHECK(eluna.ok("return type(CreateLuaEvent)") == "function");
    CHECK(eluna.ok("return type(WorldDBQuery)") == "function");
    CHECK(eluna.ok("return type(bit_and)") == "function");
    CHECK(eluna.ok("return type(Event)") == "nil");
    CHECK(eluna.ok("return type(Timer)") == "nil");
    CHECK(eluna.ok("return type(World)") == "nil");
    CHECK(eluna.ok("return type(Db)") == "nil");
    CHECK(eluna.ok("return type(Cmd)") == "nil");

    Rig test;
    CHECK(test.ok("return type(Event)") == "table");
    CHECK(test.ok("return type(RegisterPlayerEvent)") == "nil");
    CHECK(test.ok("return type(WorldDBQuery)") == "nil");
    CHECK(test.ok("return type(CreateLongLong)") == "nil");
    CHECK(test.ok("return type(GetPlayerGUID)") == "nil");
    CHECK(test.ok("return type(GetRealmID)") == "nil");
    CHECK(test.ok("return type(IsBagPos)") == "nil");
    CHECK(test.ok("return type(RegisterSpellEvent)") == "nil");
}

TEST_CASE("eluna mode: the standard libraries open only with OpenStdLibs")
{
    Rig closed([](fl::Config& c) { c = elunaConfig(c); });
    CHECK(closed.ok("return type(io)") == "nil");
    CHECK(closed.ok("return type(os)") == "nil");
    CHECK(closed.ok("return type(package)") == "nil");
    CHECK(closed.ok("return type(dofile)") == "nil");
    CHECK(closed.ok("return type(string.dump)") == "nil");

    Rig open([](fl::Config& c)
    {
        c = elunaConfig(c);
        c.openStdLibs = true;
    });
    CHECK(open.ok("return type(io)") == "table");
    CHECK(open.ok("return type(os)") == "table");
    CHECK(open.ok("return type(package)") == "table");
    CHECK(open.ok("return type(dofile)") == "function");
    CHECK(open.ok("return type(string.dump)") == "function");
    CHECK(open.ok("return package.path:find('" + open.dir.path() + "', 1, true) ~= nil") == "true");
}

TEST_CASE("eluna mode: the budgets are off by default and apply when configured")
{
    // The module's own defaults for Mode = eluna (flh::settingsFrom): a loop of six million iterations runs to the end.
    Rig off([](fl::Config& c) { applySettings(c, {{"ForeverLua.Mode", "eluna"}}); });
    fl::CallResult r = off.run("local x = 0 for i = 1, 6000000 do x = x + i end return x");
    CHECK(r.ok);
    CHECK(r.result == "18000003000000");

    // A 1 ms / 1000 instruction budget cuts the same loop off.
    Rig on([](fl::Config& c)
    {
        c = elunaConfig(c);
        c.eventBudget = {1, 1000};
    });
    fl::CallResult cut = on.run("local x = 0 for i = 1, 6000000 do x = x + i end return x");
    CHECK_FALSE(cut.ok);
    CHECK(cut.budget);
}

TEST_CASE("eluna mode: the folder loads recursively, sorted, inith.lua of a folder first")
{
    Rig rig([](fl::Config& c) { c = elunaConfig(c); });
    rig.dir.write("b.lua", "order = (order or '') .. 'b '");
    rig.dir.write("z.lua", "order = (order or '') .. 'z '");
    rig.dir.write("sub/a.lua", "order = (order or '') .. 'sub/a '");
    rig.dir.write("sub/init.lua", "order = (order or '') .. 'sub/init '");
    rig.dir.write("sub/deep/x.lua", "order = (order or '') .. 'sub/deep/x '");
    fl::LoadReport rep = rig.e().load();
    INFO(rig.host.logCount(fl::LogLevel::Error));
    REQUIRE(rep.errors.empty());
    CHECK(rig.ok("return order") == "b z sub/init sub/a sub/deep/x ");
    CHECK(rep.loaded.size() == 5);
}

TEST_CASE("eluna mode: require resolves dots, loads once, and refuses a symlink out of the folder")
{
    Rig rig([](fl::Config& c) { c = elunaConfig(c); });
    rig.dir.write("lib/util.lua", "count = (count or 0) + 1 return { name = 'util', count = count }");
    rig.dir.write("sub/init.lua", "return { name = 'init' }");
    rig.dir.write("main.lua",
                  "local a = require('lib.util')\n"
                  "local b = require('lib.util')\n"
                  "same = a == b\n"
                  "n = a.count\n"
                  "initname = require('sub').name\n");
    REQUIRE(rig.e().load().errors.empty());
    CHECK(rig.ok("return tostring(same)") == "true");
    CHECK(rig.ok("return n") == "1");
    CHECK(rig.ok("return initname") == "init");

    std::error_code ec;
    std::filesystem::create_symlink("/etc/hostname", std::filesystem::path(rig.dir.path()) / "evil.lua", ec);
    REQUIRE_FALSE(ec);
    fl::CallResult r = rig.run("require('evil')");
    CHECK_FALSE(r.ok);
    bool const refused = r.error.find("outside the script folder") != std::string::npos ||
                         r.error.find("not found") != std::string::npos;
    CHECK(refused);
}

TEST_CASE("eluna mode: the error ring holds script:line and shows in the status")
{
    Rig rig([](fl::Config& c) { c = elunaConfig(c); });
    std::string body;
    for (int i = 1; i <= 11; ++i)
        body += "x" + std::to_string(i) + " = " + std::to_string(i) + "\n";
    body += "error('boom at twelve')\n";   // line 12
    rig.dir.write("bad.lua", body);
    fl::LoadReport rep = rig.e().load();
    REQUIRE_FALSE(rep.errors.empty());
    CHECK(rep.errors[0].find("bad.lua:12") != std::string::npos);

    CHECK(rig.e().status().errors == 1);
    std::string text = rig.e().errorRingText();
    CHECK(text.find("bad.lua:12") != std::string::npos);
    CHECK(text.find("boom at twelve") != std::string::npos);
    CHECK(text.find("x1") != std::string::npos);
    CHECK(rig.e().statusText().find("bad.lua:12") != std::string::npos);

    // The same error twice is one entry with count 2.
    REQUIRE_FALSE(rig.e().load().errors.empty());
    CHECK(rig.e().status().errors == 1);
    CHECK(rig.e().errorRingText().find("(x2") != std::string::npos);
    rig.e().clearErrorRing();
    CHECK(rig.e().status().errors == 0);
    CHECK(rig.e().errorRingText().find("no script errors") != std::string::npos);
}

TEST_CASE("eluna mode: a nested Eluna event is dropped at depth 8")
{
    // The handler calls back into the host, which emits the same event again: the depth grows to the cap.
    Rig rig([](fl::Config& c) { c = elunaConfig(c); });
    fl::HandleData p = FakeHost::player(7);
    fl::ObjectInfo info;
    info.name = "Nested";
    rig.host.add(p, info);
    std::atomic<int> depth{0}, emitted{0};
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const&,
                              std::vector<fl::EventValue>&)
    {
        if (op == "object.getScale")
        {
            ++depth;
            ++emitted;
            fl::EventResult r = rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
            (void)r;
            --depth;
            return false;   // the binding then raises "not available" inside the handler
        }
        return false;
    };
    rig.ok("RegisterPlayerEvent(3, function(ev, player) player:GetScale() end) sticker = 1");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    // Eight handlers run (depths 1..8); each calls GetScale, so the host emitted eight times and the ninth was refused.
    CHECK(emitted == 8);
    CHECK(rig.host.logContains("nested Eluna call at depth 8 refused"));
}
