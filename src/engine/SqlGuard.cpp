#include "engine/SqlGuard.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace fl
{
void SchemaSnapshot::addTable(std::string const& table, std::vector<std::string> const& columns)
{
    auto& t = tables[table];
    for (auto const& c : columns)
    {
        std::string lower = c;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        t[lower] = c;
    }
}

std::string SchemaSnapshot::column(std::string const& table, std::string const& col) const
{
    auto it = tables.find(table);
    if (it == tables.end())
        return {};
    std::string lower = col;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    auto c = it->second.find(lower);
    return c == it->second.end() ? std::string() : c->second;
}

char const* sqlErrorName(SqlError e)
{
    switch (e)
    {
        case SqlError::None: return "ok";
        case SqlError::BadDb: return "bad_db";
        case SqlError::NoSchema: return "no_schema";
        case SqlError::BadName: return "bad_name";
        case SqlError::NoTable: return "no_table";
        case SqlError::NoColumn: return "no_column";
        case SqlError::BadValue: return "bad_value";
    }
    return "bad_value";
}

bool SqlGuard::validName(std::string const& name)
{
    if (name.empty() || name.size() > 64)
        return false;
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

bool SqlGuard::knownDb(std::string const& db)
{
    // "auth" is not selectable: it holds the account salts, verifiers and session keys (security review R1, 2026-11-20).
    return db == "world" || db == "characters" || db == "hotfixes";
}

std::shared_ptr<SchemaSnapshot const> SqlGuard::snapshot(std::string const& db)
{
    auto it = _snapshots.find(db);
    if (it != _snapshots.end())
        return it->second;
    auto snap = _host.schema(db);
    if (snap)
        _snapshots[db] = snap;
    return snap;
}

namespace
{
// The escaped text must not contain a quote that could end the string literal: every ' has to be escaped by a
// backslash or doubled. A host whose escape misbehaves makes the request fail instead of reaching the database.
bool escapedIsSafe(std::string const& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\')
        {
            if (i + 1 >= s.size())
                return false;  // a trailing backslash would escape our closing quote
            ++i;
        }
        else if (s[i] == '\'')
        {
            if (i + 1 < s.size() && s[i + 1] == '\'')
                ++i;
            else
                return false;
        }
    }
    return true;
}
}  // namespace

SqlBuild SqlGuard::build(SelectRequest const& req)
{
    SqlBuild r;
    auto fail = [&](SqlError e) { r.error = e; r.sql.clear(); return r; };

    // Syntax checks first: nothing here needs the database.
    if (!knownDb(req.db))
        return fail(SqlError::BadDb);
    if (!validName(req.table))
        return fail(SqlError::BadName);
    if (req.columns.empty() || req.columns.size() > kMaxColumns)
        return fail(SqlError::BadValue);
    for (auto const& c : req.columns)
        if (!validName(c))
            return fail(SqlError::BadName);
    if (req.where.size() > kMaxWhere)
        return fail(SqlError::BadValue);
    for (auto const& w : req.where)
        if (!validName(w.first))
            return fail(SqlError::BadName);
    if (!req.order.empty() && !validName(req.order))
        return fail(SqlError::BadName);
    if (req.limit < 1)
        return fail(SqlError::BadValue);
    int64_t limit = std::min(req.limit, kMaxLimit);

    // Values.
    for (auto const& w : req.where)
    {
        if (auto const* d = std::get_if<double>(&w.second))
            if (!std::isfinite(*d))
                return fail(SqlError::BadValue);
        if (auto const* s = std::get_if<std::string>(&w.second))
            if (s->size() > kMaxStringValue || s->find('\0') != std::string::npos)
                return fail(SqlError::BadValue);
    }

    // Names against the snapshot.
    auto snap = snapshot(req.db);
    if (!snap)
        return fail(SqlError::NoSchema);
    if (!snap->hasTable(req.table))
        return fail(SqlError::NoTable);

    std::string sql = "SELECT /*+ MAX_EXECUTION_TIME(" + std::to_string(kMaxExecutionMs) + ") */ ";
    bool first = true;
    for (auto const& c : req.columns)
    {
        std::string canon = snap->column(req.table, c);
        if (canon.empty())
            return fail(SqlError::NoColumn);
        if (!first)
            sql += ",";
        first = false;
        sql += "`" + canon + "`";
    }
    sql += " FROM `" + req.table + "`";

    if (!req.where.empty())
    {
        // Sorted by column name so the text does not depend on Lua's table order.
        std::vector<std::pair<std::string, WhereValue const*>> conds;
        for (auto const& w : req.where)
        {
            std::string canon = snap->column(req.table, w.first);
            if (canon.empty())
                return fail(SqlError::NoColumn);
            conds.push_back({canon, &w.second});
        }
        std::stable_sort(conds.begin(), conds.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        sql += " WHERE ";
        for (size_t i = 0; i < conds.size(); ++i)
        {
            if (i)
                sql += " AND ";
            sql += "`" + conds[i].first + "` = ";
            WhereValue const& v = *conds[i].second;
            if (auto const* n = std::get_if<int64_t>(&v))
                sql += std::to_string(*n);
            else if (auto const* d = std::get_if<double>(&v))
            {
                char buf[40];
                std::snprintf(buf, sizeof buf, "%.17g", *d);
                sql += buf;
            }
            else if (auto const* b = std::get_if<bool>(&v))
                sql += *b ? "1" : "0";
            else
            {
                std::string esc = _host.escape(req.db, std::get<std::string>(v));
                if (!escapedIsSafe(esc))
                    return fail(SqlError::BadValue);
                sql += "'" + esc + "'";
            }
        }
    }

    if (!req.order.empty())
    {
        std::string canon = snap->column(req.table, req.order);
        if (canon.empty())
            return fail(SqlError::NoColumn);
        sql += " ORDER BY `" + canon + "`" + (req.desc ? " DESC" : " ASC");
    }
    sql += " LIMIT " + std::to_string(limit);

    r.sql = std::move(sql);
    r.maxRows = static_cast<size_t>(limit);
    return r;
}

bool SqlGuard::has(std::string const& db, std::string const& table, std::string const& column, SqlError* error)
{
    if (error)
        *error = SqlError::None;
    if (!knownDb(db))
    {
        if (error) *error = SqlError::BadDb;
        return false;
    }
    if (!validName(table) || (!column.empty() && !validName(column)))
    {
        if (error) *error = SqlError::BadName;
        return false;
    }
    auto snap = snapshot(db);
    if (!snap)
    {
        if (error) *error = SqlError::NoSchema;
        return false;
    }
    if (!snap->hasTable(table))
        return false;
    return column.empty() || !snap->column(table, column).empty();
}
}  // namespace fl
