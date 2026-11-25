// Internal: the parts of the Lua environment.
#pragma once

#include "engine/Engine.h"

struct lua_State;

namespace fl
{
// Opens only the safe standard libraries and removes the unsafe base functions (Sandbox.cpp).
void openSandbox(lua_State* L, Config const& cfg);
// Event, Timer, Cmd, World, Db, Json, Gm, print and require (Api.cpp).
void registerApi(lua_State* L);
// print and require, shared by both modes (Api.cpp).
void registerCommon(lua_State* L);
// Pushes an event argument (Api.cpp).
void pushValue(lua_State* L, EventValue const& v);
}  // namespace fl
