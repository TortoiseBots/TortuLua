#include "HookQueue.h"

namespace flh
{
bool HookQueue::push(MapKey key, DeferredEvent ev, uint64_t nowMs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto& q = _byMap[{key.map, key.instance}];
    if (q.size() >= kMaxPerMap || _total.load() >= kMaxTotal)
    {
        _dropped.fetch_add(1, std::memory_order_acq_rel);
        ++_overflow[{key.map, key.instance}].unreported;
        if (q.empty())
            _byMap.erase({key.map, key.instance});
        return false;
    }
    q.push_back(Entry{std::move(ev), nowMs});
    _total.fetch_add(1, std::memory_order_release);
    return true;
}

void HookQueue::noteDrop(MapKey key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _dropped.fetch_add(1, std::memory_order_acq_rel);
    ++_overflow[{key.map, key.instance}].unreported;
}

uint64_t HookQueue::takeOverflow(MapKey key, uint64_t nowMs, uint64_t minIntervalMs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _overflow.find({key.map, key.instance});
    if (it == _overflow.end() || it->second.unreported == 0)
        return 0;
    Overflow& o = it->second;
    if (o.reported && nowMs < o.lastReportMs + minIntervalMs)
        return 0;
    uint64_t const n = o.unreported;
    o.unreported = 0;
    o.reported = true;
    o.lastReportMs = nowMs;
    return n;
}

std::vector<DeferredEvent> HookQueue::take(MapKey key)
{
    std::vector<DeferredEvent> out;
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    if (it == _byMap.end())
        return out;
    out.reserve(it->second.size());
    for (Entry& e : it->second)
        out.push_back(std::move(e.ev));
    _total.fetch_sub(it->second.size(), std::memory_order_acq_rel);
    _byMap.erase(it);
    return out;
}

std::vector<MapKey> HookQueue::keys() const
{
    std::vector<MapKey> out;
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto const& e : _byMap)
        out.push_back(MapKey{e.first.first, e.first.second});
    return out;
}

void HookQueue::drop(MapKey key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    if (it == _byMap.end())
        return;
    _total.fetch_sub(it->second.size(), std::memory_order_acq_rel);
    _byMap.erase(it);
}

size_t HookQueue::sweep(uint64_t nowMs, uint64_t maxAgeMs, std::function<void(MapKey, size_t)> const& onDrop)
{
    std::vector<std::pair<MapKey, size_t>> dropped;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _byMap.begin(); it != _byMap.end();)
        {
            size_t n = 0;
            auto& q = it->second;
            // Pushes are in time order: the old ones are the front.
            while (n < q.size() && nowMs >= q[n].queuedMs + maxAgeMs)
                ++n;
            if (n != 0)
            {
                q.erase(q.begin(), q.begin() + static_cast<std::ptrdiff_t>(n));
                _total.fetch_sub(n, std::memory_order_acq_rel);
                dropped.emplace_back(MapKey{it->first.first, it->first.second}, n);
            }
            if (q.empty())
                it = _byMap.erase(it);
            else
                ++it;
        }
    }
    size_t total = 0;
    for (auto const& d : dropped)
    {
        total += d.second;
        if (onDrop)
            onDrop(d.first, d.second);
    }
    return total;
}

void HookQueue::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _byMap.clear();
    _overflow.clear();
    _total.store(0);
}

HookQueue& hookQueue()
{
    static HookQueue q;
    return q;
}
}  // namespace flh
