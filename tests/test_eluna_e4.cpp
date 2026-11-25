// E4: per-map states. One Lua state per map instance plus the world state; map-bound events and timers in the map's
// state on its thread, player/server/group/guild events in the world state; handles never cross a state as a pointer;
// ForeverData is the only shared thing; every state has its own budgets, memory cap and timers; map create/destroy
// creates/closes the state; reload is per state.
#include "Support.h"

#include "engine/Elu.h"
#include "engine/StateSet.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace fltest;

namespace
{
fl::Config e4Config(std::string const& path)
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

struct E4Rig
{
    FakeHost host;
    TempDir dir;
    fl::Config cfg;
    std::unique_ptr<fl::StateSet> states;

    explicit E4Rig(std::function<void(fl::Config&)> tweak = nullptr)
    {
        cfg = e4Config(dir.path());
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
    bool logHas(std::string const& needle) const { return host.logContains(needle); }
};

fl::HandleData creatureOn(uint64_t id, uint32_t map, uint32_t instance = 0, uint32_t entry = 100)
{
    return FakeHost::creature(id, map, instance, entry);
}
}  // namespace

TEST_CASE("e4: map states load their own scripts, are created per map and closed with it")
{
    E4Rig rig;
    rig.dir.write("states.lua",
                  "RegisterServerEvent(33, function() PrintInfo('open state ' .. GetStateMapId()) end)\n"
                  "RegisterServerEvent(16, function() PrintInfo('close state ' .. GetStateMapId()) end)\n");
    rig.load();
    REQUIRE(rig.states->world()->stateMapId() == -1);
    rig.world().emitEluna(fl::EluKind::Server, 0, 33, {});   // what the host does for the world state
    REQUIRE(rig.logHas("open state -1"));

    size_t const before = rig.states->memoryInUse();
    fl::Engine* map = rig.states->openMap(1, 0);
    REQUIRE(map != nullptr);
    REQUIRE(map->stateMapId() == 1);
    REQUIRE(map->stateInstanceId() == 0);
    REQUIRE(rig.states->mapCount() == 1);
    REQUIRE(rig.logHas("open state 1"));
    REQUIRE(rig.states->memoryInUse() > before);

    // A second open of the same map answers the same state, it does not load a second copy.
    REQUIRE(rig.states->openMap(1, 0) == map);
    REQUIRE(rig.states->mapCount() == 1);

    rig.states->closeMap(1, 0);
    REQUIRE(rig.logHas("close state 1"));
    REQUIRE(rig.states->mapCount() == 0);
    REQUIRE(rig.states->mapState(1, 0) == nullptr);
    // The closed state gave its memory back: only the world state's heap is left.
    REQUIRE(rig.states->memoryInUse() <= before + 8192);
}

TEST_CASE("e4: a creature event runs in its map's state, player and server events in the world state")
{
    E4Rig rig;
    rig.dir.write("route.lua",
                  "RegisterCreatureEvent(100, 1, function() PrintInfo('creature state ' .. GetStateMapId()) end)\n"
                  "RegisterPlayerEvent(3, function() PrintInfo('player state ' .. GetStateMapId()) end)\n"
                  "RegisterServerEvent(13, function() PrintInfo('server state ' .. GetStateMapId()) end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));
    REQUIRE(rig.states->openMap(2, 0));

    fl::HandleData const c1 = creatureOn(10, 1);
    fl::HandleData const c2 = creatureOn(11, 2);
    rig.states->emit(fl::EluKind::Creature, 100, 1, {fl::EventValue(c1), fl::EventValue()}, c1);
    rig.states->emit(fl::EluKind::Creature, 100, 1, {fl::EventValue(c2), fl::EventValue()}, c2);
    REQUIRE(rig.logHas("creature state 1"));
    REQUIRE(rig.logHas("creature state 2"));

    fl::HandleData const p = FakeHost::player(7, 1);
    rig.states->emit(fl::EluKind::Player, 0, 3, {fl::EventValue(p)}, std::nullopt);
    REQUIRE(rig.logHas("player state -1"));
    rig.states->emit(fl::EluKind::Server, 0, 13, {fl::EventValue(int64_t(16))}, std::nullopt);
    REQUIRE(rig.logHas("server state -1"));
    // A map-bound event with no map state (or no map scope) falls back to the world state instead of being lost.
    fl::HandleData const c9 = creatureOn(12, 9);
    rig.states->emit(fl::EluKind::Creature, 100, 1, {fl::EventValue(c9), fl::EventValue()}, c9);
    REQUIRE(rig.logHas("creature state -1"));
}

TEST_CASE("e4: two map states run handlers at the same time (no shared Lua lock)")
{
    E4Rig rig;
    rig.dir.write("busy.lua",
                  "RegisterCreatureEvent(100, 1, function(e, c) c:GetHealth() end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));
    REQUIRE(rig.states->openMap(2, 0));

    std::mutex mutex;
    std::condition_variable cv;
    int arrived = 0;
    bool released = false;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const&,
                              std::vector<fl::EventValue>& out) {
        if (op != "unit.getHealth")
            return false;
        std::unique_lock<std::mutex> lock(mutex);
        ++arrived;
        cv.notify_all();
        // Both map threads must be inside their handler now: a shared lock would serialise them and time out here.
        bool const both = cv.wait_for(lock, std::chrono::seconds(3), [&] { return arrived == 2; });
        released = both;
        out.emplace_back(static_cast<int64_t>(both ? 1 : 0));
        return true;
    };

    fl::HandleData const c1 = creatureOn(10, 1);
    fl::HandleData const c2 = creatureOn(11, 2);
    rig.host.add(c1, fl::ObjectInfo{"A", 100, 0, 0, 0, 0, true});
    rig.host.add(c2, fl::ObjectInfo{"B", 100, 0, 0, 0, 0, true});
    std::thread a([&] {
        flh::MapScope scope{1, 0};
        rig.states->emit(fl::EluKind::Creature, 100, 1, {fl::EventValue(c1), fl::EventValue()}, c1);
    });
    std::thread b([&] {
        flh::MapScope scope{2, 0};
        rig.states->emit(fl::EluKind::Creature, 100, 1, {fl::EventValue(c2), fl::EventValue()}, c2);
    });
    a.join();
    b.join();
    CHECK(released);
}

TEST_CASE("e4: a handle of one map is refused in another map's state")
{
    E4Rig rig;
    rig.dir.write("cross.lua",
                  "RegisterCreatureEvent(100, 1, function()\n"
                  "  local p = GetPlayerByName('Away')\n"
                  "  PrintInfo('health ' .. p:GetHealth())\n"
                  "end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(2, 0));

    // The player is in the world, on map 1; the handler runs in map 2's state on map 2's thread.
    fl::HandleData const p = FakeHost::player(7, 1);
    fl::ObjectInfo info;
    info.name = "Away";
    info.alive = true;
    rig.host.add(p, info);
    rig.host.playersByName["Away"] = p;
    rig.host.players.push_back(p);
    rig.host.gameResults["unit.getHealth"] = {fl::EventValue(static_cast<int64_t>(100))};

    fl::HandleData const c = creatureOn(11, 2);
    {
        flh::MapScope scope{2, 0};
        rig.states->emit(fl::EluKind::Creature, 100, 1, {fl::EventValue(c), fl::EventValue()}, c);
    }
    REQUIRE(rig.logHas("object is on another map"));
    REQUIRE_FALSE(rig.logHas("health 100"));

    // The same call from the world state, on the world thread with the maps idle, works.
    fl::CallResult r = rig.world().runChunk("local p = GetPlayerByName('Away') return p:GetHealth()");
    INFO("world chunk error: " << r.error);
    REQUIRE(r.ok);
}

TEST_CASE("e4: each state has its own timers; the world tick does not run a map's timer")
{
    E4Rig rig;
    rig.dir.write("timers.lua",
                  "RegisterMapEvent(1, 1, function()\n"
                  "  CreateLuaEvent(function() PrintInfo('map timer') end, 0, 1)\n"
                  "end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));
    fl::HandleData const m = FakeHost::creature(0, 1);   // only the map id matters for the map family
    fl::HandleData mapHandle;
    mapHandle.kind = fl::ObjKind::Map;
    mapHandle.mapId = 1;
    rig.states->emit(fl::EluKind::Map, 1, 1, {fl::EventValue(), fl::EventValue(mapHandle)}, mapHandle);
    REQUIRE_FALSE(rig.logHas("map timer"));
    rig.world().tick();
    REQUIRE_FALSE(rig.logHas("map timer"));
    rig.states->tickMap(1, 0);
    REQUIRE(rig.logHas("map timer"));
}

TEST_CASE("e4: the states share only ForeverData")
{
    E4Rig rig;
    rig.dir.write("data.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));

    REQUIRE(rig.world().runChunk("ForeverData.set('k', 42)").ok);
    fl::CallResult r = rig.map(1)->runChunk("return ForeverData.get('k')");
    REQUIRE(r.ok);
    REQUIRE(r.result == "42");
    REQUIRE(rig.map(1)->runChunk("ForeverData.set('fromMap', 'yes')").ok);
    r = rig.world().runChunk("return ForeverData.get('fromMap')");
    REQUIRE(r.ok);
    REQUIRE(r.result == "yes");

    // Every other global is per state.
    REQUIRE(rig.world().runChunk("shared = 7").ok);
    r = rig.map(1)->runChunk("return tostring(shared)");
    REQUIRE(r.ok);
    REQUIRE(r.result == "nil");
}

TEST_CASE("e4: every state has its own memory cap and budget")
{
    E4Rig rig([](fl::Config& c) {
        c.memoryCap = 512u * 1024u;
        c.commandBudget = {50, 200000};
    });
    rig.dir.write("budget.lua", "RegisterServerEvent(33, function() end)\n");
    rig.load();
    REQUIRE(rig.states->openMap(1, 0));

    fl::CallResult big = rig.map(1)->runChunk("big = string.rep('x', 8 * 1024 * 1024); return #big");
    INFO("map chunk error: " << big.error);
    CHECK_FALSE(big.ok);
    // The allocator's refusal surfaces as Lua's memory error or as the string library's "too large" error, depending on
    // where Lua notices; both mean the cap held and nothing was allocated.
    CHECK((big.memory || big.error.find("too large") != std::string::npos));
    // The world state still runs with its own heap.
    fl::CallResult small = rig.world().runChunk("return #('ok')");
    REQUIRE(small.ok);
    REQUIRE(small.result == "2");

    // The map state's budget is the same copy of the config: a loop over it is cut, and the error says so.
    fl::CallResult loop = rig.map(1)->runChunk("while true do end", "=loop", true);
    CHECK_FALSE(loop.ok);
    CHECK(loop.budget);
}

TEST_CASE("e4: reload is per state (world now, each map at its own next update)")
{
    E4Rig rig;
    rig.dir.write("v.lua", "PrintInfo('version one ' .. GetStateMapId())\n");
    rig.load();
    REQUIRE(rig.logHas("version one -1"));
    REQUIRE(rig.states->openMap(1, 0));
    REQUIRE(rig.logHas("version one 1"));

    rig.dir.write("v.lua", "PrintInfo('version two ' .. GetStateMapId())\n");
    fl::LoadReport const rep = rig.states->reloadAll();
    INFO("reload errors: " << (rep.errors.empty() ? "" : rep.errors.front()));
    REQUIRE(rep.applied);
    REQUIRE(rig.logHas("version two -1"));
    REQUIRE_FALSE(rig.logHas("version two 1"));   // the map state waits for its own update
    REQUIRE(rig.states->pendingMapReloads() == 1);

    rig.states->tickMap(1, 0);
    REQUIRE(rig.logHas("version two 1"));
    REQUIRE(rig.states->pendingMapReloads() == 0);

    // A failing script keeps the map state as it was (the world is not applied either).
    rig.dir.write("v.lua", "this is not lua\n");
    fl::LoadReport const bad = rig.states->reloadAll();
    CHECK_FALSE(bad.applied);
    rig.states->tickMap(1, 0);   // the pending reload fails and logs, the map state keeps serving
    CHECK(rig.map(1) != nullptr);
}
