#include "engine/Api.h"
#include "engine/Engine.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cstring>
#include <string>
#include <vector>

namespace fl
{
namespace
{
constexpr size_t kMaxChunkBytes = 1u << 20;      // a text chunk given to load()
constexpr size_t kMaxRepBytes = 4u << 20;        // the result of string.rep
constexpr size_t kMaxMatchSubject = 1u << 20;    // bytes of subject a pattern function may scan
constexpr size_t kMaxPatternBytes = 512;         // bytes of a pattern
constexpr lua_Integer kMaxMoveCount = 1'000'000; // elements table.move copies
constexpr lua_Integer kMaxSortCount = 100'000;   // elements table.sort sorts and table.insert/remove shift
constexpr lua_Integer kMaxGcStepKb = 64;         // KB of work one collectgarbage("step", n) may ask for

// load(chunk [, chunkname [, mode [, env]]]) with the mode forced to "t": text only, never bytecode.
int safeLoad(lua_State* L)
{
    int n = lua_gettop(L);
    luaL_checkany(L, 1);
    if (lua_type(L, 1) == LUA_TSTRING && lua_rawlen(L, 1) > kMaxChunkBytes)
    {
        lua_pushnil(L);
        lua_pushliteral(L, "chunk too large");
        return 2;
    }
    lua_pushvalue(L, lua_upvalueindex(1));  // the original load
    lua_pushvalue(L, 1);
    if (n >= 2 && lua_type(L, 2) == LUA_TSTRING && Engine::from(L)->isScriptChunk(lua_tostring(L, 2)))
    {
        // A chunk a script builds with load() may not wear the name of a file the loader loaded: that name decides the
        // ForeverData namespace and appears in error messages. It keeps the text, behind a marker.
        lua_pushfstring(L, "=(load) %s", lua_tostring(L, 2));
    }
    else if (n >= 2)
        lua_pushvalue(L, 2);
    else
        lua_pushnil(L);
    lua_pushliteral(L, "t");
    int args = 3;
    if (n >= 4)
    {
        lua_pushvalue(L, 4);
        args = 4;
    }
    lua_call(L, args, LUA_MULTRET);
    return lua_gettop(L) - n;
}

// setmetatable(t, mt): refuses a metatable with a __gc key. A finalizer runs with the hooks off, so no budget would apply
// to it, and lua_close runs every pending one. (A __gc added to mt after the call does not mark the object in Lua 5.4.)
int safeSetmetatable(lua_State* L)
{
    int t = lua_type(L, 2);
    luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
    luaL_checktype(L, 1, LUA_TTABLE);
    if (t == LUA_TTABLE)
    {
        lua_pushliteral(L, "__gc");
        if (lua_rawget(L, 2) != LUA_TNIL)
            return luaL_error(L, "__gc is not allowed");
        lua_pop(L, 1);
    }
    lua_settop(L, 2);
    lua_pushvalue(L, lua_upvalueindex(1));  // the original setmetatable (it honours __metatable)
    lua_insert(L, 1);
    lua_call(L, 2, 1);
    return 1;
}

// collectgarbage: only "count" and "step". "stop" would starve every later allocation near the cap, and a full
// "collect" on a big heap is an uncancellable C call.
int safeCollectgarbage(lua_State* L)
{
    char const* opt = luaL_optstring(L, 1, "collect");
    if (std::strcmp(opt, "count") != 0 && std::strcmp(opt, "step") != 0)
        return luaL_error(L, "collectgarbage: only \"count\" and \"step\" are available");
    if (std::strcmp(opt, "step") == 0 && !lua_isnoneornil(L, 2))
    {
        // "step" with a huge argument is a whole cycle, i.e. the C call "collect" that is refused above: clamp the KB
        // of work one call may ask for (review N7).
        lua_Integer kb = luaL_checkinteger(L, 2);
        if (kb < 0)
            kb = 0;
        else if (kb > kMaxGcStepKb)
            kb = kMaxGcStepKb;
        lua_pushinteger(L, kb);
        lua_replace(L, 2);
    }
    int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= n; ++i)
        lua_pushvalue(L, i);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L) - n;
}

// string.rep with a cap on the result. An empty result returns at once (the original loops n times doing nothing).
int safeRep(lua_State* L)
{
    size_t l, lsep;
    luaL_checklstring(L, 1, &l);
    lua_Integer n = luaL_checkinteger(L, 2);
    luaL_optlstring(L, 3, "", &lsep);
    if (n <= 0 || l + lsep == 0)
    {
        lua_pushliteral(L, "");
        return 1;
    }
    if (static_cast<double>(n) * static_cast<double>(l + lsep) > static_cast<double>(kMaxRepBytes))
        return luaL_error(L, "resulting string too large");
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= top; ++i)
        lua_pushvalue(L, i);
    lua_call(L, top, 1);
    return 1;
}

// string.find/match/gmatch/gsub: subject and pattern are capped before the call. The per-character step counter
// (patch 0001) bounds the C loops, but every step still walks a subject byte: a 100 MB subject would keep one call
// busy for seconds even when every step is charged (review N2).
int safePattern(lua_State* L)
{
    size_t n = 0, pn = 0;
    luaL_checklstring(L, 1, &n);
    luaL_checklstring(L, 2, &pn);
    if (n > kMaxMatchSubject)
        return luaL_error(L, "subject too large for a pattern (max %d bytes)", static_cast<int>(kMaxMatchSubject));
    if (pn > kMaxPatternBytes)
        return luaL_error(L, "pattern too long (max %d bytes)", static_cast<int>(kMaxPatternBytes));
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));  // the original function
    lua_insert(L, 1);
    lua_call(L, top, LUA_MULTRET);
    return lua_gettop(L);  // the call consumed the function and the arguments: what is left is the result
}

// table.insert / table.remove: the loop bound is the length, and the length can come from a __len metamethod the
// script writes. Use the raw length, refuse a __len that disagrees with it, and cap the shift (review N2).
void checkRawLength(lua_State* L)
{
    // A table-like proxy (__index/__newindex/__len on a userdata) has no raw length to bound the loop: refused.
    luaL_argexpected(L, lua_istable(L, 1), 1, "table");
    lua_Integer raw = static_cast<lua_Integer>(lua_rawlen(L, 1));
    lua_Integer n = luaL_len(L, 1);  // honours __len
    if (n != raw)
        luaL_error(L, "table length is not its raw length (__len %I, raw length %I)", n, raw);
    if (raw > kMaxSortCount)
        luaL_error(L, "too many elements (%I)", raw);
}

int safeInsert(lua_State* L)
{
    checkRawLength(L);
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, top, 0);
    return 0;
}

int safeRemove(lua_State* L)
{
    checkRawLength(L);
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, top, 1);
    return 1;
}

// table.move: the range is capped (moving nils costs time but no memory, so the memory cap never stops it).
int safeMove(lua_State* L)
{
    lua_Integer f = luaL_checkinteger(L, 2);
    lua_Integer e = luaL_checkinteger(L, 3);
    luaL_checkinteger(L, 4);
    if (e >= f && static_cast<double>(e) - static_cast<double>(f) >= static_cast<double>(kMaxMoveCount))
        return luaL_error(L, "too many elements to move");
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= top; ++i)
        lua_pushvalue(L, i);
    lua_call(L, top, 1);
    return 1;
}

// table.sort: a C sort of a huge array cannot be interrupted.
int safeSort(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    if (luaL_len(L, 1) > kMaxSortCount)
        return luaL_error(L, "too many elements to sort");
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= top; ++i)
        lua_pushvalue(L, i);
    lua_call(L, top, 0);
    return 0;
}

// Replaces table[name] by a C closure around the original function.
void wrapField(lua_State* L, char const* table, char const* name, lua_CFunction wrapper)
{
    lua_getglobal(L, table);
    lua_getfield(L, -1, name);
    lua_pushcclosure(L, wrapper, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

void wrapGlobal(lua_State* L, char const* name, lua_CFunction wrapper)
{
    lua_getglobal(L, name);
    lua_pushcclosure(L, wrapper, 1);
    lua_setglobal(L, name);
}

void open(lua_State* L, char const* name, lua_CFunction f)
{
    luaL_requiref(L, name, f, 1);
    lua_pop(L, 1);
}
}  // namespace

void openSandbox(lua_State* L, Config const& cfg)
{
    open(L, LUA_GNAME, luaopen_base);
    open(L, LUA_STRLIBNAME, luaopen_string);
    open(L, LUA_TABLIBNAME, luaopen_table);
    open(L, LUA_MATHLIBNAME, luaopen_math);
    open(L, LUA_UTF8LIBNAME, luaopen_utf8);
    open(L, LUA_COLIBNAME, luaopen_coroutine);
    // The unsafe libraries stay closed unless the host asks for them (a test, never a production server). Eluna mode
    // offers them as one switch (ForeverLua.OpenStdLibs): eluna mode trusts its scripts.
    bool const stdLibs = cfg.mode == EngineMode::Eluna && cfg.openStdLibs;
    if (cfg.allowIo || stdLibs)
        open(L, LUA_IOLIBNAME, luaopen_io);
    if (cfg.allowOs || stdLibs)
        open(L, LUA_OSLIBNAME, luaopen_os);
    if (cfg.allowDebug || stdLibs)
        open(L, LUA_DBLIBNAME, luaopen_debug);
    if (cfg.allowPackage || stdLibs)
        open(L, LUA_LOADLIBNAME, luaopen_package);

    if (!stdLibs)
    {
        // No file access from the base library.
        lua_pushnil(L);
        lua_setglobal(L, "dofile");
        lua_pushnil(L);
        lua_setglobal(L, "loadfile");

        // No bytecode: string.dump is gone and load() takes text only.
        lua_getglobal(L, LUA_STRLIBNAME);
        lua_pushnil(L);
        lua_setfield(L, -2, "dump");
        lua_pop(L, 1);
    }
    wrapGlobal(L, "load", safeLoad);
    wrapGlobal(L, "setmetatable", safeSetmetatable);
    wrapGlobal(L, "collectgarbage", safeCollectgarbage);
    wrapField(L, LUA_STRLIBNAME, "rep", safeRep);
    wrapField(L, LUA_STRLIBNAME, "find", safePattern);
    wrapField(L, LUA_STRLIBNAME, "match", safePattern);
    wrapField(L, LUA_STRLIBNAME, "gmatch", safePattern);
    wrapField(L, LUA_STRLIBNAME, "gsub", safePattern);
    wrapField(L, LUA_TABLIBNAME, "move", safeMove);
    wrapField(L, LUA_TABLIBNAME, "sort", safeSort);
    wrapField(L, LUA_TABLIBNAME, "insert", safeInsert);
    wrapField(L, LUA_TABLIBNAME, "remove", safeRemove);

    if (stdLibs)
    {
        // package.path over the same folders require() resolves in (ScriptPath first, then RequirePath).
        std::string path;
        std::vector<std::string> roots;
        roots.push_back(cfg.scriptPath);
        for (auto const& r : cfg.requirePath)
            if (!r.empty())
                roots.push_back(r);
        for (auto const& r : roots)
        {
            if (!path.empty())
                path += ';';
            path += r + "/?.lua;" + r + "/?/init.lua";
        }
        lua_getglobal(L, "package");
        if (lua_istable(L, -1))
        {
            lua_pushlstring(L, path.data(), path.size());
            lua_setfield(L, -2, "path");
        }
        lua_pop(L, 1);
    }

    // The string metatable (shared by every script) cannot be read or replaced.
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1))
    {
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}
}  // namespace fl
