// ForeverLua host side, E6: the event dispatch that only exists when the core has the C1-C4 hooks of
// the maintainers' API specification, section 6. The core adapters in EluHooks.cpp are thin: they turn core objects into plain
// values and call this class, which is game-free and unit-tested on the fake world (tests/test_core_hooks.cpp). Every
// method is a no-op when the running core's Config::coreHooks lacks the hook it needs, so the same tests cover the
// fake host that has the hooks and the one that does not.
#pragma once

#include "EluRouter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flh
{
// C1/C2 are notify-only: the module never replaces, wraps or owns a creature or game object AI. A creature that has a
// ScriptName keeps its own AI untouched and gets its Lua events only through the ScriptName forever_lua path (the
// engine's AI events); the combat notifications below skip it, so nothing fires twice and a C++ script's AI is never
// hidden. ADD/REMOVE are not AI events and fire for every creature.

// RegisterCommand claims a command before the core's permission checks when C4 is present (PR #379), so the engine's
// own security check is the only one before the script runs. The effective level is max(1, minSecurity): E3 promised
// that a minSecurity below 1 changes nothing because every core command slot needed the core's GM permission, and the
// hook path keeps that promise.
struct LuaCommandClaim
{
    bool mine = false;      // the first word is a registered Lua command
    bool allowed = false;   // the player's security reaches the effective level
    size_t index = 0;
    int security = 0;
    int required = 0;
    std::string name;
    std::string tail;
};

LuaCommandClaim claimLuaCommand(std::vector<fl::Engine::LuaCommand> const& commands, std::string_view command,
                                int security);

// Where in its map's life a creature/game object hook runs. EVERY hook event is queued, never run inside the hook (the core
// forbids a synchronous kill, despawn or combat end there; OnCreatureLeaveCombat can run inside the combat-end loop): the
// event waits as plain values by GUID and the map's next update (its own state when MapStates is on) or the world update
// drains it. `creating`: the core is still creating this object's map (MapManager::CreateMap holds its exclusive map lock
// while it loads grids, so the AddWorld hooks of the first creatures arrive before the map is registered): the event goes
// to the creation queue instead and is replayed at the map's first update. `nowMs` is the queues' clock.
struct HookWhen
{
    bool creating = false;
    uint64_t nowMs = 0;
    // The map the object is attached to AT HOOK TIME (the Map it is leaving for a REMOVE, entering for an ADD: a transport
    // crossing maps fires both on its own, one map each). The event is queued under this map and runs in its state; the
    // object's own idea of its map when the handle is built or when the queue drains never routes it. Empty: the handle's.
    std::optional<MapKey> map;
};

class HookDispatch
{
public:
    using EngineFn = EluRouter::EngineFn;
    explicit HookDispatch(EngineFn engine) : _router(engine), _engine(std::move(engine)) { }

    // C1/C2: every creature/game object added to or removed from the world, with the docs on_add/on_remove and
    // server on_world_delete_* arguments. The hook runs on the map thread but only QUEUES the event (see HookWhen); the Lua
    // handler runs at the next drain, in the subject's map scope, one update later than the core event. The remove hooks
    // run AFTER the object left the world and the core may free it right after: the subject is only a handle (its GUID),
    // the Lua side never gets a pointer, and a handle of an object that is out of the world resolves to nothing: its
    // methods return nothing and cannot touch the object. A queued event whose object is gone at the drain is dropped
    // (except REMOVE).
    void creatureAdded(Subject const& me, HookWhen when = {}) const;
    void creatureRemoved(Subject const& me, HookWhen when = {}) const;
    void gameObjectAdded(Subject const& me, HookWhen when = {}) const;
    void gameObjectRemoved(Subject const& me, HookWhen when = {}) const;

    // C1 notifications (OnCreatureEngage/LeaveCombat/JustDied/Appear): docs ENTER_COMBAT (1), LEAVE_COMBAT (2), DIED (4)
    // and SPAWN (5) for a creature WITHOUT a ScriptName (`scriptId` 0); a creature with one is skipped (its own AI is
    // untouched and fires them through the ScriptName path). Notify only: a script's return value changes nothing.
    // LeaveCombat fires on EVERY combat end of a living creature in the world (an evade, a scripted end, a pet or
    // guardian whose combat ends, a charm ending), not only on an evade; it can arrive without a matching Engage after a
    // respawn. JustDied may run with the creature already out of the world (its AI despawned it). Appear is for a living
    // creature on spawn or respawn only (not a corpse, not on every AI reset): the docs RESET (23) has no hook and stays
    // a ScriptName event.
    void creatureEngaged(Subject const& me, uint32_t scriptId, fl::EventValue target, HookWhen when = {}) const;
    void creatureLeftCombat(Subject const& me, uint32_t scriptId, HookWhen when = {}) const;
    void creatureDied(Subject const& me, uint32_t scriptId, fl::EventValue killer, HookWhen when = {}) const;
    void creatureAppeared(Subject const& me, uint32_t scriptId, HookWhen when = {}) const;
    // C2 use (OnGameObjectUse): docs USE (14), (event, gameobject, player), for a game object without a ScriptName;
    // notify only, the default action still runs (a script cannot stop the use from here).
    void gameObjectUsed(Subject const& me, uint32_t scriptId, fl::EventValue player, HookWhen when = {}) const;

    // PLAYER_EVENT 28 (map change, also fired on a login): the core calls OnMapChanged inside Map::AddPlayerToMap, before a
    // logging-in player is in the core's player table, so no lookup of the GUID works yet (a handler's GetName raised
    // "Player no longer exists"). The event is queued like the creature ones and runs at the drain, with the player
    // resolved by GUID in the map it is in then (a player that moved on is followed to that map's queue). Not a core-hook
    // event: it needs no C1-C4 hook, only the Eluna mode.
    void playerMapChanged(fl::HandleData const& player, HookWhen when = {}) const;

    // C3: a gossip select whose source is a player or an item (PR #377). The core has no handler for either source.
    // A player menu is keyed by its menu id, an item menu by the item entry; the object argument is the source.
    fl::EventResult playerGossipSelect(std::optional<MapKey> scope, uint32_t menuId, fl::EventValue player,
                                       fl::EventValue object, uint32_t sender, uint32_t intid,
                                       std::optional<std::string> const& code) const;
    fl::EventResult itemGossipSelect(std::optional<MapKey> scope, uint32_t entry, fl::EventValue player,
                                     fl::EventValue item, uint32_t sender, uint32_t intid,
                                     std::optional<std::string> const& code, uint32_t menuId) const;

    // C4: docs on_command (event, player, command); true = a handler returned false and the core must stop.
    bool playerCommand(std::optional<MapKey> scope, fl::EventValue player, std::string const& command) const;

    // C3: PlayerScript::OnItemUse for every item (PR #378): the item USE event and the item gossip HELLO, docs order.
    // True = stop the item use (the core then runs neither the ItemScript nor the item's spell).
    bool itemUse(std::optional<MapKey> scope, uint32_t entry, fl::EventValue player, fl::EventValue item,
                 fl::EventValue target) const;

private:
    bool has(fl::CoreHook hook) const;
    void object(HookWhen when, fl::EluKind kind, Subject const& me, uint32_t event, std::vector<fl::EventValue> args,
                bool gone = false) const;
    void serverEvent(HookWhen when, Subject const& me, uint32_t event, bool gone) const;
    EluRouter _router;
    EngineFn _engine;
};
}  // namespace flh
