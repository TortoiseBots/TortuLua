#include "Settings.h"

#include "engine/Text.h"

#include "fl_core_hooks.h"

#include <cctype>

namespace flh
{
namespace
{
template <typename T>
T clampTo(int64_t v, int64_t lo, int64_t hi)
{
    return static_cast<T>(v < lo ? lo : (v > hi ? hi : v));
}
}  // namespace

ModuleSettings settingsFrom(ConfigSource const& c)
{
    ModuleSettings s;
    // The hooks this core tree really has (cmake/DetectCoreHooks.cmake). Not a conf key: a core either has the hook or not.
    s.engine.coreHooks = FL_CORE_HOOK_MASK;
    s.enabled = c.getBool("ForeverLua.Enable", false);
    std::string mode = c.getString("ForeverLua.Mode", "test");
    for (auto& ch : mode)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    s.engine.mode = mode == "eluna" ? fl::EngineMode::Eluna : fl::EngineMode::Test;
    s.engine.scriptPath = c.getString("ForeverLua.ScriptPath", "lua_scripts");
    s.engine.openStdLibs = c.getBool("ForeverLua.OpenStdLibs", false);
    s.engine.requirePath = fl::splitWords(c.getString("ForeverLua.RequirePath", ""));
    s.engine.mapStates = uint32_t(clampTo<int64_t>(c.getInt("ForeverLua.MapStates", 0), 0, 1));
    s.engine.slowHandlerMs = clampTo<uint64_t>(c.getInt("ForeverLua.SlowHandlerMs", 5), 0, 60000);
    s.engine.syncDbTimeoutMs = clampTo<uint64_t>(c.getInt("ForeverLua.SyncDbTimeoutMs", 5000), 100, 600000);
    s.engine.dataFlushMs = clampTo<uint64_t>(c.getInt("ForeverLua.DataFlushMs", 5000), 100, 600000);
    s.engine.commandRoots = fl::splitWords(c.getString("ForeverLua.CommandRoots", "tt"));
    s.engine.bridgeDeny =
        fl::splitWords(c.getString("ForeverLua.BridgeDeny", "lua server account rbac bnetaccount pdump ban"));
    s.engine.memoryCap = size_t(clampTo<uint32_t>(c.getInt("ForeverLua.MemoryLimitMB", 128), 4, 4096)) * 1024u * 1024u;

    bool const eluna = s.engine.mode == fl::EngineMode::Eluna;
    // Test mode: a budget of 0 would switch the sandbox off or kill every handler, so the minimum stays 1 there.
    int64_t const lowest = eluna ? 0 : 1;
    s.engine.eventBudget.maxMs = clampTo<uint64_t>(c.getInt("ForeverLua.EventBudgetMs", eluna ? 0 : 20), lowest, 10000);
    s.engine.commandBudget.maxMs = clampTo<uint64_t>(c.getInt("ForeverLua.CommandBudgetMs", eluna ? 0 : 250), lowest, 60000);
    s.engine.tickBudgetMs = clampTo<uint32_t>(c.getInt("ForeverLua.TickBudgetMs", eluna ? 0 : 25), lowest, 1000);
    if (eluna)
    {
        // "Off" must mean off: with a 0 ms budget the instruction budget of the same call is off too (the engine reads
        // 0 as "no limit" in each field).
        if (s.engine.eventBudget.maxMs == 0)
            s.engine.eventBudget.maxInstr = 0;
        if (s.engine.commandBudget.maxMs == 0)
            s.engine.commandBudget.maxInstr = 0;
        s.engine.loadBudget = fl::Budget{0, 0};
    }
    s.engine.dbTimeoutMs = clampTo<uint64_t>(c.getInt("ForeverLua.DbTimeoutMs", 2000), 100, 60000);
    s.engine.shutdownBudgetMs = clampTo<uint64_t>(c.getInt("ForeverLua.ShutdownBudgetMs", 5000), 100, 600000);
    // M5 (E4/E5 review): map states exist in eluna mode only. Test mode runs without the Lua lock on map threads and
    // its folder would load once per map, so MapStates = 1 with Mode = test is refused (one state, logged at start).
    if (!eluna && s.engine.mapStates != 0)
    {
        s.engine.mapStates = 0;
        s.mapStatesRefused = true;
    }
    s.adminSecurity = int(clampTo<int>(c.getInt("ForeverLua.AdminSecurity", 3), 0, 255));
    s.bridgeSecurity = int(clampTo<int>(c.getInt("ForeverLua.BridgeSecurity", 3), 0, 255));
    return s;
}
}  // namespace flh
