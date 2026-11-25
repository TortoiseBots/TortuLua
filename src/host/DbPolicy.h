// The game-free parts of the module's own MySQL connections (LuaDb): the reconnect backoff and the capped streaming row
// reader. The libmysql calls stay in LuaDb.cpp; what decides when to retry and when to stop reading is here, so the unit
// tests (tests/test_host_db.cpp) cover it without a database.
#pragma once

#include "engine/Host.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flh
{
// After a failed connect the next attempt waits `first` ms, doubling up to `max` ms. A success resets it. The first
// attempt is always allowed.
class ReconnectPolicy
{
public:
    explicit ReconnectPolicy(uint64_t firstMs = 1000, uint64_t maxMs = 60000) : _first(firstMs), _max(maxMs) { }
    bool mayAttempt(uint64_t nowMs) const { return nowMs >= _retryAtMs; }
    void failed(uint64_t nowMs);
    void succeeded();
    // A deliberate close (a streaming result was cut off) is not a failure: reconnect at once.
    void closedOnPurpose() { _retryAtMs = 0; }
    uint64_t retryAtMs() const { return _retryAtMs; }
    uint32_t failures() const { return _failures; }

private:
    uint64_t _first;
    uint64_t _max;
    uint64_t _retryAtMs = 0;
    uint64_t _delay = 0;
    uint32_t _failures = 0;
};

// One cell of a row as the driver hands it out (text views are valid until the next row is fetched).
struct Cell
{
    enum Kind
    {
        Null,
        Int,
        Double,
        Text
    } kind = Null;
    int64_t i = 0;
    double d = 0;
    std::string_view text;
};

// A streaming result: rows are pulled one at a time, never buffered by the reader.
class RowSource
{
public:
    virtual ~RowSource() = default;
    virtual std::vector<std::string> columns() = 0;
    // False at the end of the result.
    virtual bool next(std::vector<Cell>& row) = 0;
};

struct ReadOutcome
{
    bool exhausted = false;      // the source ended by itself
    bool rowCap = false;         // stopped at maxRows with more rows left
    bool tooLarge = false;       // the string bytes passed maxBytes (the last cell was cut)
};

// Reads at most maxRows rows and maxBytes string bytes (0 = unlimited), pulling no row it does not need: after maxRows
// rows it pulls exactly one more, to learn whether the result had more, and drops it. The caller closes the connection
// when the source was not exhausted (a streaming result cannot be abandoned any other way).
ReadOutcome readRowsCapped(RowSource& source, size_t maxRows, size_t maxBytes, fl::DbRows& out);
}  // namespace flh
