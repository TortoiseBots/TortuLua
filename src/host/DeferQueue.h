// Work a map thread (or a wrong thread) asked for that only the world thread may do: PerformIngameSpawn on another map,
// RunCommand. Bounded; the world script drains it once per tick, after the maps. Game-free.
#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

namespace flh
{
class DeferQueue
{
public:
    explicit DeferQueue(size_t capacity = 1024) : _capacity(capacity) { }
    // False when full (the request is dropped).
    bool push(std::function<void()> job);
    // World thread: runs what is queued now (jobs queued by those jobs wait for the next drain). Returns the count.
    size_t drain();
    size_t size() const;
    size_t dropped() const;

private:
    mutable std::mutex _mutex;
    std::deque<std::function<void()>> _jobs;
    size_t _capacity;
    size_t _dropped = 0;
};
}  // namespace flh
