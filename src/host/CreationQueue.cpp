#include "CreationQueue.h"

namespace flh
{
bool CreationQueue::room(Queue const& q) const
{
    return q.events.size() < kMaxPerMap && _total.load() < kMaxTotal;
}

void CreationQueue::erase(std::map<std::pair<uint32_t, uint32_t>, Queue>::iterator it)
{
    _total.fetch_sub(it->second.events.size(), std::memory_order_acq_rel);
    _byMap.erase(it);
}

bool CreationQueue::deferCreation(MapKey key, DeferredEvent ev, uint64_t nowMs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    if (it != _byMap.end() && it->second.sealed)
    {
        erase(it);   // an earlier instance of this key: never replayed into the new one
        it = _byMap.end();
    }
    if (it == _byMap.end())
    {
        Queue q;
        q.generation = _nextGeneration++;
        q.createdMs = nowMs;
        it = _byMap.emplace(std::make_pair(key.map, key.instance), std::move(q)).first;
    }
    if (!room(it->second))
        return false;
    it->second.events.push_back(std::move(ev));
    _total.fetch_add(1, std::memory_order_release);
    return true;
}

bool CreationQueue::appendLive(MapKey key, DeferredEvent ev, bool* dropped)
{
    if (!anyPending())
        return false;
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    if (it == _byMap.end())
        return false;
    it->second.sealed = true;   // the map is registered: this queue belongs to it
    if (room(it->second))
    {
        it->second.events.push_back(std::move(ev));
        _total.fetch_add(1, std::memory_order_release);
    }
    else if (dropped)
        *dropped = true;
    return true;   // queued (or dropped at the bound): never dispatched ahead of the queue
}

bool CreationQueue::pending(MapKey key) const
{
    if (!anyPending())
        return false;
    std::lock_guard<std::mutex> lock(_mutex);
    return _byMap.count({key.map, key.instance}) != 0;
}

uint64_t CreationQueue::generation(MapKey key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    return it == _byMap.end() ? 0 : it->second.generation;
}

std::vector<DeferredEvent> CreationQueue::take(MapKey key)
{
    std::vector<DeferredEvent> out;
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    if (it == _byMap.end())
        return out;
    out = std::move(it->second.events);
    it->second.events.clear();
    _total.fetch_sub(out.size(), std::memory_order_acq_rel);
    _byMap.erase(it);
    return out;
}

void CreationQueue::drop(MapKey key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byMap.find({key.map, key.instance});
    if (it != _byMap.end())
        erase(it);
}

size_t CreationQueue::sweep(uint64_t nowMs, uint64_t maxAgeMs, std::function<bool(MapKey)> const& registered,
                            std::function<void(MapKey, size_t)> const& onDrop)
{
    std::vector<std::pair<MapKey, size_t>> dropped;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _byMap.begin(); it != _byMap.end();)
        {
            MapKey const key{it->first.first, it->first.second};
            if (!it->second.sealed && registered && registered(key))
                it->second.sealed = true;
            if (nowMs >= it->second.createdMs + maxAgeMs)
            {
                dropped.emplace_back(key, it->second.events.size());
                auto next = std::next(it);
                erase(it);
                it = next;
                continue;
            }
            ++it;
        }
    }
    if (onDrop)
        for (auto const& d : dropped)
            onDrop(d.first, d.second);
    return dropped.size();
}

void CreationQueue::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _byMap.clear();
    _total.store(0);
}

CreationQueue& creationQueue()
{
    static CreationQueue q;
    return q;
}
}  // namespace flh
