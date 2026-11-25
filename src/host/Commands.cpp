#include "Commands.h"

#include "ForeverHost.h"
#include "engine/Text.h"
#include "GameAccess.h"
#include "Module.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Log.h"
#include "Optional.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

using namespace Trinity::ChatCommands;

namespace
{
constexpr size_t kMaxRoots = 8;

// The names the core's command map was built with for the 32 Lua slots. The core keeps views of these strings, so they
// live here for the whole process; a slot handler passes its name to the engine so a slot whose command changed since the
// map was built (a reload before the map was rebuilt) never runs another command.
std::array<std::string, fl::Engine::kMaxLuaCommands>& luaSlotNames()
{
    static std::array<std::string, fl::Engine::kMaxLuaCommands> names;
    return names;
}

std::vector<std::string>& rootNames()
{
    static std::vector<std::string> roots;
    return roots;
}

int securityOf(ChatHandler* h)
{
    if (h->IsConsole() || !h->GetSession())
        return 255;
    return int(h->GetSession()->GetSecurity());
}

bool adminOk(ChatHandler* h)
{
    if (securityOf(h) >= flh::settings().adminSecurity)
        return true;
    h->SendSysMessage("ForeverLua: your security level is too low for this command.");
    return false;
}

void sendAll(ChatHandler* h, std::vector<std::string> const& lines)
{
    for (auto const& l : lines)
        h->SendSysMessage(l);
}

// A forwarded root hands its whole tail to the Lua function registered with Cmd.register.
bool forward(ChatHandler* h, size_t index, std::string_view tail)
{
    auto const& roots = rootNames();
    if (index >= roots.size())
        return false;
    fl::Engine* e = flh::engine();
    if (!e)
    {
        h->SendSysMessage("ForeverLua is not running.");
        return true;
    }
    std::optional<fl::HandleData> player;
    if (Player* p = h->GetPlayer())
        player = flh::handleFor(p);
    fl::CommandResult r = e->runCommand(roots[index], std::string(tail), securityOf(h), player);
    sendAll(h, r.lines);
    switch (r.status)
    {
        case fl::CommandStatus::Ok:
        case fl::CommandStatus::Error:
            break;
        case fl::CommandStatus::NoHandler:
            h->SendSysMessage("ForeverLua: no Lua handler is registered for ." + roots[index]);
            break;
        case fl::CommandStatus::Denied:
            h->SendSysMessage("ForeverLua: your security level is too low for this command.");
            break;
        case fl::CommandStatus::Refused:
            h->SendSysMessage("ForeverLua: command refused: " + r.error);
            break;
    }
    return true;
}

template <size_t N>
bool HandleRoot(ChatHandler* handler, Tail tail)
{
    return forward(handler, N, tail);
}

bool HandleLuaStatus(ChatHandler* handler)
{
    if (!adminOk(handler))
        return true;
    flh::ModuleSettings const& s = flh::settings();
    fl::Engine* e = flh::engine();
    if (!s.enabled)
    {
        handler->SendSysMessage("ForeverLua: off (ForeverLua.Enable = 0).");
        return true;
    }
    if (!e)
    {
        handler->SendSysMessage("ForeverLua: enabled but the state is not running.");
        return true;
    }
    std::vector<std::string> lines;
    fl::splitLines(e->statusText(), lines);
    // E4: the world state plus the map states. With the default MapStates = 0 the line is absent, so the test-mode
    // output is exactly what it was.
    if (s.engine.mapStates != 0)
        if (fl::StateSet* set = flh::stateSet())
            lines.push_back("states: " + std::to_string(set->mapCount()) + " map, 1 world (MapStates = " +
                            std::to_string(s.engine.mapStates) + "), " + std::to_string(set->droppedEvents()) +
                            " map events dropped (map loading or closing)");
    sendAll(handler, lines);
    return true;
}

bool HandleLuaReload(ChatHandler* handler)
{
    if (!adminOk(handler))
        return true;
    if (!flh::engine())
    {
        handler->SendSysMessage("ForeverLua: not running.");
        return true;
    }
    fl::LoadReport rep = flh::reloadEngine();
    sendAll(handler, fl::describeLoad(rep));
    TC_LOG_INFO("forever.lua", "ForeverLua: reloaded ({} loaded, {} errors)", rep.loaded.size(), rep.errors.size());
    return true;
}

bool HandleLuaErrors(ChatHandler* handler, Optional<std::string_view> arg)
{
    if (!adminOk(handler))
        return true;
    fl::Engine* e = flh::engine();
    if (!e)
    {
        handler->SendSysMessage("ForeverLua: not running.");
        return true;
    }
    if (arg && *arg == "clear")
    {
        e->clearErrorRing();
        handler->SendSysMessage("ForeverLua: the script error ring is empty.");
        return true;
    }
    if (arg && *arg != "show")
    {
        handler->SendSysMessage("Usage: .lua errors [clear]");
        return true;
    }
    std::vector<std::string> lines;
    fl::splitLines(e->errorRingText(), lines);
    sendAll(handler, lines);
    return true;
}

bool HandleLuaRun(ChatHandler* handler, std::string_view name)
{
    if (!adminOk(handler))
        return true;
    fl::Engine* e = flh::engine();
    if (!e)
    {
        handler->SendSysMessage("ForeverLua: not running.");
        return true;
    }
    fl::CallResult r = e->runScript(std::string(name));
    handler->SendSysMessage(r.ok ? "ForeverLua: ran " + std::string(name) + ".lua" : "ForeverLua error: " + r.error);
    return true;
}

// `.lua profile [top N|reset]`: the per-handler calls/time table of E3.
bool HandleLuaProfile(ChatHandler* handler, Optional<std::string_view> what, Optional<uint32> count)
{
    if (!adminOk(handler))
        return true;
    fl::Engine* e = flh::engine();
    if (!e)
    {
        handler->SendSysMessage("ForeverLua: not running.");
        return true;
    }
    if (what && *what == "reset")
    {
        e->resetEluProfile();
        handler->SendSysMessage("ForeverLua: profile counters reset.");
        return true;
    }
    size_t top = 10;
    if (what && *what == "top")
        top = count ? std::min<size_t>(*count, 50) : 10;
    else if (what)
    {
        handler->SendSysMessage("Usage: .lua profile [top N|reset]");
        return true;
    }
    std::vector<fl::HandlerProfile> rows = e->eluProfile();
    std::sort(rows.begin(), rows.end(), [](fl::HandlerProfile const& a, fl::HandlerProfile const& b)
              { return a.totalMs != b.totalMs ? a.totalMs > b.totalMs : a.calls > b.calls; });
    handler->SendSysMessage("ForeverLua: handler profile (calls, total, max, average):");
    size_t shown = 0;
    for (auto const& r : rows)
    {
        if (shown++ >= top)
            break;
        std::string where = r.where.empty() ? std::string("(no script)") : r.where;
        handler->SendSysMessage(where + "  " + fl::eluKindName(r.kind) + " #" + std::to_string(r.event) + "  calls " +
                                std::to_string(r.calls) + ", total " + std::to_string(r.totalMs) + " ms, max " +
                                std::to_string(r.maxMs) + " ms, avg " +
                                std::to_string(r.calls ? r.totalMs / r.calls : 0) + " ms");
    }
    if (rows.empty())
        handler->SendSysMessage("  (no Eluna handler is registered)");
    return true;
}

// `.lua commands`: the RegisterCommand slots of E3.
bool HandleLuaCommands(ChatHandler* handler)
{
    if (!adminOk(handler))
        return true;
    fl::Engine* e = flh::engine();
    if (!e)
    {
        handler->SendSysMessage("ForeverLua: not running.");
        return true;
    }
    std::vector<fl::Engine::LuaCommand> cmds = e->luaCommands();
    handler->SendSysMessage("ForeverLua: " + std::to_string(cmds.size()) + " of 32 Lua commands:");
    for (auto const& c : cmds)
        // Every slot needs the core's GM permission first: a minSecurity below 1 changes nothing.
        handler->SendSysMessage("  ." + c.name + " (security " + std::to_string(std::max(1, c.minSecurity)) + ") " + c.help +
                                (c.where.empty() ? std::string() : "  [" + c.where + "]"));
    return true;
}

// One of the 32 pre-made command slots: the slot's name and handler are rebuilt from the engine's command list every
// time the core rebuilds its command map (Trinity::ChatCommands::InvalidateCommandMap).
template <size_t N>
bool HandleLuaCommand(ChatHandler* handler, Tail tail)
{
    fl::Engine* e = flh::engine();
    if (!e)
    {
        handler->SendSysMessage("ForeverLua is not running.");
        return true;
    }
    std::optional<fl::HandleData> player;
    if (Player* p = handler->GetPlayer())
        player = flh::handleFor(p);
    fl::CommandResult r = e->runLuaCommand(N, std::string(tail), securityOf(handler), player, luaSlotNames()[N]);
    sendAll(handler, r.lines);
    switch (r.status)
    {
        case fl::CommandStatus::Ok:
        case fl::CommandStatus::Error: break;
        case fl::CommandStatus::NoHandler:
            handler->SendSysMessage("ForeverLua: the command is no longer registered.");
            break;
        case fl::CommandStatus::Denied:
            handler->SendSysMessage("ForeverLua: your security level is too low for this command.");
            break;
        case fl::CommandStatus::Refused:
            handler->SendSysMessage("ForeverLua: command refused: " + r.error);
            break;
    }
    return true;
}
}  // namespace

forever_lua_commands::forever_lua_commands() : CommandScript("forever_lua_commands") { }

std::span<ChatCommandBuilder const> forever_lua_commands::GetCommands() const
{
    // Rebuilt on every call: the core clears its command map and asks again when the Lua commands change
    // (Trinity::ChatCommands::InvalidateCommandMap). Names live in fixed storage so no builder view dangles.
    static std::vector<ChatCommandBuilder> table;
    auto& luaNames = luaSlotNames();
    for (auto& n : luaNames)
        n.clear();
    table.clear();
    table.reserve(16 + kMaxRoots + fl::Engine::kMaxLuaCommands);

    static ChatCommandTable luaTable =
    {
        { "status", HandleLuaStatus, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        { "reload", HandleLuaReload, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        { "run",    HandleLuaRun,    rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        { "errors", HandleLuaErrors, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        { "profile", HandleLuaProfile, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        { "commands", HandleLuaCommands, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
    };
    table.emplace_back("lua", std::span<ChatCommandBuilder const>(luaTable));

    // One entry per configured root. A command handler cannot tell which root called it, so each slot is its own
    // function (HandleRoot<N>). Adding a root needs a restart; changing its handler needs only `.lua reload`.
    flh::ModuleSettings const& s = flh::settings();
    if (s.enabled)
    {
        auto& roots = rootNames();
        roots.reserve(kMaxRoots);   // the builders keep views of these strings: no reallocation
        if (roots.empty())
            for (std::string const& r : s.engine.commandRoots)
                if (roots.size() < kMaxRoots)
                    roots.push_back(r);
                else
                    TC_LOG_WARN("forever.lua", "ForeverLua.CommandRoots: only {} roots are supported, '{}' ignored", kMaxRoots, r);
        static std::array<bool(*)(ChatHandler*, Tail), kMaxRoots> const handlers = {
            &HandleRoot<0>, &HandleRoot<1>, &HandleRoot<2>, &HandleRoot<3>,
            &HandleRoot<4>, &HandleRoot<5>, &HandleRoot<6>, &HandleRoot<7>};
        for (size_t i = 0; i < roots.size(); ++i)
            table.emplace_back(roots[i], *handlers[i], rbac::RBAC_PERM_COMMAND_GM, Console::Yes);
    }

    // E3: the RegisterCommand slots. The slot index in the table is the index runLuaCommand uses.
    if (fl::Engine* e = flh::engine())
    {
        static std::array<bool(*)(ChatHandler*, Tail), fl::Engine::kMaxLuaCommands> const luaHandlers = {
            &HandleLuaCommand<0>, &HandleLuaCommand<1>, &HandleLuaCommand<2>, &HandleLuaCommand<3>,
            &HandleLuaCommand<4>, &HandleLuaCommand<5>, &HandleLuaCommand<6>, &HandleLuaCommand<7>,
            &HandleLuaCommand<8>, &HandleLuaCommand<9>, &HandleLuaCommand<10>, &HandleLuaCommand<11>,
            &HandleLuaCommand<12>, &HandleLuaCommand<13>, &HandleLuaCommand<14>, &HandleLuaCommand<15>,
            &HandleLuaCommand<16>, &HandleLuaCommand<17>, &HandleLuaCommand<18>, &HandleLuaCommand<19>,
            &HandleLuaCommand<20>, &HandleLuaCommand<21>, &HandleLuaCommand<22>, &HandleLuaCommand<23>,
            &HandleLuaCommand<24>, &HandleLuaCommand<25>, &HandleLuaCommand<26>, &HandleLuaCommand<27>,
            &HandleLuaCommand<28>, &HandleLuaCommand<29>, &HandleLuaCommand<30>, &HandleLuaCommand<31>};
        std::vector<fl::Engine::LuaCommand> cmds = e->luaCommands();
        for (size_t i = 0; i < cmds.size() && i < fl::Engine::kMaxLuaCommands; ++i)
        {
            luaNames[i] = cmds[i].name;
            table.emplace_back(luaNames[i], *luaHandlers[i], rbac::RBAC_PERM_COMMAND_GM, Console::Yes);
        }
    }
    return table;
}

namespace flh
{
namespace
{
// A handler that every command is visible to: it claims a session (so console-only restrictions do not hide player
// commands) and every permission. The pointer is never dereferenced: the auto-completion only asks IsConsole() and
// HasPermission().
class AllCommandsVisible final : public ChatHandler
{
public:
    AllCommandsVisible() : ChatHandler(reinterpret_cast<WorldSession*>(&_marker)) { }
    bool HasPermission(uint32 /*permission*/) const override { return true; }

private:
    char _marker = 0;
};
}  // namespace

std::vector<std::string> coreCommandRoots()
{
    AllCommandsVisible handler;
    std::vector<std::string> roots = Trinity::ChatCommands::GetAutoCompletionsFor(handler, "");
    for (auto& r : roots)
        std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return roots;
}
}  // namespace flh
