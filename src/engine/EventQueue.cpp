#include "engine/EventQueue.h"

namespace fl
{
bool EventQueue::push(QueuedEvent&& event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.size() >= _capacity)
    {
        ++_dropped;
        return false;
    }
    _queue.push_back(std::move(event));
    return true;
}

bool EventQueue::pop(QueuedEvent& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.empty())
        return false;
    out = std::move(_queue.front());
    _queue.pop_front();
    return true;
}

size_t EventQueue::size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.size();
}

uint64_t EventQueue::dropped() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _dropped;
}

void EventQueue::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.clear();
}

void EventQueue::setCapacity(size_t capacity)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _capacity = capacity;
}
}  // namespace fl
