// ForeverLua host side: how a core callback becomes an Eluna event. The adapter (EluHooks.cpp) turns the core's objects
// into plain data (a handle, an entry, an event id, an argument list); this router picks the register family, sets the
// map scope of the calling thread, merges the entry handlers with the unique-creature ones and folds the answer. It is
// game-free, so tests/test_host_router.cpp drives exactly this code on the fake world: creature gossip must reach the
// CreatureGossip family (not the creature combat events), every game object event runs inside its map scope, unique
// creature events key by guid + instance, item use fires the item gossip hello.
#pragma once

#include "AccessRules.h"
#include "CreationQueue.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flh
{
// The creature or game object a callback is about. `handle` carries its map and instance (its scope).
struct Subject
{
    fl::HandleData handle;
    uint32_t entry = 0;
};

class EluRouter
{
public:
    // E4: the state of one event: the map state for a map scope and a map-bound family, the world state otherwise;
    // null drops the event (its map state is loading or closing). The pointer pins the state for the dispatch.
    using EngineFn = std::function<std::shared_ptr<fl::Engine>(fl::EluKind, std::optional<MapKey>)>;
    explicit EluRouter(EngineFn engine) : _engine(std::move(engine)) { }

    // ---- generic ----
    bool listening(fl::EluKind kind, uint32_t entry, uint32_t event, std::optional<MapKey> map = std::nullopt) const;
    // Entry handlers; for the Creature family also the unique (guid + instance) ones.
    bool listening(fl::EluKind kind, Subject const& me, uint32_t event) const;
    // No map scope: world-thread hooks (login, server events). Merges nothing.
    fl::EventResult fire(fl::EluKind kind, uint32_t entry, uint32_t event, std::vector<fl::EventValue> args) const;
    // Inside the subject's map scope; the Creature family merges entry and unique handlers.
    fl::EventResult fireOn(fl::EluKind kind, Subject const& me, uint32_t event, std::vector<fl::EventValue> args) const;
    // A hook on a map thread that has no creature/object subject (items): inside `scope` (none: no scope).
    fl::EventResult fireIn(std::optional<MapKey> scope, fl::EluKind kind, uint32_t entry, uint32_t event,
                           std::vector<fl::EventValue> args) const;
    // A player hook that runs on a map thread: inside the player's map scope (none: no scope).
    fl::EventResult firePlayer(std::optional<MapKey> scope, uint32_t event, std::vector<fl::EventValue> args) const;

    // ---- creature AI ----
    fl::EventResult creature(Subject const& me, uint32_t event, std::vector<fl::EventValue> args) const;
    // docs: HELLO (event, player, object); false = default action
    fl::EventResult creatureGossipHello(Subject const& me, fl::EventValue player) const;
    // docs: SELECT (event, player, object, sender, intid, code, menu_id)
    fl::EventResult creatureGossipSelect(Subject const& me, fl::EventValue player, uint32_t sender, uint32_t action,
                                         std::optional<std::string> const& code, uint32_t menuId) const;
    // DAMAGE_TAKEN (9): the entry handlers and the unique ones (by guid + instance); `damage` may come back changed.
    fl::EventResult creatureDamageTaken(Subject const& me, fl::EventValue attacker, uint32_t damage) const;

    // ---- game object AI ----
    fl::EventResult gameObject(Subject const& me, uint32_t event, std::vector<fl::EventValue> args) const;
    fl::EventResult gameObjectGossipHello(Subject const& me, fl::EventValue player) const;
    fl::EventResult gameObjectGossipSelect(Subject const& me, fl::EventValue player, uint32_t sender, uint32_t action,
                                           std::optional<std::string> const& code, uint32_t menuId) const;

    // ---- group / guild (E2) ----
    // Group and Guild events run on the world thread: no map scope (their handles carry no map).
    fl::EventResult group(uint32_t event, std::vector<fl::EventValue> args) const;
    fl::EventResult guild(uint32_t event, std::vector<fl::EventValue> args) const;

    // ---- map / instance (E2) ----
    // One map callback: the Map family keyed by mapId inside the map's scope, and for an instance map the Instance
    // family keyed by instanceId as well (docs: RegisterMapEvent = every instance of a Map, RegisterInstanceEvent =
    // one instance). Also fires the server's map events (17/18/21/22/23) when `serverEvent` is not 0; the server
    // events take (event, map[, player|diff]), the map families (event, instance_data, map[, ...]).
    fl::EventResult mapEvent(uint32_t mapId, uint32_t instanceId, bool instanced, uint32_t event, uint32_t serverEvent,
                             std::vector<fl::EventValue> args, std::vector<fl::EventValue> serverArgs = {}) const;
    // The cheap gate of one map callback (M1): the Map/Instance families are asked in that map's own state (where a
    // map-only RegisterMapEvent lives), the server event in the world state.
    bool listeningMap(uint32_t mapId, uint32_t instanceId, bool instanced, uint32_t event, uint32_t serverEvent) const;

    // ---- map creation (E4/E5 final check: the core holds its exclusive map lock while it creates a map) ----
    // Queues one event of a map that is still being created (CreationQueue); no Lua runs. `subject`: the creature or game
    // object (replayed through fireOn, so unique creature handlers run too). False = dropped (bounds).
    bool deferCreation(MapKey key, uint64_t nowMs, fl::EluKind kind, uint32_t entry, uint32_t event,
                       std::vector<fl::EventValue> args, std::optional<fl::HandleData> subject = std::nullopt) const;
    // A creature / game object / battleground event of map `key`: queued while the map is being created (`creating`),
    // appended to the map's queue while that queue waits for its replay (never dispatched ahead of it), dispatched
    // otherwise. `subject` empty: a battleground event (fireIn).
    fl::EventResult mapObjectEvent(bool creating, MapKey key, uint64_t nowMs, fl::EluKind kind, uint32_t entry,
                                   uint32_t event, std::vector<fl::EventValue> args,
                                   std::optional<fl::HandleData> subject) const;
    // Replays the queue of `key` in order, on the calling (map) thread, inside the map scope and routed as usual (the
    // map's own state when it is open). Called only from the map's first-update hook (after its state exists) or, for a
    // map without such a hook, at the first player enter. Returns how many were replayed.
    size_t replayCreation(MapKey key) const;

    // ---- core-hook events (E6): never run inside the hook ----
    // Queues one creature / game object event of a core notification hook as plain values (HookQueue, by map). While the map
    // is being created, or while its creation queue waits for its replay, the event goes behind that queue instead (the
    // order is kept). No Lua runs, nothing touches the object. `needsObject`: the drain drops the event when the GUID no
    // longer resolves (false for REMOVE-type events, which are about a gone object).
    // A full queue is never silent: the drop is counted per map and logged (the first at once, then once per 10 s, the rest
    // flushed at the drain). `followPlayer`: see DeferredEvent.
    void queueHook(bool creating, MapKey key, uint64_t nowMs, fl::EluKind kind, uint32_t entry, uint32_t event,
                   std::vector<fl::EventValue> args, std::optional<fl::HandleData> subject, bool needsObject,
                   bool followPlayer = false) const;
    // MAP_PLAYER_ENTER (map event 4, instance event 4, server event 21): queued with the player as subject, run at the
    // drain with the player resolved by GUID (see queueHook). Only the families somebody listens to are queued.
    void queuePlayerEnter(bool creating, MapKey key, uint64_t nowMs, bool instanced, fl::HandleData player,
                          std::vector<fl::EventValue> mapArgs, std::vector<fl::EventValue> serverArgs, uint32_t event,
                          uint32_t serverEvent) const;
    // Runs the queued hook events of `key` in order, on the calling thread (the map's own update, or the world update for
    // a map without a state of its own), each in the state of its map. A drained event whose object is gone is dropped
    // with a debug line. Returns how many ran.
    size_t drainHooks(MapKey key) const;
    // The world update: drains the maps for which `ownsState(key)` is false (their own update drains the others).
    size_t drainWorldHooks(std::function<bool(MapKey)> const& ownsState) const;

    // Used by the replay and the drain (public for the tests): runs one queued event, or drops it, or follows its player.
    bool runQueued(MapKey key, DeferredEvent& ev) const;
    void reportDrops(MapKey key, uint64_t nowMs, uint64_t minIntervalMs) const;

    // ---- player, item gossip select and command (E6, core hooks C3/C4) ----
    // The core's PlayerScript::OnGossipSelect for a player or item menu source (PR #377). docs: SELECT
    // (event, player, object, sender, intid, code, menu_id); a player menu is keyed by its menu id.
    fl::EventResult playerGossipSelect(std::optional<MapKey> scope, uint32_t menuId, fl::EventValue player,
                                       fl::EventValue object, uint32_t sender, uint32_t intid,
                                       std::optional<std::string> const& code) const;
    // An item menu source: docs SELECT with the Item object, keyed by the item entry.
    fl::EventResult itemGossipSelect(std::optional<MapKey> scope, uint32_t entry, fl::EventValue player,
                                     fl::EventValue item, uint32_t sender, uint32_t intid,
                                     std::optional<std::string> const& code, uint32_t menuId) const;
    // docs: on_command (event, player, command); false = the script handled it and the core stops.
    fl::EventResult playerCommand(std::optional<MapKey> scope, fl::EventValue player, std::string const& command) const;

    // ---- items ----
    // ITEM USE (2) then the item gossip HELLO (1). True = cancel the spell cast (the item event returned false, or the
    // gossip hello did: docs "for item gossip, returning false also stops the spell cast").
    bool itemUse(std::optional<MapKey> playerMap, uint32_t entry, fl::EventValue player, fl::EventValue item,
                 fl::EventValue target) const;

private:
    std::shared_ptr<fl::Engine> engine(fl::EluKind kind, std::optional<MapKey> map) const
    {
        return _engine ? _engine(kind, map) : nullptr;
    }
    EngineFn _engine;
};
}  // namespace flh
