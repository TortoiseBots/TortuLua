// The one wrapper every binding uses: a C++ exception from the game code becomes an ordinary Lua error AFTER the try
// block has been left. Only std::exception is caught: Lua's own unwinding (a different type, Lua is built as C++) must
// pass through untouched, so a catch-all is forbidden anywhere near a lua_* call.
#pragma once

#include <lauxlib.h>
#include <lua.h>

#include <exception>
#include <string>

namespace fl
{
template <int (*F)(lua_State*)>
int safe(lua_State* L)
{
    std::string err;
    try
    {
        return F(L);
    }
    catch (std::exception const& e)
    {
        err = e.what();
        if (err.empty())
            err = "internal error";
    }
    return luaL_error(L, "%s", err.c_str());
}
}  // namespace fl
