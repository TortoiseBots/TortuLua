// A plain value that can cross threads inside an event snapshot and be pushed into Lua later.
#pragma once

#include "engine/Types.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fl
{
struct EventTable;
struct EventList;

struct EventValue
{
    // monostate = nil
    std::variant<std::monostate, bool, int64_t, double, std::string, HandleData, std::shared_ptr<EventTable>,
                 std::shared_ptr<EventList>>
        v;

    EventValue() = default;
    EventValue(bool b) : v(b) {}
    EventValue(int i) : v(static_cast<int64_t>(i)) {}
    EventValue(int64_t i) : v(i) {}
    EventValue(uint32_t i) : v(static_cast<int64_t>(i)) {}
    EventValue(double d) : v(d) {}
    EventValue(char const* s) : v(std::string(s)) {}
    EventValue(std::string s) : v(std::move(s)) {}
    EventValue(HandleData const& h) : v(h) {}
    EventValue(std::shared_ptr<EventTable> t) : v(std::move(t)) {}
    EventValue(std::shared_ptr<EventList> l) : v(std::move(l)) {}
};

// A positional list (WIPE's bots): pushed as a 1-based Lua table.
struct EventList
{
    std::vector<EventValue> items;
};

// String-keyed table (the creature.death snapshot, for example).
struct EventTable
{
    std::vector<std::pair<std::string, EventValue>> fields;
};
}  // namespace fl
