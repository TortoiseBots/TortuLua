#include "engine/Timers.h"

namespace fl
{
uint64_t Timers::add(uint64_t nowMs, uint64_t delayMs, uint64_t intervalMs, uint64_t count, int ref)
{
    TimerEntry e;
    e.id = _nextId++;
    e.dueMs = nowMs + delayMs;
    e.intervalMs = intervalMs;
    e.remaining = count;
    e.ref = ref;
    _order.insert({e.dueMs, e.id});
    _byId.emplace(e.id, std::move(e));
    return _nextId - 1;
}

bool Timers::cancel(uint64_t id, int* ref)
{
    auto it = _byId.find(id);
    if (it == _byId.end())
        return false;
    if (ref)
        *ref = it->second.ref;
    _order.erase({it->second.dueMs, id});
    _byId.erase(it);
    return true;
}

bool Timers::popDue(uint64_t nowMs, TimerEntry& out)
{
    if (_order.empty() || _order.begin()->first > nowMs)
        return false;
    uint64_t id = _order.begin()->second;
    auto it = _byId.find(id);
    out = it->second;
    _order.erase(_order.begin());

    bool again = out.intervalMs != 0;
    if (again && out.remaining != 0)
    {
        // `remaining` counts the firings still to come, including this one
        if (--it->second.remaining == 0)
            again = false;
    }
    if (again)
    {
        // Next run one interval after this one was due; a timer that fell behind by more than an interval restarts
        // from now instead of firing in a burst.
        uint64_t next = it->second.dueMs + it->second.intervalMs;
        if (next <= nowMs)
            next = nowMs + it->second.intervalMs;
        it->second.dueMs = next;
        _order.insert({next, id});
    }
    else
        _byId.erase(it);
    return true;
}

TimerEntry* Timers::find(uint64_t id)
{
    auto it = _byId.find(id);
    return it == _byId.end() ? nullptr : &it->second;
}

void Timers::clear()
{
    _byId.clear();
    _order.clear();
}
}  // namespace fl
