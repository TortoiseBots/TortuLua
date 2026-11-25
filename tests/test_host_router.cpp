// The host adapter's routing (src/host/EluRouter.cpp, the code EluHooks.cpp calls for every creature/game object/item
// callback) on the fake world: the right register family, the map scope around every dispatch, unique creature events
// by guid + instance, the item gossip hello. What the adapter still does by itself (building handles and argument lists
// from core objects) is compiled against the core in the game-side build.
#include "Support.h"

#include "EluRouter.h"
#include "engine/GuidText.h"

#include <thread>

using namespace fltest;

namespace
{
struct RouterRig : Rig
{
    flh::EluRouter router;
    RouterRig()
        : Rig([](fl::Config& c) { c.mode = fl::EngineMode::Eluna; c.eventBudget = {0, 0}; c.commandBudget = {0, 0};
                                  c.loadBudget = {0, 0}; c.tickBudgetMs = 0; }),
          router([this](fl::EluKind, std::optional<flh::MapKey>) { return unowned(e()); })
    {
    }
};

fl::ObjectInfo named(char const* n)
{
    fl::ObjectInfo i;
    i.name = n;
    return i;
}

flh::Subject subject(fl::HandleData const& h, uint32_t entry) { return flh::Subject{h, entry}; }

// E4: the router asks the host for the state of each event, with the map scope of the map-bound families.
TEST_CASE("router: map-bound families ask for the map's state, player/server/group/guild for the world state")
{
    struct Calls
    {
        std::vector<std::pair<fl::EluKind, std::optional<flh::MapKey>>> seen;
    } calls;
    Rig rig([](fl::Config& c) { c.mode = fl::EngineMode::Eluna; c.eventBudget = {0, 0}; c.commandBudget = {0, 0};
                                c.loadBudget = {0, 0}; c.tickBudgetMs = 0; });
    flh::EluRouter router([&](fl::EluKind kind, std::optional<flh::MapKey> map) {
        calls.seen.emplace_back(kind, map);
        return unowned(rig.e());
    });

    fl::HandleData npc = FakeHost::creature(1, 3, 4, 77);
    rig.host.add(npc, named("npc"));
    flh::Subject const me = subject(npc, 77);
    router.creature(me, 7, {});
    REQUIRE(calls.seen.back().first == fl::EluKind::Creature);
    REQUIRE(calls.seen.back().second.has_value());
    CHECK(calls.seen.back().second->map == 3);
    CHECK(calls.seen.back().second->instance == 4);

    router.firePlayer(std::nullopt, 3, {});
    REQUIRE(calls.seen.back().first == fl::EluKind::Player);
    CHECK_FALSE(calls.seen.back().second.has_value());

    router.fire(fl::EluKind::Server, 0, 13, {});
    REQUIRE(calls.seen.back().first == fl::EluKind::Server);
    CHECK_FALSE(calls.seen.back().second.has_value());

    router.group(1, {});
    REQUIRE(calls.seen.back().first == fl::EluKind::Group);
    CHECK_FALSE(calls.seen.back().second.has_value());

    // A map callback: the map family asks for the map state, the server's map event for the world state.
    router.mapEvent(3, 4, true, 3, 23, {fl::EventValue(), fl::EventValue()}, {fl::EventValue(), fl::EventValue()});
    // The Map and Instance families share the map's state (both are emitted into the engine the provider returned);
    // the server's map event goes to the world state.
    bool sawMap = false, sawServer = false;
    for (auto const& c : calls.seen)
    {
        if (c.first == fl::EluKind::Map && c.second && c.second->map == 3)
            sawMap = true;
        if (c.first == fl::EluKind::Server && !c.second)
            sawServer = true;
        CHECK(c.first != fl::EluKind::Instance);   // the instance family is keyed inside the same state
    }
    CHECK(sawMap);
    CHECK(sawServer);

    // Item use: the item's owner map, inside its scope.
    router.itemUse(flh::MapKey{3, 4}, 5, fl::EventValue(), fl::EventValue(), fl::EventValue());
    REQUIRE(calls.seen.back().first == fl::EluKind::Item);
    REQUIRE(calls.seen.back().second.has_value());
    CHECK(calls.seen.back().second->map == 3);
}

// Runs on a thread that is NOT the world thread and has no scope of its own: only the router's own scope can make an
// object resolvable there.
template <typename F>
void onWorkerThread(F f)
{
    std::thread t(f);
    t.join();
}
}  // namespace

TEST_CASE("router: creature gossip HELLO and SELECT fire the CreatureGossip family, not the combat events")
{
    RouterRig rig;
    fl::HandleData npc = FakeHost::creature(1, 0, 0, 77);
    fl::HandleData pl = FakeHost::player(2, 0);
    rig.host.add(npc, named("Innkeeper"));
    rig.host.add(pl, named("Guest"));
    rig.ok("enter, leave, hello, sel = 0, 0, 0, nil\n"
           "RegisterCreatureEvent(77, 1, function() enter = enter + 1 end)\n"          // ENTER_COMBAT
           "RegisterCreatureEvent(77, 2, function() leave = leave + 1 end)\n"          // LEAVE_COMBAT
           "RegisterCreatureGossipEvent(77, 1, function(ev, player, object)\n"
           "  hello = hello + 1 helloargs = {ev, player:GetGUIDLow(), object:GetGUIDLow()} end)\n"
           "RegisterCreatureGossipEvent(77, 2, function(ev, player, object, sender, intid, code, menu)\n"
           "  sel = {ev, sender, intid, tostring(code), menu} return false end)");
    flh::Subject me = subject(npc, 77);
    fl::EventResult hello = rig.router.creatureGossipHello(me, fl::EventValue(pl));
    CHECK(hello.calls == 1);
    CHECK_FALSE(hello.cancel);                                    // the handler returned nothing: it handled the hello
    CHECK(rig.ok("return enter .. ',' .. leave .. ',' .. hello") == "0,0,1");   // the old code ran ENTER_COMBAT here
    CHECK(rig.ok("return table.concat(helloargs, ',')") == "1,2,1");
    fl::EventResult select = rig.router.creatureGossipSelect(me, fl::EventValue(pl), 9, 4, std::string("code"), 56);
    CHECK(select.calls == 1);
    CHECK(select.cancel);                                         // false = do the default action
    CHECK(rig.ok("return enter .. ',' .. leave") == "0,0");       // ... and the old code ran LEAVE_COMBAT here
    CHECK(rig.ok("return sel[1] .. ',' .. sel[2] .. ',' .. sel[3] .. ',' .. sel[4] .. ',' .. sel[5]") == "2,9,4,code,56");
    rig.router.creatureGossipSelect(me, fl::EventValue(pl), 1, 2, std::nullopt, 0);
    CHECK(rig.ok("return sel[4]") == "nil");
    // Another creature entry is not this handler's.
    CHECK(rig.router.creatureGossipHello(subject(FakeHost::creature(5, 0, 0, 78), 78), fl::EventValue(pl)).calls == 0);
    // And the real combat events still reach the Creature family.
    CHECK(rig.router.creature(me, 1, {fl::EventValue(npc), fl::EventValue(pl)}).calls == 1);
    CHECK(rig.ok("return enter") == "1");
}

TEST_CASE("router: game object gossip and every game object event run inside the object's map scope")
{
    RouterRig rig;
    fl::HandleData go = FakeHost::gameObject(1, 2, 4, 88);
    fl::HandleData pl = FakeHost::player(2, 2, 4);              // same map and instance as the object
    fl::HandleData far = FakeHost::player(3, 0, 0);             // another map
    rig.host.add(go, named("Chest"));
    rig.host.add(pl, named("Opener"));
    rig.host.add(far, named("Far"));
    rig.host.gameResults["gameobject.getGoState"] = {fl::EventValue(int64_t(1))};
    rig.host.gameResults["unit.getLevel"] = {fl::EventValue(int64_t(60))};
    // One handler for every event id: it touches every object argument (IsInWorld goes through the access rule) and
    // records what each call said.
    rig.ok("touched, errors = 0, {}\n"
           "local function touch(x) if type(x) == 'userdata' and x.IsInWorld then\n"
           "  local ok, err = pcall(x.IsInWorld, x) if ok and err then touched = touched + 1 else errors[#errors + 1] = tostring(err) end end end\n"
           "for _, id in ipairs({1, 2, 4, 5, 6, 7, 8, 9, 10, 14}) do\n"
           "  RegisterGameObjectEvent(88, id, function(ev, a, b, c, d) touch(a) touch(b) touch(c) touch(d) end)\n"
           "end\n"
           "RegisterGameObjectGossipEvent(88, 1, function(ev, player, object) touch(player) touch(object) end)\n"
           "RegisterGameObjectGossipEvent(88, 2, function(ev, player, object) touch(player) touch(object) end)");
    flh::Subject me = subject(go, 88);
    fl::EventValue self(go), player(pl);
    fl::EventValue quest = fl::EventValue([] { fl::HandleData q; q.kind = fl::ObjKind::Quest; q.guid.lo = 100; return q; }());
    int fired = 0;
    // On a worker thread: nothing resolves there but what the router's own scope allows.
    onWorkerThread([&] {
        fired += rig.router.gameObject(me, 1, {self, fl::EventValue(int64_t(50))}).calls;
        fired += rig.router.gameObject(me, 2, {self}).calls;
        fired += rig.router.gameObject(me, 4, {player, self, quest}).calls;
        fired += rig.router.gameObject(me, 5, {player, self, quest, fl::EventValue(int64_t(1))}).calls;
        fired += rig.router.gameObject(me, 6, {player, self}).calls;
        fired += rig.router.gameObject(me, 7, {self, player}).calls;
        fired += rig.router.gameObject(me, 8, {self, player}).calls;
        fired += rig.router.gameObject(me, 9, {self, fl::EventValue(int64_t(2)), player}).calls;
        fired += rig.router.gameObject(me, 10, {self, fl::EventValue(int64_t(1))}).calls;
        fired += rig.router.gameObject(me, 14, {self, player}).calls;
        fired += rig.router.gameObjectGossipHello(me, player).calls;
        fired += rig.router.gameObjectGossipSelect(me, player, 1, 2, std::nullopt, 0).calls;
    });
    CHECK(fired == 12);
    CHECK(rig.ok("return #errors") == "0");                       // the old code refused every one: "object is on another map"
    CHECK(rig.ok("return touched") == "21");                      // every GO and player argument of the 12 events resolved (quests skipped)
    // An object of another map is still refused inside the scope: the scope is the GO's map, nothing wider.
    rig.ok("touched, errors = 0, {}");
    onWorkerThread([&] { rig.router.gameObject(me, 14, {self, fl::EventValue(far)}); });
    CHECK(rig.ok("return #errors .. ',' .. touched") == "1,1");
    CHECK(rig.ok("return errors[1]").find("another map") != std::string::npos);
    // No scope is left behind on the calling thread.
    CHECK_FALSE(flh::MapScope::current().has_value());
}

TEST_CASE("router: unique creature events key by guid and instance, damage taken passes the guid and returns a value")
{
    RouterRig rig;
    fl::HandleData c1 = FakeHost::creature(7, 0, 5, 31);        // instance 5
    fl::HandleData c2 = FakeHost::creature(7, 0, 6, 31);        // same guid counter in another instance
    fl::HandleData att = FakeHost::player(9, 0, 5);
    rig.host.add(c1, named("One"));
    rig.host.add(c2, named("Two"));
    rig.host.add(att, named("Att"));
    rig.ok("uniq, byentry = 0, 0\n"
           "RegisterUniqueCreatureEvent('" + fl::guidText(c1.guid) + "', 5, 9, function(ev, creature, attacker, damage) uniq = uniq + 1 return true, damage * 2 end)\n"
           "RegisterCreatureEvent(31, 9, function(ev, creature, attacker, damage) byentry = byentry + 1 end)");
    // DAMAGE_TAKEN of instance 5: the unique handler and the entry handler both run; the unique return folds in.
    flh::Subject s1 = subject(c1, 31);
    CHECK(rig.router.listening(fl::EluKind::Creature, s1, 9));
    fl::EventResult r = rig.router.creatureDamageTaken(s1, fl::EventValue(att), 40);
    CHECK(r.calls == 2);
    CHECK(r.handled);
    CHECK(r.hasValue);
    CHECK(r.value == 80);
    // Instance 6 has the same low guid: the unique handler does NOT run there (the old key ignored the instance), the
    // entry handler does.
    fl::EventResult r2 = rig.router.creatureDamageTaken(subject(c2, 31), fl::EventValue(att), 40);
    CHECK(r2.calls == 1);
    CHECK_FALSE(r2.hasValue);
    CHECK(rig.ok("return uniq .. ',' .. byentry") == "1,2");
    // A creature entry nobody registered for, in an instance with a unique registration only by guid: not listening.
    CHECK_FALSE(rig.router.listening(fl::EluKind::Creature, subject(FakeHost::creature(8, 0, 5, 32), 32), 9));
}

TEST_CASE("router: item use fires the USE event and the item gossip hello, false from either cancels the cast")
{
    RouterRig rig;
    fl::HandleData pl = FakeHost::player(2, 0);
    fl::HandleData item = FakeHost::item(40);
    rig.host.add(pl, named("User"));
    rig.host.add(item, named("Stone"));
    fl::EventValue p(pl), i(item), none;
    flh::MapKey const map{0, 0};

    // Only the gossip hello registered (the spec's ItemScript::OnUse path that was never dispatched).
    rig.ok("hello = 0 RegisterItemGossipEvent(500, 1, function(ev, player, item) hello = hello + 1 helloargs = {ev, player:GetGUIDLow(), item:GetGUIDLow()} end)");
    CHECK_FALSE(rig.router.itemUse(map, 500, p, i, none));            // a handler that returns nothing: the cast goes on
    CHECK(rig.ok("return hello") == "1");
    CHECK(rig.ok("return table.concat(helloargs, ',')") == "1,2,40");
    rig.ok("RegisterItemGossipEvent(501, 1, function() return false end)");
    CHECK(rig.router.itemUse(map, 501, p, i, none));                  // docs: returning false stops the spell cast
    // The USE event: false cancels (as before the fix), and both run when both are registered.
    rig.ok("use = 0 RegisterItemEvent(502, 2, function() use = use + 1 return false end)\n"
           "RegisterItemGossipEvent(502, 1, function() use = use + 10 end)");
    CHECK(rig.router.itemUse(map, 502, p, i, none));
    CHECK(rig.ok("return use") == "11");
    // Nothing registered: nothing runs, no cancel.
    CHECK_FALSE(rig.router.itemUse(map, 999, p, i, none));
}

TEST_CASE("router: a player hook runs inside the player's map scope, a world hook without one")
{
    RouterRig rig;
    fl::HandleData pl = FakeHost::player(2, 0, 7);
    fl::HandleData other = FakeHost::creature(3, 4, 0, 5);
    rig.host.add(pl, named("P"));
    rig.host.add(other, named("O"));
    rig.ok("res2 = {} RegisterPlayerEvent(8, function(ev, a, b) local ok1, v1 = pcall(a.IsInWorld, a) local ok2, v2 = pcall(b.IsInWorld, b)\n"
           "  res2[1] = tostring(ok1) .. tostring(ok2) end)");
    onWorkerThread([&] { rig.router.firePlayer(flh::MapKey{0, 7}, 8, {fl::EventValue(pl), fl::EventValue(other)}); });
    CHECK(rig.ok("return res2[1]") == "truefalse");                    // the creature lives on map 4: refused in map 0/7's scope
    // World hook: no scope, so a non-world thread resolves nothing, the world thread everything.
    rig.ok("res3 = nil RegisterPlayerEvent(3, function(ev, a) res3 = pcall(a.IsInWorld, a) end)");
    rig.router.fire(fl::EluKind::Player, 0, 3, {fl::EventValue(pl)});
    CHECK(rig.ok("return tostring(res3)") == "true");
}
