// ElunaQuery (docs: ElunaQuery/index.html): the result of WorldDBQuery/CharDBQuery/AuthDBQuery. Columns are 0-based;
// the cursor starts at the first row; NextRow() moves on and answers false past the last row.
#include "engine/Elu.h"

#include "engine/Safe.h"
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fl
{
namespace
{
char const* const kQueryMeta = "Eluna.Query";

struct QueryData
{
    std::vector<std::string> columns;
    std::vector<std::vector<DbValue>> rows;
    size_t cursor = 0;
};

QueryData* query(lua_State* L) { return static_cast<QueryData*>(luaL_checkudata(L, 1, kQueryMeta)); }

std::vector<DbValue> const* currentRow(lua_State* L)
{
    QueryData* q = query(L);
    if (q->cursor >= q->rows.size())
        return nullptr;
    return &q->rows[q->cursor];
}

DbValue const* cell(lua_State* L, int idx)
{
    std::vector<DbValue> const* row = currentRow(L);
    lua_Integer column = luaL_checkinteger(L, idx);
    if (!row || column < 0 || static_cast<size_t>(column) >= row->size())
        return nullptr;
    return &(*row)[static_cast<size_t>(column)];
}

bool asInt(DbValue const& v, int64_t& out)
{
    if (auto const* i = std::get_if<int64_t>(&v))
    {
        out = *i;
        return true;
    }
    if (auto const* d = std::get_if<double>(&v))
    {
        out = static_cast<int64_t>(*d);
        return true;
    }
    if (auto const* s = std::get_if<std::string>(&v))
    {
        char* end = nullptr;
        long long n = std::strtoll(s->c_str(), &end, 10);
        if (end == s->c_str())
            return false;
        out = n;
        return true;
    }
    return false;
}

bool asDouble(DbValue const& v, double& out)
{
    if (auto const* d = std::get_if<double>(&v))
    {
        out = *d;
        return true;
    }
    if (auto const* i = std::get_if<int64_t>(&v))
    {
        out = static_cast<double>(*i);
        return true;
    }
    if (auto const* s = std::get_if<std::string>(&v))
    {
        char* end = nullptr;
        double n = std::strtod(s->c_str(), &end);
        if (end == s->c_str())
            return false;
        out = n;
        return true;
    }
    return false;
}

// The typed getters read the column as a C integer of that width: an out-of-range value wraps the way the cast does
// (bits 64 = as is). Unsigned results are the non-negative value of that width.
int pushNumeric(lua_State* L, int bits, bool isSigned)
{
    DbValue const* v = cell(L, 2);
    int64_t n = 0;
    if (!v || !asInt(*v, n))
    {
        lua_pushnil(L);
        return 1;
    }
    switch (bits)
    {
        case 8: n = isSigned ? static_cast<int64_t>(static_cast<int8_t>(n)) : static_cast<int64_t>(static_cast<uint8_t>(n)); break;
        case 16: n = isSigned ? static_cast<int64_t>(static_cast<int16_t>(n)) : static_cast<int64_t>(static_cast<uint16_t>(n)); break;
        case 32: n = isSigned ? static_cast<int64_t>(static_cast<int32_t>(n)) : static_cast<int64_t>(static_cast<uint32_t>(n)); break;
        default: break;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(n));
    return 1;
}

int qIsNull(lua_State* L)
{
    DbValue const* v = cell(L, 2);
    lua_pushboolean(L, !v || std::holds_alternative<std::monostate>(*v));
    return 1;
}
int qColumnCount(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(query(L)->columns.size()));
    return 1;
}
int qRowCount(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(query(L)->rows.size()));
    return 1;
}
int qNextRow(lua_State* L)
{
    QueryData* q = query(L);
    if (q->rows.empty() || q->cursor + 1 >= q->rows.size())
    {
        q->cursor = q->rows.size();
        lua_pushboolean(L, 0);
        return 1;
    }
    ++q->cursor;
    lua_pushboolean(L, 1);
    return 1;
}
int qGetRow(lua_State* L)
{
    QueryData* q = query(L);
    std::vector<DbValue> const* row = currentRow(L);
    if (!row)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, static_cast<int>(row->size()), static_cast<int>(q->columns.size()));
    for (size_t i = 0; i < row->size(); ++i)
    {
        DbValue const& v = (*row)[i];
        bool const isNull = std::holds_alternative<std::monostate>(v);
        if (auto const* s = std::get_if<std::string>(&v))
            lua_pushlstring(L, s->data(), s->size());
        else if (auto const* n = std::get_if<int64_t>(&v))
            lua_pushinteger(L, static_cast<lua_Integer>(*n));
        else if (auto const* d = std::get_if<double>(&v))
            lua_pushnumber(L, *d);
        else
            lua_pushnil(L);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        if (i < q->columns.size())
        {
            if (isNull)
                lua_pushnil(L);
            else if (auto const* s = std::get_if<std::string>(&v))
                lua_pushlstring(L, s->data(), s->size());
            else if (auto const* n = std::get_if<int64_t>(&v))
                lua_pushinteger(L, static_cast<lua_Integer>(*n));
            else
                lua_pushnumber(L, std::get<double>(v));
            lua_setfield(L, -2, q->columns[i].c_str());
        }
    }
    return 1;
}
int qGetBool(lua_State* L)
{
    DbValue const* v = cell(L, 2);
    if (!v || std::holds_alternative<std::monostate>(*v))
    {
        lua_pushnil(L);
        return 1;
    }
    int64_t n = 0;
    double d = 0;
    bool b = false;
    if (asInt(*v, n))
        b = n != 0;
    else if (asDouble(*v, d))
        b = d != 0;
    else if (auto const* s = std::get_if<std::string>(v))
        b = !s->empty() && *s != "0";
    lua_pushboolean(L, b);
    return 1;
}
int qGetUInt8(lua_State* L) { return pushNumeric(L, 8, false); }
int qGetUInt16(lua_State* L) { return pushNumeric(L, 16, false); }
int qGetUInt32(lua_State* L) { return pushNumeric(L, 32, false); }
int qGetUInt64(lua_State* L) { return pushNumeric(L, 64, false); }
int qGetInt8(lua_State* L) { return pushNumeric(L, 8, true); }
int qGetInt16(lua_State* L) { return pushNumeric(L, 16, true); }
int qGetInt32(lua_State* L) { return pushNumeric(L, 32, true); }
int qGetInt64(lua_State* L) { return pushNumeric(L, 64, true); }
int qGetFloat(lua_State* L)
{
    DbValue const* v = cell(L, 2);
    double d = 0;
    if (!v || !asDouble(*v, d))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(static_cast<float>(d)));
    return 1;
}
int qGetDouble(lua_State* L)
{
    DbValue const* v = cell(L, 2);
    double d = 0;
    if (!v || !asDouble(*v, d))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(d));
    return 1;
}
int qGetString(lua_State* L)
{
    DbValue const* v = cell(L, 2);
    if (!v || std::holds_alternative<std::monostate>(*v))
    {
        lua_pushnil(L);
        return 1;
    }
    if (auto const* s = std::get_if<std::string>(v))
    {
        lua_pushlstring(L, s->data(), s->size());
        return 1;
    }
    int64_t n = 0;
    if (auto const* i = std::get_if<int64_t>(v); i && asInt(*v, n))
    {
        std::string s = std::to_string(n);
        lua_pushlstring(L, s.data(), s.size());
        return 1;
    }
    if (auto const* d = std::get_if<double>(v))
    {
        lua_pushnumber(L, *d);
        lua_tolstring(L, -1, nullptr);   // converts the number on the stack into its string
    }
    else
        lua_pushnil(L);
    return 1;
}

int qNewIndex(lua_State* L)
{
    return luaL_error(L, "ElunaQuery is read-only");
}

int qGc(lua_State* L)
{
    static_cast<QueryData*>(lua_touserdata(L, 1))->~QueryData();
    return 0;
}
}  // namespace

void eluPushQuery(lua_State* L, DbRows rows)
{
    void* ud = lua_newuserdatauv(L, sizeof(QueryData), 0);
    QueryData* q = new (ud) QueryData();
    q->columns = std::move(rows.columns);
    q->rows = std::move(rows.rows);
    luaL_setmetatable(L, kQueryMeta);
}

void registerQueryMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"IsNull", safe<qIsNull>},           {"GetColumnCount", safe<qColumnCount>},
        {"GetRowCount", safe<qRowCount>},    {"NextRow", safe<qNextRow>},
        {"GetRow", safe<qGetRow>},           {"GetBool", safe<qGetBool>},
        {"GetUInt8", safe<qGetUInt8>},       {"GetUInt16", safe<qGetUInt16>},
        {"GetUInt32", safe<qGetUInt32>},     {"GetUInt64", safe<qGetUInt64>},
        {"GetInt8", safe<qGetInt8>},         {"GetInt16", safe<qGetInt16>},
        {"GetInt32", safe<qGetInt32>},       {"GetInt64", safe<qGetInt64>},
        {"GetFloat", safe<qGetFloat>},       {"GetDouble", safe<qGetDouble>},
        {"GetString", safe<qGetString>},     {nullptr, nullptr}};

    // The methods live in the ElunaQuery class table, so a script may extend nothing but call them.
    flu::addMethods(L, "ElunaQuery", regs);
    luaL_newmetatable(L, kQueryMeta);
    lua_pushliteral(L, "ElunaQuery");
    lua_setfield(L, -2, "__name");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_pushcfunction(L, safe<qGc>);
    lua_setfield(L, -2, "__gc");
    lua_getfield(L, LUA_REGISTRYINDEX, "Eluna.class.ElunaQuery");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, safe<qNewIndex>);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);
}
}  // namespace fl
