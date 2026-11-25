// A small JSON codec over the Lua stack (Json.encode / Json.decode, for the TTJSON replies).
// Both functions are plain C++ and may throw through Lua only via lua_* calls (memory errors).
#pragma once

#include <string>

struct lua_State;

namespace fl
{
constexpr int kJsonMaxDepth = 32;
constexpr size_t kJsonMaxOutput = 4u << 20;   // encoded text over this is refused (a shared table can blow up exponentially)
constexpr size_t kJsonMaxInput = 4 * 1024 * 1024;

// Encodes the value at `idx`. Objects are written with sorted keys. A table whose keys are exactly 1..n is an array;
// an empty table is {}. Json.null (a light userdata NULL) is null. Returns false and sets `error` for a function,
// userdata, thread, NaN/inf, a non-string key, invalid UTF-8, or nesting deeper than kJsonMaxDepth (also cycles).
bool jsonEncode(lua_State* L, int idx, std::string& out, std::string& error);

// Decodes `text` and pushes one value (null becomes Json.null). Strict RFC 8259 grammar; integers without fraction or
// exponent that fit int64 become Lua integers. On failure pushes nothing and sets `error`.
bool jsonDecode(lua_State* L, std::string const& text, std::string& error);
}  // namespace fl
