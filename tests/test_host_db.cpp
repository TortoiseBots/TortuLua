// The game-free parts of LuaDb (src/host/DbPolicy.cpp): the reconnect backoff and the capped streaming row reader. The
// libmysql calls that use them are compiled in the game-side build; the decisions are tested here.
#include "DbPolicy.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace flh;

namespace
{
// A result of `total` rows that counts how many rows were pulled and what each row cost.
class CountingSource final : public RowSource
{
public:
    CountingSource(size_t total, size_t textBytes) : _total(total), _textBytes(textBytes) { }
    std::vector<std::string> columns() override { return {"id", "name", "score"}; }
    bool next(std::vector<Cell>& row) override
    {
        if (_pulled >= _total)
        {
            exhaustedReached = true;
            return false;
        }
        ++_pulled;
        row.assign(3, Cell());
        row[0].kind = Cell::Int;
        row[0].i = static_cast<int64_t>(_pulled);
        _text.assign(_textBytes, 'x');
        row[1].kind = Cell::Text;
        row[1].text = _text;
        row[2].kind = _pulled % 2 ? Cell::Null : Cell::Double;
        row[2].d = 1.5;
        return true;
    }
    size_t pulled() const { return _pulled; }
    bool exhaustedReached = false;

private:
    size_t _total, _textBytes, _pulled = 0;
    std::string _text;
};
}  // namespace

TEST_CASE("db: the reader never pulls more than the cap plus one look-ahead row, however large the result")
{
    CountingSource huge(10'000'000, 8);
    fl::DbRows out;
    ReadOutcome o = readRowsCapped(huge, 1024, 1u << 20, out);
    CHECK(out.rows.size() == 1024);
    CHECK(o.rowCap);
    CHECK_FALSE(o.exhausted);
    CHECK_FALSE(o.tooLarge);
    CHECK(huge.pulled() == 1025);                    // the old code buffered the whole table before it counted
    CHECK(out.columns.size() == 3);
    CHECK(std::get<int64_t>(out.rows[1023][0]) == 1024);
    CHECK(std::holds_alternative<std::monostate>(out.rows[0][2]));
    CHECK(std::get<double>(out.rows[1][2]) == 1.5);

    // Exactly the cap: not truncated, the look-ahead finds the end.
    CountingSource exact(1024, 8);
    fl::DbRows out2;
    ReadOutcome o2 = readRowsCapped(exact, 1024, 1u << 20, out2);
    CHECK(out2.rows.size() == 1024);
    CHECK_FALSE(o2.rowCap);
    CHECK(o2.exhausted);

    // Fewer than the cap, and an empty result.
    CountingSource small(3, 8);
    fl::DbRows out3;
    CHECK(readRowsCapped(small, 1024, 1u << 20, out3).exhausted);
    CHECK(out3.rows.size() == 3);
    CountingSource none(0, 8);
    fl::DbRows out4;
    CHECK(readRowsCapped(none, 1024, 1u << 20, out4).exhausted);
    CHECK(out4.rows.empty());
    CHECK(out4.columns.size() == 3);
}

TEST_CASE("db: the byte cap stops the stream at the first cell that passes it")
{
    CountingSource big(1000, 100'000);               // 100 kB per row, 1 MiB cap: the 11th row crosses it
    fl::DbRows out;
    ReadOutcome o = readRowsCapped(big, 1024, 1u << 20, out);
    CHECK(o.tooLarge);
    CHECK(out.tooLarge);
    CHECK(big.pulled() == 11);                       // not 1000
    size_t bytes = 0;
    for (auto const& r : out.rows)
        bytes += std::get<std::string>(r[1]).size();
    CHECK(bytes == (1u << 20));                      // the last cell is cut to the cap
    CHECK(out.maxBytes == (1u << 20));

    // No cap: everything is read.
    CountingSource free(50, 100'000);
    fl::DbRows all;
    readRowsCapped(free, 0, 0, all);
    CHECK(all.rows.size() == 50);
}

TEST_CASE("db: a lost connection backs off 1 s, 2 s, 4 s ... up to a minute and a success resets it")
{
    ReconnectPolicy p(1000, 60000);
    CHECK(p.mayAttempt(0));                          // the first attempt is always allowed
    p.failed(0);
    CHECK_FALSE(p.mayAttempt(999));
    CHECK(p.mayAttempt(1000));
    p.failed(1000);
    CHECK(p.retryAtMs() == 3000);                    // doubled
    CHECK_FALSE(p.mayAttempt(2999));
    p.failed(3000);
    CHECK(p.retryAtMs() == 7000);
    for (int i = 0; i < 20; ++i)
        p.failed(p.retryAtMs());
    CHECK(p.retryAtMs() - p.retryAtMs() == 0);
    uint64_t const before = p.retryAtMs();
    p.failed(before);
    CHECK(p.retryAtMs() - before == 60000);          // capped
    CHECK(p.failures() > 20);
    p.succeeded();
    CHECK(p.mayAttempt(0));
    CHECK(p.failures() == 0);
    p.failed(100);
    CHECK(p.retryAtMs() == 1100);                    // back to the first delay

    // A deliberate close (a cut stream, a dropped dead connection) reconnects at once.
    p.closedOnPurpose();
    CHECK(p.mayAttempt(0));
}
