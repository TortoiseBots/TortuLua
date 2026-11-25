// ForeverLua host side (game-free, unit-tested): the events of a map that is still being created.
//
// The core creates a battleground or an instance map while it holds its exclusive _mapsLock (MapManager::CreateMap):
// BattlegroundScript::OnInit fires BG CREATE there, and loading the grids runs every forever_lua creature's and game
// object's InitializeAI/Reset (creature RESET 23, game object SPAWN 2). Lua must not run then: any handle call reaches
// MapManager::FindMap, which takes a shared lock on the same mutex (a self-deadlock). Such an event is queued here by map
// key. Live object events of that map that come after the creation but before the replay are appended to the same queue
// (the order is kept, nothing is dispatched early). The queue is replayed once, on the map's thread, outside the lock:
// from the map's own first-update hook (after its state exists) when the map has one, otherwise at the first player
// enter. Each queue carries a generation from a process-wide counter (never a Map address): a new creation of the same
// key after the old one was registered starts a new generation and drops the old queue, so an old run never replays into
// a new instance. A world-tick sweep seals queues of registered maps and drops queues older than a bound (one log line
// per map), so leaked queues never fill the global cap.
#pragma once

#include "AccessRules.h"
#include "engine/Elu.h"
#include "engine/Value.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace flh
{
struct DeferredEvent
{
    fl::EluKind kind = fl::EluKind::Server;
    uint32_t entry = 0;
    uint32_t event = 0;
    std::vector<fl::EventValue> args;
    std::optional<fl::HandleData> subject;   // creature / game object events: the object (unique creature handlers too)
    // Core-hook events (HookQueue): the drain looks the GUID up again and drops the event when the object is gone. False for
    // the events about a gone object (REMOVE, the server delete events).
    bool needsObject = false;
    // A player event (subject kind Player): when the player is on another map at the drain, true follows it to that map's
    // queue (a map change), false drops the event (an event about THIS map: MAP_PLAYER_ENTER).
    bool followPlayer = false;
};

class CreationQueue
{
public:
    static constexpr size_t kMaxPerMap = 4096;
    static constexpr size_t kMaxTotal = 65536;

    // During the map's creation (the creating thread holds the core's map lock). A queue of the key that is already
    // sealed (its map was registered: an earlier creation of the same key) is dropped first and a new generation starts.
    // False when the bounds are reached (the event is dropped).
    bool deferCreation(MapKey key, DeferredEvent ev, uint64_t nowMs);
    // After the creation, before the replay: a live event of the map is appended (and the queue sealed). False when the key
    // has no queue (dispatch the event normally) or the bounds are reached (dropped).
    // `dropped` (when given) is set when the event hit the bound and was lost.
    bool appendLive(MapKey key, DeferredEvent ev, bool* dropped = nullptr);
    bool anyPending() const { return _total.load(std::memory_order_acquire) != 0; }
    bool pending(MapKey key) const;
    // The queued events of `key`, in order; the queue is gone afterwards.
    std::vector<DeferredEvent> take(MapKey key);
    void drop(MapKey key);
    // World tick: `registered(key)` true = the core lists the map now (the queue is sealed: a later creation of that key
    // is a new instance). Queues older than `maxAgeMs` are dropped; `onDrop(key, events)` once per dropped map.
    size_t sweep(uint64_t nowMs, uint64_t maxAgeMs, std::function<bool(MapKey)> const& registered,
                 std::function<void(MapKey, size_t)> const& onDrop);
    void clear();
    size_t size() const { return _total.load(std::memory_order_acquire); }
    uint64_t generation(MapKey key) const;   // 0 = no queue

private:
    struct Queue
    {
        uint64_t generation = 0;
        uint64_t createdMs = 0;
        bool sealed = false;
        std::vector<DeferredEvent> events;
    };
    bool room(Queue const& q) const;
    void erase(std::map<std::pair<uint32_t, uint32_t>, Queue>::iterator it);
    mutable std::mutex _mutex;
    std::map<std::pair<uint32_t, uint32_t>, Queue> _byMap;
    std::atomic<size_t> _total{0};
    uint64_t _nextGeneration = 1;
};

// The process' one queue (the router of every hook file uses it).
CreationQueue& creationQueue();
}  // namespace flh
