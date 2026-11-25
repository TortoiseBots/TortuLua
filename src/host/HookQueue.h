// ForeverLua host side (game-free, unit-tested): the creature and game object events of the core hooks (E6).
//
// The core's notification hooks (OnCreatureEngage/LeaveCombat/JustDied/Appear/AddWorld/RemoveWorld and the game object
// ones) forbid a synchronous kill, despawn, combat end or threat change: OnCreatureLeaveCombat can run inside
// CombatManager::EndAllPvECombat's loop over the combat references, and a Lua handler that kills or despawns another
// creature there frees a reference that loop still uses. So a hook never runs Lua. It pushes the event here as plain
// values (the object's GUID handle and the arguments) and returns; the map's next update (its own state when MapStates is
// on) or the world update (a map without a state of its own) drains the queue outside every core loop. The drain looks
// the GUID up again: an object that is gone is dropped (a REMOVE event is about a gone object and still fires). The order
// per map is the order of the pushes, so the order per object is kept. The per-map cap bounds a map that is never
// drained, and a sweep drops events that waited longer than a minute (one log line per map).
#pragma once

#include "AccessRules.h"
#include "CreationQueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace flh
{
class HookQueue
{
public:
    static constexpr size_t kMaxPerMap = 4096;
    static constexpr size_t kMaxTotal = 65536;

    // False when the bounds are reached (the event is dropped and counted per map: see takeOverflow).
    bool push(MapKey key, DeferredEvent ev, uint64_t nowMs);
    // An event of `key` was dropped at a bound somewhere (this queue or the creation queue).
    void noteDrop(MapKey key);
    // How many drops of `key` are not yet reported, when the last report is at least `minIntervalMs` old (the first one is
    // reported at once); 0 = nothing to report or too soon. Marks them reported. The drain asks with 0 to flush the rest.
    uint64_t takeOverflow(MapKey key, uint64_t nowMs, uint64_t minIntervalMs);
    bool anyPending() const { return _total.load(std::memory_order_acquire) != 0; }
    // The queued events of `key`, in order; the queue is empty afterwards.
    std::vector<DeferredEvent> take(MapKey key);
    std::vector<MapKey> keys() const;
    void drop(MapKey key);
    // World tick: events that waited longer than `maxAgeMs` are dropped; `onDrop(key, events)` once per map.
    size_t sweep(uint64_t nowMs, uint64_t maxAgeMs, std::function<void(MapKey, size_t)> const& onDrop);
    void clear();
    size_t size() const { return _total.load(std::memory_order_acquire); }
    uint64_t dropped() const { return _dropped.load(std::memory_order_acquire); }

private:
    struct Entry
    {
        DeferredEvent ev;
        uint64_t queuedMs = 0;
    };
    mutable std::mutex _mutex;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<Entry>> _byMap;
    std::atomic<size_t> _total{0};
    std::atomic<uint64_t> _dropped{0};
    struct Overflow
    {
        uint64_t unreported = 0;
        uint64_t lastReportMs = 0;
        bool reported = false;
    };
    std::map<std::pair<uint32_t, uint32_t>, Overflow> _overflow;   // under _mutex
};

// The process' one hook queue.
HookQueue& hookQueue();
}  // namespace flh
