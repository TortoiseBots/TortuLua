// Eluna Object, WorldObject and GameObject coverage: every documented method added in this round, its arguments,
// returns, round-trips through the fake world, stale-handle refusal, cross-map checks, and core/server stubs.
#include "NearSearch.h"
#include "Support.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace fltest;

namespace
{
fl::Config elunaConfig(fl::Config c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    return c;
}

struct ObjectRig : Rig
{
    ObjectRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

void storeHandles(ObjectRig& rig, fl::HandleData const& p, fl::HandleData const& mob, fl::HandleData const& go,
                  fl::HandleData const& corpse)
{
    rig.ok("RegisterPlayerEvent(3, function(ev, player) p = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    rig.ok("RegisterCreatureEvent(600, 5, function(ev, creature) mob = creature end)");
    rig.e().emitEluna(fl::EluKind::Creature, 600, 5, {fl::EventValue(mob)});
    rig.ok("RegisterGameObjectEvent(700, 2, function(ev, gameobject) go = gameobject end)");
    rig.e().emitEluna(fl::EluKind::GameObject, 700, 2, {fl::EventValue(go)});
    rig.host.gameResults["player.getCorpse"] = {fl::EventValue(corpse)};
    rig.ok("corpse = p:GetCorpse()");
}

std::vector<std::string> const& worldObjectCalls()
{
    static std::vector<std::string> const calls = {
        "Data()",
        "GetAngle(mob)",
        "GetAngle(3, 4)",
        "GetDistance2d(mob)",
        "GetDistance2d(3, 4)",
        "GetExactDistance(mob)",
        "GetExactDistance(3, 4, 5)",
        "GetExactDistance2d(mob)",
        "GetExactDistance2d(3, 4)",
        "GetRelativePoint(5, 0)",
        "GetGameObjectsInRange(50, 700, 0)",
        "GetNearObject(50, 0, 0, 0, 1)",
        "GetNearObjects(50, 0, 0, 0, 1)",
        "IsInFront(mob)",
        "IsInBack(mob)",
        "IsInMap(mob)",
        "IsInRange(mob, 1, 20, true)",
        "IsInRange2d(3, 4, 1, 20)",
        "IsInRange3d(3, 4, 0, 1, 20)",
        "IsWithinDist(mob, 20, true)",
        "IsWithinDist2d(3, 4, 20)",
        "IsWithinDist2d(mob, 20)",
        "IsWithinDist3d(3, 4, 0, 20)",
        "IsWithinDist3d(mob, 20)",
        "IsWithinLoS(mob)",
        "IsWithinLoS(3, 4, 0)",
        "PlayDirectSound(100, p)",
        "PlayDistanceSound(200)",
        "PlayMusic(300, p)",
        "SendUnitSay('hello', 0)",
        "SendUnitYell('loud', 0)",
        "SendUnitWhisper('psst', 0, p, false)",
    };
    return calls;
}

std::vector<std::string> const& gameObjectCalls()
{
    static std::vector<std::string> const calls = {
        "GetDisplayId()",
        "GetLootRecipient()",
        "GetLootRecipientGroup()",
        "HasQuest(42)",
        "IsActive()",
        "IsDestructible()",
        "IsTransport()",
        "SetRespawnTime(30)",
        "SetSpawnedByDefault(false)",
    };
    return calls;
}
}  // namespace

TEST_CASE("eluna object: ToCorpse and the generic update-field core stubs")
{
    ObjectRig rig;
    fl::HandleData p = FakeHost::player(11, 0, 0);
    fl::HandleData mob = FakeHost::creature(12, 0, 0, 600);
    fl::HandleData go = FakeHost::gameObject(13, 0, 0, 700);
    fl::HandleData corpse = FakeHost::corpse(14, 0, 0);
    fl::ObjectInfo info;
    info.name = "Obj";
    info.entry = 0;
    rig.host.add(p, info);
    info.entry = 600;
    rig.host.add(mob, info);
    info.entry = 700;
    rig.host.add(go, info);
    info.entry = 0;
    rig.host.add(corpse, info);
    storeHandles(rig, p, mob, go, corpse);

    CHECK(rig.ok("return tostring(corpse:ToCorpse() ~= nil)") == "true");
    CHECK(rig.ok("return tostring(corpse:ToCorpse() == corpse)") == "true");
    CHECK(rig.ok("return tostring(p:ToCorpse())") == "nil");
    CHECK(rig.ok("return tostring(mob:ToCorpse())") == "nil");
    CHECK(rig.ok("return tostring(go:ToCorpse())") == "nil");

    std::vector<std::string> const coreStubs = {
        "GetByteValue(1, 0)",     "GetFloatValue(1)",        "GetInt32Value(1)",       "GetUInt16Value(1, 0)",
        "GetUInt32Value(1)",      "GetUInt64Value(1)",       "HasFlag(1, 2)",          "RemoveFlag(1, 2)",
        "SetByteValue(1, 0, 3)",  "SetFlag(1, 2)",           "SetFloatValue(1, 1.5)",  "SetInt16Value(1, 0, -2)",
        "SetInt32Value(1, -5)",   "SetUInt16Value(1, 0, 2)", "SetUInt32Value(1, 5)",   "SetUInt64Value(1, 5)",
        "UpdateUInt32Value(1, 1)"};
    for (std::string const& call : coreStubs)
    {
        INFO("stub: p:" << call);
        fl::CallResult r = rig.run("p:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("not available on this core") != std::string::npos);
    }

    rig.host.remove(corpse);
    fl::CallResult gone = rig.run("corpse:ToCorpse()");
    REQUIRE_FALSE(gone.ok);
    CHECK(gone.error.find("Corpse no longer exists") != std::string::npos);
}

TEST_CASE("eluna worldobject: geometry, range, sound, chat, packet, Data and stubs")
{
    ObjectRig rig;
    fl::HandleData p = FakeHost::player(21, 0, 0);
    fl::HandleData mob = FakeHost::creature(22, 0, 0, 600);
    fl::HandleData go = FakeHost::gameObject(23, 0, 0, 700);
    fl::HandleData corpse = FakeHost::corpse(24, 0, 0);
    fl::HandleData otherMapMob = FakeHost::creature(25, 1, 0, 601);

    fl::ObjectInfo pInfo;
    pInfo.name = "Hero";
    pInfo.x = 0.0f;
    pInfo.y = 0.0f;
    pInfo.z = 0.0f;
    pInfo.o = 0.0f;
    rig.host.add(p, pInfo);

    fl::ObjectInfo mInfo;
    mInfo.name = "Wolf";
    mInfo.entry = 600;
    mInfo.x = 3.0f;
    mInfo.y = 4.0f;
    mInfo.z = 0.0f;
    mInfo.o = 0.0f;
    rig.host.add(mob, mInfo);

    fl::ObjectInfo gInfo;
    gInfo.name = "Chest";
    gInfo.entry = 700;
    gInfo.x = 10.0f;
    gInfo.y = 0.0f;
    gInfo.z = 0.0f;
    rig.host.add(go, gInfo);

    fl::ObjectInfo cInfo;
    cInfo.name = "Bones";
    cInfo.x = 0.0f;
    cInfo.y = 8.0f;
    cInfo.z = 0.0f;
    rig.host.add(corpse, cInfo);

    fl::ObjectInfo omInfo;
    omInfo.name = "Far";
    omInfo.entry = 601;
    rig.host.add(otherMapMob, omInfo);

    rig.host.woState(go).hostileToAll = false;
    rig.host.woState(mob).hostileToAll = true;
    storeHandles(rig, p, mob, go, corpse);
    rig.ok("RegisterCreatureEvent(601, 5, function(ev, creature) farMob = creature end)");
    rig.e().emitEluna(fl::EluKind::Creature, 601, 5, {fl::EventValue(otherMapMob)});

    for (std::string const& call : worldObjectCalls())
    {
        INFO("call: p:" << call);
        CHECK(rig.run("p:" + call).ok);
    }

    // Geometry round-trips
    CHECK(std::fabs(std::stod(rig.ok("return p:GetDistance2d(3, 4)")) - 5.0) < 0.01);
    CHECK(std::fabs(std::stod(rig.ok("return p:GetExactDistance(3, 4, 12)")) - 13.0) < 0.01);
    CHECK(std::fabs(std::stod(rig.ok("return p:GetExactDistance2d(mob)")) - 5.0) < 0.01);
    CHECK(std::fabs(std::stod(rig.ok("return p:GetAngle(0, 1)")) - 1.5707963) < 0.01);
    CHECK(rig.ok("local x, y, z = p:GetRelativePoint(5, 0) return string.format('%.1f,%.1f,%.1f', x, y, z)") ==
          "5.0,0.0,0.0");

    // Orientation, range, distance and LoS checks
    CHECK(rig.ok("return tostring(p:IsInFront(mob)) .. ',' .. tostring(p:IsInBack(mob))") == "true,false");
    CHECK(rig.ok("return tostring(p:IsInMap(mob)) .. ',' .. tostring(p:IsInMap(farMob))") == "true,false");
    CHECK(rig.ok("return tostring(p:IsInRange(mob, 4, 6)) .. ',' .. tostring(p:IsInRange(mob, 6, 10))") == "true,false");
    CHECK(rig.ok("return tostring(p:IsInRange2d(3, 4, 4, 6)) .. ',' .. tostring(p:IsInRange3d(3, 4, 12, 12, 14))") ==
          "true,true");
    CHECK(rig.ok("return tostring(p:IsWithinDist(mob, 6)) .. ',' .. tostring(p:IsWithinDist2d(3, 4, 4))") ==
          "true,false");
    CHECK(rig.ok("return tostring(p:IsWithinDist3d(3, 4, 12, 14))") == "true");
    CHECK(rig.ok("return tostring(p:IsWithinLoS(mob))") == "true");
    rig.host.woState(p).losOk = false;
    CHECK(rig.ok("return tostring(p:IsWithinLoS(3, 4, 0))") == "false");

    // Near objects and GameObjects in range (sorted by distance: mob=5, corpse=8, go=10)
    CHECK(rig.ok("return #p:GetGameObjectsInRange(20, 700)") == "1");
    CHECK(rig.ok("return p:GetGameObjectsInRange(20, 700)[1]:GetGUIDLow()") == "23");
    CHECK(rig.ok("return p:GetNearObject(20):GetGUIDLow()") == "22");
    CHECK(rig.ok("local list = p:GetNearObjects(20) return #list .. ':' .. list[1]:GetGUIDLow() .. ',' .. list[2]:GetGUIDLow() .. ',' .. list[3]:GetGUIDLow()") ==
          "3:22,24,23");
    // Type mask filter: 32 = GameObject, 8 = Unit, 128 = Corpse
    CHECK(rig.ok("return #p:GetNearObjects(20, 32) .. ',' .. p:GetNearObject(20, 32):GetGUIDLow()") == "1,23");
    CHECK(rig.ok("return #p:GetNearObjects(20, 128) .. ',' .. p:GetNearObject(20, 128):GetGUIDLow()") == "1,24");
    CHECK(rig.ok("return #p:GetNearObjects(20, 8, 600, 1, 1)") == "1");

    // Per-object Data() table persists across calls on the same WorldObject and is pruned after despawn
    rig.ok("go:Data().counter = 42");
    CHECK(rig.ok("return go:Data().counter") == "42");
    CHECK(rig.ok("return tostring(mob:Data().counter)") == "nil");
    rig.ok("mob:Data().tag = 'alive'");
    rig.host.advance(1000);
    rig.e().tick();
    CHECK(rig.ok("return mob:Data().tag") == "alive");
    rig.host.remove(mob);
    rig.host.advance(1000);
    rig.e().tick();
    rig.host.add(mob, mInfo);
    CHECK(rig.ok("return tostring(mob:Data().tag)") == "nil");

    // SendPacket on non-Player WorldObject (Creature / GameObject / Corpse) reaches FakeHost::sendPacket
    rig.host.packets.clear();
    rig.ok("local pkt = CreatePacket(0x123, 4) pkt:WriteULong(77) go:SendPacket(pkt) mob:SendPacket(pkt) corpse:SendPacket(pkt)");
    REQUIRE(rig.host.packets.size() == 3);
    CHECK(rig.host.packets[0].target.kind == fl::ObjKind::GameObject);
    CHECK(rig.host.packets[1].target.kind == fl::ObjKind::Creature);
    CHECK(rig.host.packets[2].target.kind == fl::ObjKind::Corpse);

    // SendUnitSay/Yell/Whisper also work on a non-Unit WorldObject (GameObject), capped at 255 bytes
    rig.host.woState(go).chats.clear();
    rig.ok("go:SendUnitSay('open', 0) go:SendUnitYell('alert', 0) go:SendUnitWhisper('secret', 0, p, true)");
    REQUIRE(rig.host.woState(go).chats.size() == 3);
    CHECK(rig.host.woState(go).chats[0].op == "worldobject.say");
    CHECK(rig.host.woState(go).chats[1].op == "worldobject.yell");
    CHECK(rig.host.woState(go).chats[2].boss == true);
    CHECK(rig.host.woState(go).chats[2].targetLow == 21);
    CHECK(rig.run("go:SendUnitSay(string.rep('a', 255), 0)").ok);
    CHECK(rig.run("go:SendUnitYell(string.rep('a', 255), 0)").ok);
    CHECK(rig.run("go:SendUnitWhisper(string.rep('a', 255), 0, p, false)").ok);
    CHECK(rig.run("go:SendUnitSay(string.rep('a', 256), 0)").error.find("255") != std::string::npos);
    CHECK(rig.run("go:SendUnitYell(string.rep('a', 256), 0)").error.find("255") != std::string::npos);
    CHECK(rig.run("go:SendUnitWhisper(string.rep('a', 256), 0, p, false)").error.find("255") != std::string::npos);

    // Sounds recorded with optional player target
    REQUIRE(rig.host.woState(p).sounds.size() >= 3);
    CHECK(rig.host.woState(p).sounds[0].op == "worldobject.playDirectSound");
    CHECK(rig.host.woState(p).sounds[0].targetPlayerLow == 21);
    CHECK(rig.host.woState(p).sounds[1].op == "worldobject.playDistanceSound");
    CHECK(rig.host.woState(p).sounds[1].targetPlayerLow == 0);
    CHECK(rig.host.woState(p).sounds[2].op == "worldobject.playMusic");

    // PhaseMask core stubs
    for (char const* call : {"GetPhaseMask()", "SetPhaseMask(1, true)"})
    {
        fl::CallResult r = rig.run(std::string("p:") + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("not available on this core") != std::string::npos);
    }

    // Bad arguments and non-finite search ranges raise before reaching the host
    CHECK_FALSE(rig.run("p:GetAngle('bad', 1)").ok);
    CHECK_FALSE(rig.run("p:IsInRange(mob, 'a', 5)").ok);
    CHECK_FALSE(rig.run("p:PlayDirectSound(-1)").ok);
    CHECK_FALSE(rig.run("p:SendPacket(123)").ok);
    CHECK(rig.run("p:GetNearObjects(1/0)").error.find("range must be a finite number") != std::string::npos);
    CHECK(rig.run("p:GetNearObject(0/0)").error.find("range must be a finite number") != std::string::npos);
    CHECK(rig.run("p:GetNearObjects(-1/0)").error.find("range must be a finite number") != std::string::npos);
    CHECK(rig.run("p:GetNearObjects(1e309)").error.find("range must be a finite number") != std::string::npos);
    CHECK(rig.run("p:GetGameObjectsInRange(1/0)").error.find("range must be a finite number") != std::string::npos);
    CHECK(rig.ok("return #p:GetNearObjects(1e30)") == "3");
    CHECK(rig.ok("return #p:GetNearObjects(-5)") == "0");

    // Cross-map refusal
    rig.host.denyAccess = true;
    CHECK(rig.run("p:GetAngle(mob)").error.find("object is on another map") != std::string::npos);
    rig.host.denyAccess = false;

    // Gone receiver raises stale-handle error on every WorldObject method
    rig.host.remove(p);
    for (std::string const& call : worldObjectCalls())
    {
        INFO("gone call: p:" << call);
        fl::CallResult r = rig.run("p:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Player no longer exists") != std::string::npos);
    }
    CHECK(rig.run("p:SendPacket(CreatePacket(1, 0))").error.find("Player no longer exists") != std::string::npos);
}

TEST_CASE("eluna gameobject: reads, setters, RemoveFromWorld and SaveToDB stub")
{
    ObjectRig rig;
    fl::HandleData p = FakeHost::player(31, 0, 0);
    fl::HandleData grp;
    grp.kind = fl::ObjKind::Group;
    grp.guid.lo = 88;
    fl::HandleData go = FakeHost::gameObject(32, 0, 0, 700);
    fl::HandleData corpse = FakeHost::corpse(33, 0, 0);
    fl::HandleData mob = FakeHost::creature(34, 0, 0, 600);

    fl::ObjectInfo info;
    info.name = "Obj";
    rig.host.add(p, info);
    info.entry = 600;
    rig.host.add(mob, info);
    info.entry = 700;
    rig.host.add(go, info);
    info.entry = 0;
    rig.host.add(corpse, info);
    storeHandles(rig, p, mob, go, corpse);

    auto& gs = rig.host.goState(go);
    gs.displayId = 4321;
    gs.lootRecipient = p;
    gs.lootRecipientGroup = grp;
    gs.quests.insert(42);
    gs.active = true;
    gs.destructible = false;
    gs.transport = true;

    for (std::string const& call : gameObjectCalls())
    {
        INFO("call: go:" << call);
        CHECK(rig.run("go:" + call).ok);
    }

    CHECK(rig.ok("return go:GetDisplayId()") == "4321");
    CHECK(rig.ok("return go:GetLootRecipient():GetGUIDLow()") == "31");
    CHECK(rig.ok("return tostring(go:GetLootRecipientGroup())") == "Guid-0-88");
    CHECK(rig.ok("return tostring(go:HasQuest(42)) .. ',' .. tostring(go:HasQuest(99))") == "true,false");
    CHECK(rig.ok("return tostring(go:IsActive()) .. ',' .. tostring(go:IsDestructible()) .. ',' .. tostring(go:IsTransport())") ==
          "true,false,true");
    CHECK(gs.respawnTime == 30);
    CHECK_FALSE(gs.spawnedByDefault);

    // SaveToDB refuses unsynchronised shared spawn data writes from a map thread
    fl::CallResult save = rig.run("go:SaveToDB()");
    REQUIRE_FALSE(save.ok);
    CHECK(save.error.find("not available on this server") != std::string::npos);
    CHECK(save.error.find("spawn data") != std::string::npos);

    // RemoveFromWorld(true) refuses DB delete on game thread; RemoveFromWorld() removes from world and prunes Data()
    rig.ok("go:Data().kept = true");
    fl::CallResult delDb = rig.run("go:RemoveFromWorld(true)");
    REQUIRE_FALSE(delDb.ok);
    CHECK(delDb.error.find("database delete") != std::string::npos);

    rig.ok("go:RemoveFromWorld()");
    CHECK(rig.ok("return tostring(go:IsInWorld())") == "false");
    rig.host.add(go, info);
    CHECK(rig.ok("return tostring(go:Data().kept)") == "nil");
    rig.host.remove(go);

    // Gone GameObject raises on all methods
    for (std::string const& call : gameObjectCalls())
    {
        INFO("gone call: go:" << call);
        fl::CallResult r = rig.run("go:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("GameObject no longer exists") != std::string::npos);
    }
    CHECK(rig.run("go:SaveToDB()").error.find("GameObject no longer exists") != std::string::npos);
    CHECK(rig.run("go:RemoveFromWorld()").error.find("GameObject no longer exists") != std::string::npos);
}

TEST_CASE("eluna worldobject: pure near-search range clamp and VisitAllObjects dispatch")
{
    CHECK(flh::clampSearchRange(-10.0) == 0.0f);
    CHECK(flh::clampSearchRange(25.0) == 25.0f);
    CHECK(flh::clampSearchRange(1e30) == 533.333f);
    CHECK(flh::clampSearchRange(std::numeric_limits<double>::infinity()) == 533.333f);
    CHECK(flh::clampSearchRange(-std::numeric_limits<double>::infinity()) == 533.333f);
    CHECK(flh::clampSearchRange(std::numeric_limits<double>::quiet_NaN()) == 533.333f);

    // Core GridDefines.h:91-92 splits GameObject and non-pet Creature into GridTypeMapContainer; only
    // VisitAllObjects visits both containers so GetNearObjects/GetNearObject see creatures and game objects.
    struct FakeCell
    {
        static void VisitWorldObjects(int*, std::vector<char const*>& out, float)
        {
            out.push_back("player");
            out.push_back("corpse");
        }
        static void VisitGridObjects(int*, std::vector<char const*>& out, float)
        {
            out.push_back("creature");
            out.push_back("gameobject");
        }
        static void VisitAllObjects(int* o, std::vector<char const*>& out, float r)
        {
            VisitWorldObjects(o, out, r);
            VisitGridObjects(o, out, r);
        }
    };
    int origin = 0;
    std::vector<char const*> visited;
    flh::visitAllNearObjects<FakeCell>(&origin, visited, flh::clampSearchRange(100.0));
    REQUIRE(visited.size() == 4);
    CHECK(std::string(visited[2]) == "creature");
    CHECK(std::string(visited[3]) == "gameobject");
}
