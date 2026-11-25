// Review round of E2/E3, host-side rules that are game-free: the command map gate (never rebuild the core's command map
// inside a command), the reserved command names, world-only calls queued from a map thread, and the ForeverData SQL.
// The core adapter (Commands.cpp, EluWorld.cpp) only supplies the facts and is compiled against the core in the
// game-side build.
#include "Support.h"

#include "CommandGate.h"
#include "DataSql.h"
#include "WorldCall.h"

#include <memory>
#include <thread>

using namespace fltest;

namespace
{
// A stand-in for the core's COMMAND_MAP: nodes that die when the map is cleared, and an execute() that, like the core's
// ChatCommandNode::TryExecuteCommand, calls the invoker and THEN reads the node again (the permission, the help).
struct ModelNode
{
    std::function<void()> invoker;
    int permission = 3;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    ~ModelNode() { *alive = false; }
};

struct ModelMap
{
    std::map<std::string, std::unique_ptr<ModelNode>> nodes;
    int rebuilds = 0;

    void clear() { nodes.clear(); }
    // Returns whether the node was still alive after the invoker ran; reads it (ASan: a freed node is a use-after-free)
    // only when it is.
    bool execute(std::string const& name, int* permissionRead = nullptr)
    {
        ModelNode* node = nodes.at(name).get();
        std::shared_ptr<bool> alive = node->alive;
        node->invoker();
        if (!*alive)
            return false;
        if (permissionRead)
            *permissionRead = node->permission;
        return true;
    }
};

// The host the module has: commandsChanged() only marks the gate.
struct GateHost : FakeHost
{
    flh::CommandMapGate gate;
    void commandsChanged() override
    {
        FakeHost::commandsChanged();
        gate.markChanged();
    }
};

// The behaviour before the fix: the map is cleared at once, inside the running command.
struct ImmediateHost : FakeHost
{
    ModelMap* map = nullptr;
    void commandsChanged() override
    {
        FakeHost::commandsChanged();
        if (map)
            map->clear();
    }
};

fl::Config elunaCfg(std::string const& path)
{
    fl::Config c = testConfig(path);
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    return c;
}

// The command table the module builds: ".lua reload" plus one node per Lua command, like forever_lua_commands.
void buildMap(ModelMap& map, fl::Engine& engine)
{
    map.nodes.clear();
    ++map.rebuilds;
    auto lua = std::make_unique<ModelNode>();
    lua->invoker = [&engine] { engine.reload(); };
    map.nodes["lua"] = std::move(lua);
    std::vector<fl::Engine::LuaCommand> cmds = engine.luaCommands();
    for (size_t i = 0; i < cmds.size(); ++i)
    {
        auto node = std::make_unique<ModelNode>();
        node->invoker = [&engine, i, name = cmds[i].name] { engine.runLuaCommand(i, "", 3, std::nullopt, name); };
        map.nodes[cmds[i].name] = std::move(node);
    }
}
}  // namespace

TEST_CASE("command gate: an in-game .lua reload never clears the core's command map inside the command")
{
    GateHost host;
    TempDir dir;
    dir.write("a.lua", "RegisterCommand('one', 1, function() return 'one' end)\n");
    fl::Engine engine(host, elunaCfg(dir.path()));
    REQUIRE(engine.start());
    REQUIRE(engine.load().errors.empty());
    CHECK(host.gate.apply([] {}));   // the registration at load marked the map changed
    ModelMap map;
    buildMap(map, engine);
    REQUIRE(map.nodes.count("one") == 1);

    // The script changes: the reload adds a second command, from inside the ".lua" command.
    dir.write("b.lua", "RegisterCommand('two', 1, function() return 'two' end)\n");
    int permission = 0;
    CHECK(map.execute("lua", &permission));   // the node survived the invoker (it would be a use-after-free otherwise)
    CHECK(permission == 3);
    CHECK(map.nodes.count("lua") == 1);
    CHECK(host.gate.pending());               // the change is queued ...
    CHECK(host.gate.apply([&] { map.clear(); }));   // ... and applied by the world script, outside the command
    buildMap(map, engine);
    CHECK(map.nodes.count("two") == 1);
    CHECK_FALSE(host.gate.pending());

    // A reload that leaves the command list as it was does not ask for a rebuild.
    CHECK(map.execute("lua"));
    CHECK_FALSE(host.gate.pending());
}

TEST_CASE("command gate: RegisterCommand inside a Lua command does not clear the map under the running command")
{
    GateHost host;
    TempDir dir;
    dir.write("a.lua",
              "RegisterCommand('maker', 1, function()\n"
              "  RegisterCommand('made', 1, function() return 'made' end)\n"
              "  RegisterCommand('maker', 1, function() return 'again' end)   -- replacing a name changes no map name\n"
              "  return 'ok'\n"
              "end)\n");
    fl::Engine engine(host, elunaCfg(dir.path()));
    REQUIRE(engine.start());
    REQUIRE(engine.load().errors.empty());
    host.gate.apply([] {});
    ModelMap map;
    buildMap(map, engine);
    int permission = 0;
    CHECK(map.execute("maker", &permission));
    CHECK(permission == 3);
    CHECK(host.gate.pending());
    CHECK(host.gate.apply([&] { map.clear(); }));
    buildMap(map, engine);
    CHECK(map.nodes.count("made") == 1);
}

TEST_CASE("command gate: a slot whose command changed before the map was rebuilt never runs another command")
{
    GateHost host;
    TempDir dir;
    dir.write("a.lua", "RegisterCommand('first', 1, function() return 'first' end)\n"
                       "RegisterCommand('second', 1, function() return 'second' end)\n");
    fl::Engine engine(host, elunaCfg(dir.path()));
    REQUIRE(engine.start());
    REQUIRE(engine.load().errors.empty());
    // The core's map was built with slot 1 = "second"; a reload then drops "first": slot 1 would now be out of range, and a
    // reload that reorders would put another command there.
    dir.write("a.lua", "RegisterCommand('second', 1, function() return 'second' end)\n"
                       "RegisterCommand('first', 1, function() return 'first' end)\n");
    REQUIRE(engine.reload().applied);
    fl::CommandResult r = engine.runLuaCommand(1, "", 3, std::nullopt, "second");   // the old slot 1 name
    CHECK(r.status == fl::CommandStatus::NoHandler);
    CHECK(r.lines.empty());
    CHECK(engine.runLuaCommand(0, "", 3, std::nullopt, "second").lines[0] == "second");
}

// Documents the bug this round fixed: with the old host (commandsChanged clears the map at once) the node dies under the
// running command. The model catches that; Catch2 expects this case to fail, so it passes only while the model still
// detects the old behaviour.
TEST_CASE("command gate: the old behaviour (clearing the map at once) is caught by the model", "[!shouldfail]")
{
    ImmediateHost host;
    TempDir dir;
    dir.write("a.lua", "RegisterCommand('one', 1, function() return 'one' end)\n");
    fl::Engine engine(host, elunaCfg(dir.path()));
    REQUIRE(engine.start());
    REQUIRE(engine.load().errors.empty());
    ModelMap map;
    host.map = &map;
    buildMap(map, engine);
    dir.write("b.lua", "RegisterCommand('two', 1, function() return 'two' end)\n");
    CHECK(map.execute("lua"));   // fails: the reload cleared the map while the "lua" command was running
}

TEST_CASE("command names: a name that equals or starts a core or module root is reserved")
{
    flh::CommandNameSet names;
    names.assign({"Help", "gm", "account", "cast", "lookup", "lua", "tt"});
    for (char const* n : {"help", "hel", "h", "gm", "g", "account", "acc", "a", "cast", "c", "lookup", "lo", "lua", "lu", "l",
                          "tt", "t", "HELP"})
        CHECK(names.reserves(n));
    for (char const* n : {"helper", "mycmd", "gmx", "ttx", "greet", "luahello", "castle"})
        CHECK_FALSE(names.reserves(n));
    // The table could not be read: nothing is safe, every name is refused.
    flh::CommandNameSet unknown;
    CHECK(unknown.reserves("mycmd"));
}

TEST_CASE("RegisterCommand refuses a core or module root, so the core's duplicate-name assert can never fire")
{
    FakeHost host;
    host.coreCommandRoots = {"help", "gm", "account", "cast", "lookup", "reload"};
    TempDir dir;
    fl::Config cfg = elunaCfg(dir.path());
    cfg.commandRoots = {"tt"};
    fl::Engine engine(host, cfg);
    REQUIRE(engine.start());
    auto attempt = [&](std::string const& name)
    {
        fl::CallResult r = engine.runChunk("RegisterCommand('" + name + "', 1, function() end)", "=test");
        return r.ok ? std::string("ok") : r.error;
    };
    for (char const* n : {"help", "hel", "h", "gm", "g", "account", "acc", "cast", "c", "lookup", "lo", "reload", "lua", "lu",
                          "l", "tt", "t", ".help"})
        CHECK(attempt(n).find("equals or starts") != std::string::npos);
    CHECK(attempt("mycmd") == "ok");
    CHECK(attempt("helper") == "ok");   // longer than a root: not a prefix of it
    // What the core would load: the roots and our commands, with no name twice (LoadFromBuilder asserts on a duplicate).
    std::set<std::string> table(host.coreCommandRoots.begin(), host.coreCommandRoots.end());
    table.insert("lua");
    table.insert("tt");
    for (auto const& c : engine.luaCommands())
        CHECK(table.insert(c.name).second);
    CHECK(engine.luaCommands().size() == 2);
}

TEST_CASE("world calls: a map thread's world-only call is queued and runs on the world thread's next tick")
{
    flh::AccessRules rules;
    rules.setWorldThread();
    rules.setMapsIdle(true);
    flh::DeferQueue queue(2);
    int ran = 0;

    // The world thread with the maps idle: now.
    CHECK(flh::runOnWorld(rules, queue, [&] { ++ran; }) == flh::WorldCallResult::Ran);
    CHECK(ran == 1);

    // The maps are updating (the world thread inside the map phase): queued.
    rules.setMapsIdle(false);
    CHECK(flh::runOnWorld(rules, queue, [&] { ++ran; }) == flh::WorldCallResult::Queued);
    CHECK(ran == 1);

    // Inside a map's scope, even on the world thread: queued. The queue is bounded: the third request is dropped.
    rules.setMapsIdle(true);
    {
        flh::MapScope scope(1, 0);
        CHECK(flh::runOnWorld(rules, queue, [&] { ++ran; }) == flh::WorldCallResult::Queued);
        CHECK(flh::runOnWorld(rules, queue, [&] { ++ran; }) == flh::WorldCallResult::Dropped);
    }
    CHECK(ran == 1);
    CHECK(queue.drain() == 2);
    CHECK(ran == 3);
    CHECK(queue.dropped() == 1);

    // A map thread (another thread, with its scope): the job runs on the world thread when it drains.
    std::thread::id jobThread;
    flh::WorldCallResult how = flh::WorldCallResult::Ran;
    std::thread mapThread([&]
    {
        flh::MapScope scope(1, 0);
        how = flh::runOnWorld(rules, queue, [&] { jobThread = std::this_thread::get_id(); });
    });
    mapThread.join();
    CHECK(how == flh::WorldCallResult::Queued);
    CHECK(jobThread == std::thread::id{});
    queue.drain();
    CHECK(jobThread == std::this_thread::get_id());
}

TEST_CASE("world calls: a call with a result answers now on the world thread and queues a copy from a map thread")
{
    flh::AccessRules rules;
    rules.setWorldThread();
    rules.setMapsIdle(true);
    flh::DeferQueue queue;
    int ran = 0;
    std::string arg = "ban me";
    auto job = [=, &ran]() -> int
    {
        ++ran;
        return static_cast<int>(arg.size());
    };
    flh::WorldCallResult how = flh::WorldCallResult::Dropped;
    std::optional<int> direct = flh::callOnWorld<int>(rules, queue, job, &how);
    REQUIRE(direct);
    CHECK(*direct == 6);
    CHECK(how == flh::WorldCallResult::Ran);

    rules.setMapsIdle(false);
    std::optional<int> queued = flh::callOnWorld<int>(rules, queue, job, &how);
    CHECK_FALSE(queued);   // the script gets nil (Ban, SendMail)
    CHECK(how == flh::WorldCallResult::Queued);
    CHECK(ran == 1);       // not run yet
    rules.setMapsIdle(true);
    queue.drain();
    CHECK(ran == 2);
}

TEST_CASE("ForeverData SQL: binary key columns, hex literals, exact bytes, a migration for an older table")
{
    std::string const ddl = flh::dataTableDdl();
    CHECK(ddl.find("script VARBINARY(255)") != std::string::npos);
    CHECK(ddl.find("k VARBINARY(255)") != std::string::npos);
    CHECK(ddl.find("v MEDIUMBLOB") != std::string::npos);
    CHECK(ddl.find("VARCHAR") == std::string::npos);
    CHECK(ddl.find("MEDIUMTEXT") == std::string::npos);

    CHECK(flh::sqlHex(std::string("a\0\xff A", 5)) == "X'6100FF2041'");
    // "Gold" and "gold", "a" and "a ": different bytes, different literals, so different rows in a binary column.
    CHECK(flh::sqlHex("Gold") != flh::sqlHex("gold"));
    CHECK(flh::sqlHex("a") != flh::sqlHex("a "));
    std::string const up = flh::dataUpsertSql("s.lua", std::string("k\0z", 3), "{\"a\":1}");
    CHECK(up.find("X'" "6B007A" "'") != std::string::npos);   // the NUL byte is in the literal, not a terminator
    CHECK(up.find("ON DUPLICATE KEY UPDATE") != std::string::npos);
    CHECK(flh::dataEraseSql("s", "k") == "DELETE FROM forever_lua_data WHERE script=X'73' AND k=X'6B'");

    using Cols = std::vector<std::pair<std::string, std::string>>;
    CHECK(flh::dataTableNeedsMigration(Cols{{"script", "varchar"}, {"k", "varchar"}, {"v", "mediumtext"}, {"updated", "bigint"}}));
    CHECK(flh::dataTableNeedsMigration(Cols{{"script", "varbinary"}, {"k", "varchar"}, {"v", "mediumblob"}}));
    CHECK_FALSE(flh::dataTableNeedsMigration(Cols{{"script", "varbinary"}, {"k", "varbinary"}, {"v", "mediumblob"}, {"updated", "bigint"}}));
    CHECK(flh::dataMigrateSql().find("MODIFY k VARBINARY(255)") != std::string::npos);
}
