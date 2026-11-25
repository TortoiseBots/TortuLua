// E5: the long tail. CreatePacket and the WorldPacket class, the packet events, RegisterBGEvent/BattleGround, the corpse
// functions, Map:SetWeather and the other Map methods, AddTaxiPath, and the Classic 1.60 stubs of spec section 7.
#include "Support.h"

#include "engine/Elu.h"

#include <string>
#include <vector>

using namespace fltest;

namespace
{
fl::Config e5Config(std::string const& path, uint32_t mapStates = 0)
{
    fl::Config c = testConfig(path);
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    c.slowHandlerMs = 0;
    c.mapStates = mapStates;
    return c;
}

struct E5Rig : Rig
{
    explicit E5Rig(std::function<void(fl::Config&)> tweak = nullptr)
        : Rig([tweak](fl::Config& c)
              {
                  c = e5Config(c.scriptPath);
                  if (tweak)
                      tweak(c);
              })
    {
    }
    std::string errorOf(std::string const& code)
    {
        fl::CallResult r = run(code);
        return r.ok ? std::string("no error") : r.error;
    }
};

fl::HandleData corpse(uint64_t id, uint32_t map = 0)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Corpse;
    h.guid = FakeHost::makeGuid(14, id);
    h.mapId = map;
    return h;
}

fl::HandleData mapHandleOf(uint32_t mapId, uint32_t instance = 0)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Map;
    h.mapId = mapId;
    h.instanceId = instance;
    return h;
}

}  // namespace

TEST_CASE("e5: CreatePacket and the WorldPacket reads and writes")
{
    E5Rig rig;
    rig.ok("p = CreatePacket(0x1234, 0)");
    REQUIRE(rig.ok("return p:GetOpcode()") == "4660");
    REQUIRE(rig.ok("return p:GetSize()") == "0");
    rig.ok("p:WriteUByte(7) p:WriteShort(-2) p:WriteULong(4000000000) p:WriteFloat(1.5) p:WriteString('hi')");
    REQUIRE(rig.ok("return p:GetSize()") == std::to_string(1 + 2 + 4 + 4 + 3));
    REQUIRE(rig.ok("return p:ReadUByte()") == "7");
    REQUIRE(rig.ok("return p:ReadShort()") == "-2");
    REQUIRE(rig.ok("return p:ReadULong()") == "4000000000");
    REQUIRE(rig.ok("return string.format('%.1f', p:ReadFloat())") == "1.5");
    REQUIRE(rig.ok("return p:ReadString()") == "hi");
    rig.ok("p:SetOpcode(42)");
    REQUIRE(rig.ok("return p:GetOpcode()") == "42");
    // A read past the written bytes is an error, not uninitialised memory.
    CHECK(rig.errorOf("return p:ReadByte()").find("past the end") != std::string::npos);
    // CreatePacket's size is a reserve (the core's WorldPacket(opcode, res); E4/E5 review M4) and the per-packet cap.
    REQUIRE(rig.ok("q = CreatePacket(1, 4) return q:GetSize()") == "0");
    CHECK(rig.errorOf("q:WriteString(string.rep('x', 70000))").find("exceed") != std::string::npos);
    // The packet value is a handle of the state, not a world object: it is gone with the state (a new state has no id).
    CHECK(rig.errorOf("return CreatePacket(1, 1):GetOpcode()") == "no error");
}

TEST_CASE("e5: Player, Group and Guild SendPacket hand the bytes to the host")
{
    E5Rig rig;
    fl::HandleData const p = FakeHost::player(3, 0);
    fl::ObjectInfo info;
    info.name = "P";
    rig.host.add(p, info);
    rig.host.players.push_back(p);
    rig.host.playersByName["P"] = p;

    fl::HandleData guild;
    guild.kind = fl::ObjKind::Guild;
    guild.guid.lo = 9;
    rig.host.guildsByName["G"] = guild;

    rig.host.add(guild, fl::ObjectInfo{"G", 0, 0, 0, 0, 0, true});
    fl::HandleData group;
    group.kind = fl::ObjKind::Group;
    group.guid = FakeHost::makeGuid(29, 5);
    rig.host.add(group, fl::ObjectInfo{"Party", 0, 0, 0, 0, 0, true});
    rig.host.gameResults["player.getGroup"] = {fl::EventValue(group)};

    rig.ok("p = GetPlayerByName('P')\n"
           "packet = CreatePacket(0x55, 0)\n"
           "packet:WriteUByte(3)\n"
           "p:SendPacket(packet)\n"
           "GetGuildByName('G'):SendPacket(packet)\n"
           "p:GetGroup():SendPacket(packet, true, 12)");

    REQUIRE(rig.host.packets.size() == 3);
    CHECK(rig.host.packets[0].target.kind == fl::ObjKind::Player);
    CHECK(rig.host.packets[0].opcode == 0x55);
    REQUIRE(rig.host.packets[0].data.size() == 1);
    CHECK(rig.host.packets[0].data[0] == 3);
    CHECK(rig.host.packets[1].target.kind == fl::ObjKind::Guild);
    CHECK(rig.host.packets[2].target.kind == fl::ObjKind::Group);
    CHECK(rig.host.packets[2].ignorePlayersInBg);
    CHECK(rig.host.packets[2].ignore == 12);
}

TEST_CASE("e5: packet events dispatch with the read-only packet copy and the player")
{
    E5Rig rig;
    rig.ok("rcv = 'none' snd = 'none'\n"
           "RegisterPacketEvent(0x77, 5, function(e, packet, player)\n"
           "  rcv = e .. ':' .. packet:ReadUByte() .. ':' .. tostring(player)\n"
           "end)\n"
           "RegisterPacketEvent(0x77, 7, function(e, packet, player)\n"
           "  snd = e .. ':' .. packet:GetSize() .. ':' .. tostring(player)\n"
           "end)");
    fl::HandleData const p = FakeHost::player(4, 0);
    rig.host.add(p, fl::ObjectInfo{"P", 0, 0, 0, 0, 0, true});

    rig.e().emitPacketEvent(0x77, 5, {9, 8}, fl::EventValue(p));
    REQUIRE(rig.ok("return rcv") == "5:9:" + rig.host.guidToString(p.guid));

    rig.e().emitPacketEvent(0x77, 7, {1, 2, 3}, fl::EventValue());
    REQUIRE(rig.ok("return snd") == "7:3:nil");
}

TEST_CASE("e5: the corpse functions of Player and Corpse")
{
    E5Rig rig;
    fl::HandleData const p = FakeHost::player(8, 1);
    fl::ObjectInfo info;
    info.name = "Dead";
    rig.host.add(p, info);
    rig.host.players.push_back(p);
    rig.host.playersByName["Dead"] = p;

    fl::HandleData const c = corpse(50, 1);
    rig.host.add(c, fl::ObjectInfo{"Dead", 0, 0, 0, 0, 0, false});
    rig.host.gameResults["player.getCorpse"] = {fl::EventValue(c)};
    rig.host.gameResults["corpse.getGhostTime"] = {fl::EventValue(static_cast<int64_t>(1700000000))};
    rig.host.gameResults["corpse.getOwnerGuid"] = {fl::EventValue(static_cast<int64_t>(8))};
    rig.host.gameResults["corpse.getType"] = {fl::EventValue(static_cast<int64_t>(0))};
    rig.host.gameResults["corpse.resetGhostTime"] = {};
    rig.host.gameResults["corpse.saveToDB"] = {};

    rig.ok("c = GetPlayerByName('Dead'):GetCorpse()");
    REQUIRE(rig.ok("return c:GetGhostTime()") == "1700000000");
    REQUIRE(rig.ok("return c:GetOwnerGUID()") == "8");
    REQUIRE(rig.ok("return c:GetType()") == "0");
    rig.ok("c:ResetGhostTime() c:SaveToDB()");
    // Inherited WorldObject methods answer through the host like every world object.
    rig.host.gameResults["worldobject.getX"] = {fl::EventValue(1.5)};
    REQUIRE(rig.ok("return c:GetX()") == "1.5");

    // The corpse leaves the world: a stored handle raises.
    rig.host.remove(c);
    CHECK(rig.errorOf("return c:GetGhostTime()").find("Corpse no longer exists") != std::string::npos);
}

TEST_CASE("e5: RegisterBGEvent, the BattleGround class and ClearBattleGroundEvents")
{
    E5Rig rig;
    rig.ok("seen = ''\n"
           "RegisterBGEvent(1, function(e, bg, bgId, instanceId)\n"
           "  b = bg\n"
           "  seen = e .. ':' .. bg:GetMapId() .. ':' .. bg:GetInstanceId() .. ':' .. bg:GetTypeId()\n"
           "          .. ':' .. bgId .. ':' .. instanceId\n"
           "end)\n"
           "RegisterBGEvent(2, function(e, bg, bgId, instanceId, winner)\n"
           "  seen = 'end:' .. winner\n"
           "end)");
    fl::HandleData bg;
    bg.kind = fl::ObjKind::BattleGround;
    bg.mapId = 30;
    bg.guid.lo = 77;
    bg.guid.hi = 3;
    bg.instanceId = 77;
    rig.host.add(bg, fl::ObjectInfo{"Warsong Gulch", 3, 0, 0, 0, 0, true});
    rig.e().emitEluna(fl::EluKind::BattleGround, 0, 1,
                      {fl::EventValue(bg), fl::EventValue(int64_t(3)), fl::EventValue(int64_t(77))});
    REQUIRE(rig.ok("return seen") == "1:30:77:3:3:77");
    rig.e().emitEluna(fl::EluKind::BattleGround, 0, 2,
                      {fl::EventValue(bg), fl::EventValue(int64_t(3)), fl::EventValue(int64_t(77)),
                       fl::EventValue(int64_t(0))});
    REQUIRE(rig.ok("return seen") == "end:0");

    // The class answers GetMapId/GetInstanceId/GetTypeId from the handle and asks the host for the rest.
    rig.host.gameResults["battleground.getName"] = {fl::EventValue(std::string("Warsong Gulch"))};
    REQUIRE(rig.ok("return b:GetName()") == "Warsong Gulch");
    rig.ok("ClearBattleGroundEvents()");
    rig.e().emitEluna(fl::EluKind::BattleGround, 0, 1,
                      {fl::EventValue(bg), fl::EventValue(int64_t(3)), fl::EventValue(int64_t(77))});
    REQUIRE(rig.ok("return seen") == "end:0");   // unchanged: nothing was bound any more
}

TEST_CASE("e5: the Map long tail (SetWeather, players, flags, Data)")
{
    E5Rig rig;
    rig.host.gameResults["map.setWeather"] = {};
    rig.host.gameResults["map.getPlayerCount"] = {fl::EventValue(static_cast<int64_t>(3))};
    rig.host.gameResults["map.isDungeon"] = {fl::EventValue(true)};
    rig.host.gameResults["map.getDifficulty"] = {fl::EventValue(static_cast<int64_t>(1))};
    rig.host.gameResults["map.getAreaId"] = {fl::EventValue(static_cast<int64_t>(12))};
    rig.host.gameResults["map.getHeight"] = {fl::EventValue(2.5)};

    rig.host.add(mapHandleOf(1, 0), fl::ObjectInfo{"Map", 0, 0, 0, 0, 0, true});
    rig.ok("m = GetMapById(1, 0)");
    rig.ok("m:SetWeather(12, 1, 0.5)");
    REQUIRE(rig.host.gameCalls.back() == "map.setWeather");
    REQUIRE(rig.ok("return m:GetPlayerCount()") == "3");
    REQUIRE(rig.ok("return tostring(m:IsDungeon())") == "true");
    REQUIRE(rig.ok("return m:GetDifficulty()") == "1");
    REQUIRE(rig.ok("return m:GetAreaId(1, 2, 3)") == "12");
    REQUIRE(rig.ok("return m:GetHeight(1, 2)") == "2.5");
    // Map:Data is a per-state table tied to the map handle.
    REQUIRE(rig.ok("m:Data().x = 5 return m:Data().x") == "5");
    // The map handle must name a loaded map; map 2 does not exist on the fake host.
    CHECK(rig.errorOf("return GetMapById(2, 0)") == "no error");   // GetMapById answers nil for an unknown map
    rig.e().runChunk("bad = GetMapById(2, 0)");
    CHECK(rig.ok("return tostring(bad)") == "nil");
}

TEST_CASE("e5: AddTaxiPath says what this core cannot do")
{
    E5Rig rig;
    CHECK(rig.errorOf("return AddTaxiPath({{0, 1, 2, 3}}, 1, 2)").find("AddTaxiPath: not available on this server") !=
          std::string::npos);
}

TEST_CASE("e5: Classic 1.60 stubs raise the documented error")
{
    E5Rig rig;
    fl::HandleData const p = FakeHost::player(5, 0);
    rig.host.add(p, fl::ObjectInfo{"P", 0, 0, 0, 0, 0, true});
    rig.host.players.push_back(p);
    rig.host.playersByName["P"] = p;
    fl::HandleData guild;
    guild.kind = fl::ObjKind::Guild;
    guild.guid.lo = 1;
    rig.host.guildsByName["G"] = guild;
    rig.host.add(guild, fl::ObjectInfo{"G", 0, 0, 0, 0, 0, true});

    char const* methods[][2] = {
        {"p:HasAchieved(1)", "HasAchieved"},
        {"p:SetAchievement(1)", "SetAchievement"},
        {"p:ResetAchievements()", "ResetAchievements"},
        {"p:GetArenaPoints()", "GetArenaPoints"},
        {"p:SetArenaPoints(1)", "SetArenaPoints"},
        {"p:ModifyArenaPoints(1)", "ModifyArenaPoints"},
        {"p:IsInArenaTeam(0)", "IsInArenaTeam"},
        {"p:GetSpecsCount()", "GetSpecsCount"},
        {"p:GetActiveSpec()", "GetActiveSpec"},
        {"p:InRandomLfgDungeon()", "InRandomLfgDungeon"},
        {"p:IsUsingLfg()", "IsUsingLfg"},
        {"p:GetLFGRole()", "GetLFGRole"},
        {"p:GetVehicle()", "GetVehicle"},
        {"p:GetVehicleKit()", "GetVehicleKit"},
        {"p:IsOnVehicle()", "IsOnVehicle"},
        {"GetGuildByName('G'):SetBankTabText(0, 'x')", "SetBankTabText"},
    };
    for (auto const& m : methods)
    {
        std::string const err = rig.errorOf(std::string("local p = GetPlayerByName('P') ") + m[0]);
        INFO(m[0] << " -> " << err);
        CHECK(err.find(std::string(m[1]) + ": not available on Classic 1.60") != std::string::npos);
    }
    CHECK(rig.errorOf("return GetOwnerHalaa()").find("GetOwnerHalaa: not available on Classic 1.60") !=
          std::string::npos);
    CHECK(rig.errorOf("RegisterVehicleEvent(1, function() end)")
              .find("RegisterVehicleEvent: not available on Classic 1.60") != std::string::npos);
    // The event ids of the absent systems register and log their stub once.
    rig.ok("RegisterPlayerEvent(45, function() end) RegisterPlayerEvent(50, function() end)");
    CHECK(rig.host.logContains("ACHIEVEMENT_COMPLETE: not available on Classic 1.60"));
    CHECK(rig.host.logContains("CAN_JOIN_LFG: not available on Classic 1.60"));
}

TEST_CASE("e5: GetStateMap, GetPlayersOnMap and IsCompatibilityMode")
{
    E5Rig world;
    REQUIRE(world.ok("return GetStateMapId()") == "-1");
    REQUIRE(world.ok("return GetStateInstanceId()") == "0");
    REQUIRE(world.ok("return tostring(GetStateMap())") == "nil");
    REQUIRE(world.ok("return tostring(IsCompatibilityMode())") == "true");
    CHECK(world.errorOf("return GetPlayersOnMap(2)").find("only available in a map state") != std::string::npos);

    E5Rig map([](fl::Config& c) {
        c.mapStates = 1;
        c.stateMapId = 1;
        c.stateInstanceId = 4;
    });
    REQUIRE(map.ok("return GetStateMapId()") == "1");
    REQUIRE(map.ok("return GetStateInstanceId()") == "4");
    REQUIRE(map.ok("return GetStateMap():GetMapId()") == "1");
    REQUIRE(map.ok("return GetStateMap():GetInstanceId()") == "4");
    REQUIRE(map.ok("return tostring(IsCompatibilityMode())") == "false");

    fl::HandleData const p1 = FakeHost::player(1, 1, 4);
    fl::HandleData const p2 = FakeHost::player(2, 2, 0);
    map.host.add(p1, fl::ObjectInfo{"A", 0, 0, 0, 0, 0, true});
    map.host.add(p2, fl::ObjectInfo{"B", 0, 0, 0, 0, 0, true});
    map.host.players = {p1, p2};
    REQUIRE(map.ok("local t = GetPlayersOnMap(2) return #t") == "1");
    REQUIRE(map.ok("local t = GetPlayersOnMap(2) return t[1]:GetName()") == "A");
}
