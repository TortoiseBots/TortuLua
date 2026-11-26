// The E4/E5 review round (independent code reviews of the per-map states and the E5 long tail): one
// case per finding, on the fake world with real threads for the map updates. Every case failed before its fix (or, where
// the old call path no longer exists, the model shows the old behaviour is caught: see docs/E4-E5-NOTES.md).
#include "Support.h"

#include "EluRouter.h"
#include "Settings.h"
#include "engine/Elu.h"
#include "engine/StateSet.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace fltest;

namespace
{
fl::Config reviewConfig(std::string const& path)
{
    fl::Config c = testConfig(path);
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    c.slowHandlerMs = 0;
    c.dataFlushMs = 5000;
    c.mapStates = 1;
    return c;
}

struct ReviewRig
{
    FakeHost host;
    TempDir dir;
    fl::Config cfg;
    std::unique_ptr<fl::StateSet> states;

    explicit ReviewRig(std::function<void(fl::Config&)> tweak = nullptr)
    {
        cfg = reviewConfig(dir.path());
        if (tweak)
            tweak(cfg);
        states = std::make_unique<fl::StateSet>(host, cfg);
        REQUIRE(states->start());
    }
    fl::Engine& world() { return *states->world(); }
    fl::Engine* map(uint32_t mapId, uint32_t instance = 0) { return states->mapState(mapId, instance); }
    void load()
    {
        fl::LoadReport const rep = states->load();
        INFO("load errors: " << (rep.errors.empty() ? "" : rep.errors.front()));
        REQUIRE(rep.errors.empty());
    }
    size_t logCount(std::string const& needle) const
    {
        size_t n = 0;
        for (auto const& l : host.logs)
            n += l.second.find(needle) != std::string::npos ? 1 : 0;
        return n;
    }
    bool logHas(std::string const& needle) const { return host.logContains(needle); }
};

// One map thread of the core's pool: every job runs on the same OS thread, in order, and run() waits for it. (Catch2's
// REQUIRE is not thread-safe: jobs return their facts and the test checks them on the test thread.)
class MapThread
{
public:
    MapThread() : _thread([this] { loop(); }) { }
    ~MapThread()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stop = true;
        }
        _cv.notify_all();
        _thread.join();
    }
    template <typename F>
    auto run(F&& f) -> decltype(f())
    {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> result = task->get_future();
        post([task] { (*task)(); });
        return result.get();
    }
    void post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _jobs.push_back(std::move(job));
        }
        _cv.notify_all();
    }

private:
    void loop()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cv.wait(lock, [this] { return _stop || !_jobs.empty(); });
                if (_jobs.empty())
                    return;
                job = std::move(_jobs.front());
                _jobs.pop_front();
            }
            job();
        }
    }
    std::mutex _mutex;
    std::condition_variable _cv;
    std::deque<std::function<void()>> _jobs;
    bool _stop = false;
    std::thread _thread;
};

fl::DbRows oneValue(int64_t v)
{
    fl::DbRows rows;
    rows.columns = {"v"};
    rows.rows.push_back({fl::DbValue(v)});
    return rows;
}

uint64_t asyncIdOf(FakeHost& host, std::string const& sql)
{
    for (auto const& r : host.asyncRequests)
        if (r.sql == sql)
            return r.id;
    return 0;
}
}  // namespace

// H1: the host's database structures (schema loads, select callbacks, finished results, ForeverData write results) have
// one owner, the world thread. Before the fix every map state's tick called pollDb()/takeRawQueryResult() on its map
// thread (two map threads erased the same QueryCallback on the real host).
TEST_CASE("review e4 H1: only the world thread polls the host database, map ticks never do")
{
    ReviewRig rig;
    rig.dir.write("idle.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    MapThread t1, t2;
    REQUIRE(t1.run([&] { return rig.states->openMap(1, 0) != nullptr; }));
    REQUIRE(t2.run([&] { return rig.states->openMap(2, 0) != nullptr; }));
    for (int i = 0; i < 50; ++i)
    {
        t1.post([&] { rig.states->tickMap(1, 0); });
        t2.post([&] { rig.states->tickMap(2, 0); });
        rig.world().tick();
    }
    t1.run([] { return 0; });
    t2.run([] { return 0; });
    INFO("first off-world op: " << (rig.host.offWorldOps.empty() ? "" : rig.host.offWorldOps.front()));
    CHECK(rig.host.offWorldPolls.load() == 0);
}

// H2: async results are routed by their process-wide id to the inbox of the state that asked. Before the fix the first
// state to tick popped the shared FIFO: the world state took (and dropped) the map state's answer.
TEST_CASE("review e4 H2: an async answer reaches the state that asked, whichever state ticks first")
{
    ReviewRig rig([](fl::Config& c) { c.dbTimeoutMs = 600000; });
    rig.dir.write("idle.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    MapThread t1;
    REQUIRE(t1.run([&] { return rig.states->openMap(1, 0) != nullptr; }));
    std::string const mapAsk =
        "got = 'none' WorldDBQueryAsync('SELECT map', function(q) got = q and q:GetUInt32(0) or 'nil' end)";
    REQUIRE(t1.run([&] { return rig.map(1)->runChunk(mapAsk).ok; }));
    REQUIRE(rig.world().runChunk("got = 'none' WorldDBQueryAsync('SELECT world', function(q) "
                                 "got = q and q:GetUInt32(0) or 'nil' end)").ok);
    uint64_t const mapId = asyncIdOf(rig.host, "SELECT map");
    uint64_t const worldId = asyncIdOf(rig.host, "SELECT world");
    REQUIRE(mapId != 0);
    REQUIRE(worldId != 0);
    REQUIRE(mapId != worldId);   // one process-wide id counter (E2/E3)
    // The map state's answer is first in the host's queue and the world ticks first.
    rig.host.completeAsync(mapId, oneValue(11));
    rig.host.completeAsync(worldId, oneValue(22));
    rig.world().tick();
    t1.run([&] { return rig.states->tickMap(1, 0); });
    CHECK(rig.world().runChunk("return got").result == "22");
    CHECK(t1.run([&] { return rig.map(1)->runChunk("return got").result; }) == "11");
    CHECK(rig.host.offWorldPolls.load() == 0);
}

// H3: RegisterCommand (at load) and a reload in a map state only mark the command map changed when the world state's
// commands change; a map state never does (its commands are never served). Before: every map state's load marked it.
TEST_CASE("review e4 H3: a map state's RegisterCommand and reload never touch the core's command map")
{
    ReviewRig rig;
    rig.dir.write("cmd.lua", "RegisterCommand('luahello', 1, function() return 'hi' end)\n");
    rig.load();
    int const afterWorldLoad = rig.host.commandsChangedCalls.load();
    CHECK(afterWorldLoad == 1);
    MapThread t1;
    REQUIRE(t1.run([&] { return rig.states->openMap(1, 0) != nullptr; }));
    CHECK(rig.host.commandsChangedCalls.load() == afterWorldLoad);
    fl::LoadReport const rep = rig.states->reloadAll();
    REQUIRE(rep.applied);
    t1.run([&] { return rig.states->tickMap(1, 0); });   // the map state's own reload
    CHECK(rig.states->pendingMapReloads() == 0);
    CHECK(rig.host.commandsChangedCalls.load() == afterWorldLoad);   // same names in the world: nothing to rebuild
    t1.run([&] { rig.states->closeMap(1, 0); return 0; });
    CHECK(rig.host.commandsChangedCalls.load() == afterWorldLoad);
}

// H4: a packet hook runs on any map thread. The copy is made, dispatched and freed under the world state's Lua lock
// (run this case under -fsanitize=thread: before the fix addPacketCopy() wrote the packet table outside the lock).
TEST_CASE("review e5 H4: packet events from several map threads at once, while the world uses packets")
{
    ReviewRig rig;
    rig.dir.write("packets.lua",
                  "seen = 0\n"
                  "RegisterPacketEvent(0x55, 7, function(e, p) seen = seen + p:GetSize() end)\n");
    rig.load();
    constexpr int kThreads = 4;
    constexpr int kEach = 250;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            while (!go.load())
                std::this_thread::yield();
            flh::MapScope scope{static_cast<uint32_t>(t + 1), 0};
            for (int i = 0; i < kEach; ++i)
                rig.world().emitPacketEvent(0x55, 7, {1, 2, 3}, fl::EventValue());
        });
    go.store(true);
    for (int i = 0; i < 200; ++i)
        REQUIRE(rig.world().runChunk("keep = keep or {} local p = CreatePacket(0x10, 8) p:WriteULong(7) "
                                     "keep[#keep + 1] = p return p:GetSize()").ok);
    for (auto& t : threads)
        t.join();
    CHECK(rig.world().runChunk("return seen").result == std::to_string(kThreads * kEach * 3));
    CHECK(rig.world().packetCount() == 200);   // the event copies were freed; only the script's packets are left
}

// M3: an event copy is not a script packet: it can neither push a script's packet out of the 1024-packet ring nor outlive
// its event. Before: every packet event added a copy to the ring, and after 1024 sends the template was gone.
TEST_CASE("review e5 M3: packet event copies never evict a script's packets")
{
    ReviewRig rig;
    rig.dir.write("template.lua",
                  "template = CreatePacket(0x20, 4) template:WriteULong(99)\n"
                  "kept = nil\n"
                  "RegisterPacketEvent(0x56, 7, function(e, p) kept = p end)\n");
    rig.load();
    for (int i = 0; i < 2000; ++i)
        rig.world().emitPacketEvent(0x56, 7, {1}, fl::EventValue());
    fl::CallResult r = rig.world().runChunk("return template:GetSize()");
    INFO("template: " << r.error);
    CHECK(r.ok);
    CHECK(r.result == "4");
    // A copy kept past its event is gone (read-only, event-scoped).
    fl::CallResult late = rig.world().runChunk("return kept:GetSize()");
    CHECK_FALSE(late.ok);
    CHECK(late.error.find("WorldPacket no longer exists") != std::string::npos);
}

// M3: packet bytes count against the state's MemoryLimitMB. Before: 1024 x 64 KB lived outside the cap.
TEST_CASE("review e5 M3: packet bytes count against the state's memory cap")
{
    ReviewRig rig([](fl::Config& c) { c.memoryCap = 2u * 1024u * 1024u; });
    rig.dir.write("idle.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    size_t const before = rig.world().memoryUsed();
    fl::CallResult r = rig.world().runChunk(
        "keep = {} for i = 1, 1000 do keep[i] = CreatePacket(1, 65536) end return #keep");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("not enough memory") != std::string::npos);
    CHECK(rig.world().memoryUsed() <= 2u * 1024u * 1024u);
    CHECK(rig.world().memoryUsed() > before + 1024u * 1024u);   // the packets that fit are counted
    // A collected packet gives its bytes back (its __gc), so the next packets fit again.
    size_t const full = rig.world().memoryUsed();
    REQUIRE(rig.world().runChunk("keep = nil for i = 1, 2000 do collectgarbage('step', 64) end").ok);
    CHECK(rig.world().memoryUsed() + 1024u * 1024u < full);
    fl::CallResult again = rig.world().runChunk("local p = CreatePacket(1, 65536) return p:GetSize()");
    INFO(again.error);
    CHECK(again.ok);
    // Writes are charged too: a packet cannot grow past the cap.
    fl::CallResult grow = rig.world().runChunk(
        "hold = {} for i = 1, 100 do local p = CreatePacket(1, 0) for j = 1, 16000 do p:WriteULong(j) end hold[i] = p end");
    CHECK_FALSE(grow.ok);
    CHECK(grow.error.find("not enough memory") != std::string::npos);
    CHECK(rig.world().memoryUsed() <= 2u * 1024u * 1024u);
}

// M4: CreatePacket(opcode, size) reserves `size` bytes, as the core's WorldPacket(opcode, res) does; it does not fill
// them. Before: CreatePacket(op, 4) + WriteULong sent 8 bytes (4 zeros first).
TEST_CASE("review e5 M4: CreatePacket's size is a reserve, the writes start at byte 0")
{
    ReviewRig rig;
    rig.dir.write("idle.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    fl::CallResult r = rig.world().runChunk(
        "local p = CreatePacket(0x30, 4) local before = p:GetSize() p:WriteULong(0x01020304) "
        "return before .. ':' .. p:GetSize() .. ':' .. p:ReadULong()");
    INFO(r.error);
    REQUIRE(r.ok);
    CHECK(r.result == "0:4:16909060");
}

// H5: OnCreate runs under the core's exclusive _mapsLock; any host call that finds a map would re-lock it (a
// self-deadlock). The map is only scheduled there; its first update (on its own thread, outside the lock) loads it.
TEST_CASE("review e4 H5: OnCreate only schedules; the first update loads the state, outside the map lock")
{
    ReviewRig rig;
    rig.dir.write("open.lua",
                  "if GetStateMapId() >= 0 then PrintInfo('top players ' .. #GetPlayersOnMap()) end\n"
                  "RegisterServerEvent(33, function() PrintInfo('open players ' .. #GetPlayersOnMap()) end)\n");
    rig.load();
    MapThread t1;
    // The modelled OnCreate: the core's lock is held on this thread.
    t1.run([&] {
        FakeHost::mapsLockHeld = true;
        rig.states->scheduleMap(1, 0);
        FakeHost::mapsLockHeld = false;
        return 0;
    });
    CHECK(rig.host.mapsRelocks.load() == 0);
    CHECK_FALSE(rig.logHas("top players"));
    CHECK(rig.map(1) == nullptr);   // nothing is open before the first update
    CHECK(t1.run([&] { return rig.states->beginMapUpdate(1, 0); }));   // MAP_INITIALIZE fires now (true once)
    CHECK_FALSE(t1.run([&] { return rig.states->beginMapUpdate(1, 0); }));
    CHECK(rig.logHas("top players 0"));
    CHECK(rig.logHas("open players 0"));
    CHECK(rig.host.mapsRelocks.load() == 0);

    // The model catches the old behaviour (the state loaded inside OnCreate, under the lock).
    t1.run([&] {
        FakeHost::mapsLockHeld = true;
        rig.states->openMap(2, 0);
        FakeHost::mapsLockHeld = false;
        return 0;
    });
    CHECK(rig.host.mapsRelocks.load() > 0);

    // MapStates = 0: the MAP_INITIALIZE of a scheduled map also waits for its first update, once.
    ReviewRig single([](fl::Config& c) { c.mapStates = 0; });
    single.load();
    single.states->scheduleMap(5, 0);
    CHECK(single.states->beginMapUpdate(5, 0));
    CHECK_FALSE(single.states->beginMapUpdate(5, 0));
}

// H5 / LOW (review: concurrent open runs the folder twice): only the first update after a schedule builds the state.
TEST_CASE("review e4 H5: two threads at a map's first update load its folder once")
{
    ReviewRig rig;
    rig.dir.write("once.lua", "if GetStateMapId() == 3 then PrintInfo('loaded map three') end\n");
    rig.load();
    rig.states->scheduleMap(3, 0);
    std::atomic<int> opened{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([&] {
            while (!go.load())
                std::this_thread::yield();
            if (rig.states->beginMapUpdate(3, 0))
                ++opened;
        });
    go.store(true);
    for (auto& t : threads)
        t.join();
    CHECK(opened.load() == 1);
    CHECK(rig.logCount("loaded map three") == 1);
}

// H6: the router routes by event kind. A player event from a map is the world state's (the docs table), whatever map
// the player is on; only the map-bound families go to a map state. The router is driven with real kinds through the real
// StateSet::route (before: the host returned the map state for any kind with a map key).
TEST_CASE("review e4 H6: player events from a map run in the world state, creature events in the map state")
{
    ReviewRig rig;
    rig.dir.write("route.lua",
                  "RegisterPlayerEvent(6, function() PrintInfo('kill state ' .. GetStateMapId()) end)\n"
                  "RegisterCreatureEvent(100, 1, function() PrintInfo('creature state ' .. GetStateMapId()) end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));
    flh::EluRouter router([&](fl::EluKind kind, std::optional<flh::MapKey> map) {
        std::optional<fl::StateSet::Key> key;
        if (map)
            key = fl::StateSet::Key{static_cast<int32_t>(map->map), map->instance};
        return rig.states->route(kind, key);
    });
    fl::HandleData const killer = FakeHost::player(7, 1);
    router.firePlayer(flh::MapKey{1, 0}, 6, {fl::EventValue(killer), fl::EventValue()});
    CHECK(rig.logHas("kill state -1"));
    CHECK_FALSE(rig.logHas("kill state 1"));
    fl::HandleData const npc = FakeHost::creature(10, 1, 0, 100);
    router.creature(flh::Subject{npc, 100}, 1, {fl::EventValue(npc), fl::EventValue()});
    CHECK(rig.logHas("creature state 1"));
    for (fl::EluKind kind : {fl::EluKind::Player, fl::EluKind::Server, fl::EluKind::PlayerGossip, fl::EluKind::Group,
                             fl::EluKind::Guild, fl::EluKind::Packet})
        CHECK(rig.states->route(kind, fl::StateSet::Key{1, 0}).get() == &rig.world());
    for (fl::EluKind kind : {fl::EluKind::Creature, fl::EluKind::GameObject, fl::EluKind::Item, fl::EluKind::CreatureGossip,
                             fl::EluKind::GameObjectGossip, fl::EluKind::ItemGossip, fl::EluKind::Map,
                             fl::EluKind::Instance, fl::EluKind::BattleGround, fl::EluKind::Spell})
        CHECK(rig.states->route(kind, fl::StateSet::Key{1, 0}).get() == rig.map(1));
}

// H7: DestroyMap calls OnDestroy and only then UnloadAll, which fires creature events. A closing map never reopens:
// those events are dropped with a log line; the next OnCreate of the same instance id gets a fresh state.
TEST_CASE("review e4 H7: events during a map's destroy are dropped, a reused instance id gets a fresh state")
{
    ReviewRig rig;
    rig.dir.write("life.lua",
                  "if GetStateMapId() == 1 then PrintInfo('loaded map one') end\n"
                  "RegisterCreatureEvent(100, 4, function() PrintInfo('death in ' .. GetStateMapId()) end)\n"
                  "RegisterServerEvent(16, function() PrintInfo('closed ' .. GetStateMapId()) end)\n");
    rig.load();
    MapThread t1;
    REQUIRE(t1.run([&] { return rig.states->openMap(1, 7) != nullptr; }));
    REQUIRE(t1.run([&] { return rig.map(1, 7)->runChunk("life = 'first'").ok; }));
    rig.states->closeMap(1, 7);   // OnDestroy (world thread)
    CHECK(rig.logHas("closed 1"));
    // UnloadAll: a summon despawns, its creature event is routed to the closing map.
    fl::HandleData const add = FakeHost::creature(20, 1, 7, 100);
    rig.states->emit(fl::EluKind::Creature, 100, 4, {fl::EventValue(add), fl::EventValue()}, add);
    CHECK_FALSE(rig.logHas("death in"));
    CHECK(rig.logHas("is closing"));
    CHECK(rig.states->droppedEvents() == 1);
    CHECK(rig.states->mapCount() == 0);
    CHECK(rig.logCount("loaded map one") == 1);   // nothing reopened it
    // FreeInstanceId hands the id to the next run: a fresh state, not the old run's globals.
    rig.states->scheduleMap(1, 7);
    REQUIRE(t1.run([&] { return rig.states->beginMapUpdate(1, 7); }));
    CHECK(t1.run([&] { return rig.map(1, 7)->runChunk("return tostring(life)").result; }) == "nil");
    CHECK(rig.logCount("loaded map one") == 2);
    // A second OnCreate of a key whose state was never closed retires the old state (LUA_STATE_CLOSE) first.
    size_t const closesBefore = rig.logCount("closed 1");
    rig.states->scheduleMap(1, 7);
    REQUIRE(t1.run([&] { return rig.states->beginMapUpdate(1, 7); }));
    CHECK(rig.logCount("closed 1") == closesBefore + 1);
    t1.run([&] { rig.states->closeMap(1, 7); return 0; });
}

// LOW / critical: a router holds the state it routed to; a concurrent close cannot free it under the caller.
TEST_CASE("review e4: a routed state stays valid while a close runs, and answers nothing once closed")
{
    ReviewRig rig;
    rig.dir.write("pin.lua", "RegisterCreatureEvent(100, 1, function() PrintInfo('ran') end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));
    std::shared_ptr<fl::Engine> pinned = rig.states->route(fl::EluKind::Creature, fl::StateSet::Key{1, 0});
    REQUIRE(pinned);
    std::thread closer([&] { rig.states->closeMap(1, 0); });
    closer.join();
    fl::HandleData const npc = FakeHost::creature(10, 1, 0, 100);
    fl::EventResult r = pinned->emitEluna(fl::EluKind::Creature, 100, 1, {fl::EventValue(npc), fl::EventValue()});
    CHECK(r.calls == 0);
    CHECK_FALSE(pinned->listeningEluna(fl::EluKind::Creature, 100, 1));
}

// M1: the cheap gate of a map callback asks the map's own state. Before: it asked the world state's listener index, so a
// map-only RegisterMapEvent (the point of map states) never fired.
TEST_CASE("review e4 M1: map callbacks are gated by the map's own state")
{
    ReviewRig rig;
    rig.dir.write("maponly.lua",
                  "if GetStateMapId() == 1 then RegisterMapEvent(1, 3, function() end) end\n"
                  "if GetStateMapId() == -1 then RegisterMapEvent(2, 3, function() end) end\n"
                  "if GetStateMapId() == -1 then RegisterServerEvent(23, function() end) end\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));
    REQUIRE(rig.states->openMap(2, 0));
    flh::EluRouter router([&](fl::EluKind kind, std::optional<flh::MapKey> map) {
        std::optional<fl::StateSet::Key> key;
        if (map)
            key = fl::StateSet::Key{static_cast<int32_t>(map->map), map->instance};
        return rig.states->route(kind, key);
    });
    CHECK(router.listeningMap(1, 0, false, 3, 0));         // registered in map 1's state only
    CHECK_FALSE(router.listeningMap(2, 0, false, 3, 0));   // registered in the world state only: never fires for map 2
    CHECK(router.listeningMap(2, 0, false, 3, 23));        // the server event lives in the world state
}

// M2: ForeverData has one writer (the world state) and per-key versions: a write confirmed after the value changed again
// does not clean the key, so a stale value never wins in the database. Map states write the world's store under its lock.
TEST_CASE("review e4 M2: ForeverData from several map threads: one writer, and the latest value reaches the database")
{
    ReviewRig rig([](fl::Config& c) { c.dataFlushMs = 1; });
    rig.dir.write("data.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    MapThread t1, t2;
    REQUIRE(t1.run([&] { return rig.states->openMap(1, 0) != nullptr; }));
    REQUIRE(t2.run([&] { return rig.states->openMap(2, 0) != nullptr; }));

    // The stale-write case: v1 is in flight, a map state sets v2, then v1 is confirmed.
    rig.host.dataHold = true;
    REQUIRE(rig.world().runChunk("ForeverData.set('k', 'v1')").ok);
    rig.world().flushData(rig.host.nowMs(), true);
    REQUIRE(rig.host.dataPending.size() == 1);
    REQUIRE(t1.run([&] { return rig.map(1)->runChunk("ForeverData.set('k', 'v2')").ok; }));
    rig.host.finishDataWrites();   // the database confirms v1
    rig.host.dataHold = false;
    rig.world().flushData(rig.host.nowMs(), true);   // v1's confirmation does not clean k: v2 is written
    rig.world().flushData(rig.host.nowMs(), true);
    auto const k = rig.host.storedData.find({"chunk", "k"});
    bool const found = k != rig.host.storedData.end();
    REQUIRE(found);
    CHECK(k->second == "\"v2\"");
    CHECK(rig.world().dataPending() == 0);

    // Stress: two map threads and the world write at once while the world flushes; the database ends up equal to memory.
    auto writer = [&](fl::Engine* e, char const* prefix) {
        for (int i = 0; i < 200; ++i)
            e->runChunk(std::string("ForeverData.set('") + prefix + std::to_string(i % 20) + "', " + std::to_string(i) +
                        ")");
    };
    t1.post([&] { writer(rig.map(1), "a"); });
    t2.post([&] { writer(rig.map(2), "b"); });
    for (int i = 0; i < 200; ++i)
    {
        writer(&rig.world(), "w");
        rig.world().tick();
    }
    t1.run([] { return 0; });
    t2.run([] { return 0; });
    for (int i = 0; i < 5; ++i)
        rig.world().flushData(rig.host.nowMs(), true);
    CHECK(rig.world().dataPending() == 0);
    for (char const* prefix : {"a", "b", "w"})
        for (int i = 0; i < 20; ++i)
        {
            std::string const key = prefix + std::to_string(i);
            auto it = rig.host.storedData.find({"chunk", key});
            REQUIRE(it != rig.host.storedData.end());
            CHECK(it->second == std::to_string(180 + i));
        }
    CHECK(rig.host.offWorldPolls.load() == 0);   // no map thread ever wrote or polled
}

// M5: Mode = test with MapStates = 1 is refused (test mode has no Lua lock on map threads and loads its folder per state).
TEST_CASE("review e4 M5: MapStates = 1 needs eluna mode")
{
    MapConfig conf;
    conf.values = {{"ForeverLua.Mode", "test"}, {"ForeverLua.MapStates", "1"}};
    flh::ModuleSettings const s = flh::settingsFrom(conf);
    CHECK(s.engine.mapStates == 0);
    CHECK(s.mapStatesRefused);
    conf.values["ForeverLua.Mode"] = "eluna";
    flh::ModuleSettings const e = flh::settingsFrom(conf);
    CHECK(e.engine.mapStates == 1);
    CHECK_FALSE(e.mapStatesRefused);
}

// LOW: a map's reload fires LUA_STATE_CLOSE only right before the adopt (a reload with an error keeps the state as it
// was, close handlers included); a failed world reload schedules no map reload (no version skew).
TEST_CASE("review e4: per-map reload closes only on success; a failed world reload leaves the maps alone")
{
    ReviewRig rig;
    rig.dir.write("v.lua", "RegisterServerEvent(16, function() PrintInfo('close ' .. GetStateMapId()) end)\n");
    rig.load();
    MapThread t1;
    REQUIRE(t1.run([&] { return rig.states->openMap(1, 0) != nullptr; }));
    rig.dir.write("v.lua", "this is not lua\n");
    rig.states->requestMapReload(1, 0);
    t1.run([&] { return rig.states->tickMap(1, 0); });
    CHECK_FALSE(rig.logHas("close 1"));
    CHECK(rig.map(1) != nullptr);

    fl::LoadReport const bad = rig.states->reloadAll();
    CHECK_FALSE(bad.applied);
    CHECK(rig.states->pendingMapReloads() == 0);
    CHECK_FALSE(rig.logHas("close -1"));
    t1.run([&] { rig.states->closeMap(1, 0); return 0; });
}

// Final check (H5, the battleground half): MapManager::CreateMap holds the core's exclusive map lock while it creates a
// battleground (OnInit = BG CREATE) and loads its grids (every forever_lua game object's SPAWN, every creature's RESET).
// Those events are queued (CreationQueue). Object updates of the BG's first Map::Update come before the map hook: they
// are appended to the queue, and the hook (after the state exists) replays everything in order, outside the lock, in the
// BG map's own state. Before: the events ran at once in the world state and a handle call re-took the lock; then (first
// queue version) an object update replayed the queue before the state existed and every event was dropped.
TEST_CASE("review final H5: map creation events wait for the map's own first-update hook, in order")
{
    flh::creationQueue().clear();
    for (uint32_t mapStates : {1u, 0u})
    {
        INFO("MapStates = " << mapStates);
        ReviewRig rig([mapStates](fl::Config& c) { c.mapStates = mapStates; });
        rig.dir.write("bg.lua",
                      "order = ''\n"
                      "RegisterBGEvent(3, function(e, bg) order = order .. 'C' "
                      "PrintInfo('bg create in ' .. GetStateMapId() .. ' ' .. bg:GetName()) end)\n"
                      "RegisterGameObjectEvent(200, 2, function(e, go) order = order .. 'S' "
                      "PrintInfo('spawn in ' .. GetStateMapId() .. ' ' .. go:GetName()) end)\n"
                      "RegisterGameObjectEvent(200, 1, function(e, go) order = order .. 'U' "
                      "PrintInfo('update in ' .. GetStateMapId()) end)\n");
        rig.load();
        rig.host.onGameCall = [](fl::HandleData const&, std::string const&, std::vector<fl::EventValue> const&,
                                 std::vector<fl::EventValue>& out) {
            out.emplace_back(std::string("Name"));
            return true;
        };
        flh::EluRouter router([&](fl::EluKind kind, std::optional<flh::MapKey> map) {
            std::optional<fl::StateSet::Key> key;
            if (map)
                key = fl::StateSet::Key{static_cast<int32_t>(map->map), map->instance};
            return rig.states->route(kind, key);
        });
        flh::MapKey const key{30, 5};
        fl::HandleData bg;
        bg.kind = fl::ObjKind::BattleGround;
        bg.mapId = 30;
        bg.instanceId = 5;
        fl::HandleData const go = FakeHost::gameObject(40, 30, 5, 200);
        fl::ObjectInfo info;
        info.name = "Name";
        rig.host.add(go, info);
        rig.host.add(bg, info);
        MapThread creator;
        creator.run([&] {
            FakeHost::mapsLockHeld = true;   // CreateMap -> CreateBattleground -> OnInit, then LoadAllGrids
            router.mapObjectEvent(true, key, 1000, fl::EluKind::BattleGround, 0, 3, {fl::EventValue(bg)}, std::nullopt);
            router.mapObjectEvent(true, key, 1000, fl::EluKind::GameObject, 200, 2, {fl::EventValue(go)}, go);
            rig.states->scheduleMap(30, 5);   // OnCreateMap (the BG map script's OnCreate)
            FakeHost::mapsLockHeld = false;
            return 0;
        });
        CHECK(rig.host.mapsRelocks.load() == 0);
        MapThread mapThread;
        // The BG's first Map::Update: the object updates come first (before OnMapUpdate) and are queued behind.
        fl::EventResult early = mapThread.run([&] {
            return router.mapObjectEvent(false, key, 1001, fl::EluKind::GameObject, 200, 1, {fl::EventValue(go)}, go);
        });
        CHECK(early.calls == 0);
        CHECK(rig.states->awaitingFirstUpdate(30, 5));
        CHECK_FALSE(rig.logHas("bg create"));
        CHECK_FALSE(rig.logHas("update in"));
        CHECK(flh::creationQueue().size() == 3);
        // Then the map hook: the state is built, then the queue is replayed in it, in order.
        CHECK(mapThread.run([&] { return rig.states->beginMapUpdate(30, 5); }));
        CHECK(mapThread.run([&] { return router.replayCreation(key); }) == 3);
        std::string const state = mapStates ? "30" : "-1";
        CHECK(rig.logHas("bg create in " + state + " Name"));
        CHECK(rig.logHas("spawn in " + state + " Name"));
        CHECK(rig.logHas("update in " + state));
        fl::Engine* bgState = mapStates ? rig.map(30, 5) : &rig.world();
        REQUIRE(bgState != nullptr);
        CHECK(mapThread.run([&] { return bgState->runChunk("return order").result; }) == "CSU");
        CHECK(rig.host.mapsRelocks.load() == 0);
        CHECK(flh::creationQueue().size() == 0);
        // Once replayed, the map's events run at once again.
        mapThread.run([&] {
            return router.mapObjectEvent(false, key, 1002, fl::EluKind::GameObject, 200, 1, {fl::EventValue(go)}, go);
        });
        CHECK(mapThread.run([&] { return bgState->runChunk("return order").result; }) == "CSUU");

        // The model catches the old behaviour: CREATE dispatched at once, under the lock, re-locks.
        creator.run([&] {
            FakeHost::mapsLockHeld = true;
            router.fireIn(flh::MapKey{31, 0}, fl::EluKind::BattleGround, 0, 3, {fl::EventValue(bg)});
            FakeHost::mapsLockHeld = false;
            return 0;
        });
        CHECK(rig.host.mapsRelocks.load() > 0);
        flh::creationQueue().clear();
    }
}

// Final check: queues carry a generation from a process-wide counter (never a Map address). Once the map of a queue is
// registered (sealed by an append or by the world-tick sweep), a new creation of the same key is a new instance: the old
// queue is dropped and never replays into it.
TEST_CASE("review final: an old run's creation queue never replays into a new instance of the same id")
{
    flh::CreationQueue q;
    flh::MapKey const key{33, 9};
    auto ev = [](uint32_t event) {
        flh::DeferredEvent e;
        e.kind = fl::EluKind::GameObject;
        e.entry = 200;
        e.event = event;
        return e;
    };
    REQUIRE(q.deferCreation(key, ev(2), 100));
    uint64_t const first = q.generation(key);
    // The world tick sees the map registered (it got no player and no update, e.g. CanEnter failed), and it is destroyed.
    q.sweep(200, 60000, [](flh::MapKey) { return true; }, nullptr);
    // FreeInstanceId hands the id out again: the next creation of the key starts a new generation.
    REQUIRE(q.deferCreation(key, ev(5), 300));
    CHECK(q.generation(key) > first);
    std::vector<flh::DeferredEvent> got = q.take(key);
    REQUIRE(got.size() == 1);
    CHECK(got[0].event == 5);   // only the new instance's event
    CHECK(q.size() == 0);
    // An unsealed queue (the same creation still running) keeps collecting.
    REQUIRE(q.deferCreation(key, ev(2), 400));
    REQUIRE(q.deferCreation(key, ev(3), 401));
    CHECK(q.take(key).size() == 2);
}

// Final check: a queue whose map never gets its replay (no update, no player) is dropped by the world-tick sweep after the
// bound, logged once per map, so leaked queues never fill the process-wide cap.
TEST_CASE("review final: the sweep drops old creation queues, and leaked queues never fill the cap")
{
    flh::CreationQueue q;
    std::vector<flh::MapKey> logged;
    auto onDrop = [&](flh::MapKey k, size_t) { logged.push_back(k); };
    flh::DeferredEvent e;
    e.kind = fl::EluKind::GameObject;
    REQUIRE(q.deferCreation({40, 1}, e, 0));
    REQUIRE(q.deferCreation({40, 1}, e, 0));
    CHECK(q.sweep(59999, 60000, nullptr, onDrop) == 0);   // not old enough
    CHECK(q.sweep(60000, 60000, nullptr, onDrop) == 1);
    REQUIRE(logged.size() == 1);   // one line per map, not per event
    CHECK(logged[0] == flh::MapKey{40, 1});
    CHECK(q.size() == 0);

    // Leak more dungeon queues than the cap holds; the sweep frees them and a new map can queue again.
    size_t accepted = 0;
    for (uint32_t m = 0; m < 20; ++m)
        for (size_t i = 0; i < flh::CreationQueue::kMaxPerMap; ++i)
            accepted += q.deferCreation({100 + m, 1}, e, 1000) ? 1 : 0;
    CHECK(accepted == flh::CreationQueue::kMaxTotal);
    CHECK_FALSE(q.deferCreation({500, 1}, e, 1000));   // full
    q.sweep(1000 + 60000, 60000, nullptr, onDrop);
    CHECK(q.size() == 0);
    CHECK(q.deferCreation({500, 1}, e, 61001));
}
