// Small text helpers shared by the host side and the tests (no game types, no Lua).
#pragma once

#include "engine/Engine.h"

#include <string>
#include <string_view>
#include <vector>

namespace fl
{
// "a  b c" -> {"a", "b", "c"} (config lists such as ForeverLua.CommandRoots).
std::vector<std::string> splitWords(std::string const& s);
// Appends the non-empty lines of a captured chat/CLI message (a message may hold newlines and a trailing \r).
void splitLines(std::string_view text, std::vector<std::string>& out);
// The reply lines of a script load: "loaded N file(s): a.lua, b.lua", then one line per error (and the folder warning).
std::vector<std::string> describeLoad(LoadReport const& r);
}  // namespace fl
