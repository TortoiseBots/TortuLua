#include "engine/Api.h"

#include "engine/Handle.h"
#include "engine/Json.h"
#include "engine/Safe.h"
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fl
{
namespace
{
Engine& eng(lua_State* L) { return *Engine::from(L); }

bool validEventName(char const* s, size_t n)
{
    if (n == 0 || n > 64)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
            return false;
    }
    return true;
}

// ---- Event ----
int eventOn(lua_State* L)
{
    size_t n;
    char const* name = luaL_checklstring(L, 1, &n);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    luaL_argcheck(L, validEventName(name, n), 1, "bad event name");
    Engine::HandlerFilter f;
    if (!lua_isnoneornil(L, 3))
    {
        luaL_checktype(L, 3, LUA_TTABLE);
        lua_getfield(L, 3, "entry");
        if (!lua_isnil(L, -1))
        {
            int isnum;
            lua_Integer e = lua_tointegerx(L, -1, &isnum);
            luaL_argcheck(L, isnum && e >= 0 && e <= 0xFFFFFFFFll, 3, "filter.entry must be a non-negative integer");
            f.hasEntry = true;
            f.entry = static_cast<uint32_t>(e);
        }
        lua_pop(L, 1);
        lua_getfield(L, 3, "bots");
        if (!lua_isnil(L, -1))
        {
            luaL_argcheck(L, lua_isboolean(L, -1), 3, "filter.bots must be a boolean");
            f.bots = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id = eng(L).addHandler(std::string(name, n), ref, f);
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "too many event handlers");
    }
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

int eventOff(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    lua_pushboolean(L, id > 0 && eng(L).removeHandler(static_cast<uint64_t>(id)));
    return 1;
}

// ---- Timer ----
int timerAdd(lua_State* L, bool repeat)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_Integer count = 0;
    if (repeat && !lua_isnoneornil(L, 3))
    {
        count = luaL_checkinteger(L, 3);
        luaL_argcheck(L, count >= 1, 3, "count must be at least 1");
    }
    luaL_argcheck(L, ms >= 0, 1, "delay must not be negative");
    uint64_t d = static_cast<uint64_t>(ms);
    uint64_t minMs = eng(L).config().minTimerMs;
    if (d < minMs)
        d = minMs;
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id = eng(L).addTimer(ref, d, repeat ? d : 0, static_cast<uint64_t>(count));
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "too many timers");
    }
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}
int timerAfter(lua_State* L) { return timerAdd(L, false); }
int timerEvery(lua_State* L) { return timerAdd(L, true); }
int timerCancel(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    lua_pushboolean(L, id > 0 && eng(L).cancelTimer(static_cast<uint64_t>(id)));
    return 1;
}

// ---- Cmd ----
int cmdRegister(lua_State* L)
{
    size_t n;
    char const* root = luaL_checklstring(L, 1, &n);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_Integer sec = luaL_checkinteger(L, 3);
    luaL_argcheck(L, sec >= 0 && sec <= 255, 3, "minSecurity must be 0..255");
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (!eng(L).registerCommand(std::string(root, n), ref, static_cast<int>(sec)))
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "root not configured");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ctxReply(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t n;
    char const* s = luaL_checklstring(L, 2, &n);
    eng(L).appendReply(std::string(s, n));
    return 0;
}

// ---- World ----
int worldNow(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(eng(L).host().unixTime()));
    return 1;
}
int worldClock(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(eng(L).host().nowMs()));
    return 1;
}
int worldGameTime(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(eng(L).host().gameTime()));
    return 1;
}
int worldLog(lua_State* L)
{
    char const* names[] = {"debug", "info", "warn", "error", nullptr};
    int lv = luaL_checkoption(L, 1, nullptr, names);
    size_t n;
    char const* s = luaL_checklstring(L, 2, &n);
    eng(L).logLua(static_cast<LogLevel>(lv), std::string(s, n > 4096 ? 4096 : n));
    return 0;
}

// ---- Db ----
int dbRefuse(lua_State* L, SqlError err)
{
    lua_pushnil(L);
    lua_pushstring(L, sqlErrorName(err));
    return 2;
}

int dbSelect(lua_State* L)
{
    // Db.select(db, table, columns [, where [, opts]] , callback): the callback is the last argument and is required.
    // The query runs on the database's asynchronous connection; the callback gets (rows) or (nil, reason) on a later
    // tick, never inside this call. A synchronous SELECT held the world thread through pool waits and slow servers
    // (review, 2026-11-20).
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING || lua_type(L, 3) != LUA_TTABLE)
        return dbRefuse(L, SqlError::BadValue);
    int top = lua_gettop(L);
    if (top < 4 || lua_type(L, top) != LUA_TFUNCTION)
        return luaL_error(L, "Db.select needs a callback function as its last argument");
    int nopt = top - 4;   // where [, opts], both optional
    if (nopt > 2)
        return dbRefuse(L, SqlError::BadValue);
    int idxWhere = nopt >= 1 ? 4 : 0;
    int idxOpts = nopt >= 2 ? 5 : 0;

    SelectRequest req;
    req.db = lua_tostring(L, 1);
    req.table = lua_tostring(L, 2);

    lua_Integer ncols = static_cast<lua_Integer>(lua_rawlen(L, 3));
    if (ncols < 1 || ncols > static_cast<lua_Integer>(SqlGuard::kMaxColumns))
        return dbRefuse(L, SqlError::BadValue);
    for (lua_Integer i = 1; i <= ncols; ++i)
    {
        lua_rawgeti(L, 3, i);
        if (lua_type(L, -1) != LUA_TSTRING)
            return dbRefuse(L, SqlError::BadValue);
        req.columns.emplace_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    if (idxWhere && !lua_isnoneornil(L, idxWhere))
    {
        if (lua_type(L, idxWhere) != LUA_TTABLE)
            return dbRefuse(L, SqlError::BadValue);
        lua_pushnil(L);
        while (lua_next(L, idxWhere))
        {
            if (lua_type(L, -2) != LUA_TSTRING)
                return dbRefuse(L, SqlError::BadName);
            std::string key = lua_tostring(L, -2);
            switch (lua_type(L, -1))
            {
                case LUA_TNUMBER:
                    if (lua_isinteger(L, -1))
                        req.where.emplace_back(key, WhereValue(static_cast<int64_t>(lua_tointeger(L, -1))));
                    else
                        req.where.emplace_back(key, WhereValue(static_cast<double>(lua_tonumber(L, -1))));
                    break;
                case LUA_TBOOLEAN:
                    req.where.emplace_back(key, WhereValue(lua_toboolean(L, -1) != 0));
                    break;
                case LUA_TSTRING:
                {
                    size_t n;
                    char const* str = lua_tolstring(L, -1, &n);
                    req.where.emplace_back(key, WhereValue(std::string(str, n)));
                    break;
                }
                default:
                    return dbRefuse(L, SqlError::BadValue);
            }
            lua_pop(L, 1);
            if (req.where.size() > SqlGuard::kMaxWhere)
                return dbRefuse(L, SqlError::BadValue);
        }
    }

    if (idxOpts && !lua_isnoneornil(L, idxOpts))
    {
        if (lua_type(L, idxOpts) != LUA_TTABLE)
            return dbRefuse(L, SqlError::BadValue);
        lua_getfield(L, idxOpts, "limit");
        if (!lua_isnil(L, -1))
        {
            int isnum;
            lua_Integer lim = lua_tointegerx(L, -1, &isnum);
            if (!isnum)
                return dbRefuse(L, SqlError::BadValue);
            req.limit = lim;
        }
        lua_pop(L, 1);
        lua_getfield(L, idxOpts, "order");
        if (!lua_isnil(L, -1))
        {
            if (lua_type(L, -1) != LUA_TSTRING)
                return dbRefuse(L, SqlError::BadValue);
            req.order = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        lua_getfield(L, idxOpts, "desc");
        if (!lua_isnil(L, -1))
        {
            if (!lua_isboolean(L, -1))
                return dbRefuse(L, SqlError::BadValue);
            req.desc = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    }

    Engine& e = eng(L);
    SqlBuild b = e.sql().build(req);
    if (b.error != SqlError::None)
        return dbRefuse(L, b.error);

    lua_pushvalue(L, top);   // the callback
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string err;
    if (!e.startSelect(req.db, b.sql, b.maxRows, ref, err))
    {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushboolean(L, 1);   // queued; the callback gets (rows) or (nil, reason) on a later tick
    return 1;
}

int dbHas(lua_State* L)
{
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING ||
        (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TSTRING))
        return dbRefuse(L, SqlError::BadValue);
    SqlError err;
    bool has = eng(L).sql().has(lua_tostring(L, 1), lua_tostring(L, 2),
                                lua_isnoneornil(L, 3) ? std::string() : std::string(lua_tostring(L, 3)), &err);
    if (err != SqlError::None)
        return dbRefuse(L, err);
    lua_pushboolean(L, has);
    return 1;
}

// ---- Json ----
int jsonEncodeFn(lua_State* L)
{
    luaL_checkany(L, 1);
    std::string out, err;
    if (!jsonEncode(L, 1, out, err))
    {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}
int jsonDecodeFn(lua_State* L)
{
    size_t n;
    char const* s = luaL_checklstring(L, 1, &n);
    std::string text(s, n), err;
    if (!jsonDecode(L, text, err))
    {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    return 1;
}

// ---- Gm bridge ----
struct BridgeScope
{
    Engine& e;
    explicit BridgeScope(Engine& en) : e(en) { e.enterBridge(); }
    ~BridgeScope() { e.leaveBridge(); }
};

int gmRun(lua_State* L)
{
    size_t n;
    char const* s = luaL_checklstring(L, 1, &n);
    std::string line(s, n);
    Engine& e = eng(L);
    std::vector<std::string> out;
    std::string refusal = bridgeRefusal(line, e.bridgeDeny(), e.forwardedRoots(), e.bridgeDepth());
    bool ok = false;
    if (!refusal.empty())
        out.push_back("refused: " + refusal);
    else
    {
        BridgeScope scope(e);
        ok = e.host().runGmLine(line, out);
    }
    lua_pushboolean(L, ok);
    size_t count = out.size() < 200 ? out.size() : 200;
    lua_createtable(L, static_cast<int>(count), 0);
    for (size_t i = 0; i < count; ++i)
    {
        lua_pushlstring(L, out[i].data(), out[i].size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 2;
}

// ---- print / require ----
int luaPrint(lua_State* L)
{
    int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i)
    {
        size_t len;
        char const* s = luaL_tolstring(L, i, &len);
        if (i > 1)
            line += '\t';
        line.append(s, len);
        lua_pop(L, 1);
        if (line.size() > 4096)
        {
            line.resize(4096);
            break;
        }
    }
    eng(L).logLua(LogLevel::Info, line);
    return 0;
}

char const* const kLoaded = "fl.loaded";
char const* const kLoading = "fl.loading";

int luaRequire(lua_State* L)
{
    size_t n;
    char const* name = luaL_checklstring(L, 1, &n);
    std::string mod(name, n);

    luaL_getsubtable(L, LUA_REGISTRYINDEX, kLoaded);
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1))
        return 1;
    lua_pop(L, 1);
    luaL_getsubtable(L, LUA_REGISTRYINDEX, kLoading);
    lua_getfield(L, -1, name);
    if (lua_toboolean(L, -1))
        return luaL_error(L, "circular require of '%s'", name);
    lua_pop(L, 1);

    std::string err, rel;
    std::string path = eng(L).resolveModuleEx(mod, &rel, err);
    if (path.empty())
        return luaL_error(L, "%s", err.c_str());
    std::error_code sizeEc;
    auto fileSize = std::filesystem::file_size(path, sizeEc);
    if (sizeEc || fileSize > (1u << 20))
        return luaL_error(L, "module '%s' is too large or unreadable", name);
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
    if (src.size() > (1u << 20))
        return luaL_error(L, "module '%s' is too large", name);
    std::string chunk = "@" + (rel.empty() ? mod + ".lua" : rel);
    eng(L).noteScriptChunk(chunk, rel.empty() ? mod + ".lua" : rel);
    if (luaL_loadbufferx(L, src.data(), src.size(), chunk.c_str(), "t") != LUA_OK)
        return lua_error(L);

    lua_pushboolean(L, 1);
    lua_setfield(L, -3, name);  // loading[name] = true  (-3 = the loading table below the chunk and the boolean)
    lua_pushvalue(L, 1);
    int status = lua_pcall(L, 1, 1, 0);
    lua_pushnil(L);
    lua_setfield(L, -3, name);  // loading[name] = nil, also after a failure, so the module can be required again
    if (status != LUA_OK)
        return lua_error(L);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    // stack: loaded, loading, result
    lua_pushvalue(L, -1);
    lua_setfield(L, -4, name);
    return 1;
}

void setFuncs(lua_State* L, char const* table, luaL_Reg const* regs)
{
    lua_newtable(L);
    luaL_setfuncs(L, regs, 0);
    lua_setglobal(L, table);
}
}  // namespace

void pushValue(lua_State* L, EventValue const& v)
{
    if (!lua_checkstack(L, 8))
        luaL_error(L, "stack overflow");
    struct Visitor
    {
        lua_State* L;
        void operator()(std::monostate) const { lua_pushnil(L); }
        void operator()(bool b) const { lua_pushboolean(L, b); }
        void operator()(int64_t i) const { lua_pushinteger(L, static_cast<lua_Integer>(i)); }
        void operator()(double d) const { lua_pushnumber(L, d); }
        void operator()(std::string const& s) const { lua_pushlstring(L, s.data(), s.size()); }
        void operator()(HandleData const& h) const { pushHandle(L, h); }
        void operator()(std::shared_ptr<EventTable> const& t) const
        {
            lua_createtable(L, 0, t ? static_cast<int>(t->fields.size()) : 0);
            if (t)
                for (auto const& f : t->fields)
                {
                    pushValue(L, f.second);
                    lua_setfield(L, -2, f.first.c_str());
                }
        }
        void operator()(std::shared_ptr<EventList> const& l) const
        {
            lua_createtable(L, l ? static_cast<int>(l->items.size()) : 0, 0);
            if (l)
                for (size_t i = 0; i < l->items.size(); ++i)
                {
                    pushValue(L, l->items[i]);
                    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                }
        }
    };
    std::visit(Visitor{L}, v.v);
}

void registerApi(lua_State* L)
{
    static luaL_Reg const event[] = {{"on", safe<eventOn>}, {"off", safe<eventOff>}, {nullptr, nullptr}};
    static luaL_Reg const timer[] = {{"after", safe<timerAfter>}, {"every", safe<timerEvery>},
                                     {"cancel", safe<timerCancel>}, {nullptr, nullptr}};
    static luaL_Reg const cmd[] = {{"register", safe<cmdRegister>}, {nullptr, nullptr}};
    static luaL_Reg const world[] = {{"now", safe<worldNow>}, {"clock", safe<worldClock>},
                                     {"gameTime", safe<worldGameTime>}, {"log", safe<worldLog>}, {nullptr, nullptr}};
    static luaL_Reg const db[] = {{"select", safe<dbSelect>}, {"has", safe<dbHas>}, {nullptr, nullptr}};
    static luaL_Reg const json[] = {{"encode", safe<jsonEncodeFn>}, {"decode", safe<jsonDecodeFn>}, {nullptr, nullptr}};
    static luaL_Reg const gm[] = {{"run", safe<gmRun>}, {nullptr, nullptr}};

    registerHandleType(L);
    setFuncs(L, "Event", event);
    setFuncs(L, "Timer", timer);
    setFuncs(L, "Cmd", cmd);
    setFuncs(L, "World", world);
    setFuncs(L, "Db", db);
    setFuncs(L, "Gm", gm);
    setFuncs(L, "Json", json);
    lua_getglobal(L, "Json");
    lua_pushlightuserdata(L, nullptr);
    lua_setfield(L, -2, "null");
    lua_pop(L, 1);

    // command context metatable: ctx:reply(text)
    luaL_newmetatable(L, "fl.Ctx");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");   // scripts can neither read nor change it
    lua_newtable(L);
    lua_pushcfunction(L, safe<ctxReply>);
    lua_setfield(L, -2, "reply");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    registerCommon(L);
    // Our bot API (B2): the two party-level order calls exist in test mode too and answer `test-mode` (the spec's
    // permissions section): a script shared by both modes gets the reason instead of a nil call. The Bot handle, and so
    // the per-bot order methods, is an Eluna mode class (B1).
    lua_pushcfunction(L, safe<flu::pBotsInterrupt>);
    lua_setglobal(L, "BotsInterrupt");
    lua_pushcfunction(L, safe<flu::pBotCommanderPlace>);
    lua_setglobal(L, "BotCommanderPlace");
}

void registerCommon(lua_State* L)
{
    lua_pushcfunction(L, safe<luaPrint>);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, safe<luaRequire>);
    lua_setglobal(L, "require");
}
}  // namespace fl
