// E4: one StateSet (the world state plus, with MapStates = 1, one state per map instance). See StateSet.h for the rules.
#include "engine/StateSet.h"

#include <algorithm>

namespace fl
{
namespace
{
StateSet::Key keyOf(int32_t mapId, uint32_t instanceId)
{
    return {mapId, instanceId};
}

std::string keyText(StateSet::Key const& k)
{
    return "map " + std::to_string(k.first) + " instance " + std::to_string(k.second);
}

// Tombstones of closed maps: enough for every instance id the core can hand out again soon (it reuses freed ids), and
// bounded so a long uptime cannot grow the set without end.
constexpr size_t kMaxTombstones = 65536;
}  // namespace

StateSet::StateSet(Host& host, Config config) : _host(host), _cfg(std::move(config)) { }

StateSet::~StateSet()
{
    // shutdown() is the host's call; the destructor only makes sure no Lua state outlives the set.
    closeAllMaps();
    if (_world)
        _world->shutdown();
}

bool StateSet::mapBound(EluKind kind)
{
    switch (kind)
    {
        case EluKind::Creature:
        case EluKind::GameObject:
        case EluKind::Item:
        case EluKind::CreatureGossip:
        case EluKind::GameObjectGossip:
        case EluKind::ItemGossip:
        case EluKind::Map:
        case EluKind::Instance:
        case EluKind::BattleGround:
        case EluKind::Spell:
            return true;
        case EluKind::Player:
        case EluKind::Server:
        case EluKind::PlayerGossip:
        case EluKind::Group:
        case EluKind::Guild:
        case EluKind::Packet:
        case EluKind::Bot:
            return false;   // the bot snapshot is process-wide: a bot event is the world state's
    }
    return false;
}

void StateSet::setBinder(std::function<void(lua_State*)> binder)
{
    _binder = std::move(binder);
    if (_world)
        _world->setBinder(_binder);
}

bool StateSet::start()
{
    if (_world)
        return true;
    auto world = std::make_shared<Engine>(_host, _cfg);
    if (_binder)
        world->setBinder(_binder);
    if (!world->start())
        return false;
    _world = std::move(world);
    return true;
}

LoadReport StateSet::load()
{
    if (!_world)
    {
        LoadReport rep;
        rep.errors.push_back("engine not running");
        return rep;
    }
    return _world->load();
}

void StateSet::closeAllMaps()
{
    std::vector<Key> keys;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        keys.reserve(_maps.size());
        for (auto const& e : _maps)
            keys.push_back(e.first);
        _initPending.clear();
    }
    for (auto const& k : keys)
        closeMap(k.first, k.second);
}

void StateSet::shutdown()
{
    closeAllMaps();
    if (_world)
        _world->shutdown();
}

bool StateSet::running() const { return _world && _world->running(); }

std::shared_ptr<Engine> StateSet::mapStateShared(int32_t mapId, uint32_t instanceId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _maps.find(keyOf(mapId, instanceId));
    if (it == _maps.end() || it->second.phase != Phase::Open)
        return nullptr;
    return it->second.engine;
}

Engine* StateSet::mapState(int32_t mapId, uint32_t instanceId) const
{
    return mapStateShared(mapId, instanceId).get();
}

std::shared_ptr<Engine> StateSet::makeMapEngine(int32_t mapId, uint32_t instanceId)
{
    Config cfg = _cfg;
    cfg.stateMapId = mapId;
    cfg.stateInstanceId = instanceId;
    auto engine = std::make_shared<Engine>(_host, cfg);
    if (_world)
        engine->useDataOwner(_world.get());   // ForeverData: the world state's store (the one loader and writer)
    if (_binder)
        engine->setBinder(_binder);
    if (!engine->start())
    {
        _host.log(LogLevel::Error, "ForeverLua: " + keyText(keyOf(mapId, instanceId)) +
                                       ": the Lua state could not be created; its events stay in the world state");
        return nullptr;
    }
    LoadReport const rep = engine->load();
    if (!rep.errors.empty())
    {
        _host.log(LogLevel::Warn, "ForeverLua: " + keyText(keyOf(mapId, instanceId)) + ": " +
                                      std::to_string(rep.errors.size()) +
                                      " script error(s); the state keeps the files that loaded");
        for (auto const& e : rep.errors)
            _host.log(LogLevel::Error, "ForeverLua: " + e);
    }
    return engine;
}

void StateSet::retire(std::shared_ptr<Engine> const& engine) const
{
    if (engine)
        engine->closeAsMapState();   // LUA_STATE_CLOSE in that state, then its memory is freed (on this thread)
}

void StateSet::noteDrop(Key const& key, char const* why) const
{
    // Called with _mutex held.
    ++_dropped;
    if (_dropLogged.insert(key).second)
        _host.log(LogLevel::Warn, "ForeverLua: " + keyText(key) + " " + why + ": its map events are dropped (logged once)");
}

void StateSet::scheduleMap(int32_t mapId, uint32_t instanceId)
{
    Key const key = keyOf(mapId, instanceId);
    std::lock_guard<std::mutex> lock(_mutex);
    if (_closed.erase(key))
    {
        auto pos = std::find(_closedOrder.begin(), _closedOrder.end(), key);
        if (pos != _closedOrder.end())
            _closedOrder.erase(pos);
    }
    _dropLogged.erase(key);
    if (_cfg.mapStates == 0)
    {
        _initPending.insert(key);   // only MAP_INITIALIZE waits for the first update
        return;
    }
    MapState& slot = _maps[key];
    if (slot.engine)
    {
        // H7: an instance id reused while the old state was never closed: the new map gets a fresh state, the old one is
        // closed at the first update (never reused with the old run's globals).
        if (slot.retired)
            _host.log(LogLevel::Warn, "ForeverLua: " + keyText(key) + ": a third life before the first update");
        slot.retired = std::move(slot.engine);
    }
    slot.engine.reset();
    slot.phase = Phase::Scheduled;
    slot.reloadPending = false;
}

bool StateSet::beginMapUpdate(int32_t mapId, uint32_t instanceId)
{
    Key const key = keyOf(mapId, instanceId);
    std::shared_ptr<Engine> retired;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_cfg.mapStates == 0)
            return _initPending.erase(key) != 0;
        auto it = _maps.find(key);
        if (it == _maps.end() || it->second.phase != Phase::Scheduled)
            return false;
        it->second.phase = Phase::Opening;   // only this thread builds it; events keep being dropped until it is open
        retired = std::move(it->second.retired);
    }
    if (!_world || !_world->running())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _maps.erase(key);
        return false;
    }
    retire(retired);
    retired.reset();
    // H5: the state is built and its folder loaded here, on the thread updating the map and outside the core's map lock.
    std::shared_ptr<Engine> engine = makeMapEngine(mapId, instanceId);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _maps.find(key);
        if (it == _maps.end() || it->second.phase != Phase::Opening)
        {
            // Closed while it loaded: the tombstone stands and this state never serves.
            retired = std::move(engine);
            engine.reset();
        }
        else if (!engine)
            _maps.erase(it);   // the state could not start: the map's events stay in the world state
        else
        {
            it->second.engine = engine;
            it->second.phase = Phase::Open;
        }
    }
    if (retired)
    {
        retire(retired);
        return false;
    }
    if (engine)
        engine->emitEluna(EluKind::Server, 0, 33, {});   // LUA_STATE_OPEN, in the new state
    return true;   // the host fires MAP_INITIALIZE now (the first time its scripts can see the map)
}

bool StateSet::awaitingFirstUpdate(int32_t mapId, uint32_t instanceId) const
{
    Key const key = keyOf(mapId, instanceId);
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initPending.count(key))
        return true;
    auto it = _maps.find(key);
    return it != _maps.end() && it->second.phase != Phase::Open;
}

bool StateSet::ownsMapState(int32_t mapId, uint32_t instanceId) const
{
    if (_cfg.mapStates == 0)
        return false;
    Key const key = keyOf(mapId, instanceId);
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initPending.count(key))
        return true;
    auto it = _maps.find(key);
    return it != _maps.end();
}

Engine* StateSet::openMap(int32_t mapId, uint32_t instanceId)
{
    if (_cfg.mapStates == 0)
        return _world.get();
    if (Engine* open = mapState(mapId, instanceId))
        return open;
    scheduleMap(mapId, instanceId);
    beginMapUpdate(mapId, instanceId);
    return mapState(mapId, instanceId);
}

void StateSet::closeMap(int32_t mapId, uint32_t instanceId)
{
    Key const key = keyOf(mapId, instanceId);
    std::shared_ptr<Engine> engine, retired;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _initPending.erase(key);
        auto it = _maps.find(key);
        if (it != _maps.end())
        {
            engine = std::move(it->second.engine);
            retired = std::move(it->second.retired);
            _maps.erase(it);
        }
        if (_cfg.mapStates != 0 && _closed.insert(key).second)
        {
            // H7: the tombstone comes before LUA_STATE_CLOSE: nothing routed to this key from now on (the core unloads
            // the map after OnDestroy, and that fires creature events) can reopen it.
            _closedOrder.push_back(key);
            while (_closedOrder.size() > kMaxTombstones)
            {
                _closed.erase(_closedOrder.front());
                _closedOrder.pop_front();
            }
        }
    }
    retire(retired);
    retire(engine);
}

std::vector<StateSet::Key> StateSet::maps() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Key> out;
    out.reserve(_maps.size());
    for (auto const& e : _maps)
        if (e.second.phase == Phase::Open)
            out.push_back(e.first);
    return out;
}

size_t StateSet::mapCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    size_t n = 0;
    for (auto const& e : _maps)
        n += e.second.phase == Phase::Open ? 1 : 0;
    return n;
}

uint64_t StateSet::droppedEvents() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _dropped;
}

size_t StateSet::memoryInUse() const
{
    size_t total = _world ? _world->memoryUsed() : 0;
    std::vector<std::shared_ptr<Engine>> states;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        states.reserve(_maps.size());
        for (auto const& e : _maps)
            if (e.second.engine)
                states.push_back(e.second.engine);
    }
    for (auto const& e : states)
        total += e->memoryUsed();
    return total;
}

std::shared_ptr<Engine> StateSet::route(EluKind kind, std::optional<Key> map) const
{
    std::shared_ptr<Engine> world = _world;
    if (!world || _cfg.mapStates == 0 || !map || !mapBound(kind))
        return world;   // H6: a player, server, group, guild or packet event is the world state's from any map
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _maps.find(*map);
    if (it != _maps.end())
    {
        if (it->second.phase == Phase::Open && it->second.engine)
            return it->second.engine;
        noteDrop(*map, "is still loading its scripts");
        return nullptr;
    }
    if (_closed.count(*map))
    {
        noteDrop(*map, "is closing (OnDestroy ran)");   // H7: never reopened from here
        return nullptr;
    }
    return world;   // a map with no map state (dungeons and raids: no map hook at this core) uses the world state
}

std::shared_ptr<Engine> StateSet::stateFor(EluKind kind, std::optional<HandleData> const& subject) const
{
    if (!subject || !mapBound(kind) || _cfg.mapStates == 0)
        return _world;
    uint32_t mapId = 0, instanceId = 0;
    if (!_host.currentMap(*subject, mapId, instanceId))
        return _world;
    return route(kind, keyOf(static_cast<int32_t>(mapId), instanceId));
}

bool StateSet::listening(EluKind kind, uint32_t entry, uint32_t event, std::optional<HandleData> const& subject) const
{
    std::shared_ptr<Engine> engine = stateFor(kind, subject);
    return engine && engine->listeningEluna(kind, entry, event);
}

EventResult StateSet::emit(EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> const& args,
                           std::optional<HandleData> const& subject) const
{
    std::shared_ptr<Engine> engine = stateFor(kind, subject);
    return engine ? engine->emitEluna(kind, entry, event, args) : EventResult();
}

EventResult StateSet::emitUnique(EluKind kind, Guid const& guid, uint32_t instance, uint32_t event,
                                 std::vector<EventValue> const& args, std::optional<HandleData> const& subject) const
{
    std::shared_ptr<Engine> engine = stateFor(kind, subject);
    return engine ? engine->emitElunaUnique(kind, guid, instance, event, args) : EventResult();
}

size_t StateSet::tickMap(int32_t mapId, uint32_t instanceId)
{
    if (_cfg.mapStates == 0)
        return 0;
    std::shared_ptr<Engine> engine;
    bool reload = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _maps.find(keyOf(mapId, instanceId));
        if (it == _maps.end() || it->second.phase != Phase::Open || !it->second.engine)
            return 0;
        engine = it->second.engine;
        reload = it->second.reloadPending;
        it->second.reloadPending = false;
    }
    engine->claimOwnerThread();
    if (reload)
    {
        // LUA_STATE_CLOSE only right before the adopt (as the world's reload): a reload with a script error keeps the old
        // state exactly as it was, close handlers included.
        LoadReport const rep = engine->reload([&engine] { engine->emitEluna(EluKind::Server, 0, 16, {}); });
        if (rep.applied)
            engine->emitEluna(EluKind::Server, 0, 33, {});
        else
        {
            for (auto const& e : rep.errors)
                _host.log(LogLevel::Error, "ForeverLua: map " + std::to_string(mapId) + " reload: " + e);
        }
    }
    return engine->tick();
}

void StateSet::requestMapReload(int32_t mapId, uint32_t instanceId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _maps.find(keyOf(mapId, instanceId));
    if (it != _maps.end() && it->second.phase == Phase::Open)
        it->second.reloadPending = true;
}

size_t StateSet::pendingMapReloads() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    size_t n = 0;
    for (auto const& e : _maps)
        n += e.second.reloadPending ? 1 : 0;
    return n;
}

LoadReport StateSet::reloadAll()
{
    LoadReport rep;
    if (!_world || !_world->running())
    {
        rep.errors.push_back("engine not running");
        return rep;
    }
    rep = _world->reload([this] { _world->emitEluna(EluKind::Server, 0, 16, {}); });
    if (!rep.applied)
        return rep;   // the world kept its old scripts: so do the maps (no version skew from a failed reload)
    _world->emitEluna(EluKind::Server, 0, 33, {});
    // Map states reload on their own map's thread at the next update: one state's in-flight handler never waits for
    // another state's reload. Each reads the folder at that moment (documented: a file changed in between is seen by the
    // later states only).
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& e : _maps)
            if (e.second.phase == Phase::Open)
                e.second.reloadPending = true;
    }
    return rep;
}
}  // namespace fl
