// The bounded queue between the map threads (producers) and the world thread (the only consumer).
#pragma once

#include "engine/Value.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace fl
{
struct QueuedEvent
{
    std::string name;
    std::vector<EventValue> args;
    bool hasEntry = false;     // creature/gameobject events: matched against the {entry = n} filter
    uint32_t entry = 0;
    bool isBot = false;        // player events: delivered only to handlers with {bots = true}
    bool longBudget = false;   // use the command budget (server.stop)
};

class EventQueue
{
public:
    explicit EventQueue(size_t capacity = 10000) : _capacity(capacity) {}

    // Returns false (and counts a drop) when the queue is full.
    bool push(QueuedEvent&& event);
    bool pop(QueuedEvent& out);
    size_t size() const;
    uint64_t dropped() const;
    void clear();  // keeps the drop counter
    void setCapacity(size_t capacity);

private:
    mutable std::mutex _mutex;
    std::deque<QueuedEvent> _queue;
    size_t _capacity;
    uint64_t _dropped = 0;
};
}  // namespace fl
