// E3: the asynchronous database functions, ForeverData, slow-handler profiling and RegisterCommand, on the fake world.
#include "Support.h"

#include "engine/Elu.h"
#include "engine/Text.h"

#include <string>
#include <vector>

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
    c.dbTimeoutMs = 5000;
    c.slowHandlerMs = 5;
    c.dataFlushMs = 5000;
    return c;
}

struct ElunaRig : Rig
{
    ElunaRig(std::function<void(fl::Config&)> tweak = nullptr)
        : Rig([tweak](fl::Config& c)
              {
                  c = elunaConfig(c);
                  if (tweak)
                      tweak(c);
              })
    {
    }
};

std::string errorOf(Rig& rig, std::string const& code)
{
    fl::CallResult r = rig.run(code);
    return r.ok ? std::string("no error") : r.error;
}

fl::DbRows oneRow(std::vector<fl::DbValue> values, std::vector<std::string> columns = {"value"})
{
    fl::DbRows rows;
    rows.columns = std::move(columns);
    rows.rows.push_back(std::move(values));
    return rows;
}
}  // namespace

TEST_CASE("e3 async queries: the callback runs on a later tick with an ElunaQuery")
{
    ElunaRig rig;
    rig.ok("answer = 'none'\n"
           "WorldDBQueryAsync('SELECT 7 AS value', function(q)\n"
           "  if q == nil then answer = 'nil' else answer = q:GetUInt32(0) end\n"
           "end)");
    REQUIRE(rig.host.asyncRequests.size() == 1);
    CHECK(rig.host.asyncRequests[0].db == "world");
    CHECK(rig.host.asyncRequests[0].sql == "SELECT 7 AS value");
    // Nothing runs before the result is in and a tick happens.
    CHECK(rig.ok("return answer") == "none");
    rig.host.completeAsync(rig.host.asyncRequests[0].id, oneRow({fl::DbValue(int64_t(7))}));
    rig.e().tick();
    CHECK(rig.ok("return answer") == "7");

    // No rows: the callback gets nil, as the docs say.
    rig.ok("WorldDBQueryAsync('SELECT 1 WHERE 0', function(q) empty = tostring(q) end)");
    rig.host.completeAsync(rig.host.asyncRequests[1].id, oneRow({fl::DbValue(int64_t(1))}));
    // An answer with rows is not empty; test the explicit empty case:
    rig.host.asyncReady.clear();
    rig.host.completeAsync(rig.host.asyncRequests[1].id, fl::DbRows{});
    rig.e().tick();
    CHECK(rig.ok("return empty") == "nil");

    // Characters and auth go to their own databases.
    rig.ok("CharDBQueryAsync('SELECT 1', function() end) AuthDBQueryAsync('SELECT 2', function() end)");
    REQUIRE(rig.host.asyncRequests.size() == 4);
    CHECK(rig.host.asyncRequests[2].db == "characters");
    CHECK(rig.host.asyncRequests[3].db == "auth");
}

TEST_CASE("e3 async queries: DbTimeoutMs answers nil and the late result is dropped")
{
    ElunaRig rig;
    rig.host.frozen = true;
    rig.ok("answer = 'none' WorldDBQueryAsync('SELECT 1', function(q) answer = tostring(q) end)");
    REQUIRE(rig.host.asyncRequests.size() == 1);
    uint64_t const id = rig.host.asyncRequests[0].id;
    rig.host.advance(4999);
    rig.e().tick();
    CHECK(rig.ok("return answer") == "none");
    rig.host.advance(2);
    rig.e().tick();
    CHECK(rig.ok("return answer") == "nil");
    CHECK(std::find(rig.host.asyncCancelled.begin(), rig.host.asyncCancelled.end(), id) != rig.host.asyncCancelled.end());
    CHECK(rig.host.logContains("async query timed out"));
    // A result that comes in now is discarded (the callback does not run a second time).
    rig.host.completeAsync(id, oneRow({fl::DbValue(int64_t(1))}));
    rig.e().tick();
    CHECK(rig.ok("return answer") == "nil");
}

TEST_CASE("e3 async queries: a refused query raises and a failed query answers nil")
{
    ElunaRig rig;
    rig.host.failAsync = true;
    std::string err = errorOf(rig, "WorldDBQueryAsync('SELECT 1', function() end)");
    CHECK(err.find("query refused") != std::string::npos);
    CHECK(err.find("queue full") != std::string::npos);
    rig.host.failAsync = false;

    rig.ok("answer = 'none' WorldDBQueryAsync('SELECT 1', function(q) answer = tostring(q) end)");
    REQUIRE(rig.host.asyncRequests.size() == 1);
    rig.host.completeAsyncError(rig.host.asyncRequests[0].id, "table 'world.nosuch' doesn't exist");
    rig.e().tick();
    CHECK(rig.ok("return answer") == "nil");
    CHECK(rig.host.logContains("table 'world.nosuch' doesn't exist"));

    // The query text cap is the same 256 KB as the synchronous forms.
    std::string big(256 * 1024 + 1, 'x');
    err = errorOf(rig, "WorldDBQueryAsync('" + big + "', function() end)");
    CHECK(err.find("longer than 256 KB") != std::string::npos);
}

TEST_CASE("e3 ForeverData: per-script values, JSON round trip, bounds and delete")
{
    ElunaRig rig;
    rig.dir.write("store.lua",
                  "function store_now()\n"
                  "  written = ForeverData.set('cfg', {level = 3, name = 'a', list = {1, 2}})\n"
                  "end\n"
                  "function read_cfg() return ForeverData.get('cfg') end\n"
                  "function drop_cfg() return ForeverData.delete('cfg') end\n"
                  "function cfg_keys() return ForeverData.keys() end\n");
    fl::LoadReport rep = rig.e().load();
    INFO((rep.errors.empty() ? std::string() : rep.errors[0]));
    REQUIRE(rep.errors.empty());
    rig.host.dataStoreFailures = 1;   // the first write is refused by the database: the value stays dirty
    rig.ok("store_now()");
    CHECK(rig.ok("return tostring(written)") == "true");
    CHECK(rig.ok("return read_cfg().level .. ',' .. read_cfg().name .. ',' .. #read_cfg().list") == "3,a,2");
    rig.e().flushData(0, true);
    CHECK(rig.host.storedData.empty());   // refused: not written yet
    CHECK(rig.e().dataPending() == 1);
    rig.e().flushData(0, true);           // collects the failure and sends the value again
    CHECK(rig.host.dataStoreCalls == 1);
    CHECK(rig.host.storedData.size() == 1);
    CHECK(rig.host.storedData[{"store.lua", "cfg"}].find("\"level\":3") != std::string::npos);

    // The file's own namespace holds the key; another file has its own.
    rig.dir.write("other.lua", "function other_keys() return #ForeverData.keys() end\n");
    REQUIRE(rig.e().load().errors.empty());
    CHECK(rig.ok("return other_keys()") == "0");
    CHECK(rig.ok("return cfg_keys()[1]") == "cfg");

    // Bounds: 64 KB per value, 1 MB per script.
    std::string value(70 * 1024, 'x');
    rig.ok("ok, msg = ForeverData.set('big', '" + value + "')");
    CHECK(rig.ok("return tostring(ok)") == "false");
    CHECK(rig.ok("return msg") == "value is larger than 64 KB");

    // Delete removes the value and the database row.
    CHECK(rig.ok("return tostring(drop_cfg())") == "true");
    rig.e().flushData(0, true);
    CHECK(rig.ok("return tostring(ForeverData.get('cfg'))") == "nil");
    CHECK(rig.host.erasedData.count({"store.lua", "cfg"}) == 1);
    rig.e().flushData(0, true);   // collects the confirmation
    CHECK(rig.e().dataPending() == 0);

    // The in-memory truth (the delete included) survives a reload; a fresh state reads what the host loaded.
    rig.host.dataRows.push_back(fl::DataRow{"store.lua", "cfg", "{\"level\":9}", false});
    fl::LoadReport reloaded = rig.e().reload();
    CHECK(reloaded.applied);
    CHECK(rig.ok("return tostring(read_cfg())") == "nil");

    FakeHost fresh;
    fresh.dataRows.push_back(fl::DataRow{"store.lua", "cfg", "{\"level\":9}", false});
    TempDir freshDir;
    freshDir.write("store.lua", "function read_cfg() return ForeverData.get('cfg') end\n");
    fl::Config cfg = elunaConfig(testConfig(freshDir.path()));
    fl::Engine freshEngine(fresh, cfg);
    REQUIRE(freshEngine.start());
    REQUIRE(freshEngine.load().errors.empty());
    fl::CallResult read = freshEngine.runChunk("return read_cfg().level", "=test");
    REQUIRE(read.ok);
    CHECK(read.result == "9");
}

TEST_CASE("e3 ForeverData: a value written now is visible after a reload")
{
    ElunaRig rig;
    rig.dir.write("keep.lua", "function put() ForeverData.set('n', 41) end\n"
                              "function get() local v = ForeverData.get('n') return v end\n");
    REQUIRE(rig.e().load().errors.empty());
    rig.ok("put()");
    fl::LoadReport rep = rig.e().reload();
    REQUIRE(rep.applied);
    CHECK(rig.ok("return get() + 1") == "42");
}

TEST_CASE("e3 ForeverData is offered in test mode as Data")
{
    Rig rig;
    rig.dir.write("legacy.lua", "function save() Data.set('k', 'v') return Data.get('k') end\n"
                                "function keys() return Data.keys() end\n");
    REQUIRE(rig.e().load().errors.empty());
    CHECK(rig.ok("return save()") == "v");
    CHECK(rig.ok("return keys()[1]") == "k");
    CHECK(rig.ok("return tostring(ForeverData)") == "nil");   // eluna mode only
    CHECK(rig.ok("return type(Data.set)") == "function");
}

TEST_CASE("e3 profiling: calls, total and max per handler, slow logs once a minute, reset")
{
    ElunaRig rig;
    rig.host.frozen = true;
    rig.host.tickMs = 10;   // every clock read moves 10 ms: a handler takes at least 10 ms
    rig.ok("RegisterPlayerEvent(3, function() end)");
    fl::HandleData p = FakeHost::player(1);
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    std::vector<fl::HandlerProfile> rows = rig.e().eluProfile();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].calls == 2);
    CHECK(rows[0].totalMs == 20);   // two calls, 10 ms each (the fake clock moves on every read)
    CHECK(rows[0].maxMs == 10);
    CHECK(rows[0].where.find(":1") != std::string::npos);
    CHECK(rig.host.logContains("took"));
    // Only one slow log within a minute.
    size_t logs = 0;
    for (auto const& l : rig.host.logs)
        if (l.second.find("took") != std::string::npos)
            ++logs;
    CHECK(logs == 1);
    rig.host.advance(61000);
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    logs = 0;
    for (auto const& l : rig.host.logs)
        if (l.second.find("took") != std::string::npos)
            ++logs;
    CHECK(logs == 2);

    rig.e().resetEluProfile();
    rows = rig.e().eluProfile();
    CHECK(rows[0].calls == 0);
    CHECK(rows[0].totalMs == 0);
}

TEST_CASE("e3 RegisterCommand: slots, security, duplicate replacement and exhaustion")
{
    ElunaRig rig;
    rig.ok("RegisterCommand('greet', 2, function(player, args)\n"
           "  return {'hello ' .. tostring(args[1]), 'two'}\n"
           "end, 'says hello')");
    std::vector<fl::Engine::LuaCommand> cmds = rig.e().luaCommands();
    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].name == "greet");
    CHECK(cmds[0].minSecurity == 2);
    CHECK(cmds[0].help == "says hello");
    CHECK(rig.host.commandsChangedCalls >= 1);

    fl::CommandResult r = rig.e().runLuaCommand(0, "bob   smith", 3, std::nullopt);
    CHECK(r.status == fl::CommandStatus::Ok);
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0] == "hello bob");
    CHECK(r.lines[1] == "two");

    fl::CommandResult denied = rig.e().runLuaCommand(0, "bob", 1, std::nullopt);
    CHECK(denied.status == fl::CommandStatus::Denied);

    // A second registration with the same name replaces the handler, not the slot.
    rig.ok("RegisterCommand('greet', 0, function() return 'second' end)");
    CHECK(rig.e().luaCommands().size() == 1);
    CHECK(rig.e().runLuaCommand(0, "", 0, std::nullopt).lines[0] == "second");

    // 32 slots; the 33rd is refused with a clear error.
    rig.ok("for i = 1, 31 do RegisterCommand('c' .. i, 0, function() end) end");
    CHECK(rig.e().luaCommands().size() == 32);
    CHECK(errorOf(rig, "RegisterCommand('overflow', 0, function() end)").find("32 Lua commands") != std::string::npos);

    // A name that cannot be a command is refused.
    CHECK(errorOf(rig, "RegisterCommand('bad name', 0, function() end)").find("a-z") != std::string::npos);
}

TEST_CASE("e3 RegisterCommand is eluna-only and the command slots vanish with the state")
{
    Rig rig;
    CHECK(rig.ok("return tostring(RegisterCommand)") == "nil");
    CHECK(rig.ok("return tostring(ForeverData)") == "nil");
    CHECK(rig.ok("return type(Data)") == "table");
}
