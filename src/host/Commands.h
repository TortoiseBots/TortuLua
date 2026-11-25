// ForeverLua host side: the `.lua` commands and the forwarded roots (CommandScript).
#pragma once

#include "ScriptMgr.h"

class forever_lua_commands : public CommandScript
{
public:
    forever_lua_commands();
    std::span<Trinity::ChatCommands::ChatCommandBuilder const> GetCommands() const override;
};

#include <string>
#include <vector>

namespace flh
{
// The lower-case names at the top of the core's chat command table, read through the core's own auto-completion with a
// handler that sees every command (console-only and player-only ones included). Call it once at start, when the table is
// built and no Lua command has been added.
std::vector<std::string> coreCommandRoots();
}  // namespace flh
