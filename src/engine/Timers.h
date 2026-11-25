// Timer table: pure scheduling, no Lua. The engine keeps the Lua function behind `ref`.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace fl
{
struct TimerEntry
{
    uint64_t id = 0;
    uint64_t dueMs = 0;
    uint64_t intervalMs = 0;   // 0 = one shot
    uint64_t remaining = 0;    // repeats left for an `every` with a count; 0 = unlimited
    int ref = 0;               // Lua registry reference of the callback
    std::vector<uint64_t> breaks;  // times of budget breaks (disables the timer after too many)
};

class Timers
{
public:
    // Adds a timer. The caller has already clamped delay and interval to the minimum.
    uint64_t add(uint64_t nowMs, uint64_t delayMs, uint64_t intervalMs, uint64_t count, int ref);
    // Removes a timer; returns its Lua ref in *ref when it existed.
    bool cancel(uint64_t id, int* ref);
    // Takes the earliest timer that is due. A repeating timer is re-armed before it is returned, so the callback
    // can cancel it. Returns false when nothing is due.
    bool popDue(uint64_t nowMs, TimerEntry& out);
    TimerEntry* find(uint64_t id);
    size_t size() const { return _byId.size(); }
    uint64_t nextId() const { return _nextId; }
    void setNextId(uint64_t id) { if (id > _nextId) _nextId = id; }
    // Every ref still held (used when the engine closes the state; the refs die with it).
    void clear();

private:
    uint64_t _nextId = 1;
    std::map<uint64_t, TimerEntry> _byId;
    std::set<std::pair<uint64_t, uint64_t>> _order;  // (dueMs, id)
};
}  // namespace fl
