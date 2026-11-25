// The only way Lua reaches a database: it builds a SELECT itself from checked parts. At the pinned core,
// MySQLConnection aborts the whole server on an unknown table, an unknown column or a SQL parse error, so Lua never
// gets to send SQL text. Every name is matched against an information_schema snapshot before any text is built.
#pragma once

#include "engine/Host.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace fl
{
// Why a request was refused. The Lua side returns the name (nil, "no_table").
enum class SqlError
{
    None,
    BadDb,      // not world / characters / hotfixes (auth is refused)
    NoSchema,   // the database is allowed but its schema snapshot is not loaded yet (fail closed)
    BadName,    // does not match ^[A-Za-z0-9_]{1,64}$
    NoTable,    // not in the snapshot
    NoColumn,   // not in the snapshot for that table
    BadValue    // wrong kind of value, NaN/inf, NUL byte, too long, bad limit, empty column list
};

char const* sqlErrorName(SqlError e);

using WhereValue = std::variant<int64_t, double, bool, std::string>;

struct SelectRequest
{
    std::string db;
    std::string table;
    std::vector<std::string> columns;
    std::vector<std::pair<std::string, WhereValue>> where;  // equality checks, AND-ed
    int64_t limit = 100;     // 1..500
    std::string order;       // optional column
    bool desc = false;
};

struct SqlBuild
{
    SqlError error = SqlError::None;
    std::string sql;
    size_t maxRows = 0;
};

class SqlGuard
{
public:
    static constexpr int64_t kDefaultLimit = 100;
    static constexpr int64_t kMaxLimit = 500;
    static constexpr size_t kMaxStringValue = 4096;
    static constexpr size_t kMaxColumns = 64;
    static constexpr size_t kMaxWhere = 16;
    static constexpr int kMaxExecutionMs = 500;   // server-side time limit of one SELECT (MySQL optimizer hint)

    explicit SqlGuard(Host& host) : _host(host) {}

    static bool validName(std::string const& name);
    static bool knownDb(std::string const& db);

    // Builds the SQL text, or says why not. Calls Host::schema (once per db, cached) and Host::escape only.
    SqlBuild build(SelectRequest const& req);
    // has(db, table) / has(db, table, column). Error is set for a bad db or bad name.
    bool has(std::string const& db, std::string const& table, std::string const& column, SqlError* error);
    // Forget the cached snapshots (reload).
    void reset() { _snapshots.clear(); }

private:
    std::shared_ptr<SchemaSnapshot const> snapshot(std::string const& db);

    Host& _host;
    std::map<std::string, std::shared_ptr<SchemaSnapshot const>> _snapshots;
};
}  // namespace fl
