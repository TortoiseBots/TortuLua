// ForeverLua host side: which thread may touch which object. Game-free (no core header), so the rule that decides
// "Ok / Gone / OtherMap" and "run now or queue to the world thread" is the same code the module runs and the unit tests
// drive (tests/test_host_access.cpp). The core adapter (GameAccess.cpp) only supplies the facts: where an object is now.
//
// The model: the world thread runs the world script, the timers and the server hooks while no map is updating
// ("maps idle"); every map has its own update thread, which runs the player/creature/gameobject/item hooks of that map
// inside a MapScope. A handle may be resolved only by the world thread with the maps idle, or by the thread whose scope
// is the object's CURRENT map.
#pragma once

#include "engine/Types.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace flh
{
struct MapKey
{
    uint32_t map = 0;
    uint32_t instance = 0;
    bool operator==(MapKey const& o) const { return map == o.map && instance == o.instance; }
    bool operator!=(MapKey const& o) const { return !(*this == o); }
};

// The map the calling thread is updating, or none. Scopes nest: the inner scope restores the outer one.
class MapScope
{
public:
    explicit MapScope(uint32_t mapId, uint32_t instanceId);
    explicit MapScope(MapKey key) : MapScope(key.map, key.instance) { }
    ~MapScope();
    MapScope(MapScope const&) = delete;
    MapScope& operator=(MapScope const&) = delete;
    static std::optional<MapKey> current();

private:
    std::optional<MapKey> _previous;
};

class AccessRules
{
public:
    // The calling thread is the world thread.
    void setWorldThread() { _worldThread.store(std::this_thread::get_id()); }
    bool onWorldThread() const { return _worldThread.load() == std::this_thread::get_id(); }
    void setMapsIdle(bool idle) { _mapsIdle.store(idle, std::memory_order_relaxed); }
    bool mapsIdle() const { return _mapsIdle.load(std::memory_order_relaxed); }

    // `h` is the handle (the map it was made on), `current` where the object is now (nullopt: not in the world). A world
    // object only: a handle of a kind that has no map (group, guild, quest) is Ok.
    //  * inside a MapScope: the object must be on that very map. Not in the world, or no longer on the map the handle
    //    names (it moved: a stale handle): Gone. On another map than the scope: OtherMap. A map thread therefore never
    //    reaches an object that another thread updates, nor one it can no longer name.
    //  * outside any scope: Ok on the world thread with the maps idle (the world thread may reach every map then),
    //    OtherMap on any other thread or while the maps update.
    // `declared` is the map the handle claims; pass `current` for handles that carry none (items).
    fl::Access check(std::optional<MapKey> declared, std::optional<MapKey> current) const;

    enum class Place
    {
        Direct,   // run it here, now
        Defer     // queue it to the world thread's next tick
    };
    // A world change (spawn) on `target`: Direct inside that map's scope, or on the world thread with the maps idle.
    Place placeForMap(MapKey target) const;
    // A change that is the world thread's alone (RunCommand): Direct only outside any scope on the world thread, maps idle.
    Place placeForWorld() const;

private:
    std::atomic<std::thread::id> _worldThread{};
    std::atomic<bool> _mapsIdle{true};
};

// Where every player is now, as recorded by the hooks that run on the thread that owns the change (login, map change,
// logout) and refreshed by the world thread each tick. Any thread may read it: a map thread asks "is this player on my
// map?" here instead of touching the Player. A player in transit is momentarily on its old map here, so it answers
// "gone" or "other map" for an instant, never an access to a player another thread updates.
class PlayerMaps
{
public:
    void set(fl::Guid const& guid, MapKey key);
    void forget(fl::Guid const& guid);
    std::optional<MapKey> get(fl::Guid const& guid) const;
    // World thread, maps idle: the truth of this tick (drops players that are no longer listed).
    void replaceAll(std::vector<std::pair<fl::Guid, MapKey>> const& all);
    size_t size() const;
    std::vector<std::pair<fl::Guid, MapKey>> snapshot() const;

private:
    using Key = std::pair<uint64_t, uint64_t>;
    mutable std::mutex _mutex;
    std::map<Key, MapKey> _maps;
};
}  // namespace flh
