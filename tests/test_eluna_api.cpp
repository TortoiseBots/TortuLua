// E1: the Register* event families, CreateLuaEvent, the E1 class methods, the gossip calls, the global lookups and
// ElunaQuery, all on the fake world (no game). The docs argument order is checked here; the live menu/packets are in
// docs/E0-E1-NOTES.md.
#include "Support.h"

#include "engine/GuidText.h"

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

struct ElunaRig : Rig
{
    ElunaRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

// Stores a handle in the Lua state through an event, so the tests can call methods on it.
void storePlayer(Rig& rig, fl::HandleData const& p)
{
    rig.ok("RegisterPlayerEvent(3, function(ev, player) stored = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
}

std::string errorOf(Rig& rig, std::string const& code)
{
    fl::CallResult r = rig.run(code);
    return r.ok ? std::string("no error") : r.error;
}
}  // namespace

TEST_CASE("eluna events: ids dispatch with the docs argument order and returns flow back")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(11);
    fl::HandleData victim = FakeHost::creature(22);
    fl::ObjectInfo info;
    info.name = "Hero";
    rig.host.add(p, info);
    rig.host.add(victim, info);

    rig.ok("seen = {} RegisterPlayerEvent(12, function(event, player, amount, victim)\n"
           "  seen.event, seen.amount, seen.victim = event, amount, victim\n"
           "  return 250\n"
           "end)");
    fl::EventResult r = rig.e().emitEluna(fl::EluKind::Player, 0, 12,
                                         {fl::EventValue(p), fl::EventValue(100), fl::EventValue(victim)});
    CHECK(r.calls == 1);
    CHECK(r.hasValue);
    CHECK(r.value == 250);
    CHECK(rig.ok("return tostring(seen.event)") == "12");
    CHECK(rig.ok("return tostring(seen.amount)") == "100");
    CHECK(rig.ok("return seen.victim:GetGUIDLow()") == "22");

    // A returning handler can also stop normal action (docs: true).
    rig.ok("seen_stop = false RegisterPlayerEvent(41, function(event, player, target) seen_stop = true return false end)");
    fl::EventResult veto = rig.e().emitEluna(fl::EluKind::Player, 0, 41, {fl::EventValue(p), fl::EventValue(victim)});
    CHECK(veto.cancel);
    CHECK(rig.ok("return tostring(seen_stop)") == "true");

    // Chat: false, newMessage (docs order: event, player, msg, Type, lang).
    rig.ok("chat = 'none' RegisterPlayerEvent(18, function(event, player, msg, kind, lang) chat = msg .. '/' .. kind .. '/' .. lang return false, 'filtered' end)");
    fl::EventResult chat = rig.e().emitEluna(fl::EluKind::Player, 0, 18,
                                            {fl::EventValue(p), fl::EventValue(std::string("hello")),
                                             fl::EventValue(int64_t(1)), fl::EventValue(int64_t(0))});
    CHECK(chat.cancel);
    CHECK(chat.hasText);
    CHECK(chat.text == "filtered");
    CHECK(rig.ok("return chat") == "hello/1/0");
}

TEST_CASE("eluna events: register returns a cancel function, shots limit the handler, Clear* removes")
{
    ElunaRig rig;
    rig.ok("calls = 0 RegisterCreatureEvent(77, 4, function(event, creature, killer) calls = calls + 1 end, 2)");
    fl::HandleData c = FakeHost::creature(5);
    for (int i = 0; i < 3; ++i)
        rig.e().emitEluna(fl::EluKind::Creature, 77, 4, {fl::EventValue(c), fl::EventValue()});
    CHECK(rig.ok("return calls") == "2");

    rig.ok("calls = 0 cancel = RegisterCreatureEvent(77, 4, function() calls = calls + 1 end)\n"
           "cancel()");
    rig.e().emitEluna(fl::EluKind::Creature, 77, 4, {fl::EventValue(c), fl::EventValue()});
    CHECK(rig.ok("return calls") == "0");

    rig.ok("calls = 0 RegisterCreatureEvent(77, 4, function() calls = calls + 1 end)\n"
           "ClearCreatureEvents(77, 4)");
    rig.e().emitEluna(fl::EluKind::Creature, 77, 4, {fl::EventValue(c), fl::EventValue()});
    CHECK(rig.ok("return calls") == "0");
    CHECK(rig.e().status().elunaHandlers == 0);
}

TEST_CASE("eluna events: a unique-creature handler is keyed by the full guid and the instance, and Clear removes it")
{
    ElunaRig rig;
    fl::HandleData c = FakeHost::creature(7, 0, 5, 321);
    std::string const g = fl::guidText(c.guid);
    rig.ok("calls = 0 RegisterUniqueCreatureEvent('" + g + "', 5, 4, function(event, creature, killer) calls = calls + 1 end)");
    std::vector<fl::EventValue> args{fl::EventValue(c), fl::EventValue()};
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, c.guid, 5, 4, args).calls == 1);
    // Another instance, another creature of the same low counter (a different high half), another event: not this one.
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, c.guid, 6, 4, args).calls == 0);
    fl::Guid other = FakeHost::creature(7, 0, 5, 999).guid;   // same low counter, different entry in the high half
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, other, 5, 4, args).calls == 0);
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, FakeHost::creature(8, 0, 5).guid, 5, 4, args).calls == 0);
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, c.guid, 5, 3, args).calls == 0);
    CHECK(rig.e().emitEluna(fl::EluKind::Creature, 321, 4, args).calls == 0);   // not an entry handler
    CHECK(rig.e().listeningElunaUnique(fl::EluKind::Creature, c.guid, 5, 4));
    CHECK_FALSE(rig.e().listeningElunaUnique(fl::EluKind::Creature, c.guid, 6, 4));
    rig.ok("ClearUniqueCreatureEvents('" + g + "', 5)");
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, c.guid, 5, 4, args).calls == 0);
    CHECK_FALSE(rig.e().listeningElunaUnique(fl::EluKind::Creature, c.guid, 5, 4));

    // The legacy integer form (a low counter) matches any creature with that low guid in the instance.
    rig.ok("calls = 0 RegisterUniqueCreatureEvent(7, 5, 4, function() calls = calls + 1 end)");
    CHECK(rig.e().emitElunaUnique(fl::EluKind::Creature, other, 5, 4, args).calls == 1);
}

TEST_CASE("eluna events: event families that need a core hook log it once at registration")
{
    ElunaRig rig;
    rig.ok("RegisterCreatureEvent(1, 1, function() end)");
    CHECK(rig.host.logContains("creature ENTER_COMBAT needs core hook C1"));
    size_t before = rig.host.logs.size();
    rig.ok("RegisterCreatureEvent(2, 1, function() end)");
    CHECK(rig.host.logs.size() == before);   // once per event family and id, not per registration
    rig.ok("RegisterPlayerEvent(42, function() end)");
    CHECK(rig.host.logContains("player COMMAND needs core hook C4"));
    rig.ok("RegisterItemGossipEvent(1, 2, function() end)");
    CHECK(rig.host.logContains("item gossip SELECT needs core hook C3"));
}

TEST_CASE("eluna events: the gossip select arguments are in the docs order")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(11), c = FakeHost::creature(22);
    rig.ok("g = {} RegisterCreatureGossipEvent(33, 2, function(event, player, object, sender, intid, code, menu_id)\n"
           "  g = {event, player:GetGUIDLow(), object:GetGUIDLow(), sender, intid, code, menu_id}\n"
           "end)");
    rig.e().emitEluna(fl::EluKind::CreatureGossip, 33, 2,
                      {fl::EventValue(p), fl::EventValue(c), fl::EventValue(int64_t(9)),
                       fl::EventValue(int64_t(4)), fl::EventValue(std::string("1234")), fl::EventValue(int64_t(56))});
    CHECK(rig.ok("return table.concat(g, ',')") == "2,11,22,9,4,1234,56");
}

TEST_CASE("eluna timed events: delay ranges, repeat counts and removal")
{
    ElunaRig rig;
    rig.host.frozen = true;
    rig.host.tickMs = 0;
    rig.ok("shots = {} id = CreateLuaEvent(function(eventId, delay, repeats)\n"
           "  shots[#shots + 1] = eventId .. ':' .. delay .. ':' .. repeats\n"
           "end, {100, 200}, 3)");
    for (int i = 0; i < 4; ++i)
    {
        rig.host.advance(250);
        rig.e().tick();
    }
    CHECK(rig.ok("return #shots") == "3");
    CHECK(rig.ok("return tostring(id ~= nil)") == "true");
    CHECK(rig.ok("return shots[1]:find('^%d+:1[0-9][0-9]:3$') ~= nil") == "true");
    CHECK(rig.ok("return shots[2]:find(':2$') ~= nil") == "true");
    CHECK(rig.ok("return shots[3]:find(':1$') ~= nil") == "true");

    // RemoveEventById stops an infinite event.
    rig.ok("n = 0 id2 = CreateLuaEvent(function() n = n + 1 end, 50, 0)");
    rig.host.advance(60);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
    rig.ok("RemoveEventById(id2)");
    rig.host.advance(300);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");

    // RemoveEvents clears the rest.
    rig.ok("CreateLuaEvent(function() n = n + 100 end, 50, 0) RemoveEvents()");
    rig.host.advance(300);
    rig.e().tick();
    CHECK(rig.ok("return n") == "1");
}

TEST_CASE("eluna timed events: RemoveEvents(false) keeps object-bound events, RemoveEvents(true) clears all")
{
    ElunaRig rig;
    rig.host.frozen = true;
    fl::HandleData p = FakeHost::player(3);
    fl::ObjectInfo info;
    info.name = "Owner";
    rig.host.add(p, info);
    storePlayer(rig, p);
    rig.ok("global, own = 0, 0 id1 = CreateLuaEvent(function() global = global + 1 end, 10, 0)\n"
           "stored:RegisterEvent(function() own = own + 1 end, 10, 0)");
    rig.ok("RemoveEvents()");   // the docs default: global events only
    rig.host.advance(15);
    rig.e().tick();
    CHECK(rig.ok("return global") == "0");
    CHECK(rig.ok("return own") == "1");
    rig.ok("RemoveEvents(true)");
    rig.host.advance(50);
    rig.e().tick();
    CHECK(rig.ok("return own") == "1");
}

TEST_CASE("eluna timed events: with the tick budget off one tick runs every due event")
{
    ElunaRig rig;
    rig.host.frozen = true;
    rig.ok("a, b = 0, 0 CreateLuaEvent(function() a = a + 1 end, 0, 1) CreateLuaEvent(function() b = b + 1 end, 0, 1)");
    rig.e().tick();
    CHECK(rig.ok("return a .. ',' .. b") == "1,1");
}

TEST_CASE("eluna reload: the new state's handlers and timed events are the adopted ones")
{
    ElunaRig rig;
    rig.host.frozen = true;
    rig.dir.write("reg.lua", "calls = (calls or 0)\n"
                            "RegisterPlayerEvent(12, function(event, player, amount) calls = calls + 1 return amount + 1 end)\n"
                            "CreateLuaEvent(function() ticks = (ticks or 0) + 1 end, 10, 1)\n");
    REQUIRE(rig.e().load().errors.empty());
    fl::HandleData p = FakeHost::player(5);
    fl::ObjectInfo info;
    info.name = "Reloaded";
    rig.host.add(p, info);
    CHECK(rig.e().emitEluna(fl::EluKind::Player, 0, 12, {fl::EventValue(p), fl::EventValue(int64_t(10))}).value == 11);
    fl::LoadReport rep = rig.e().reload();
    REQUIRE(rep.applied);
    CHECK(rig.ok("return calls") == "0");
    CHECK(rig.e().emitEluna(fl::EluKind::Player, 0, 12, {fl::EventValue(p), fl::EventValue(int64_t(10))}).value == 11);
    rig.host.advance(15);
    rig.e().tick();
    CHECK(rig.ok("return ticks") == "1");
}

TEST_CASE("eluna timed events: WorldObject:RegisterEvent belongs to the object")
{
    ElunaRig rig;
    rig.host.frozen = true;
    fl::HandleData a = FakeHost::creature(1), b = FakeHost::creature(2);
    fl::ObjectInfo info;
    info.name = "A";
    rig.host.add(a, info);
    rig.host.add(b, info);
    fl::HandleData owner = FakeHost::player(9);
    rig.host.add(owner, info);
    rig.ok("RegisterPlayerEvent(3, function(ev, p) obj = p end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(owner)});
    rig.ok("hit = 0 eid = obj:RegisterEvent(function() hit = hit + 1 end, 10, 0)");
    INFO("timer id: " << rig.ok("return tostring(eid)") << " timers: " << rig.e().status().timers);
    rig.host.advance(15);
    rig.e().tick();
    CHECK(rig.ok("return hit") == "1");
    rig.ok("obj:RemoveEvents()");
    rig.host.advance(50);
    rig.e().tick();
    CHECK(rig.ok("return hit") == "1");
}

TEST_CASE("eluna objects: a gone object raises, the five cached readers still answer, cross-map raises")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(31, 1);
    fl::ObjectInfo info;
    info.name = "Old";
    info.entry = 0;
    rig.host.add(p, info);
    storePlayer(rig, p);

    CHECK(errorOf(rig, "return stored:GetGUIDLow()") == "no error");
    CHECK(rig.ok("return stored:GetGUIDLow()") == "31");
    CHECK(rig.ok("return stored:GetEntry()") == "0");
    CHECK(rig.ok("return stored:GetTypeId()") == "4");
    CHECK(rig.ok("return tostring(stored:IsInWorld())") == "true");
    CHECK(rig.ok("return tostring(stored:IsPlayer())") == "true");

    rig.host.remove(p);
    CHECK(rig.ok("return tostring(stored:IsInWorld())") == "false");
    CHECK(errorOf(rig, "stored:GetHealth()").find("Player no longer exists") != std::string::npos);
    CHECK(errorOf(rig, "stored:GetName()").find("Player no longer exists") != std::string::npos);
    CHECK(errorOf(rig, "stored:GetMapId()").find("Player no longer exists") != std::string::npos);
    CHECK(errorOf(rig, "stored:SendBroadcastMessage('x')").find("Player no longer exists") != std::string::npos);

    rig.host.add(p, info);
    rig.host.denyAccess = true;
    CHECK(errorOf(rig, "stored:GetHealth()").find("object is on another map") != std::string::npos);
}

TEST_CASE("eluna objects: methods reach the host with the documented arguments and returns")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(41);
    fl::ObjectInfo info;
    info.name = "Args";
    rig.host.add(p, info);
    storePlayer(rig, p);

    rig.host.gameResults["unit.getHealth"] = {fl::EventValue(int64_t(77))};
    rig.host.gameResults["worldobject.getLocation"] = {fl::EventValue(1.5), fl::EventValue(2.5),
                                                       fl::EventValue(3.5), fl::EventValue(4.5)};
    rig.host.gameResults["player.getGroup"] = {fl::EventValue([] {
        fl::HandleData g;
        g.kind = fl::ObjKind::Group;
        g.guid.lo = 8;
        return g;
    }())};
    rig.host.gameResults["object.setScale"] = {};
    rig.host.gameResults["player.modifyMoney"] = {};

    CHECK(rig.ok("return stored:GetHealth()") == "77");
    CHECK(rig.ok("local x, y, z, o = stored:GetLocation() return x .. ',' .. y .. ',' .. z .. ',' .. o") ==
          "1.5,2.5,3.5,4.5");
    CHECK(rig.ok("return tostring(stored:GetGroup())") == "Guid-0-8");
    rig.ok("stored:SetScale(1.5)");
    CHECK(rig.host.gameCalls.back() == "object.setScale");
    CHECK(rig.ok("stored:ModifyMoney(-500)") == "nil");
    CHECK(rig.host.gameCalls.back() == "player.modifyMoney");

    // The argument list reached the host unchanged (checked through a capturing host).
    std::vector<fl::EventValue> captured;
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const& args,
                              std::vector<fl::EventValue>& out)
    {
        if (op != "unit.setHealth")
            return false;
        captured = args;
        out.clear();
        return true;
    };
    rig.ok("stored:SetHealth(55)");
    REQUIRE(captured.size() == 1);
    CHECK(std::get<int64_t>(captured[0].v) == 55);

    // An op the core does not provide raises instead of returning nothing.
    rig.host.onGameCall = nullptr;
    CHECK(errorOf(rig, "stored:SetDisplayId(5)").find("SetDisplayId: not available on this server") !=
          std::string::npos);
}

TEST_CASE("eluna query: the cursor, the typed getters and the row table")
{
    ElunaRig rig;
    fl::DbRows rows;
    rows.columns = {"entry", "name", "score"};
    rows.rows = {{int64_t(7), std::string("Seven"), std::monostate()},
                 {int64_t(8), std::string("Eight"), 2.5}};
    rig.host.rawResults["SELECT * FROM t"] = rows;

    rig.ok("q = WorldDBQuery('SELECT * FROM t')");
    CHECK(rig.ok("return tostring(q ~= nil)") == "true");
    CHECK(rig.ok("return q:GetRowCount() .. ',' .. q:GetColumnCount()") == "2,3");
    CHECK(rig.ok("return q:GetUInt32(0) .. ',' .. q:GetString(1)") == "7,Seven");
    CHECK(rig.ok("return tostring(q:IsNull(2))") == "true");
    CHECK(rig.ok("return tostring(q:IsNull(0))") == "false");
    CHECK(rig.ok("return tostring(q:NextRow())") == "true");
    CHECK(rig.ok("return q:GetInt64(0) .. ',' .. q:GetDouble(2) .. ',' .. q:GetFloat(2)") == "8,2.5,2.5");
    CHECK(rig.ok("local r = q:GetRow() return r.entry .. ',' .. r.name") == "8,Eight");
    CHECK(rig.ok("return tostring(q:NextRow())") == "false");

    // Zero rows and a failed query are nil (the docs example guards with `if Q`).
    rig.ok("return tostring(WorldDBQuery('SELECT * FROM empty'))");
    fl::DbRows empty;
    empty.columns = {"a"};
    rig.host.rawResults["SELECT * FROM t2"] = empty;
    CHECK(rig.ok("return tostring(WorldDBQuery('SELECT * FROM t2'))") == "nil");
    rig.host.rawFail = true;
    CHECK(rig.ok("return tostring(CharDBQuery('SELECT * FROM nosuch'))") == "nil");
    CHECK(rig.host.logContains("characters query failed"));
    CHECK(rig.ok("return tostring(AuthDBQuery('SELECT 1'))") == "nil");
}

TEST_CASE("eluna globals: lookups, spawn, world message, core info and guid helpers")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(51);
    fl::ObjectInfo info;
    info.name = "Global";
    rig.host.add(p, info);
    rig.host.players.push_back(p);
    rig.host.playersByName["Global"] = p;
    fl::HandleData spawned = FakeHost::creature(60, 1, 0, 777);
    rig.host.spawnResult = spawned;

    CHECK(rig.ok("return GetPlayerByName('Global'):GetGUIDLow()") == "51");
    CHECK(rig.ok("return tostring(GetPlayerByName('Nobody'))") == "nil");
    // The 128-bit guid round-trips: GetGUID carries the high half, and the lookup matches the whole guid.
    CHECK(rig.ok("return GetPlayerByGUID(GetPlayerByName('Global'):GetGUID()):GetGUIDLow()") == "51");
    CHECK(rig.ok("return GetPlayerByGUID(51):GetGUIDLow()") == "51");   // the legacy low-counter form of a player
    CHECK(rig.ok("return tostring(GetPlayerByGUID('" + fl::guidText(FakeHost::creature(51).guid) + "'))") == "nil");   // same low, a creature
    CHECK(rig.ok("return tostring(GetPlayerByGUID('00000000000000000000000000000034'))") == "nil");
    CHECK(rig.ok("return #GetPlayersInWorld()") == "1");
    CHECK(rig.ok("return #GetPlayersInWorld(0, true)") == "1");
    CHECK(rig.ok("return GetPlayerCount()") == "1");
    rig.ok("SendWorldMessage('hello all')");
    REQUIRE(rig.host.worldMessages.size() == 1);
    CHECK(rig.host.worldMessages[0] == "hello all");

    CHECK(rig.ok("return PerformIngameSpawn(1, 100, 1, 0, 1.0, 2.0, 3.0, 4.0):GetGUIDLow()") == "60");
    REQUIRE(rig.host.spawns.size() == 1);
    CHECK(rig.host.spawns[0].entry == 100);
    CHECK(rig.host.spawns[0].mapId == 1);
    CHECK(rig.ok("return tostring(PerformIngameSpawn(99, 1, 1, 0, 0, 0, 0, 0))") == "nil");

    CHECK(rig.ok("return GetCoreName() .. ',' .. GetCoreVersion() .. ',' .. GetLuaEngine()") ==
          "ForeverCore,test-1.0,ForeverLua");
    CHECK(rig.ok("return GetCoreExpansion()") == "0");
    CHECK(rig.ok("return tostring(GetGameTime())") == "4242");
    CHECK(rig.ok("return type(GetCurrTime())") == "number");
    CHECK(rig.ok("return GetStateMapId() .. ',' .. GetStateInstanceId()") == "-1,0");
    CHECK(rig.ok("return GetGUIDLow(4294967361)") == "65");
    CHECK(rig.ok("return GetGUIDType(GetPlayerByName('Global'):GetGUID()) .. ',' .. GetGUIDEntry(GetPlayerByName('Global'):GetGUID())") == "4,0");
    CHECK(rig.ok("local c = PerformIngameSpawn(1, 100, 1, 0, 1.0, 2.0, 3.0, 4.0) return GetGUIDType(c:GetGUID()) .. ',' .. GetGUIDEntry(c:GetGUID()) .. ',' .. c:GetEntry() .. ',' .. GetGUIDLow(c:GetGUID())") == "9,777,777,60");
    CHECK(rig.ok("return GetAreaName(12)") == "Elwynn Forest");
    CHECK(rig.ok("return tostring(GetAreaName(13))") == "nil");
    CHECK(rig.ok("return GetMapById(1):GetMapId()") == "1");
    CHECK(rig.ok("return tostring(GetMapById(9))") == "nil");
    CHECK(rig.ok("return GetItemLink(123):find('item:123', 1, true) ~= nil") == "true");
    CHECK(rig.ok("return tostring(GetQuest(100))") == "Guid-0-100");
    CHECK(rig.ok("return tostring(GetQuest(101))") == "nil");
    rig.ok("WorldDBExecute('DELETE FROM x') CharDBExecute('DELETE FROM y') AuthDBExecute('DELETE FROM z')");
    CHECK(rig.host.rawExecuted.size() == 3);
    rig.ok("ReloadEluna()");
    CHECK(rig.host.reloadsRequested == 1);
}

TEST_CASE("eluna globals: the bit helpers")
{
    ElunaRig rig;
    CHECK(rig.ok("return bit_and(6, 3)") == "2");
    CHECK(rig.ok("return bit_or(4, 1)") == "5");
    CHECK(rig.ok("return bit_xor(7, 3)") == "4");
    CHECK(rig.ok("return bit_lshift(1, 4)") == "16");
    CHECK(rig.ok("return bit_rshift(16, 4)") == "1");
    CHECK(rig.ok("return tostring(bit_not(0))") == "-1");
}

TEST_CASE("eluna gossip: the menu calls reach the host with the docs arguments")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(71), c = FakeHost::creature(72);
    fl::ObjectInfo info;
    info.name = "NPC";
    rig.host.add(p, info);
    rig.host.add(c, info);
    storePlayer(rig, p);
    std::vector<std::string> ops;
    std::vector<std::vector<fl::EventValue>> args;
    rig.host.gameResults["gossip.addItem"] = {};
    rig.host.gameResults["gossip.sendMenu"] = {};
    rig.host.gameResults["gossip.complete"] = {};
    rig.host.gameResults["gossip.clear"] = {};
    rig.host.gameResults["gossip.addQuests"] = {};
    rig.host.onGameCall = [&](fl::HandleData const&, std::string const& op, std::vector<fl::EventValue> const& a,
                              std::vector<fl::EventValue>& out)
    {
        if (op.rfind("gossip.", 0) != 0)
            return false;
        ops.push_back(op);
        args.push_back(a);
        out.clear();
        return true;
    };
    rig.ok("stored:GossipMenuAddItem(0, 'Hi', 5, 6, true, 'popup', 100)");
    rig.ok("stored:GossipSendMenu(100, stored, 7)");
    rig.ok("stored:GossipComplete() stored:GossipClearMenu() stored:GossipAddQuests(stored)");
    REQUIRE(ops.size() == 5);
    CHECK(ops[0] == "gossip.addItem");
    REQUIRE(args[0].size() == 7);
    CHECK(std::get<int64_t>(args[0][0].v) == 0);                        // icon
    CHECK(std::get<std::string>(args[0][1].v) == "Hi");                // msg
    CHECK(std::get<int64_t>(args[0][2].v) == 5);                       // sender
    CHECK(std::get<int64_t>(args[0][3].v) == 6);                       // intid
    CHECK(std::get<bool>(args[0][4].v) == true);                       // code
    CHECK(std::get<std::string>(args[0][5].v) == "popup");
    CHECK(std::get<int64_t>(args[0][6].v) == 100);                     // money
    CHECK(std::get<int64_t>(args[1][0].v) == 100);                     // npc_text
    CHECK(std::get<int64_t>(args[1][2].v) == 7);                       // menu_id
    CHECK(ops[4] == "gossip.addQuests");
}

TEST_CASE("eluna globals: 64-bit integer helpers, GUID builders, realm id, inventory slots and spell stub")
{
    ElunaRig rig;
    fl::HandleData p = FakeHost::player(51);
    fl::ObjectInfo info;
    info.name = "Hero";
    rig.host.add(p, info);
    rig.host.players.push_back(p);
    rig.host.playersByName["Hero"] = p;

    // CreateLongLong / CreateULongLong
    CHECK(rig.ok("return CreateLongLong()") == "0");
    CHECK(rig.ok("return CreateLongLong(-42)") == "-42");
    CHECK(rig.ok("return CreateLongLong('-9223372036854775808')") == "-9223372036854775808");
    CHECK(rig.ok("return CreateLongLong('9223372036854775807')") == "9223372036854775807");
    CHECK(rig.ok("return CreateULongLong()") == "0");
    CHECK(rig.ok("return CreateULongLong(42)") == "42");
    CHECK(rig.ok("return string.format('%u', CreateULongLong('18446744073709551615'))") == "18446744073709551615");
    CHECK(errorOf(rig, "return CreateLongLong('notanumber')").find("valid") != std::string::npos);
    CHECK(errorOf(rig, "return CreateULongLong(-1)").find("negative") != std::string::npos);
    CHECK(errorOf(rig, "return CreateULongLong('-1')").find("valid") != std::string::npos);

    // GUID builders round-trip through the GUID helpers and lookup; mapId defaults to 0 and selects non-zero maps
    CHECK(rig.ok("local g = GetPlayerGUID(51) return GetGUIDType(g) .. ',' .. GetGUIDLow(g) .. ',' .. GetPlayerByGUID(g):GetName()") ==
          "4,51,Hero");
    CHECK(rig.ok("local g = GetItemGUID(99) return GetGUIDType(g) .. ',' .. GetGUIDLow(g)") == "3,99");
    CHECK(rig.ok("local g = GetUnitGUID(12, 777) return GetGUIDType(g) .. ',' .. GetGUIDEntry(g) .. ',' .. GetGUIDLow(g)") ==
          "9,777,12");
    CHECK(rig.ok("local g = GetObjectGUID(15, 888) return GetGUIDType(g) .. ',' .. GetGUIDEntry(g) .. ',' .. GetGUIDLow(g)") ==
          "11,888,15");
    fl::HandleData mobMap1 = FakeHost::creature(12, 1, 0, 777);
    rig.host.add(mobMap1, info);
    rig.ok("RegisterCreatureEvent(1, 5, function(ev, creature) map1Mob = creature end)");
    rig.e().emitEluna(fl::EluKind::Creature, 1, 5, {fl::EventValue(mobMap1)});
    CHECK(rig.ok("return tostring(GetUnitGUID(12, 777, 1) == map1Mob:GetGUID()) .. ',' .. "
                 "tostring(GetUnitGUID(12, 777) == map1Mob:GetGUID())") == "true,false");
    fl::HandleData goMap532 = FakeHost::gameObject(15, 532, 0, 888);
    rig.host.add(goMap532, info);
    rig.ok("RegisterGameObjectEvent(1, 14, function(ev, go) map532Go = go end)");
    rig.e().emitEluna(fl::EluKind::GameObject, 1, 14, {fl::EventValue(goMap532)});
    CHECK(rig.ok("return tostring(GetObjectGUID(15, 888, 532) == map532Go:GetGUID()) .. ',' .. "
                 "tostring(GetObjectGUID(15, 888) == map532Go:GetGUID())") == "true,false");
    CHECK(errorOf(rig, "return GetUnitGUID(12, 777, -1)").find("mapId must be an integer 0..65535") != std::string::npos);
    CHECK(errorOf(rig, "return GetObjectGUID(15, 888, 65536)").find("mapId must be an integer 0..65535") !=
          std::string::npos);

    // GetRealmID
    rig.host.fakeRealmId = 7;
    CHECK(rig.ok("return GetRealmID()") == "7");

    // Inventory position checks
    CHECK(rig.ok("return tostring(IsBagPos(255, 30)) .. ',' .. tostring(IsBagPos(255, 35))") == "true,false");
    CHECK(rig.ok("return tostring(IsBankPos(255, 63)) .. ',' .. tostring(IsBankPos(64, 0)) .. ',' .. tostring(IsBankPos(255, 35))") ==
          "true,true,false");
    CHECK(rig.ok("return tostring(IsEquipmentPos(255, 0)) .. ',' .. tostring(IsEquipmentPos(255, 35))") == "true,false");
    CHECK(rig.ok("return tostring(IsInventoryPos(255, 35)) .. ',' .. tostring(IsInventoryPos(30, 0)) .. ',' .. tostring(IsInventoryPos(255, 63))") ==
          "true,true,false");
    CHECK(errorOf(rig, "return IsBagPos(256, 0)").find("bag must be an integer 0..255") != std::string::npos);
    CHECK(errorOf(rig, "return IsInventoryPos(0, -1)").find("slot must be an integer 0..255") != std::string::npos);

    // RegisterSpellEvent stub
    CHECK(errorOf(rig, "return RegisterSpellEvent(133, 1, function() end)").find("RegisterSpellEvent: not available on this core") !=
          std::string::npos);
}
