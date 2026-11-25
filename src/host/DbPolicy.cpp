#include "DbPolicy.h"

#include <algorithm>

namespace flh
{
void ReconnectPolicy::failed(uint64_t nowMs)
{
    _delay = _delay == 0 ? _first : std::min(_delay * 2, _max);
    _retryAtMs = nowMs + _delay;
    ++_failures;
}

void ReconnectPolicy::succeeded()
{
    _delay = 0;
    _retryAtMs = 0;
    _failures = 0;
}

ReadOutcome readRowsCapped(RowSource& source, size_t maxRows, size_t maxBytes, fl::DbRows& out)
{
    ReadOutcome outcome;
    out.columns = source.columns();
    out.maxBytes = maxBytes;
    size_t bytes = 0;
    std::vector<Cell> row;
    for (;;)
    {
        if (maxRows && out.rows.size() >= maxRows)
        {
            // One row of look-ahead decides "exactly maxRows" from "more than maxRows"; it is dropped.
            if (source.next(row))
                outcome.rowCap = true;
            else
                outcome.exhausted = true;
            return outcome;
        }
        if (!source.next(row))
        {
            outcome.exhausted = true;
            return outcome;
        }
        std::vector<fl::DbValue> values(row.size());
        for (size_t i = 0; i < row.size(); ++i)
        {
            Cell const& c = row[i];
            switch (c.kind)
            {
                case Cell::Null: values[i] = std::monostate(); break;
                case Cell::Int: values[i] = c.i; break;
                case Cell::Double: values[i] = c.d; break;
                case Cell::Text:
                {
                    size_t n = c.text.size();
                    if (maxBytes)
                    {
                        if (bytes + n > maxBytes)
                        {
                            outcome.tooLarge = true;
                            out.tooLarge = true;
                            n = maxBytes > bytes ? maxBytes - bytes : 0;
                        }
                        bytes += n;
                    }
                    values[i] = std::string(c.text.substr(0, n));
                    break;
                }
            }
        }
        out.rows.push_back(std::move(values));
        if (outcome.tooLarge)
            return outcome;
    }
}
}  // namespace flh
