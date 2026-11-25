// Guards: a handle that outlives its object (despawn, logout, instance unload) crashing or acting on a stale pointer.
#include "Support.h"

using namespace fltest;

namespace
{
void giveHandle(Rig& rig, fl::HandleData const& h, std::string const& globalName = "h")
{
    // Store the handle in a global of the Lua state through an event that carries it.
    rig.ok("Event.on('give', function(x) " + globalName + " = x end)");
    rig.emit("give", {fl::EventValue(h)});
}
}  // namespace

TEST_CASE("handles: reads re-resolve every time and answer nil once the object is gone")
{
    Rig rig;
    auto c = FakeHost::creature(11, 489, 3);
    rig.host.add(c, {"Hakkar's Servant", 14507, 10.f, 20.f, 30.f, 1.5f, true});
    giveHandle(rig, c);
    CHECK(rig.ok("return tostring(h:valid()) .. '|' .. h:name() .. '|' .. h:entry() .. '|' .. tostring(h:alive())") ==
          "true|Hakkar's Servant|14507|true");
    CHECK(rig.ok("local x, y, z, o = h:pos() return x .. ',' .. y .. ',' .. z .. ',' .. o") == "10.0,20.0,30.0,1.5");
    // the object moves: no cached position
    rig.host.find(c)->x = 99.f;
    CHECK(rig.ok("return (h:pos())") == "99.0");

    rig.host.remove(c);   // despawned, logged out, instance unloaded
    CHECK(rig.ok("return tostring(h:valid())") == "false");
    CHECK(rig.ok("return tostring(h:name()) .. tostring(h:entry()) .. tostring(h:alive()) .. tostring(h:pos())") == "nilnilnilnil");
    // the handle itself still knows its identity
    CHECK(rig.ok("return h:guid()") == "Guid-" + std::to_string(c.guid.hi) + "-11");
    CHECK(rig.ok("return h:kind() .. h:mapId() .. h:instanceId()") == "creature4893");
}

TEST_CASE("handles: actions on a gone object return false and \"gone\"")
{
    Rig rig;
    auto c = FakeHost::creature(12);
    rig.host.add(c, {"Boar", 1, 0, 0, 0, 0, true});
    giveHandle(rig, c);
    CHECK(rig.ok("local ok, why = h:despawn() return tostring(ok) .. tostring(why)") == "truenil");
    CHECK(rig.host.find(c) == nullptr);
    CHECK(rig.ok("local ok, why = h:despawn() return tostring(ok) .. '|' .. tostring(why)") == "false|gone");
    CHECK(rig.ok("local ok, why = h:despawn() return tostring(ok) .. '|' .. tostring(why)") == "false|gone");   // and again
}

TEST_CASE("handles: a stored handle survives across calls and across a new object reusing nothing")
{
    Rig rig;
    auto c = FakeHost::creature(13);
    rig.host.add(c, {"First", 1, 0, 0, 0, 0, true});
    giveHandle(rig, c);
    rig.host.remove(c);
    // another object appears with a different guid: the old handle must not see it
    rig.host.add(FakeHost::creature(14), {"Second", 2, 0, 0, 0, 0, true});
    CHECK(rig.ok("return tostring(h:valid()) .. tostring(h:name())") == "falsenil");
    // a player handle and a creature handle with the same low guid are different objects in the world
    rig.host.add(FakeHost::player(13), {"Playerone", 0, 0, 0, 0, 0, true});
    CHECK(rig.ok("return tostring(h:valid())") == "false");
}

TEST_CASE("handles: map handles compare by map and instance, not by an empty guid")
{
    Rig rig;
    fl::HandleData a;
    a.kind = fl::ObjKind::Map;
    a.mapId = 389;
    a.instanceId = 0;
    fl::HandleData b = a;
    b.instanceId = 1;
    fl::HandleData c = a;
    c.mapId = 1;
    rig.ok("Event.on('maps', function(x, y, z) A, B, C = x, y, z end)");
    rig.emit("maps", {fl::EventValue(a), fl::EventValue(b), fl::EventValue(c)});
    CHECK(rig.ok("return tostring(A == A) .. tostring(A == B) .. tostring(A == C)") == "truefalsefalse");
}

TEST_CASE("handles: equality is by guid, tostring is the guid string")
{
    Rig rig;
    rig.ok("Event.on('give2', function(a, b, c) A, B, C = a, b, c end)");
    rig.emit("give2", {fl::EventValue(FakeHost::creature(5)), fl::EventValue(FakeHost::creature(5)),
                       fl::EventValue(FakeHost::creature(6))});
    CHECK(rig.ok("return tostring(A == B) .. tostring(A == C) .. tostring(A ~= C)") == "truefalsetrue");
    CHECK(rig.ok("return tostring(A)") == "Guid-" + std::to_string(FakeHost::creature(5).guid.hi) + "-5");
    CHECK(rig.ok("local t = {} t[1] = A return tostring(t[1] == B) .. tostring(A == 5) .. tostring(A == nil)") == "truefalsefalse");
}

TEST_CASE("handles: userdata with a locked metatable and no writable fields")
{
    Rig rig;
    giveHandle(rig, FakeHost::creature(1));
    CHECK(rig.ok("return type(h)") == "userdata");
    CHECK(rig.ok("return getmetatable(h)") == "fl.Handle");   // a string: scripts cannot reach the real metatable
    CHECK_FALSE(rig.run("h.x = 1").ok);
    CHECK_FALSE(rig.run("setmetatable(h, {})").ok);
    CHECK_FALSE(rig.run("return h:nosuchmethod()").ok);
}

TEST_CASE("handles: a map handle follows its instance (unloaded instance reads as gone)")
{
    Rig rig;
    fl::HandleData m;
    m.kind = fl::ObjKind::Map;
    m.guid.lo = 531;
    m.mapId = 531;
    m.instanceId = 77;
    rig.host.add(m, {"Temple of Ahn'Qiraj", 0, 0, 0, 0, 0, true});
    giveHandle(rig, m, "map");
    CHECK(rig.ok("return tostring(map:valid()) .. map:kind()") == "truemap");
    rig.host.remove(m);   // instance unloaded
    CHECK(rig.ok("return tostring(map:valid()) .. tostring(map:name())") == "falsenil");
}

TEST_CASE("handles: a handle from before a reload is not usable in the new state (new state, new objects)")
{
    Rig rig;
    giveHandle(rig, FakeHost::creature(9));
    rig.e().reload();
    CHECK(rig.ok("return tostring(h)") == "nil");
}
