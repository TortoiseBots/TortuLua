// ForeverLua host side: the module settings, read through a small config interface so the real defaulting rules (the
// Eluna mode budgets, the test mode minimums) are the code the unit tests run. Module.cpp adapts the core's config
// manager to ConfigSource; the tests use a map. No core header here.
#pragma once

#include "engine/Engine.h"

#include <cstdint>
#include <string>

namespace flh
{
struct ConfigSource
{
    virtual ~ConfigSource() = default;
    virtual bool getBool(std::string const& key, bool def) const = 0;
    virtual std::string getString(std::string const& key, std::string const& def) const = 0;
    virtual int64_t getInt(std::string const& key, int64_t def) const = 0;
};

struct ModuleSettings
{
    bool enabled = false;
    fl::Config engine;
    int adminSecurity = 3;
    int bridgeSecurity = 3;
    bool mapStatesRefused = false;   // MapStates = 1 was set with Mode = test: forced to 0 (M5)
};

// Test mode keeps the sandbox limits and their minimum of 1 ms; Eluna mode trusts its scripts: its three budgets default
// to 0 (off), and a 0 millisecond budget means no instruction budget either (both 0 = no limit, no hook).
ModuleSettings settingsFrom(ConfigSource const& config);
}  // namespace flh
