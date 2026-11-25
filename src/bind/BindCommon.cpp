#include "Bind.h"

#include "host/Module.h"

namespace flb
{
fl::HandleData const& handleArg(lua_State* L, int idx, unsigned mask)
{
    fl::HandleData const* h = fl::toHandle(L, idx);
    if (!h)
        luaL_typeerror(L, idx, "handle");
    if (!(mask & kindBit(h->kind)))
        luaL_error(L, "this method does not work on a %s handle", fl::kindName(h->kind));
    return *h;
}

void pushString(lua_State* L, std::string const& s) { lua_pushlstring(L, s.data(), s.size()); }

void addHandleMethods(lua_State* L, luaL_Reg const* regs)
{
    luaL_getmetatable(L, "fl.Handle");
    lua_getfield(L, -1, "__index");
    luaL_setfuncs(L, regs, 0);
    lua_pop(L, 2);
}
}  // namespace flb

namespace flh
{
void bindGame(lua_State* L)
{
    flb::bindWorld(L);
    flb::bindMap(L);
    flb::bindObjects(L);
}
}  // namespace flh
