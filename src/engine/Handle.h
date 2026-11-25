// Handle userdata: {kind, guid, map, instance}. Never a pointer. Every method asks the Host again.
#pragma once

#include "engine/Host.h"

struct lua_State;

namespace fl
{
// Creates the metatable "fl.Handle" in the state.
void registerHandleType(lua_State* L);
// Pushes a new handle userdata.
void pushHandle(lua_State* L, HandleData const& data);
// The handle at `idx`, or null when it is not one.
HandleData const* toHandle(lua_State* L, int idx);
// The same for the Eluna class userdata (its metatable carries `__handle = true`).
HandleData const* toHandleAny(lua_State* L, int idx);
char const* kindName(ObjKind kind);
}  // namespace fl
