// E6: the core hooks C1-C4 (the maintainers' API specification, section 6). The game-free half is tested here on the fake world:
// the event dispatch of every hook under a core that has it and a core that does not (Config::coreHooks), the
// notify-only creature/game object events (and that a creature with a ScriptName is skipped), the item-use destroy
// guard, the command claim and the security check, and the "needs core hook" lines. The adapters that turn core objects
// into handles are compiled only against the core (the game-side build). There is no AI wrapper: the module never
// replaces an AI.
#include "Support.h"

#include "CoreHooks.h"
#include "CreatureLookup.h"
#include "HookQueue.h"
#include "engine/StateSet.h"

using namespace fltest;

namespace
{
unsigned const kAllHooks = static_cast<unsigned>(fl::CoreHook::AllCreature) |
                           static_cast<unsigned>(fl::CoreHook::AllGameObject) |
                           static_cast<unsigned>(fl::CoreHook::GossipSelect) |
                           static_cast<unsigned>(fl::CoreHook::ItemUse) |
                           static_cast<unsigned>(fl::CoreHook::PlayerCommand) |
                           static_cast<unsigned>(fl::CoreHook::CreatureEvents) |
                           static_cast<unsigned>(fl::CoreHook::GameObjectUse);

struct HooksRig : Rig
{
    flh::HookDispatch dispatch;
    flh::EluRouter router;   // the map update / world update side: drains what the hooks queued
    explicit HooksRig(unsigned hooks)
        : Rig([hooks](fl::Config& c) {
              c.mode = fl::EngineMode::Eluna;
              c.eventBudget = {0, 0};
              c.commandBudget = {0, 0};
              c.loadBudget = {0, 0};
              c.tickBudgetMs = 0;
              c.coreHooks = hooks;
          }),
          dispatch([this](fl::EluKind, std::optional<flh::MapKey>) { return unowned(e()); }),
          router([this](fl::EluKind, std::optional<flh::MapKey>) { return unowned(e()); })
    {
        // The queues are process-wide: every rig starts empty.
        flh::hookQueue().clear();
        flh::creationQueue().clear();
    }
    // The next update of the map (or the world update): runs what the hooks queued.
    size_t drain(flh::MapKey key = flh::MapKey{0, 0}) { return router.drainHooks(key); }
};

flh::Subject subject(fl::HandleData const& h, uint32_t entry) { return flh::Subject{h, entry}; }

fl::EventValue handle(fl::HandleData const& h) { return fl::EventValue(h); }
}  // namespace

TEST_CASE("core hooks: a core without C1-C4 still logs the missing hook once and a core with them does not")
{
    {
        HooksRig rig(0);
        rig.ok("RegisterCreatureEvent(1, 1, function() end)\n"
               "RegisterGameObjectEvent(1, 12, function() end)\n"
               "RegisterItemEvent(1, 2, function() end)\n"
               "RegisterPlayerEvent(42, function() end)\n"
               "RegisterItemGossipEvent(1, 2, function() end)\n"
               "RegisterPlayerGossipEvent(7, 2, function() end)\n"
               "RegisterServerEvent(31, function() end)\n"
               "RegisterServerEvent(32, function() end)");
        CHECK(rig.host.logContains("creature ENTER_COMBAT needs core hook C1"));
        CHECK(rig.host.logContains("gameobject ADD needs core hook C2"));
        CHECK(rig.host.logContains("item USE needs core hook C3"));
        CHECK(rig.host.logContains("item gossip SELECT needs core hook C3"));
        CHECK(rig.host.logContains("player gossip SELECT needs core hook C3"));
        CHECK(rig.host.logContains("player COMMAND needs core hook C4"));
        CHECK(rig.host.logContains("server DELETE_CREATURE needs core hook C1"));
        CHECK(rig.host.logContains("server DELETE_GAMEOBJECT needs core hook C2"));
    }
    {
        HooksRig rig(kAllHooks);
        rig.ok("RegisterCreatureEvent(1, 1, function() end)\n"
               "RegisterGameObjectEvent(1, 12, function() end)\n"
               "RegisterItemEvent(1, 2, function() end)\n"
               "RegisterPlayerEvent(42, function() end)\n"
               "RegisterItemGossipEvent(1, 2, function() end)\n"
               "RegisterPlayerGossipEvent(7, 2, function() end)\n"
               "RegisterServerEvent(31, function() end)\n"
               "RegisterServerEvent(32, function() end)");
        CHECK_FALSE(rig.host.logContains("needs core hook C1"));
        CHECK_FALSE(rig.host.logContains("needs core hook C2"));
        CHECK_FALSE(rig.host.logContains("needs core hook C3"));
        CHECK_FALSE(rig.host.logContains("needs core hook C4"));
    }
}

TEST_CASE("core hooks: only the hooks the core has silence the log, and the AI-only events always name the ScriptName")
{
    // A core with ADD/REMOVE but not the combat notifications: ENTER_COMBAT still needs the hook.
    HooksRig partial(static_cast<unsigned>(fl::CoreHook::AllCreature) | static_cast<unsigned>(fl::CoreHook::AllGameObject));
    partial.ok("RegisterCreatureEvent(1, 36, function() end)\n"
               "RegisterCreatureEvent(1, 1, function() end)\n"
               "RegisterGameObjectEvent(1, 12, function() end)\n"
               "RegisterGameObjectEvent(1, 14, function() end)");
    CHECK_FALSE(partial.host.logContains("creature ADD needs core hook"));
    CHECK(partial.host.logContains("creature ENTER_COMBAT needs core hook C1"));
    CHECK_FALSE(partial.host.logContains("gameobject ADD needs core hook"));
    CHECK(partial.host.logContains("gameobject USE needs core hook C2"));

    // With every hook: the hooked events are silent; the others fire only through the ScriptName, and say so.
    HooksRig full(kAllHooks);
    full.ok("RegisterCreatureEvent(1, 1, function() end)\n"
            "RegisterCreatureEvent(1, 4, function() end)\n"
            "RegisterCreatureEvent(1, 5, function() end)\n"
            "RegisterCreatureEvent(1, 23, function() end)\n"
            "RegisterCreatureEvent(1, 2, function() end)\n"
            "RegisterCreatureEvent(1, 7, function() end)\n"
            "RegisterGameObjectEvent(1, 1, function() end)\n"
            "RegisterGameObjectEvent(1, 14, function() end)");
    CHECK_FALSE(full.host.logContains("ENTER_COMBAT"));
    CHECK_FALSE(full.host.logContains("creature DIED"));
    CHECK_FALSE(full.host.logContains("creature SPAWN"));
    // The hook is Appear (spawn/respawn only), not every AI reset: RESET stays a ScriptName event and says so.
    CHECK(full.host.logContains("creature RESET fires only for creatures whose ScriptName is forever_lua"));
    CHECK_FALSE(full.host.logContains("creature LEAVE_COMBAT"));
    CHECK_FALSE(full.host.logContains("gameobject USE"));
    CHECK(full.host.logContains("creature AIUPDATE fires only for creatures whose ScriptName is forever_lua"));
    CHECK(full.host.logContains("gameobject AIUPDATE fires only for game objects whose ScriptName is forever_lua_gameobject"));
    CHECK_FALSE(full.host.logContains("needs core hook C1"));
    CHECK_FALSE(full.host.logContains("needs core hook C2"));
}

TEST_CASE("hook dispatch: the creature notifications only queue; the map update runs them, for a creature without a ScriptName only")
{
    {
        HooksRig rig(kAllHooks);
        fl::HandleData c = FakeHost::creature(1, 0, 0, 77);
        fl::HandleData killer = FakeHost::player(5, 0, 0);
        rig.host.add(c, fl::ObjectInfo());
        rig.host.add(killer, fl::ObjectInfo());
        rig.ok("log = {}\n"
               "RegisterCreatureEvent(77, 1, function(ev, cr, who) log[#log+1] = 'e' .. ev .. ':' .. cr:GetGUIDLow() .. ':' .. who:GetGUIDLow() end)\n"
               "RegisterCreatureEvent(77, 2, function(ev, cr) log[#log+1] = 'e' .. ev .. ':' .. cr:GetGUIDLow() return true end)\n"
               "RegisterCreatureEvent(77, 4, function(ev, cr, k) log[#log+1] = 'e' .. ev .. ':' .. k:GetGUIDLow() end)\n"
               "RegisterCreatureEvent(77, 5, function(ev, cr) log[#log+1] = 'e' .. ev end)");
        rig.dispatch.creatureEngaged(subject(c, 77), 0, handle(killer));
        rig.dispatch.creatureLeftCombat(subject(c, 77), 0);
        rig.dispatch.creatureDied(subject(c, 77), 0, handle(killer));
        rig.dispatch.creatureAppeared(subject(c, 77), 0);
        // A hook runs no Lua: four events wait as plain values, nothing reached the handlers (or the host).
        CHECK(rig.ok("return #log") == "0");
        CHECK(flh::hookQueue().size() == 4);
        // The map's next update runs them, in the order of the hooks.
        CHECK(rig.drain() == 4);
        CHECK(rig.ok("return table.concat(log, ',')") == "e1:1:5,e2:1,e4:5,e5");
        CHECK(flh::hookQueue().size() == 0);
        // A creature with a ScriptName keeps its own AI: its events come from the ScriptName path, never from here.
        rig.ok("log = {}");
        rig.dispatch.creatureEngaged(subject(c, 77), 123, handle(killer));
        rig.dispatch.creatureLeftCombat(subject(c, 77), 123);
        rig.dispatch.creatureDied(subject(c, 77), 123, handle(killer));
        rig.dispatch.creatureAppeared(subject(c, 77), 123);
        CHECK(flh::hookQueue().size() == 0);
        CHECK(rig.drain() == 0);
        CHECK(rig.ok("return #log") == "0");
        // Another entry is not this handler's: nothing is even queued for it.
        fl::HandleData other = FakeHost::creature(2, 0, 0, 78);
        rig.host.add(other, fl::ObjectInfo());
        rig.dispatch.creatureEngaged(subject(other, 78), 0, handle(killer));
        CHECK(flh::hookQueue().size() == 0);
        CHECK(rig.ok("return #log") == "0");
    }
    {
        // A core with ADD/REMOVE but not the creature notifications, and a core with none: nothing is queued, nothing
        // fires (the build without the hooks behaves as before E6).
        for (unsigned mask : {static_cast<unsigned>(fl::CoreHook::AllCreature), 0u})
        {
            HooksRig rig(mask);
            fl::HandleData c = FakeHost::creature(1, 0, 0, 77);
            rig.host.add(c, fl::ObjectInfo());
            rig.ok("n = 0\n"
                   "RegisterCreatureEvent(77, 1, function() n = n + 1 end)\n"
                   "RegisterCreatureEvent(77, 2, function() n = n + 1 end)\n"
                   "RegisterCreatureEvent(77, 4, function() n = n + 1 end)\n"
                   "RegisterCreatureEvent(77, 5, function() n = n + 1 end)");
            rig.dispatch.creatureEngaged(subject(c, 77), 0, fl::EventValue());
            rig.dispatch.creatureLeftCombat(subject(c, 77), 0);
            rig.dispatch.creatureDied(subject(c, 77), 0, fl::EventValue());
            rig.dispatch.creatureAppeared(subject(c, 77), 0);
            CHECK(flh::hookQueue().size() == 0);
            CHECK(rig.drain() == 0);
            CHECK(rig.ok("return n") == "0");
        }
    }
}

TEST_CASE("hook dispatch: a LEAVE_COMBAT handler that despawns its creature never touches it inside the core's combat-end loop")
{
    // The model of CombatManager::EndAllPvECombat: the player's combat ends with two mobs, and for each reference the core
    // ends the creature's engagement and calls OnCreatureLeaveCombat. A handler that despawns or kills (here: despawns its
    // creature and the OTHER one) frees a reference the loop still holds if it runs inside the hook.
    HooksRig rig(kAllHooks);
    fl::HandleData a = FakeHost::creature(1, 0, 0, 77);
    fl::HandleData b = FakeHost::creature(2, 0, 0, 77);
    rig.host.add(a, fl::ObjectInfo());
    rig.host.add(b, fl::ObjectInfo());
    rig.host.gameResults["creature.despawn"] = {};
    rig.ok("RegisterCreatureEvent(77, 2, function(ev, cr)\n"
           "  cr:DespawnOrUnsummon()                          -- despawn on leave combat\n"
           "end)\n"
           "RegisterCreatureEvent(77, 4, function(ev, cr) cr:DespawnOrUnsummon() end)");
    size_t const before = rig.host.gameCalls.size();
    for (fl::HandleData const* mob : {&a, &b})   // the core's loop over the combat references
    {
        rig.dispatch.creatureLeftCombat(subject(*mob, 77), 0);
        rig.dispatch.creatureDied(subject(*mob, 77), 0, fl::EventValue());
    }
    // Inside the loop (the hooks) the host saw nothing: no despawn, no kill, no object access at all.
    CHECK(rig.host.gameCalls.size() == before);
    CHECK(flh::hookQueue().size() == 4);
    // After the loop, the map's update runs the handlers: only now the calls reach the host.
    CHECK(rig.drain() == 4);
    size_t despawns = 0;
    for (size_t i = before; i < rig.host.gameCalls.size(); ++i)
        if (rig.host.gameCalls[i] == "creature.despawn")
            ++despawns;
    CHECK(despawns == 4);
}

TEST_CASE("hook dispatch: the drain looks the GUID up again; a gone object drops its event, a REMOVE still fires")
{
    HooksRig rig(kAllHooks);
    fl::HandleData c = FakeHost::creature(1, 0, 0, 77);
    fl::HandleData go = FakeHost::gameObject(2, 0, 0, 88);
    fl::HandleData pl = FakeHost::player(11, 0, 0);
    rig.host.add(c, fl::ObjectInfo());
    rig.host.add(go, fl::ObjectInfo());
    rig.host.add(pl, fl::ObjectInfo());
    rig.ok("log = {}\n"
           "RegisterCreatureEvent(77, 2, function(ev, cr) log[#log+1] = 'leave' end)\n"
           "RegisterCreatureEvent(77, 37, function(ev, cr) log[#log+1] = 'removed:' .. tostring(cr:IsInWorld()) end)\n"
           "RegisterGameObjectEvent(88, 14, function(ev, o, p) log[#log+1] = 'use' end)\n"
           "RegisterGameObjectEvent(88, 13, function(ev, o) log[#log+1] = 'goremoved:' .. tostring(o:IsInWorld()) end)\n"
           "RegisterServerEvent(31, function(ev, o) log[#log+1] = 'del:' .. tostring(o:IsInWorld()) end)");
    // The creature leaves combat and then leaves the world; the game object is used and then removed, all before the map
    // updates again.
    rig.dispatch.creatureLeftCombat(subject(c, 77), 0);
    rig.dispatch.gameObjectUsed(subject(go, 88), 0, handle(pl));
    rig.dispatch.creatureRemoved(subject(c, 77));
    rig.dispatch.gameObjectRemoved(subject(go, 88));
    rig.host.remove(c);
    rig.host.remove(go);
    CHECK(rig.ok("return #log") == "0");
    CHECK(flh::hookQueue().size() == 5);   // leave, use, creature remove + server 31, game object remove (no 32 handler)
    rig.drain();
    // LEAVE_COMBAT and USE found their objects gone and were dropped (debug line); the REMOVE and the server delete events
    // are about a gone object: they fire, and the handle resolves to nothing (IsInWorld is false, no pointer exists).
    CHECK(rig.host.logContains("a queued creature event 2 was dropped: the object is gone"));
    CHECK(rig.host.logContains("a queued gameobject event 14 was dropped: the object is gone"));
    CHECK(rig.ok("return table.concat(log, ',')") == "removed:false,del:false,goremoved:false");
}

TEST_CASE("player map change event: a login + map change resolves the player by GUID at the drain, so GetName works")
{
    // Live finding: PLAYER_EVENT 28 fires inside Map::AddPlayerToMap, before the core puts a logging-in player into its
    // player table, so a handler's GetName raised "Player no longer exists" on every login.
    HooksRig rig(kAllHooks);
    fl::HandleData pl = FakeHost::player(11, 0, 0);
    rig.ok("names = {} RegisterPlayerEvent(28, function(ev, p) names[#names+1] = p:GetName() .. '@' .. p:GetMapId() end)");
    // The hook: the player is on map 0 but not yet in the player table (the host cannot find it).
    CHECK_FALSE(rig.host.lookup(pl, nullptr));
    rig.dispatch.playerMapChanged(pl);
    CHECK(rig.ok("return #names") == "0");        // nothing ran inside the hook
    CHECK(flh::hookQueue().size() == 1);
    // The login completes (the player is in the table) and the player is teleported to map 1 before the next update.
    fl::ObjectInfo info;
    info.name = "Hyperion";
    rig.host.add(pl, info);
    rig.host.moveTo(pl, 1, 0);
    CHECK(rig.drain(flh::MapKey{0, 0}) == 0);     // map 0's update: the player is not on map 0 any more, followed
    CHECK(flh::hookQueue().size() == 1);
    CHECK(rig.drain(flh::MapKey{1, 0}) == 1);     // map 1's update: resolved in the map the player is in now
    CHECK(rig.ok("return names[1]") == "Hyperion@1");
    CHECK(flh::hookQueue().size() == 0);

    // A player that logged out again before the drain is dropped (debug line), nothing runs.
    HooksRig gone(kAllHooks);
    gone.ok("n = 0 RegisterPlayerEvent(28, function() n = n + 1 end)");
    gone.dispatch.playerMapChanged(pl);
    gone.drain();
    CHECK(gone.ok("return n") == "0");
    CHECK(gone.host.logContains("a queued player event 28 was dropped: the player is gone"));

    // No handler: nothing is queued.
    HooksRig quiet(kAllHooks);
    quiet.dispatch.playerMapChanged(pl);
    CHECK(flh::hookQueue().size() == 0);
}

namespace
{
// Map states on (E4): the router and the dispatch route like the module (the state of the key's map).
struct StatesRig
{
    FakeHost host;
    TempDir dir;
    fl::Config cfg;
    std::unique_ptr<fl::StateSet> states;
    std::unique_ptr<flh::HookDispatch> dispatch;
    std::unique_ptr<flh::EluRouter> router;
    StatesRig()
    {
        cfg = testConfig(dir.path());
        cfg.mode = fl::EngineMode::Eluna;
        cfg.eventBudget = {0, 0};
        cfg.commandBudget = {0, 0};
        cfg.loadBudget = {0, 0};
        cfg.tickBudgetMs = 0;
        cfg.mapStates = 1;
        cfg.coreHooks = kAllHooks;
        states = std::make_unique<fl::StateSet>(host, cfg);
        REQUIRE(states->start());
        auto route = [this](fl::EluKind kind, std::optional<flh::MapKey> map) {
            std::optional<fl::StateSet::Key> key;
            if (map)
                key = fl::StateSet::Key{static_cast<int32_t>(map->map), map->instance};
            return states->route(kind, key);
        };
        dispatch = std::make_unique<flh::HookDispatch>(route);
        router = std::make_unique<flh::EluRouter>(route);
        flh::hookQueue().clear();
        flh::creationQueue().clear();
    }
};
}  // namespace

TEST_CASE("transport: add and remove are routed by the map recorded at hook time, never by the drain or the handle")
{
    StatesRig rig;
    rig.dir.write("t.lua",
                  "RegisterGameObjectEvent(88, 13, function(ev, o) PrintInfo('rm in ' .. GetStateMapId() .. ' inworld=' .. tostring(o:IsInWorld())) end)\n"
                  "RegisterGameObjectEvent(88, 12, function(ev, o) PrintInfo('add in ' .. GetStateMapId()) end)\n");
    fl::LoadReport const rep = rig.states->load();
    REQUIRE(rep.errors.empty());
    REQUIRE(rig.states->openMap(0, 0) != nullptr);
    REQUIRE(rig.states->openMap(1, 0) != nullptr);
    // A transport crosses from map 0 to map 1. The object's own idea of its map (the handle) already names the destination
    // when the REMOVE hook for the map it is leaving runs (review: the core may update it before the notification).
    fl::HandleData ship = FakeHost::gameObject(2, 1, 0, 88);
    rig.host.add(ship, fl::ObjectInfo());
    flh::HookWhen leaving;
    leaving.map = flh::MapKey{0, 0};
    flh::HookWhen entering;
    entering.map = flh::MapKey{1, 0};
    rig.dispatch->gameObjectRemoved(flh::Subject{ship, 88}, leaving);
    rig.dispatch->gameObjectAdded(flh::Subject{ship, 88}, entering);
    CHECK_FALSE(rig.host.logContains("rm in"));   // nothing ran inside the hooks
    // The destination's update drains its own queue: the ADD, in map 1's state. Nothing of the REMOVE.
    CHECK(rig.router->drainHooks(flh::MapKey{1, 0}) == 1);
    CHECK(rig.host.logContains("add in 1"));
    CHECK_FALSE(rig.host.logContains("rm in"));
    // The source's update drains the REMOVE, in map 0's state, with a handle that never resolves.
    CHECK(rig.router->drainHooks(flh::MapKey{0, 0}) == 1);
    CHECK(rig.host.logContains("rm in 0 inworld=false"));
    CHECK_FALSE(rig.host.logContains("rm in 1"));
}

TEST_CASE("creation replay: lifecycle events of an object that is gone are dropped, a REMOVE delivers a GUID-only handle")
{
    HooksRig rig(kAllHooks);
    fl::HandleData c = FakeHost::creature(7, 30, 5, 77);
    // The creature was added, appeared, died and was removed while the map was being created; it is not in the world now.
    rig.ok("log = {}\n"
           "RegisterCreatureEvent(77, 36, function(ev, o) log[#log+1] = 'add' end)\n"
           "RegisterCreatureEvent(77, 5, function(ev, o) log[#log+1] = 'spawn' end)\n"
           "RegisterCreatureEvent(77, 4, function(ev, o) log[#log+1] = 'died' end)\n"
           "RegisterCreatureEvent(77, 37, function(ev, o)\n"
           "  local ok = pcall(function() return o:GetName() end)\n"
           "  log[#log+1] = 'rm:' .. o:GetGUIDLow() .. ':' .. tostring(o:IsInWorld()) .. ':' .. tostring(ok)\n"
           "end)");
    flh::HookWhen const creating{true, 1000, flh::MapKey{30, 5}};
    rig.dispatch.creatureAdded(subject(c, 77), creating);
    rig.dispatch.creatureAppeared(subject(c, 77), 0, creating);
    rig.dispatch.creatureDied(subject(c, 77), 0, fl::EventValue(), creating);
    rig.dispatch.creatureRemoved(subject(c, 77), creating);
    CHECK(flh::creationQueue().size() == 4);
    CHECK(rig.router.replayCreation(flh::MapKey{30, 5}) == 1);   // only the REMOVE ran
    CHECK(rig.ok("return table.concat(log, ',')") == "rm:7:false:false");
    CHECK(rig.host.logContains("a queued creature event 36 was dropped: the object is gone"));
    CHECK(rig.host.logContains("a queued creature event 5 was dropped: the object is gone"));
    CHECK(rig.host.logContains("a queued creature event 4 was dropped: the object is gone"));

    // The same GUID is in the world again at the drain (a respawn, a transport coming back): the REMOVE of the earlier
    // life still gets only the GUID, never a handle that resolves to the new one.
    rig.host.add(c, fl::ObjectInfo());
    rig.ok("log = {}");
    rig.dispatch.creatureRemoved(subject(c, 77), flh::HookWhen{false, 1, flh::MapKey{30, 5}});
    CHECK(rig.drain(flh::MapKey{30, 5}) == 1);
    CHECK(rig.ok("return table.concat(log, ',')") == "rm:7:false:false");
}

TEST_CASE("hook queue saturation is never silent: counted per map, one warning at once, the rest flushed at the drain")
{
    HooksRig rig(kAllHooks);
    fl::HandleData c = FakeHost::creature(1, 0, 0, 77);
    rig.host.add(c, fl::ObjectInfo());
    rig.ok("n = 0 RegisterCreatureEvent(77, 2, function() n = n + 1 end)");
    for (size_t i = 0; i < flh::HookQueue::kMaxPerMap + 3; ++i)
        rig.dispatch.creatureLeftCombat(subject(c, 77), 0, flh::HookWhen{false, 5});
    CHECK(flh::hookQueue().size() == flh::HookQueue::kMaxPerMap);
    CHECK(flh::hookQueue().dropped() == 3);
    CHECK(rig.host.logContains("1 core-hook events were dropped (the event queue of the map is full)"));   // at once
    CHECK_FALSE(rig.host.logContains("2 core-hook events were dropped"));                                  // rate limited
    rig.drain();   // the rest is flushed with the drain
    CHECK(rig.host.logContains("2 core-hook events were dropped"));
    CHECK(rig.ok("return n") == std::to_string(flh::HookQueue::kMaxPerMap));

    // The creation queue is bounded too, and loses nothing silently.
    HooksRig creating(kAllHooks);
    creating.host.add(c, fl::ObjectInfo());
    creating.ok("RegisterCreatureEvent(77, 36, function() end)");
    for (size_t i = 0; i < flh::CreationQueue::kMaxPerMap + 1; ++i)
        creating.dispatch.creatureAdded(subject(c, 77), flh::HookWhen{true, 9});
    CHECK(creating.host.logContains("1 core-hook events were dropped"));
}

TEST_CASE("MAP_PLAYER_ENTER (map 4 / server 21) resolves the player by GUID at the drain, like event 28")
{
    // The same exposure as event 28: Map::AddPlayerToMap fires OnPlayerEnter before a logging-in player is in the core's
    // player table.
    HooksRig rig(kAllHooks);
    fl::HandleData pl = FakeHost::player(11, 0, 0);
    fl::HandleData map;
    map.kind = fl::ObjKind::Map;
    map.mapId = 0;
    rig.ok("names = {}\n"
           "RegisterMapEvent(0, 4, function(ev, inst, m, p) names[#names+1] = 'map:' .. p:GetName() end)\n"
           "RegisterServerEvent(21, function(ev, m, p) names[#names+1] = 'server:' .. p:GetName() end)");
    CHECK_FALSE(rig.host.lookup(pl, nullptr));   // not in the player table yet
    rig.router.queuePlayerEnter(false, flh::MapKey{0, 0}, 0, false, pl, {fl::EventValue(), fl::EventValue(map), fl::EventValue(pl)},
                                {fl::EventValue(map), fl::EventValue(pl)}, 4, 21);
    CHECK(flh::hookQueue().size() == 2);
    CHECK(rig.ok("return #names") == "0");
    fl::ObjectInfo info;
    info.name = "Hyperion";
    rig.host.add(pl, info);
    CHECK(rig.drain(flh::MapKey{0, 0}) == 2);
    CHECK(rig.ok("return table.concat(names, ',')") == "map:Hyperion,server:Hyperion");

    // A player that already left the map is not delivered for it (the event is about this map).
    rig.ok("names = {}");
    rig.router.queuePlayerEnter(false, flh::MapKey{0, 0}, 0, false, pl, {fl::EventValue(), fl::EventValue(map), fl::EventValue(pl)},
                                {fl::EventValue(map), fl::EventValue(pl)}, 4, 21);
    rig.host.moveTo(pl, 1, 0);
    CHECK(rig.drain(flh::MapKey{0, 0}) == 0);
    CHECK(rig.ok("return #names") == "0");
    CHECK(flh::hookQueue().size() == 0);
    CHECK(rig.host.logContains("was dropped: the player left the map"));

    // Nobody listening: nothing is queued.
    HooksRig quiet(kAllHooks);
    quiet.router.queuePlayerEnter(false, flh::MapKey{0, 0}, 0, false, pl, {}, {}, 4, 21);
    CHECK(flh::hookQueue().size() == 0);
}

TEST_CASE("hook queue: per-map cap, the world sweep and order per map")
{
    flh::HookQueue q;
    flh::MapKey const a{1, 0}, b{2, 0};
    for (size_t i = 0; i < flh::HookQueue::kMaxPerMap; ++i)
    {
        flh::DeferredEvent ev;
        ev.event = static_cast<uint32_t>(i);
        REQUIRE(q.push(a, ev, 1000));
    }
    flh::DeferredEvent extra;
    CHECK_FALSE(q.push(a, extra, 1000));   // the per-map cap: dropped, counted
    CHECK(q.dropped() == 1);
    flh::DeferredEvent first;
    first.event = 7;
    CHECK(q.push(b, first, 50000));         // another map is not affected
    size_t swept = 0;
    uint64_t sweptMap = 0;
    // The one-minute sweep drops what waited too long (map a) and keeps the young events (map b).
    q.sweep(62000, 60000, [&](flh::MapKey k, size_t n) { swept += n; sweptMap = k.map; });
    CHECK(swept == flh::HookQueue::kMaxPerMap);
    CHECK(sweptMap == 1);
    CHECK(q.size() == 1);
    std::vector<flh::DeferredEvent> events = q.take(b);
    REQUIRE(events.size() == 1);
    CHECK(events[0].event == 7);
    CHECK(q.size() == 0);
    // Order per map is the order of the pushes.
    for (uint32_t i = 0; i < 5; ++i)
    {
        flh::DeferredEvent ev;
        ev.event = i;
        q.push(a, ev, 1);
    }
    std::vector<flh::DeferredEvent> ordered = q.take(a);
    REQUIRE(ordered.size() == 5);
    for (uint32_t i = 0; i < 5; ++i)
        CHECK(ordered[i].event == i);
}

TEST_CASE("hook dispatch: the game object use notification queues and skips an object with a ScriptName")
{
    HooksRig rig(kAllHooks);
    fl::HandleData go = FakeHost::gameObject(2, 0, 0, 88);
    fl::HandleData pl = FakeHost::player(11, 0, 0);
    rig.host.add(go, fl::ObjectInfo());
    rig.host.add(pl, fl::ObjectInfo());
    rig.ok("log = {}\nRegisterGameObjectEvent(88, 14, function(ev, o, p) log[#log+1] = ev .. ':' .. o:GetGUIDLow() .. ':' .. p:GetGUIDLow() end)");
    rig.dispatch.gameObjectUsed(subject(go, 88), 0, handle(pl));
    rig.dispatch.gameObjectUsed(subject(go, 88), 55, handle(pl));   // a ScriptName game object fires it from its own AI
    CHECK(rig.ok("return #log") == "0");
    CHECK(flh::hookQueue().size() == 1);
    rig.drain();
    CHECK(rig.ok("return table.concat(log, ',')") == "14:2:11");
    HooksRig none(static_cast<unsigned>(fl::CoreHook::AllGameObject));
    none.host.add(go, fl::ObjectInfo());
    none.ok("n = 0 RegisterGameObjectEvent(88, 14, function() n = n + 1 end)");
    none.dispatch.gameObjectUsed(subject(go, 88), 0, handle(pl));
    CHECK(flh::hookQueue().size() == 0);
    CHECK(none.drain() == 0);
    CHECK(none.ok("return n") == "0");
}

TEST_CASE("item use: a handler may not destroy the item the core still uses")
{
    HooksRig rig(kAllHooks);
    fl::HandleData pl = FakeHost::player(11, 0, 0);
    fl::HandleData item = FakeHost::item(13);
    fl::HandleData other = FakeHost::item(14);
    rig.host.add(pl, fl::ObjectInfo());
    rig.host.add(item, fl::ObjectInfo());
    rig.host.gameResults["player.removeItem"] = {};
    rig.ok("RegisterItemEvent(90003, 2, function(ev, player, it)\n"
           "  player:RemoveItem(it)         -- the item being used: refused\n"
           "  player:RemoveItem(90003, 1)   -- by its entry: refused\n"
           "  player:RemoveItem(90004, 1)   -- another entry: allowed\n"
           "end)");
    rig.dispatch.itemUse(std::nullopt, 90003, handle(pl), handle(item), fl::EventValue());
    size_t removes = 0;
    for (std::string const& op : rig.host.gameCalls)
        if (op == "player.removeItem")
            ++removes;
    CHECK(removes == 1);
    CHECK(rig.host.logContains("Player:RemoveItem on the item being used was refused"));
    // The scope ends with the hook: the same calls outside a handler reach the host.
    CHECK_FALSE(fl::ItemUseScope::blocks(&item.guid, 0));
    CHECK_FALSE(fl::ItemUseScope::blocks(nullptr, 90003));
    {
        fl::ItemUseScope outer(item.guid, 90003);
        CHECK(fl::ItemUseScope::blocks(&item.guid, 0));
        CHECK_FALSE(fl::ItemUseScope::blocks(&other.guid, 0));
        {
            fl::ItemUseScope inner(other.guid, 90004);   // scopes nest
            CHECK(fl::ItemUseScope::blocks(&item.guid, 0));
            CHECK(fl::ItemUseScope::blocks(nullptr, 90004));
        }
        CHECK_FALSE(fl::ItemUseScope::blocks(nullptr, 90004));
    }
    CHECK_FALSE(fl::ItemUseScope::blocks(&item.guid, 0));
}

TEST_CASE("hook dispatch: creature and game object ADD/REMOVE are queued for every entry, only when C1/C2 are present")
{
    {
        HooksRig rig(0);
        fl::HandleData c = FakeHost::creature(1, 0, 0, 77);
        fl::HandleData go = FakeHost::gameObject(2, 0, 0, 88);
        rig.host.add(c, fl::ObjectInfo());
        rig.host.add(go, fl::ObjectInfo());
        rig.ok("n = 0\n"
               "RegisterCreatureEvent(77, 36, function() n = n + 1 end)\n"
               "RegisterCreatureEvent(77, 37, function() n = n + 1 end)\n"
               "RegisterGameObjectEvent(88, 12, function() n = n + 1 end)\n"
               "RegisterGameObjectEvent(88, 13, function() n = n + 1 end)\n"
               "RegisterServerEvent(31, function() n = n + 1 end)\n"
               "RegisterServerEvent(32, function() n = n + 1 end)");
        rig.dispatch.creatureAdded(subject(c, 77));
        rig.dispatch.creatureRemoved(subject(c, 77));
        rig.dispatch.gameObjectAdded(subject(go, 88));
        rig.dispatch.gameObjectRemoved(subject(go, 88));
        CHECK(flh::hookQueue().size() == 0);
        CHECK(rig.drain() == 0);
        CHECK(rig.ok("return n") == "0");   // the adapters are not compiled into a core without the hooks
    }
    {
        HooksRig rig(kAllHooks);
        fl::HandleData c = FakeHost::creature(1, 0, 0, 77);
        fl::HandleData go = FakeHost::gameObject(2, 0, 0, 88);
        rig.host.add(c, fl::ObjectInfo());
        rig.host.add(go, fl::ObjectInfo());
        rig.ok("added, removed, goadded, goremoved, s31, s32 = 0, 0, 0, 0, 0, 0\n"
               "RegisterCreatureEvent(77, 36, function(ev, o) added = added + 1 end)\n"
               "RegisterCreatureEvent(77, 37, function(ev, o) removed = removed + o:GetGUIDLow() end)\n"
               "RegisterGameObjectEvent(88, 12, function() goadded = goadded + 1 end)\n"
               "RegisterGameObjectEvent(88, 13, function() goremoved = goremoved + 1 end)\n"
               "RegisterServerEvent(31, function(ev, o) s31 = s31 + o:GetGUIDLow() end)\n"
               "RegisterServerEvent(32, function(ev, o) s32 = s32 + o:GetGUIDLow() end)");
        rig.dispatch.creatureAdded(subject(c, 77));
        rig.dispatch.creatureRemoved(subject(c, 77));
        rig.dispatch.gameObjectAdded(subject(go, 88));
        rig.dispatch.gameObjectRemoved(subject(go, 88));
        CHECK(rig.ok("return table.concat({added, removed, goadded, goremoved, s31, s32}, ',')") == "0,0,0,0,0,0");   // queued
        CHECK(rig.drain() == 6);   // the map's update
        CHECK(rig.ok("return table.concat({added, removed, goadded, goremoved, s31, s32}, ',')") == "1,1,1,1,1,2");
    }
}

TEST_CASE("hook dispatch: player and item gossip select use the docs argument order (C3)")
{
    HooksRig rig(kAllHooks);
    fl::HandleData pl = FakeHost::player(11, 0, 0);
    fl::HandleData other = FakeHost::player(12, 0, 0);
    fl::HandleData item = FakeHost::item(13);
    rig.host.add(pl, fl::ObjectInfo());
    rig.host.add(other, fl::ObjectInfo());
    rig.ok("psel, isel = nil, nil\n"
           "RegisterPlayerGossipEvent(56, 2, function(ev, player, object, sender, intid, code, menu)\n"
           "  psel = {ev, player:GetGUIDLow(), object:GetGUIDLow(), sender, intid, tostring(code), menu} end)\n"
           "RegisterItemGossipEvent(90003, 2, function(ev, player, object, sender, intid, code, menu)\n"
           "  isel = {ev, player:GetGUIDLow(), object:GetGUIDLow(), sender, intid, tostring(code), menu} end)");
    fl::EventResult p = rig.dispatch.playerGossipSelect(std::nullopt, 56, handle(pl), handle(other), 9, 4,
                                                        std::string("1234"));
    CHECK(p.calls == 1);
    CHECK(rig.ok("return table.concat(psel, ',')") == "2,11,12,9,4,1234,56");
    fl::EventResult i = rig.dispatch.itemGossipSelect(std::nullopt, 90003, handle(pl), handle(item), 3, 2,
                                                      std::nullopt, 56);
    CHECK(i.calls == 1);
    CHECK(rig.ok("return table.concat(isel, ',')") == "2,11,13,3,2,nil,56");
    // Another menu id / entry is not this handler's.
    CHECK(rig.dispatch.playerGossipSelect(std::nullopt, 57, handle(pl), handle(other), 0, 0, std::nullopt).calls == 0);
    CHECK(rig.dispatch.itemGossipSelect(std::nullopt, 90004, handle(pl), handle(item), 0, 0, std::nullopt, 56).calls == 0);

    // A core with only the item-use half of C3 does not answer gossip selects.
    HooksRig noGossip(static_cast<unsigned>(fl::CoreHook::ItemUse));
    CHECK(noGossip.dispatch.playerGossipSelect(std::nullopt, 56, handle(pl), handle(other), 0, 0, std::nullopt).calls == 0);
    CHECK(noGossip.dispatch.itemGossipSelect(std::nullopt, 90003, handle(pl), handle(item), 0, 0, std::nullopt, 56)
              .calls == 0);
}

TEST_CASE("hook dispatch: the global item use fires USE and the item gossip HELLO and stops the use when a script says so")
{
    {
        HooksRig rig(kAllHooks);
        fl::HandleData pl = FakeHost::player(11, 0, 0);
        fl::HandleData item = FakeHost::item(13);
        rig.host.add(pl, fl::ObjectInfo());
        rig.ok("use, hello = 0, 0\n"
               "RegisterItemEvent(90003, 2, function(ev, player, item, target) use = use + 1 return false end)\n"
               "RegisterItemGossipEvent(90003, 1, function(ev, player, object) hello = hello + 1 end)");
        CHECK(rig.dispatch.itemUse(std::nullopt, 90003, handle(pl), handle(item), fl::EventValue()));
        CHECK(rig.ok("return table.concat({use, hello}, ',')") == "1,1");
    }
    {
        HooksRig rig(0);   // no item-use hook: the ItemScript path does it, not this dispatch
        fl::HandleData pl = FakeHost::player(11, 0, 0);
        fl::HandleData item = FakeHost::item(13);
        rig.host.add(pl, fl::ObjectInfo());
        rig.ok("use = 0 RegisterItemEvent(90003, 2, function() use = use + 1 return false end)");
        CHECK_FALSE(rig.dispatch.itemUse(std::nullopt, 90003, handle(pl), handle(item), fl::EventValue()));
        CHECK(rig.ok("return use") == "0");
    }
}

TEST_CASE("hook dispatch: the command event runs before the core and a false return stops the command (C4)")
{
    {
        HooksRig rig(kAllHooks);
        fl::HandleData pl = FakeHost::player(11, 0, 0);
        rig.host.add(pl, fl::ObjectInfo());
        rig.ok("seen = nil\n"
               "RegisterPlayerEvent(42, function(ev, player, command) seen = {ev, player:GetGUIDLow(), command} return false end)");
        CHECK(rig.dispatch.playerCommand(std::nullopt, handle(pl), "bot add"));
        CHECK(rig.ok("return table.concat(seen, ',')") == "42,11,bot add");
        // true = a handler returned false (the docs' "can return false"); no handler, no stop.
        rig.ok("ClearPlayerEvents()");
        CHECK_FALSE(rig.dispatch.playerCommand(std::nullopt, handle(pl), "bot add"));
    }
    {
        HooksRig rig(0);
        fl::HandleData pl = FakeHost::player(11, 0, 0);
        rig.host.add(pl, fl::ObjectInfo());
        rig.ok("seen = 0 RegisterPlayerEvent(42, function() seen = seen + 1 return false end)");
        CHECK_FALSE(rig.dispatch.playerCommand(std::nullopt, handle(pl), "bot add"));
        CHECK(rig.ok("return seen") == "0");
    }
}

TEST_CASE("RegisterCommand through C4: the claim is exact and the engine's security level keeps E3's GM gate")
{
    std::vector<fl::Engine::LuaCommand> cmds(2);
    cmds[0].name = "luahello";
    cmds[0].minSecurity = 0;
    cmds[1].name = "luakick";
    cmds[1].minSecurity = 5;

    flh::LuaCommandClaim c = flh::claimLuaCommand(cmds, "luahello there  you", 0);
    CHECK(c.mine);
    CHECK_FALSE(c.allowed);   // minSecurity 0 still needs security 1: the core's command slots always did
    CHECK(c.index == 0);
    CHECK(c.required == 1);
    CHECK(c.tail == "there  you");

    c = flh::claimLuaCommand(cmds, "  LuAHeLLo", 1);
    CHECK(c.mine);
    CHECK(c.allowed);
    CHECK(c.tail.empty());

    c = flh::claimLuaCommand(cmds, "luakick 7", 4);
    CHECK(c.mine);
    CHECK_FALSE(c.allowed);
    c = flh::claimLuaCommand(cmds, "luakick 7", 5);
    CHECK(c.allowed);
    CHECK(c.required == 5);
    CHECK(c.tail == "7");

    // No prefix match, no other command, no blank claim.
    CHECK_FALSE(flh::claimLuaCommand(cmds, "luahell", 255).mine);
    CHECK_FALSE(flh::claimLuaCommand(cmds, "lua", 255).mine);
    CHECK_FALSE(flh::claimLuaCommand(cmds, "", 255).mine);
    // A line forwarded with its dot still matches.
    CHECK(flh::claimLuaCommand(cmds, ".luahello x", 1).tail == "x");
}

TEST_CASE("hook dispatch: creature and game object hooks queue by GUID while the map is created and never keep a pointer")
{
    HooksRig rig(kAllHooks);
    fl::HandleData c = FakeHost::creature(1, 30, 5, 77);
    fl::HandleData go = FakeHost::gameObject(2, 30, 5, 88);
    rig.host.add(c, fl::ObjectInfo());
    rig.host.add(go, fl::ObjectInfo());
    rig.ok("log = {}\n"
           "RegisterCreatureEvent(77, 36, function(ev, o) log[#log+1] = 'add' end)\n"
           "RegisterCreatureEvent(77, 5, function(ev, o) log[#log+1] = 'spawn' end)\n"
           "RegisterGameObjectEvent(88, 12, function(ev, o) log[#log+1] = 'goadd' end)\n"
           "RegisterCreatureEvent(77, 4, function(ev, o) log[#log+1] = 'died' end)");
    flh::MapKey const key{30, 5};
    flh::HookWhen const creating{true, 1000};
    // CreateMap holds the core's exclusive map lock: no Lua runs, the events wait in the creation queue as plain values.
    rig.dispatch.creatureAdded(subject(c, 77), creating);
    rig.dispatch.creatureAppeared(subject(c, 77), 0, creating);
    rig.dispatch.gameObjectAdded(subject(go, 88), creating);
    CHECK(rig.ok("return #log") == "0");
    CHECK(flh::creationQueue().size() == 3);
    // A hook that arrives after the creation but before the replay goes behind the creation queue (the order is kept),
    // not into the hook queue.
    rig.dispatch.creatureDied(subject(c, 77), 0, fl::EventValue());
    CHECK(flh::creationQueue().size() == 4);
    CHECK(flh::hookQueue().size() == 0);
    flh::EluRouter router([&rig](fl::EluKind, std::optional<flh::MapKey>) { return unowned(rig.e()); });
    CHECK(router.replayCreation(key) == 4);
    CHECK(rig.ok("return table.concat(log, ',')") == "add,spawn,goadd,died");
    CHECK(flh::creationQueue().size() == 0);

    // A handler nobody registered is never queued (a creature's ADD during map creation costs nothing without Lua).
    HooksRig quiet(kAllHooks);
    quiet.dispatch.creatureAdded(subject(c, 99), creating);
    CHECK(flh::creationQueue().size() == 0);

    // The remove hooks run after the object left the world: the handler gets a handle (its GUID), the object resolves to
    // nothing, and the server delete events queue the same way.
    rig.ok("log = {} RegisterCreatureEvent(77, 37, function(ev, o) log[#log+1] = 'rm' .. o:GetGUIDLow() end)\n"
           "RegisterServerEvent(31, function(ev, o) log[#log+1] = 'del' end)");
    rig.dispatch.creatureRemoved(subject(c, 77), creating);
    CHECK(rig.ok("return #log") == "0");
    CHECK(flh::creationQueue().size() == 2);
    CHECK(router.replayCreation(key) == 2);
    CHECK(rig.ok("return table.concat(log, ',')") == "rm1,del");
}

// A hunter pet is kept in the core's Pet store, not the Creature store (Map::GetCreature never finds it): the lookup of a
// creature handle must fall back to GetPet, or a queued pet ADD resolves to nothing at the drain and is dropped.
namespace
{
struct PetGuid
{
    bool pet;
    bool IsPet() const { return pet; }
};
struct PetMap
{
    int creature = 1, petObj = 2;
    bool hasCreature = false, hasPet = false;
    int* GetCreature(PetGuid const&) { return hasCreature ? &creature : nullptr; }
    int* GetPet(PetGuid const&) { return hasPet ? &petObj : nullptr; }
};
}  // namespace

TEST_CASE("pet add: a creature handle with a Pet guid resolves through the Pet store")
{
    PetMap m;
    m.hasPet = true;
    CHECK(flh::findCreatureOrPet<int>(m, PetGuid{true}) == &m.petObj);     // the pet is found
    CHECK(flh::findCreatureOrPet<int>(m, PetGuid{false}) == nullptr);      // a non-pet guid never reads the Pet store
    m.hasCreature = true;
    CHECK(flh::findCreatureOrPet<int>(m, PetGuid{false}) == &m.creature);  // a plain creature is unchanged
    CHECK(flh::findCreatureOrPet<int>(m, PetGuid{true}) == &m.creature);
}

// The queued-event path for a pet: the AddWorld hook queues the ADD by its Pet GUID on the pet's map, the map's drain resolves
// the handle with the same lookup rule the host uses (GetCreature, then GetPet), and the handler runs once. With the
// fallback removed (the pre-fix lookup) the handle never resolves and the event is dropped.
TEST_CASE("pet add: a queued ADD with a Pet guid is delivered once on its own map, never on another map or after removal")
{
    auto run = [](bool fallback, uint32_t drainMap, bool removeFirst, size_t& ran, std::string& log) {
        HooksRig rig(kAllHooks);
        rig.host.petFallback = fallback;
        fl::HandleData p = FakeHost::pet(5, 1, 0, 299);
        rig.host.add(p, fl::ObjectInfo());
        rig.ok("log = {}\n"
               "RegisterCreatureEvent(299, 36, function(ev, o) log[#log+1] = 'add:' .. o:GetGUIDLow() end)");
        rig.dispatch.creatureAdded(subject(p, 299), flh::HookWhen{false, 1, flh::MapKey{1, 0}});
        if (removeFirst)
            rig.host.remove(p);
        ran = rig.drain(flh::MapKey{drainMap, 0});
        log = rig.ok("return table.concat(log, ',')");
    };
    size_t ran = 0;
    std::string log;
    run(true, 1, false, ran, log);
    CHECK(ran == 1);
    CHECK(log == "add:5");                      // delivered exactly once
    run(false, 1, false, ran, log);
    CHECK(ran == 0);                            // the pre-fix lookup drops the pet ADD
    CHECK(log.empty());
    run(true, 0, false, ran, log);              // another map's drain never runs it
    CHECK(ran == 0);
    CHECK(log.empty());
    run(true, 1, true, ran, log);               // dismissed before the drain: gone, dropped
    CHECK(ran == 0);
    CHECK(log.empty());
}
