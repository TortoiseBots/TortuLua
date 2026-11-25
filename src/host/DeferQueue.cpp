#include "DeferQueue.h"

#include <utility>

namespace flh
{
bool DeferQueue::push(std::function<void()> job)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_jobs.size() >= _capacity)
    {
        ++_dropped;
        return false;
    }
    _jobs.push_back(std::move(job));
    return true;
}

size_t DeferQueue::drain()
{
    std::deque<std::function<void()>> jobs;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        jobs.swap(_jobs);
    }
    for (auto& j : jobs)
        j();
    return jobs.size();
}

size_t DeferQueue::size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _jobs.size();
}

size_t DeferQueue::dropped() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _dropped;
}
}  // namespace flh
