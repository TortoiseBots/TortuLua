// ForeverLua bindings (game side): helpers shared by the binding files. Every binding reads its arguments, does the
// game work inside the std::exception-only wrapper `safe<>` and pushes the results. A catch-all is forbidden here: it
// would swallow Lua's own unwinding (Lua is built as C++, lua_error throws).
#pragma once

#include "engine/Engine.h"
#include "engine/Handle.h"
#include "engine/Safe.h"
#include "host/GameAccess.h"

#include <lauxlib.h>
#include <lua.h>

#include <exception>
#include <string>

namespace flb
{
using fl::safe;

constexpr unsigned kindBit(fl::ObjKind k) { return 1u << unsigned(k); }
constexpr unsigned kUnit = kindBit(fl::ObjKind::Player) | kindBit(fl::ObjKind::Creature);
constexpr unsigned kPlayer = kindBit(fl::ObjKind::Player);
constexpr unsigned kCreature = kindBit(fl::ObjKind::Creature);
constexpr unsigned kGameObject = kindBit(fl::ObjKind::GameObject);
constexpr unsigned kMapKind = kindBit(fl::ObjKind::Map);
constexpr unsigned kObject = kUnit | kGameObject;

// The handle at `idx`; raises a Lua error when it is not a handle or its kind is not in `mask`.
fl::HandleData const& handleArg(lua_State* L, int idx, unsigned mask);

inline fl::Engine& eng(lua_State* L) { return *fl::Engine::from(L); }

// Pushes `nil` (a read on a gone object).
inline int pushNil(lua_State* L)
{
    lua_pushnil(L);
    return 1;
}
// Pushes `false, "gone"` (an action on a gone object).
inline int pushGone(lua_State* L)
{
    lua_pushboolean(L, 0);
    lua_pushliteral(L, "gone");
    return 2;
}

void pushString(lua_State* L, std::string const& s);

// Adds the game functions to the engine's tables and to the handle type (Bind*.cpp).
void bindWorld(lua_State* L);
void bindMap(lua_State* L);
void bindObjects(lua_State* L);

// Registers `regs` into the "__index" table of the handle metatable.
void addHandleMethods(lua_State* L, luaL_Reg const* regs);
}  // namespace flb
