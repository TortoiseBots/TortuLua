// ForeverLua host side: two small game-free rules about the core's chat command map. Unit-tested in
// tests/test_host_commands.cpp; the core adapter (Commands.cpp, ForeverHost) only supplies the facts.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace flh
{
// The core's command map must never be cleared while the core is inside ChatCommandNode::TryExecuteCommand: after the
// invoker returns the core still reads its command node (the permission, the help), and clearing the map frees it.
// RegisterCommand and `.lua reload` run inside such a call, so they only mark the map changed; the world script applies
// the change from its own update, outside every command.
class CommandMapGate
{
public:
    // Any thread, any time (also inside a command).
    void markChanged() { _changed.store(true, std::memory_order_release); }
    bool pending() const { return _changed.load(std::memory_order_acquire); }
    // The world thread, outside every command: runs `rebuild` once when a change is pending. True when it ran.
    bool apply(std::function<void()> const& rebuild)
    {
        if (!_changed.exchange(false, std::memory_order_acq_rel))
            return false;
        rebuild();
        return true;
    }

private:
    std::atomic<bool> _changed{false};
};

// The command roots of the core's table (and the module's own). A name that equals a root, or is a prefix of one, may not
// be registered: an equal name would make the core abort building its map ("Duplicate blank sub-command") or hijack the
// command, and a prefix would take over the core's short form of it (the core matches a typed prefix when it is unique).
class CommandNameSet
{
public:
    void assign(std::vector<std::string> roots);
    // Names are compared in lower case. An empty set (the table could not be read) reserves every name: a command is
    // refused rather than risking the core's duplicate-name abort.
    bool reserves(std::string const& name) const;
    size_t size() const { return _roots.size(); }

private:
    std::vector<std::string> _roots;
};
}  // namespace flh
