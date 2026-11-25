// Shared helpers of the engine tests.
#pragma once

#include "FakeHost.h"
#include "Settings.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>

namespace fltest
{
// A non-owning pin of an engine the test owns (the router hands out shared pointers since the E4/E5 review round).
inline std::shared_ptr<fl::Engine> unowned(fl::Engine& e) { return std::shared_ptr<fl::Engine>(std::shared_ptr<fl::Engine>(), &e); }

// A config source over a map: the module's real settings loader (flh::settingsFrom) runs on it.
struct MapConfig : flh::ConfigSource
{
    std::map<std::string, std::string> values;
    bool getBool(std::string const& k, bool def) const override
    {
        auto it = values.find(k);
        return it == values.end() ? def : (it->second == "1" || it->second == "true");
    }
    std::string getString(std::string const& k, std::string const& def) const override
    {
        auto it = values.find(k);
        return it == values.end() ? def : it->second;
    }
    int64_t getInt(std::string const& k, int64_t def) const override
    {
        auto it = values.find(k);
        return it == values.end() ? def : std::stoll(it->second);
    }
};

// The engine config the module would build from these conf keys, keeping the rig's script folder.
inline void applySettings(fl::Config& c, std::map<std::string, std::string> const& conf)
{
    MapConfig mc;
    mc.values = conf;
    std::string path = c.scriptPath;
    c = flh::settingsFrom(mc).engine;
    c.scriptPath = path;
    c.commandRoots = {"tt"};
}

// A host, an empty script folder and a started engine.
struct Rig
{
    FakeHost host;
    TempDir dir;
    fl::Config cfg;
    std::unique_ptr<fl::Engine> engine;

    explicit Rig(std::function<void(fl::Config&)> tweak = nullptr, bool startEngine = true)
    {
        cfg = testConfig(dir.path());
        if (tweak)
            tweak(cfg);
        engine = std::make_unique<fl::Engine>(host, cfg);
        if (startEngine)
            REQUIRE(engine->start());
    }
    fl::Engine& e() { return *engine; }

    // Runs a chunk and returns tostring of its first result; fails the test when the chunk fails.
    std::string ok(std::string const& code)
    {
        fl::CallResult r = engine->runChunk(code, "=test");
        INFO("chunk: " << code << "\nerror: " << r.error);
        REQUIRE(r.ok);
        return r.result;
    }
    fl::CallResult run(std::string const& code) { return engine->runChunk(code, "=test"); }
    // Db.select is asynchronous: queues the call (the helper appends the callback), runs a tick, and returns what the
    // callback received: "table|nil" on rows, "nil|reason" on a refusal or failure.
    std::string dbSelect(std::string const& args)
    {
        ok("__db = 'none' Db.select(" + args +
           ", function(rows, err) __db = tostring(rows) .. '|' .. tostring(err) end)");
        e().tick();
        return ok("return __db");
    }
    void emit(std::string const& name, std::vector<fl::EventValue> args = {})
    {
        fl::QueuedEvent ev;
        ev.name = name;
        ev.args = std::move(args);
        engine->emit(std::move(ev));
    }
};
}  // namespace fltest
