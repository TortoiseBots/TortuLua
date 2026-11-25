// Toolkit-style scripts (tests/lua/*.lua) running on the fake world: a script folder is loaded for real, a command
// is run, and the reply is the TTJSON line the agent expects.
#include "Support.h"

#include <filesystem>

using namespace fltest;

TEST_CASE("lua scripts: the mock toolkit folder loads without errors and answers .tt selftest")
{
    Rig rig([](fl::Config& c) { c.scriptPath = FL_TEST_LUA_DIR; });
    rig.host.frozen = true;
    rig.host.add(FakeHost::creature(21, 531), {"Anubisath Sentinel", 15264, 5.f, 6.f, 7.f, 0.f, true});
    fl::LoadReport rep = rig.e().load();
    INFO("errors: " << (rep.errors.empty() ? "" : rep.errors[0]));
    CHECK(rep.errors.empty());
    CHECK(rep.loaded.size() >= 2);
    CHECK(rep.loaded.front() == "init.lua");

    auto r = rig.e().runCommand("tt", "selftest #7", 3, FakeHost::player(1));
    REQUIRE(r.status == fl::CommandStatus::Ok);
    REQUIRE(r.lines.size() == 1);
    CHECK(r.lines[0] == R"(TTJSON {"cmd":"selftest","id":"7","ok":true,"timers":2})");

    // the timers the command started fire on later ticks
    rig.host.advance(100);
    rig.e().tick();
    rig.host.advance(100);
    rig.e().tick();
    CHECK(rig.ok("return fired_after .. ',' .. fired_every") == "1,2");

    // the death event reaches the handler registered by the script, with the snapshot and the handle
    auto snap = std::make_shared<fl::EventTable>();
    snap->fields.push_back({"entry", fl::EventValue(15264)});
    rig.emit("creature.death", {fl::EventValue(snap), fl::EventValue(FakeHost::creature(21, 531)), fl::EventValue()});
    CHECK(rig.ok("return deaths[1]") == "15264:Anubisath Sentinel");
    rig.host.remove(FakeHost::creature(21, 531));
    rig.emit("creature.death", {fl::EventValue(snap), fl::EventValue(FakeHost::creature(21, 531)), fl::EventValue()});
    CHECK(rig.ok("return deaths[2]") == "15264:gone");   // the corpse despawned: the snapshot still has the entry
}
