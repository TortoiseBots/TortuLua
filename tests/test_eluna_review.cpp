// The E2/E3 review round: instance events as a core-hook gap, world-only reads, asynchronous query ids across a reload and a
// shutdown, and the ForeverData fixes (load refusal, byte-exact keys, confirmed writes, shutdown flush, reload hand-over,
// namespaces). Each case names the failure it covers.
#include "Support.h"

#include "engine/Elu.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace fltest;

namespace
{
fl::Config elunaCfg(fl::Config c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    c.dbTimeoutMs = 5000;
    c.dataFlushMs = 5000;
    return c;
}

struct ElunaRig : Rig
{
    explicit ElunaRig(std::function<void(fl::Config&)> tweak = nullptr)
        : Rig([tweak](fl::Config& c)
              {
                  c = elunaCfg(c);
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

fl::DbRows oneRow(int64_t v)
{
    fl::DbRows rows;
    rows.columns = {"value"};
    rows.rows.push_back({fl::DbValue(v)});
    return rows;
}

fl::HandleData groupHandle(uint64_t id)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Group;
    h.guid.hi = 1;
    h.guid.lo = id;
    return h;
}

void setGlobal(Rig& rig, char const* name, fl::HandleData const& h)
{
    lua_State* L = rig.e().state();
    int top = lua_gettop(L);
    fl::eluPushHandle(L, h);
    lua_setglobal(L, name);
    lua_settop(L, top);
}
}  // namespace

// ---- instance events ----

TEST_CASE("review: RegisterInstanceEvent and dungeon/raid map events raise the core-hook error at registration")
{
    ElunaRig rig;
    rig.host.instancedMaps = {33, 409};
    std::string err = errorOf(rig, "RegisterInstanceEvent(5, 1, function() end)");
    CHECK(err.find("core hook C11") != std::string::npos);
    CHECK(err.find("InstanceMapScript is database-bound") != std::string::npos);
    err = errorOf(rig, "RegisterMapEvent(409, 4, function() end)");
    CHECK(err.find("core hook C11") != std::string::npos);
    err = errorOf(rig, "RegisterMapEvent(33, 1, function() end)");
    CHECK(err.find("dungeon or raid map") != std::string::npos);
    // Nothing was registered (no silent no-op that looks like a working handler).
    CHECK(rig.e().status().elunaHandlers == 0);
    // A world map still registers; so does ClearInstanceEvents (nothing to clear).
    CHECK(errorOf(rig, "RegisterMapEvent(0, 1, function() end)") == "no error");
    CHECK(rig.e().status().elunaHandlers == 1);
    CHECK(errorOf(rig, "ClearInstanceEvents(5)") == "no error");
    // The server's own map events register, and say once that they fire for world maps only.
    CHECK(errorOf(rig, "RegisterServerEvent(21, function() end) RegisterServerEvent(22, function() end)") == "no error");
    CHECK(rig.host.logContains("fire for world (continent) maps only"));
    size_t notes = 0;
    for (auto const& l : rig.host.logs)
        if (l.second.find("fire for world (continent) maps only") != std::string::npos)
            ++notes;
    CHECK(notes == 1);
}

// ---- world-only reads ----

TEST_CASE("review: Group/Guild member lists are refused on a map thread, and a bad remove method is refused")
{
    ElunaRig rig;
    fl::HandleData g = groupHandle(5);
    rig.host.add(g, fl::ObjectInfo{});
    rig.host.gameResults["group.getMembers"] = {};
    rig.host.gameResults["group.removeMember"] = {fl::EventValue(true)};
    setGlobal(rig, "group", g);
    CHECK(errorOf(rig, "return #group:GetMembers()") == "no error");
    rig.host.worldReadOk = false;   // a map-thread handler
    std::string err = errorOf(rig, "return group:GetMembers()");
    CHECK(err.find("only available on the world thread") != std::string::npos);
    fl::HandleData gu;
    gu.kind = fl::ObjKind::Guild;
    gu.guid.lo = 9;
    rig.host.add(gu, fl::ObjectInfo{});
    setGlobal(rig, "guild", gu);
    err = errorOf(rig, "return guild:GetMembers()");
    CHECK(err.find("only available on the world thread") != std::string::npos);
    rig.host.worldReadOk = true;

    CHECK(errorOf(rig, "return group:RemoveMember('00000000000000000000000000000001', 4)").find("method must be 0..3") !=
          std::string::npos);
    CHECK(errorOf(rig, "return group:RemoveMember('00000000000000000000000000000001', 3)") == "no error");
}

// ---- asynchronous queries ----

TEST_CASE("review: async query ids are unique for the process, a reload drops the old callbacks and delivers nothing across")
{
    ElunaRig rig;
    rig.ok("old_ran = 0 WorldDBQueryAsync('SELECT 1', function(q) old_ran = old_ran + 1 end)");   // the running state's query
    REQUIRE(rig.host.asyncRequests.size() == 1);
    uint64_t const oldId = rig.host.asyncRequests[0].id;

    // The candidate's load issues its own query. Before the fix its id restarted at 1: the same id as the running state's.
    rig.dir.write("new.lua", "new_rows = 'none'\nWorldDBQueryAsync('SELECT 2', function(q) new_rows = q:GetUInt32(0) end)\n"
                             "RegisterPlayerEvent(3, function() handler_ran = true end)\n");
    fl::LoadReport rep = rig.e().reload();
    REQUIRE(rep.applied);
    REQUIRE(rig.host.asyncRequests.size() == 2);
    uint64_t const newId = rig.host.asyncRequests[1].id;
    CHECK(newId != oldId);
    CHECK(newId > oldId);
    CHECK(std::find(rig.host.asyncCancelled.begin(), rig.host.asyncCancelled.end(), oldId) != rig.host.asyncCancelled.end());
    CHECK(std::find(rig.host.asyncCancelled.begin(), rig.host.asyncCancelled.end(), newId) == rig.host.asyncCancelled.end());
    CHECK(rig.host.logContains("reload dropped 1 pending asynchronous database queries"));

    // The old query answers late: nothing runs. The new query gets its own rows.
    rig.host.completeAsync(oldId, oneRow(111));
    rig.host.completeAsync(newId, oneRow(222));
    rig.e().tick();
    CHECK(rig.ok("return tostring(old_ran)") == "nil");      // the old state is gone: its callback never ran
    CHECK(rig.ok("return new_rows") == "222");
    // The adopted state's registry is intact: the handler the candidate registered still runs (the old query's registry
    // reference used to be released in the NEW state, freeing an unrelated reference there).
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(FakeHost::player(1))});
    CHECK(rig.ok("return tostring(handler_ran)") == "true");
}

TEST_CASE("review: a failed reload keeps the running state's pending query and cancels only the candidate's own")
{
    ElunaRig rig;
    rig.ok("kept = 'none' WorldDBQueryAsync('SELECT 1', function(q) kept = q:GetUInt32(0) end)");
    uint64_t const keptId = rig.host.asyncRequests[0].id;
    rig.dir.write("bad.lua", "WorldDBQueryAsync('SELECT 2', function() end)\nerror('boom')\n");
    fl::LoadReport rep = rig.e().reload();
    CHECK_FALSE(rep.applied);
    REQUIRE(rig.host.asyncRequests.size() == 2);
    uint64_t const candidateId = rig.host.asyncRequests[1].id;
    CHECK(candidateId != keptId);
    CHECK(std::find(rig.host.asyncCancelled.begin(), rig.host.asyncCancelled.end(), keptId) == rig.host.asyncCancelled.end());
    CHECK(std::find(rig.host.asyncCancelled.begin(), rig.host.asyncCancelled.end(), candidateId) !=
          rig.host.asyncCancelled.end());
    rig.host.completeAsync(keptId, oneRow(7));
    rig.e().tick();
    CHECK(rig.ok("return kept") == "7");
}

TEST_CASE("review: shutdown with a pending query drops the callback with a log line and delivers nothing later")
{
    ElunaRig rig;
    rig.ok("ran = false WorldDBQueryAsync('SELECT 1', function() ran = true end)");
    uint64_t const id = rig.host.asyncRequests[0].id;
    rig.e().shutdown();
    CHECK(std::find(rig.host.asyncCancelled.begin(), rig.host.asyncCancelled.end(), id) != rig.host.asyncCancelled.end());
    CHECK(rig.host.logContains("1 pending asynchronous database queries were dropped"));
    rig.host.completeAsync(id, oneRow(1));
    CHECK(rig.e().tick() == 0);   // not running: nothing is delivered
    CHECK_FALSE(rig.e().running());
}

// ---- ForeverData ----

TEST_CASE("review: ForeverData that could not be read is unavailable (nothing is overwritten) and the load is retried")
{
    FakeHost fresh;
    fresh.dataLoadOk = false;
    fresh.dataRows.push_back(fl::DataRow{"counter.lua", "n", "41", false});
    TempDir dir;
    dir.write("counter.lua", "function bump() local v = ForeverData.get('n') or 0 local ok, why = ForeverData.set('n', v + 1) "
                             "return tostring(ok) .. ':' .. tostring(why) end\n"
                             "function read() return tostring(ForeverData.get('n')) end\n");
    fl::Engine engine(fresh, elunaCfg(testConfig(dir.path())));
    REQUIRE(engine.start());
    REQUIRE(engine.load().errors.empty());
    CHECK_FALSE(engine.dataAvailable());
    CHECK(fresh.logContains("ForeverData could not be loaded: database down"));
    // The script initialises from "nothing" and tries to write: refused, so the stored 41 can never be overwritten with 1.
    fl::CallResult r = engine.runChunk("return bump()", "=test");
    REQUIRE(r.ok);
    CHECK(r.result.find("false:ForeverData is unavailable") == 0);
    engine.flushData(0, true);
    CHECK(fresh.storedData.empty());
    CHECK(fresh.dataWriteOrder.empty());
    // The database comes back: the next retry (30 s later) loads the table.
    fresh.dataLoadOk = true;
    int const calls = fresh.dataLoadCalls;
    fresh.advance(10000);
    engine.tick();
    CHECK(fresh.dataLoadCalls == calls);   // too early
    fresh.advance(21000);
    engine.tick();
    CHECK(engine.dataAvailable());
    CHECK(engine.runChunk("return read()", "=test").result == "41");
    // The failed load was logged once, not on every tick.
    size_t logged = 0;
    for (auto const& l : fresh.logs)
        if (l.second.find("ForeverData could not be loaded") != std::string::npos)
            ++logged;
    CHECK(logged == 1);
}

TEST_CASE("review: stored rows over a bound are skipped with a log line, never silently")
{
    FakeHost host;
    for (int i = 0; i < 20; ++i)   // 20 values of 60 KB = 1.2 MB: the namespace bound is 1 MB
        host.dataRows.push_back(fl::DataRow{"big.lua", "k" + std::to_string(i), std::string(60 * 1024, 'x'), false});
    host.dataRows.push_back(fl::DataRow{"ok.lua", "k", "1", false});
    host.dataRows.push_back(fl::DataRow{"ok.lua", std::string(256, 'k'), "1", false});   // a key over 255 bytes
    TempDir dir;
    fl::Engine engine(host, elunaCfg(testConfig(dir.path())));
    REQUIRE(engine.start());
    CHECK(engine.dataAvailable());
    CHECK(engine.dataKeys("big.lua").size() < 20);
    CHECK(engine.dataKeys("big.lua").size() > 10);
    CHECK(engine.dataKeys("ok.lua").size() == 1);
    CHECK(host.logContains("stored ForeverData rows were not loaded"));
}

TEST_CASE("review: ForeverData keys are byte-exact: case, trailing spaces, NUL and high bytes, 255 bytes at most")
{
    ElunaRig rig;
    rig.dir.write("keys.lua", "function put(k, v) return ForeverData.set(k, v) end\n"
                              "function get(k) return ForeverData.get(k) end\n"
                              "function count() return #ForeverData.keys() end\n");
    REQUIRE(rig.e().load().errors.empty());
    rig.ok("put('Gold', 1) put('gold', 2) put('a', 3) put('a ', 4) put('k\\0z', 5) put('k\\255', 6)");
    CHECK(rig.ok("return count()") == "6");
    CHECK(rig.ok("return get('Gold') .. ',' .. get('gold') .. ',' .. get('a') .. ',' .. get('a ') .. ',' .. get('k\\0z') .. ',' .. get('k\\255')") ==
          "1,2,3,4,5,6");
    CHECK(rig.ok("return tostring(get('k'))") == "nil");
    CHECK(rig.ok("local ok = put(string.rep('x', 255), 1) return tostring(ok)") == "true");
    CHECK(rig.ok("local ok, why = put(string.rep('x', 256), 1) return tostring(ok) .. ':' .. why") ==
          "false:key must be 1..255 bytes");
    rig.e().flushData(0, true);
    // The host received the exact bytes of every key, as separate writes.
    CHECK(rig.host.storedData.count({"keys.lua", "Gold"}) == 1);
    CHECK(rig.host.storedData.count({"keys.lua", "gold"}) == 1);
    CHECK(rig.host.storedData.count({"keys.lua", "a "}) == 1);
    CHECK(rig.host.storedData.count({"keys.lua", std::string("k\0z", 3)}) == 1);
    CHECK(rig.host.storedData.count({"keys.lua", "k\xff"}) == 1);
    CHECK(rig.host.storedData.size() == 7);
}

TEST_CASE("review: a ForeverData write is done only when the database confirmed it")
{
    ElunaRig rig;
    rig.dir.write("w.lua", "function put(v) return ForeverData.set('k', v) end\n");
    REQUIRE(rig.e().load().errors.empty());
    rig.host.dataHold = true;   // the writer has accepted the writes but the database has not answered
    rig.ok("put(1)");
    rig.e().flushData(0, true);
    CHECK(rig.host.dataWriteOrder.size() == 1);
    CHECK(rig.e().dataPending() == 1);      // queued is not done
    rig.e().flushData(0, true);
    CHECK(rig.host.dataWriteOrder.size() == 1);   // one write in flight per key: not sent twice
    CHECK(rig.e().dataPending() == 1);

    // The key changes while its write is in flight: the confirmation of the old value does not clear it.
    rig.ok("put(2)");
    rig.host.finishDataWrites();
    rig.e().flushData(0, true);
    CHECK(rig.host.storedData[{"w.lua", "k"}] == "1");
    CHECK(rig.e().dataPending() == 1);   // the newer value is still to be written (and has just been sent)
    rig.host.finishDataWrites();
    rig.e().flushData(0, true);
    CHECK(rig.host.storedData[{"w.lua", "k"}] == "2");
    CHECK(rig.e().dataPending() == 0);

    // A write the database refuses stays dirty and is sent again; each failure is logged.
    rig.host.dataHold = false;
    rig.host.dataStoreFailures = 1;
    rig.ok("put(3)");
    rig.e().flushData(0, true);
    CHECK(rig.e().dataPending() == 1);
    rig.e().flushData(0, true);   // the failure is collected, the key is sent again and confirmed
    rig.e().flushData(0, true);
    CHECK(rig.e().dataPending() == 0);
    CHECK(rig.host.storedData[{"w.lua", "k"}] == "3");
    CHECK(rig.host.logContains("ForeverData writes failed (write refused)"));

    // A full host queue refuses the write: the key stays dirty.
    rig.host.dataQueueFull = true;
    rig.ok("put(4)");
    rig.e().flushData(0, true);
    CHECK(rig.e().dataPending() == 1);
    rig.host.dataQueueFull = false;
    rig.e().flushData(0, true);
    rig.e().flushData(0, true);
    CHECK(rig.e().dataPending() == 0);
}

TEST_CASE("review: shutdown writes every changed key, past 512 and past the host queue, then closes")
{
    ElunaRig rig;
    rig.dir.write("many.lua", "for i = 1, 1500 do ForeverData.set('key' .. i, i) end\n");
    REQUIRE(rig.e().load().errors.empty());
    rig.host.dataHold = true;          // the database answers only when polled ...
    rig.host.dataQueueLimit = 64;      // ... and the writer holds 64 writes at a time
    rig.host.dataCompleteOnPoll = true;
    CHECK(rig.e().dataPending() == 1500);
    rig.e().shutdown();
    CHECK(rig.host.storedData.size() == 1500);
    CHECK(rig.e().dataPending() == 0);
    CHECK_FALSE(rig.host.logContains("were not written to the database at shutdown"));
}

TEST_CASE("review: a shutdown flush that cannot finish is bounded and logs what was not written")
{
    ElunaRig rig([](fl::Config& c) { c.dataShutdownMs = 80; });
    rig.dir.write("stuck.lua", "ForeverData.set('alpha', 1) ForeverData.set('beta', 2)\n");
    REQUIRE(rig.e().load().errors.empty());
    rig.host.dataHold = true;   // the database never answers
    auto const t0 = std::chrono::steady_clock::now();
    rig.e().shutdown();
    auto const took = std::chrono::steady_clock::now() - t0;
    CHECK(took < std::chrono::seconds(3));
    CHECK(rig.host.logContains("2 ForeverData changes were not written to the database at shutdown"));
    CHECK(rig.host.logContains("stuck.lua:alpha"));
    CHECK_FALSE(rig.e().running());
}

TEST_CASE("review: writes made by LUA_STATE_CLOSE handlers during a reload are kept")
{
    ElunaRig rig;
    rig.dir.write("close.lua", "RegisterServerEvent(16, function() ForeverData.set('closed_at', 'close-handler') end)\n"
                               "function get(k) return ForeverData.get(k) end\n");
    REQUIRE(rig.e().load().errors.empty());
    fl::LoadReport rep = rig.e().reload([&] { rig.e().emitEluna(fl::EluKind::Server, 0, 16, {}); });
    REQUIRE(rep.applied);
    // The new state sees what the old state's close handler wrote, and it is the new state that writes it.
    CHECK(rig.ok("return tostring(get('closed_at'))") == "close-handler");
    rig.e().flushData(0, true);
    CHECK(rig.host.storedData[{"close.lua", "closed_at"}].find("close-handler") != std::string::npos);
}

TEST_CASE("review: a failed reload's candidate writes nothing to the database and the running state is unchanged")
{
    ElunaRig rig;
    rig.dir.write("keep.lua", "ForeverData.set('keep', 1)\n");
    REQUIRE(rig.e().load().errors.empty());
    rig.e().flushData(0, true);
    rig.dir.write("zbad.lua", "ForeverData.set('from_failed_load', 1)\nerror('load failed')\n");
    fl::LoadReport rep = rig.e().reload();
    CHECK_FALSE(rep.applied);
    rig.e().flushData(0, true);
    rig.e().shutdown();
    for (auto const& w : rig.host.dataWriteOrder)
        CHECK(w.find("from_failed_load") == std::string::npos);
    CHECK(rig.host.storedData.count({"zbad.lua", "from_failed_load"}) == 0);
    CHECK(rig.host.storedData.count({"keep.lua", "keep"}) == 1);
}

TEST_CASE("review: a reload does not read the whole table again")
{
    ElunaRig rig;
    rig.dir.write("r.lua", "ForeverData.set('x', 1)\n");
    REQUIRE(rig.e().load().errors.empty());
    int const loads = rig.host.dataLoadCalls;
    CHECK(loads == 1);
    REQUIRE(rig.e().reload().applied);
    REQUIRE(rig.e().reload().applied);
    CHECK(rig.host.dataLoadCalls == loads);
}

TEST_CASE("review: the ForeverData namespace is the loader's file, whatever frame or chunk name calls it")
{
    ElunaRig rig;
    std::string deep(100, 'd');
    rig.dir.write("ns.lua",
                  "function via_pcall() pcall(ForeverData.set, 'k', 1) return tostring(ForeverData.get('k')) end\n"
                  "function via_coroutine() local co = coroutine.wrap(function() ForeverData.set('c', 2) end) co() "
                  "return tostring(ForeverData.get('c')) end\n"
                  "function forged()\n"
                  "  for i = 1, 40 do load(\"ForeverData.set('f\" .. i .. \"', 1)\", '=forged' .. i)() end\n"
                  "  load(\"ForeverData.set('impostor', 1)\", '@ns.lua')()\n"
                  "end\n");
    rig.dir.write(deep + "/" + deep + "/" + deep + "/long.lua",
                  "function via_long() ForeverData.set('long', 3) return tostring(ForeverData.get('long')) end\n");
    REQUIRE(rig.e().load().errors.empty());
    CHECK(rig.ok("return via_pcall()") == "1");        // used to land in the shared "chunk" namespace and read back nil
    CHECK(rig.ok("return via_coroutine()") == "2");
    CHECK(rig.ok("return via_long()") == "3");          // the path is over 255 bytes: the namespace is shortened and hashed
    rig.ok("forged()");
    rig.e().flushData(0, true);
    size_t forged = 0;
    for (auto const& s : rig.host.storedData)
    {
        CHECK(s.first.first.size() <= 255);
        // Nothing lands in a namespace a script picked: only the loader's files and the shared "chunk".
        CHECK((s.first.first == "ns.lua" || s.first.first == "chunk" || s.first.first.find("#") != std::string::npos));
        if (s.first.first == "chunk")
            ++forged;
    }
    CHECK(forged == 41);   // 40 chosen names + the impostor of a loaded file all went to "chunk"
    CHECK(rig.host.storedData.count({"ns.lua", "impostor"}) == 0);
    CHECK(rig.e().dataKeys("ns.lua").size() == 2);   // k and c
}

TEST_CASE("review: keys count against the script's 1 MB, so 255-byte keys cannot add up to hundreds of MB")
{
    ElunaRig rig;
    rig.dir.write("flood.lua", "function flood()\n"
                               "  local n = 0\n"
                               "  for i = 1, 20000 do\n"
                               "    local ok = ForeverData.set(string.rep('k', 250) .. string.format('%05d', i), 1)\n"
                               "    if not ok then return n end\n"
                               "    n = n + 1\n"
                               "  end\n"
                               "  return n\n"
                               "end\n");
    REQUIRE(rig.e().load().errors.empty());
    int64_t const stored = std::stoll(rig.ok("return flood()"));
    CHECK(stored < 4200);    // 1 MB / (255 + 1) bytes per entry
    CHECK(stored > 4000);
    CHECK(rig.e().dataKeys("flood.lua").size() == static_cast<size_t>(stored));
}
