#include "Module.h"

#include "engine/Text.h"
#include "Commands.h"
#include "CreationQueue.h"
#include "EluRouter.h"
#include "HookQueue.h"
#include "GameAccess.h"

#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"

#include <cctype>
#include <chrono>
#include <sstream>
#include <string>

namespace flh
{
namespace
{
ModuleSettings g_settings;
bool g_settingsRead = false;
std::unique_ptr<ForeverHost> g_host;
std::unique_ptr<fl::StateSet> g_states;
std::atomic<fl::Engine*> g_live{nullptr};   // the world state (null until it exists / after shutdown)

// The core's config manager as the settings reader's source.
class CoreConfig final : public ConfigSource
{
public:
    bool getBool(std::string const& key, bool def) const override { return sConfigMgr->GetBoolDefault(key, def); }
    std::string getString(std::string const& key, std::string const& def) const override
    {
        return sConfigMgr->GetStringDefault(key, def);
    }
    int64_t getInt(std::string const& key, int64_t def) const override
    {
        return sConfigMgr->GetIntDefault(key, static_cast<int32>(def));
    }
};
}  // namespace

ModuleSettings readSettings()
{
    return settingsFrom(CoreConfig());
}

ModuleSettings const& settings()
{
    if (!g_settingsRead)
    {
        g_settings = readSettings();
        g_settingsRead = true;
    }
    return g_settings;
}

fl::Engine* engine() { return g_live.load(std::memory_order_acquire); }

std::shared_ptr<fl::Engine> eventEngine(fl::EluKind kind, std::optional<MapKey> map)
{
    // E4 review: the decision is StateSet::route (unit-tested): map-bound families go to their map's open state, every
    // other family to the world state (H6); a map that is loading or closing drops the event with a log line, and no
    // event ever opens a state (H7: only OnCreate/startup schedule one, the map's first update builds it).
    if (!g_states || !engine())
        return nullptr;
    std::optional<fl::StateSet::Key> key;
    if (map)
        key = fl::StateSet::Key{static_cast<int32_t>(map->map), map->instance};
    return g_states->route(kind, key);
}

void startEngine()
{
    if (g_states)
        return;
    g_settings = readSettings();
    g_settingsRead = true;
    if (!g_settings.enabled)
        return;
    if (g_settings.mapStatesRefused)
        TC_LOG_WARN("forever.lua", "ForeverLua: MapStates = 1 needs Mode = eluna; test mode runs with one state");
    flh::setWorldThread();
    flh::setMapsIdle(true);
    g_host = std::make_unique<ForeverHost>(g_settings.bridgeSecurity, g_settings.engine.syncDbTimeoutMs);
    // The core's command table is built by now (before the first world tick) and no Lua command exists yet: its roots are
    // the names RegisterCommand must not take.
    g_host->setCoreCommandRoots(flh::coreCommandRoots());
    g_states = std::make_unique<fl::StateSet>(*g_host, g_settings.engine);
    g_states->setBinder(&bindGame);
    if (!g_states->start())
    {
        TC_LOG_ERROR("forever.lua", "ForeverLua: the Lua state could not be created");
        g_states.reset();
        g_host.reset();
        return;
    }
    fl::Engine* world = g_states->world();
    g_live.store(world, std::memory_order_release);
    fl::LoadReport rep = g_states->load();
    for (auto const& line : fl::describeLoad(rep))
        TC_LOG_INFO("forever.lua", "{}", line);
    if (g_settings.engine.mode == fl::EngineMode::Eluna && g_settings.engine.mapStates != 0)
    {
        // E4: maps that already exist (the prewarmed continents) are scheduled now; the maps created later are
        // scheduled in their OnCreate.
        // H5: DoForAllMaps holds the core's map lock: only collect the keys there, schedule them after it returned. Each
        // map builds its state at its next update, on its own thread (and fires MAP_INITIALIZE then).
        std::vector<MapKey> existing;
        sMapMgr->DoForAllMaps([&existing](Map* map)
        {
            if (map)
                existing.push_back(MapKey{map->GetId(), map->GetInstanceId()});
        });
        for (MapKey const& k : existing)
            flh::scheduleMapState(k.map, k.instance);
    }
    if (g_settings.engine.mode == fl::EngineMode::Eluna)
    {
        // The state starts after the core's OnStartup, so STARTUP is fired here (Eluna's server event 14).
        fl::EventResult ignored = world->emitEluna(fl::EluKind::Server, 0, 14, {});
        (void)ignored;
    }
    fl::QueuedEvent ev;
    ev.name = "server.start";
    ev.args.emplace_back(false);  // arg 1: true after a .lua reload
    world->emit(std::move(ev));
}

void stopEngine()
{
    if (!g_states)
        return;
    fl::Engine* world = g_live.load(std::memory_order_acquire);
    pumpBots();
    if (world && g_settings.engine.mode == fl::EngineMode::Eluna)
    {
        fl::EventResult ignored = world->emitEluna(fl::EluKind::Server, 0, 15, {});   // SHUTDOWN
        (void)ignored;
    }
    if (world)
    {
        fl::QueuedEvent ev;
        ev.name = "server.stop";
        ev.longBudget = true;
        world->emit(std::move(ev));
        world->finishPending();   // server.stop handlers that did not fit one tick still run before the close
    }
    g_live.store(nullptr, std::memory_order_release);
    g_states->shutdown();   // every map state (LUA_STATE_CLOSE) and then the world state
    g_states.reset();
    deferred().drain();   // queued spawns/commands still hold the host: run them before it goes
    g_host.reset();
}

bool takeReloadRequest()
{
    return g_host && g_host->takeReloadRequest();
}

void applyCommandMapChange()
{
    if (g_host)
        g_host->applyCommandMapChange();
}

void pumpBots()
{
    if (fl::Engine* world = engine())
        world->pumpBots();
}

fl::LoadReport reloadEngine()
{
    if (!g_states)
    {
        fl::LoadReport r;
        r.errors.push_back("ForeverLua is off");
        return r;
    }
    // LUA_STATE_CLOSE reaches the old world state only when the new one loaded cleanly and is about to replace it: a
    // reload with a script error keeps the old state exactly as it was, close handlers included. Every map state gets
    // the same request and reloads at its own next update, on its own map's thread.
    fl::LoadReport rep = g_states->reloadAll();
    if (rep.applied)   // a reload with an error keeps the old state and does not fire server.start
    {
        fl::Engine* world = g_live.load(std::memory_order_acquire);
        fl::QueuedEvent ev;
        ev.name = "server.start";
        ev.args.emplace_back(true);
        if (world)
            world->emit(std::move(ev));
    }
    return rep;
}

void scheduleMapState(uint32_t mapId, uint32_t instanceId)
{
    if (g_states)
        g_states->scheduleMap(static_cast<int32_t>(mapId), instanceId);
}

bool beginMapUpdate(uint32_t mapId, uint32_t instanceId)
{
    MapScope scope{mapId, instanceId};   // the new state's top-level code and LUA_STATE_OPEN may touch this map's objects
    return g_states && g_states->beginMapUpdate(static_cast<int32_t>(mapId), instanceId);
}

bool awaitingFirstUpdate(uint32_t mapId, uint32_t instanceId)
{
    return g_states && g_states->awaitingFirstUpdate(static_cast<int32_t>(mapId), instanceId);
}

bool mapOwnsState(uint32_t mapId, uint32_t instanceId)
{
    return g_states && g_states->ownsMapState(static_cast<int32_t>(mapId), instanceId);
}

size_t drainMapHooks(uint32_t mapId, uint32_t instanceId)
{
    if (!hookQueue().anyPending() || !mapOwnsState(mapId, instanceId))
        return 0;
    static EluRouter const router(&eventEngine);
    return router.drainHooks(MapKey{mapId, instanceId});
}

void closeMapState(uint32_t mapId, uint32_t instanceId)
{
    MapScope scope{mapId, instanceId};   // LUA_STATE_CLOSE runs before the map goes
    creationQueue().drop(MapKey{mapId, instanceId});   // a map destroyed before its first update: nothing replays
    hookQueue().drop(MapKey{mapId, instanceId});       // its queued core-hook events go with it
    if (g_states)
        g_states->closeMap(static_cast<int32_t>(mapId), instanceId);
}

size_t tickMapState(uint32_t mapId, uint32_t instanceId)
{
    MapScope scope{mapId, instanceId};   // a map state's timers and reload run on this map's thread
    return g_states ? g_states->tickMap(static_cast<int32_t>(mapId), instanceId) : 0;
}

fl::StateSet* stateSet() { return g_states.get(); }
}  // namespace flh
