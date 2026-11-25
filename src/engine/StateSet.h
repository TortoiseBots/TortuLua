// E4: the set of Lua states. With MapStates = 0 there is one state (the world state) and every event is routed to it,
// exactly as in E0-E3. With MapStates = 1 the set holds the world state plus one state per map instance: a map state is
// scheduled on the map's OnCreate (nothing is loaded there: the core holds its map lock), built and loaded on the map's
// first update on the thread updating that map, and closed on its OnDestroy. It owns the map-bound handlers (creature,
// game object, item, gossip, map/instance, battleground) and their timed events. Player, server, group, guild and packet
// handlers stay in the world state. Every state has its own Lua state, handlers, timers, budgets and memory cap; the
// states share only ForeverData, which lives in the world state (the one loader and writer).
//
// Lifecycle of one map key (review round H5/H7): scheduleMap (OnCreate, startup) -> the first beginMapUpdate builds and
// loads it and fires LUA_STATE_OPEN; the host then fires MAP_INITIALIZE -> closeMap (OnDestroy) first leaves a tombstone,
// so an event that arrives while the core unloads the map (UnloadAll runs after OnDestroy) is dropped with a log line and
// can never reopen the state; a later OnCreate of the same instance id gets a fresh state. A map that was never scheduled
// (dungeon and raid maps: the core's InstanceMapScript is database-bound) keeps its events in the world state.
//
// A handle never crosses a state as a pointer: it is plain HandleData (kind, guid, map, instance) and every state pushes
// its own userdata for it. A method of a handle that names another map is refused by Host::checkAccess. Calls that must
// run somewhere else (a spawn on another map, RunCommand, a reload) go through the host's deferred queue, never through
// another state's Lua stack.
#pragma once

#include "engine/Elu.h"
#include "engine/Engine.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fl
{
class StateSet
{
public:
    using Key = std::pair<int32_t, uint32_t>;

    StateSet(Host& host, Config config);
    ~StateSet();

    // Creates the world state and records the calling thread as its owner. Loads no script.
    bool start();
    // Loads the script folder into the world state (called by the host after start()).
    LoadReport load();
    // Closes every map state (LUA_STATE_CLOSE first) and then the world state.
    void shutdown();

    bool running() const;
    Engine* world() const { return _world.get(); }
    std::shared_ptr<Engine> worldShared() const { return _world; }

    // The open map state of that exact map instance, or null (a raw pointer for the tests and the status lines; the
    // router uses route(), which pins the state).
    Engine* mapState(int32_t mapId, uint32_t instanceId) const;
    std::shared_ptr<Engine> mapStateShared(int32_t mapId, uint32_t instanceId) const;

    // H5: OnCreate and startup. Records the map (with MapStates = 0: only its pending MAP_INITIALIZE); loads nothing,
    // fires nothing and touches no game object, so it is safe under the core's _mapsLock. An earlier state of the same key
    // that was never closed is retired at the first update (a reused instance id gets a fresh state).
    void scheduleMap(int32_t mapId, uint32_t instanceId);
    // H5: the map's update, before its UPDATE event, on the thread updating the map. The first call after scheduleMap
    // builds and loads the map state there and fires its LUA_STATE_OPEN; it answers true exactly once per schedule, and
    // the host then fires MAP_INITIALIZE (both modes). Later calls answer false.
    bool beginMapUpdate(int32_t mapId, uint32_t instanceId);
    // True while a scheduled map has not had its first update yet (its hook will replay what its creation queued).
    bool awaitingFirstUpdate(int32_t mapId, uint32_t instanceId) const;
    // E6: does this map key have a state of its own (open, or scheduled and waiting for its first update)? Its own update
    // drains its queued core-hook events; a key without one (MapStates off, dungeons and raids) is drained by the world
    // update.
    bool ownsMapState(int32_t mapId, uint32_t instanceId) const;
    // Tests and tools: scheduleMap + beginMapUpdate on the calling thread; the open state (or the world state with
    // MapStates = 0).
    Engine* openMap(int32_t mapId, uint32_t instanceId);
    // H7: the tombstone first (nothing can reopen the key), then LUA_STATE_CLOSE and the state's memory is freed.
    void closeMap(int32_t mapId, uint32_t instanceId);
    void closeAllMaps();
    std::vector<Key> maps() const;
    size_t mapCount() const;
    // Map events dropped because their map was still loading or already closing (H7), for the tests and `.lua status`.
    uint64_t droppedEvents() const;
    // Sum of the Lua heap of every state (the E4 test proves a closed state gives its memory back).
    size_t memoryInUse() const;

    // H6: the state that owns one event. Only the map-bound families (StateSet::mapBound) go to a map state; every
    // other family goes to the world state whatever map it comes from. Null = drop the event (its map state is loading
    // or closing). The state is pinned for as long as the caller holds the pointer.
    std::shared_ptr<Engine> route(EluKind kind, std::optional<Key> map) const;
    // The same, with the map taken from the subject's current map.
    std::shared_ptr<Engine> stateFor(EluKind kind, std::optional<HandleData> const& subject) const;
    static bool mapBound(EluKind kind);

    // The extra bindings every state installs (the module's bindGame): applied to the world state now and to every map
    // state when it is created.
    void setBinder(std::function<void(lua_State*)> binder);

    // Event routing (the router of the host calls these). Map-bound families go to the subject's map state; player,
    // server, group and guild events go to the world state.
    bool listening(EluKind kind, uint32_t entry, uint32_t event, std::optional<HandleData> const& subject) const;
    EventResult emit(EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> const& args,
                     std::optional<HandleData> const& subject) const;
    EventResult emitUnique(EluKind kind, Guid const& guid, uint32_t instance, uint32_t event,
                           std::vector<EventValue> const& args, std::optional<HandleData> const& subject) const;

    // The map's own tick: claims the calling thread as the state's owner, applies a pending reload, then runs the state's
    // timers and queued work. Called from the map's OnUpdate.
    size_t tickMap(int32_t mapId, uint32_t instanceId);
    // Puts that map's state on the next tick's reload list (an event in flight finishes first).
    void requestMapReload(int32_t mapId, uint32_t instanceId);
    size_t pendingMapReloads() const;

    // `.lua reload`: the world state reloads now (LUA_STATE_CLOSE before the adopt, LUA_STATE_OPEN after it); when it was
    // applied, every map state reloads on its own thread at its next update. A failed world reload keeps the old world
    // state and schedules no map reload.
    LoadReport reloadAll();

private:
    enum class Phase
    {
        Scheduled,   // OnCreate ran; the first update builds the state
        Opening,     // the first update is building it on the map's thread
        Open,
    };
    struct MapState
    {
        Phase phase = Phase::Scheduled;
        std::shared_ptr<Engine> engine;
        std::shared_ptr<Engine> retired;   // a state of the same key that was never closed: closed at the first update
        bool reloadPending = false;
    };
    std::shared_ptr<Engine> makeMapEngine(int32_t mapId, uint32_t instanceId);
    void retire(std::shared_ptr<Engine> const& engine) const;
    void noteDrop(Key const& key, char const* why) const;

    Host& _host;
    Config _cfg;
    std::shared_ptr<Engine> _world;
    mutable std::mutex _mutex;
    std::map<Key, MapState> _maps;
    std::set<Key> _initPending;          // MapStates = 0: maps whose MAP_INITIALIZE waits for their first update
    std::set<Key> _closed;               // tombstones of closed maps (cleared by the next scheduleMap of the key)
    std::deque<Key> _closedOrder;        // bounds the tombstones
    mutable std::set<Key> _dropLogged;   // one log line per key and life
    mutable uint64_t _dropped = 0;
    std::function<void(lua_State*)> _binder;
};
}  // namespace fl
