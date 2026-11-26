#include "EluRouter.h"
#include "HookQueue.h"

#include <chrono>

namespace flh
{
using fl::EluKind;
using fl::EventResult;
using fl::EventValue;

namespace
{
EventValue handleValue(Subject const& me) { return EventValue(me.handle); }

EventResult mergeResults(EventResult const& byEntry, EventResult const& byGuid)
{
    EventResult out;
    out.calls = byEntry.calls + byGuid.calls;
    out.handled = byEntry.handled || byGuid.handled;
    out.cancel = byEntry.cancel || byGuid.cancel;
    out.hasText = byEntry.hasText || byGuid.hasText;
    out.text = byEntry.hasText ? byEntry.text : byGuid.text;
    out.hasValue = byEntry.hasValue || byGuid.hasValue;
    out.value = byEntry.hasValue ? byEntry.value : byGuid.value;
    return out;
}

// A Group/Guild hook the core itself called: everything the handler does now runs inside a core mutation that is still
// open. A change the handler asks for must be queued (Host::enterSocialHook), not run inside it.
struct SocialHookScope
{
    explicit SocialHookScope(fl::Engine* e) : host(e ? &e->host() : nullptr)
    {
        if (host)
            host->enterSocialHook();
    }
    ~SocialHookScope()
    {
        if (host)
            host->leaveSocialHook();
    }
    fl::Host* host;
};
}  // namespace

bool EluRouter::listening(EluKind kind, uint32_t entry, uint32_t event, std::optional<MapKey> map) const
{
    std::shared_ptr<fl::Engine> e = engine(kind, map);
    return e && e->listeningEluna(kind, entry, event);
}

bool EluRouter::listening(EluKind kind, Subject const& me, uint32_t event) const
{
    std::shared_ptr<fl::Engine> e = engine(kind, MapKey{me.handle.mapId, me.handle.instanceId});
    if (!e)
        return false;
    if (e->listeningEluna(kind, me.entry, event))
        return true;
    return kind == EluKind::Creature && e->listeningElunaUnique(kind, me.handle.guid, me.handle.instanceId, event);
}

EventResult EluRouter::fire(EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> args) const
{
    std::shared_ptr<fl::Engine> e = engine(kind, std::nullopt);   // player, server, group, guild: the world state
    return e ? e->emitEluna(kind, entry, event, args) : EventResult();
}

EventResult EluRouter::fireOn(EluKind kind, Subject const& me, uint32_t event, std::vector<EventValue> args) const
{
    MapKey const key{me.handle.mapId, me.handle.instanceId};
    // The callback runs on the map thread of the subject: its handlers may touch objects of that map only.
    MapScope scope(me.handle.mapId, me.handle.instanceId);
    std::shared_ptr<fl::Engine> e = engine(kind, key);   // E4: the state of that map instance
    if (!e)
        return EventResult();
    EventResult byEntry = e->emitEluna(kind, me.entry, event, args);
    if (kind != EluKind::Creature)
        return byEntry;
    EventResult byGuid = e->emitElunaUnique(kind, me.handle.guid, me.handle.instanceId, event, args);
    return mergeResults(byEntry, byGuid);
}

EventResult EluRouter::fireIn(std::optional<MapKey> map, EluKind kind, uint32_t entry, uint32_t event,
                              std::vector<EventValue> args) const
{
    std::optional<MapScope> guard;
    if (map)
        guard.emplace(*map);
    std::shared_ptr<fl::Engine> e = engine(kind, map);
    return e ? e->emitEluna(kind, entry, event, args) : EventResult();
}

EventResult EluRouter::firePlayer(std::optional<MapKey> scope, uint32_t event, std::vector<EventValue> args) const
{
    return fireIn(scope, EluKind::Player, 0, event, std::move(args));
}

EventResult EluRouter::creature(Subject const& me, uint32_t event, std::vector<EventValue> args) const
{
    return fireOn(EluKind::Creature, me, event, std::move(args));
}

EventResult EluRouter::creatureGossipHello(Subject const& me, EventValue player) const
{
    return fireOn(EluKind::CreatureGossip, me, 1, {std::move(player), handleValue(me)});
}

EventResult EluRouter::creatureGossipSelect(Subject const& me, EventValue player, uint32_t sender, uint32_t action,
                                            std::optional<std::string> const& code, uint32_t menuId) const
{
    return fireOn(EluKind::CreatureGossip, me, 2,
                  {std::move(player), handleValue(me), EventValue(static_cast<int64_t>(sender)),
                   EventValue(static_cast<int64_t>(action)), code ? EventValue(*code) : EventValue(),
                   EventValue(static_cast<int64_t>(menuId))});
}

EventResult EluRouter::creatureDamageTaken(Subject const& me, EventValue attacker, uint32_t damage) const
{
    return fireOn(EluKind::Creature, me, 9, {handleValue(me), std::move(attacker), EventValue(static_cast<int64_t>(damage))});
}

EventResult EluRouter::gameObject(Subject const& me, uint32_t event, std::vector<EventValue> args) const
{
    return fireOn(EluKind::GameObject, me, event, std::move(args));
}

EventResult EluRouter::gameObjectGossipHello(Subject const& me, EventValue player) const
{
    return fireOn(EluKind::GameObjectGossip, me, 1, {std::move(player), handleValue(me)});
}

EventResult EluRouter::gameObjectGossipSelect(Subject const& me, EventValue player, uint32_t sender, uint32_t action,
                                              std::optional<std::string> const& code, uint32_t menuId) const
{
    return fireOn(EluKind::GameObjectGossip, me, 2,
                  {std::move(player), handleValue(me), EventValue(static_cast<int64_t>(sender)),
                   EventValue(static_cast<int64_t>(action)), code ? EventValue(*code) : EventValue(),
                   EventValue(static_cast<int64_t>(menuId))});
}

EventResult EluRouter::group(uint32_t event, std::vector<EventValue> args) const
{
    std::shared_ptr<fl::Engine> e = _engine(EluKind::Group, std::nullopt);
    SocialHookScope scope(e.get());
    return e ? e->emitEluna(EluKind::Group, 0, event, std::move(args)) : EventResult();
}

EventResult EluRouter::guild(uint32_t event, std::vector<EventValue> args) const
{
    std::shared_ptr<fl::Engine> e = _engine(EluKind::Guild, std::nullopt);
    SocialHookScope scope(e.get());
    return e ? e->emitEluna(EluKind::Guild, 0, event, std::move(args)) : EventResult();
}

EventResult EluRouter::mapEvent(uint32_t mapId, uint32_t instanceId, bool instanced, uint32_t event,
                                uint32_t serverEvent, std::vector<EventValue> args,
                                std::vector<EventValue> serverArgs) const
{
    MapKey const key{mapId, instanceId};
    // A map callback runs on the thread that updates the map: its handlers may touch that map's objects only.
    MapScope scope(mapId, instanceId);
    std::shared_ptr<fl::Engine> e = engine(EluKind::Map, key);   // E4: the map family lives in that map's state
    if (!e)
        return EventResult();
    EventResult out = e->emitEluna(EluKind::Map, mapId, event, args);
    if (instanced)
    {
        EventResult byInstance = e->emitEluna(EluKind::Instance, instanceId, event, args);
        out.calls += byInstance.calls;
        out.handled = out.handled || byInstance.handled;
        out.cancel = out.cancel || byInstance.cancel;
        out.hasValue = out.hasValue || byInstance.hasValue;
        out.value = out.hasValue ? out.value : byInstance.value;
    }
    if (serverEvent && !serverArgs.empty())
    {
        // The server's map events stay in the world state (E4: server events belong to the world state).
        std::shared_ptr<fl::Engine> world = engine(EluKind::Server, std::nullopt);
        EventResult byServer = world ? world->emitEluna(EluKind::Server, 0, serverEvent, serverArgs) : EventResult();
        out.calls += byServer.calls;
        out.handled = out.handled || byServer.handled;
    }
    return out;
}

EventResult EluRouter::playerGossipSelect(std::optional<MapKey> scope, uint32_t menuId, EventValue player,
                                          EventValue object, uint32_t sender, uint32_t intid,
                                          std::optional<std::string> const& code) const
{
    return fireIn(scope, EluKind::PlayerGossip, menuId, 2,
                  {std::move(player), std::move(object), EventValue(static_cast<int64_t>(sender)),
                   EventValue(static_cast<int64_t>(intid)), code ? EventValue(*code) : EventValue(),
                   EventValue(static_cast<int64_t>(menuId))});
}

EventResult EluRouter::itemGossipSelect(std::optional<MapKey> scope, uint32_t entry, EventValue player,
                                        EventValue item, uint32_t sender, uint32_t intid,
                                        std::optional<std::string> const& code, uint32_t menuId) const
{
    return fireIn(scope, EluKind::ItemGossip, entry, 2,
                  {std::move(player), std::move(item), EventValue(static_cast<int64_t>(sender)),
                   EventValue(static_cast<int64_t>(intid)), code ? EventValue(*code) : EventValue(),
                   EventValue(static_cast<int64_t>(menuId))});
}

EventResult EluRouter::playerCommand(std::optional<MapKey> scope, EventValue player, std::string const& command) const
{
    return fireIn(scope, EluKind::Player, 0, 42, {std::move(player), EventValue(command)});
}

bool EluRouter::listeningMap(uint32_t mapId, uint32_t instanceId, bool instanced, uint32_t event,
                             uint32_t serverEvent) const
{
    MapKey const key{mapId, instanceId};
    if (event)
    {
        std::shared_ptr<fl::Engine> e = engine(EluKind::Map, key);
        if (e && (e->listeningEluna(EluKind::Map, mapId, event) ||
                  (instanced && e->listeningEluna(EluKind::Instance, instanceId, event))))
            return true;
    }
    if (serverEvent)
    {
        std::shared_ptr<fl::Engine> world = engine(EluKind::Server, std::nullopt);
        if (world && world->listeningEluna(EluKind::Server, 0, serverEvent))
            return true;
    }
    return false;
}

namespace
{
DeferredEvent deferred(fl::EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> args,
                       std::optional<fl::HandleData> subject)
{
    DeferredEvent ev;
    ev.kind = kind;
    ev.entry = entry;
    ev.event = event;
    ev.args = std::move(args);
    ev.subject = std::move(subject);
    return ev;
}

// Runs one deferred event as the creation replay and the hook drain do: on the subject (unique creature handlers too) or
// in the key's state.
void runDeferred(EluRouter const& router, MapKey key, DeferredEvent& ev)
{
    if (ev.subject)
        router.fireOn(ev.kind, Subject{*ev.subject, ev.entry}, ev.event, std::move(ev.args));
    else
        router.fireIn(key, ev.kind, ev.entry, ev.event, std::move(ev.args));
}
}  // namespace

bool EluRouter::deferCreation(MapKey key, uint64_t nowMs, EluKind kind, uint32_t entry, uint32_t event,
                              std::vector<EventValue> args, std::optional<fl::HandleData> subject) const
{
    return creationQueue().deferCreation(key, deferred(kind, entry, event, std::move(args), std::move(subject)), nowMs);
}

EventResult EluRouter::mapObjectEvent(bool creating, MapKey key, uint64_t nowMs, EluKind kind, uint32_t entry,
                                      uint32_t event, std::vector<EventValue> args,
                                      std::optional<fl::HandleData> subject) const
{
    if (creating)
    {
        if (!deferCreation(key, nowMs, kind, entry, event, std::move(args), std::move(subject)))
        {
            hookQueue().noteDrop(key);   // never silent
            reportDrops(key, nowMs, 10000);
        }
        return EventResult();
    }
    bool dropped = false;
    if (creationQueue().pending(key) &&
        creationQueue().appendLive(key, deferred(kind, entry, event, args, subject), &dropped))
    {
        if (dropped)
        {
            hookQueue().noteDrop(key);
            reportDrops(key, nowMs, 10000);
        }
        return EventResult();
    }
    if (subject)
        return fireOn(kind, Subject{*subject, entry}, event, std::move(args));
    return fireIn(key, kind, entry, event, std::move(args));
}

namespace
{
uint64_t steadyNowMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

void EluRouter::reportDrops(MapKey key, uint64_t nowMs, uint64_t minIntervalMs) const
{
    uint64_t const n = hookQueue().takeOverflow(key, nowMs, minIntervalMs);
    if (n == 0)
        return;
    std::shared_ptr<fl::Engine> world = engine(EluKind::Server, std::nullopt);
    if (world)
        world->host().log(fl::LogLevel::Warn, "ForeverLua: map " + std::to_string(key.map) + " instance " +
                                                  std::to_string(key.instance) + ": " + std::to_string(n) +
                                                  " core-hook events were dropped (the event queue of the map is full)");
}

// One queued core-hook event, run on the calling thread (the map's update or the world update) after the core's hook stack
// has unwound. Returns false when the event was not run (dropped, or followed a player to another map).
bool EluRouter::runQueued(MapKey key, DeferredEvent& ev) const
{
    std::shared_ptr<fl::Engine> e = (ev.needsObject && ev.subject) ? engine(ev.kind, key) : nullptr;
    if (ev.needsObject && ev.subject && ev.subject->kind == fl::ObjKind::Player)
    {
        // A player event is resolved by GUID NOW, in the map the player is in now (the hook ran inside
        // Map::AddPlayerToMap, before the core registers a logging-in player in its player table, so a lookup at that
        // moment finds nothing). The world's player lookup answers, never a map-local one.
        e = engine(EluKind::Player, key);
        if (!e)
            return false;
        uint32_t mapId = 0, instanceId = 0;
        if (!e->host().objectInWorld(*ev.subject) || !e->host().currentMap(*ev.subject, mapId, instanceId))
        {
            e->host().log(fl::LogLevel::Debug, std::string("ForeverLua: a queued ") +
                                                   fl::eluKindName(ev.kind) + " event " + std::to_string(ev.event) +
                                                   " was dropped: the player is gone");
            return false;
        }
        if (MapKey{mapId, instanceId} != key)
        {
            if (!ev.followPlayer)
            {
                e->host().log(fl::LogLevel::Debug, std::string("ForeverLua: a queued ") + fl::eluKindName(ev.kind) +
                                                       " event " + std::to_string(ev.event) +
                                                       " was dropped: the player left the map");
                return false;
            }
            // A map change: the player is followed to the map it is on now (it runs in that map's own update).
            uint64_t const now = steadyNowMs();
            MapKey const to{mapId, instanceId};
            if (!hookQueue().push(to, std::move(ev), now))
                reportDrops(to, now, 10000);
            return false;
        }
        ev.subject->mapId = mapId;
        ev.subject->instanceId = instanceId;
        for (fl::EventValue& a : ev.args)
            if (auto* h = std::get_if<fl::HandleData>(&a.v))
                if (h->kind == fl::ObjKind::Player && h->guid.hi == ev.subject->guid.hi && h->guid.lo == ev.subject->guid.lo)
                {
                    h->mapId = mapId;
                    h->instanceId = instanceId;
                }
    }
    else if (ev.needsObject && ev.subject)
    {
        // The GUID is looked up again now: the hook ran when the object was in the world, this is a later update (or the
        // replay of a map that was being created). An object that is gone drops every event except a REMOVE.
        if (e && !e->host().objectInWorld(*ev.subject))
        {
            e->host().log(fl::LogLevel::Debug, std::string("ForeverLua: a queued ") + fl::eluKindName(ev.kind) +
                                                   " event " + std::to_string(ev.event) +
                                                   " was dropped: the object is gone");
            return false;
        }
    }
    runDeferred(*this, key, ev);
    return true;
}

size_t EluRouter::replayCreation(MapKey key) const
{
    if (!creationQueue().anyPending())
        return 0;
    std::vector<DeferredEvent> events = creationQueue().take(key);
    size_t ran = 0;
    for (auto& ev : events)
        if (runQueued(key, ev))
            ++ran;
    reportDrops(key, steadyNowMs(), 0);
    return ran;
}

void EluRouter::queueHook(bool creating, MapKey key, uint64_t nowMs, EluKind kind, uint32_t entry, uint32_t event,
                          std::vector<EventValue> args, std::optional<fl::HandleData> subject, bool needsObject,
                          bool followPlayer) const
{
    DeferredEvent ev = deferred(kind, entry, event, std::move(args), std::move(subject));
    ev.needsObject = needsObject;
    ev.followPlayer = followPlayer;
    if (creating)
    {
        if (!creationQueue().deferCreation(key, std::move(ev), nowMs))   // under the core's map lock: no Lua
            hookQueue().noteDrop(key);
    }
    else
    {
        // Behind the creation queue while it waits for its replay (the order is kept), otherwise in the hook queue
        // (which counts its own drop).
        bool dropped = false;
        if (creationQueue().pending(key) && creationQueue().appendLive(key, ev, &dropped))
        {
            if (dropped)
                hookQueue().noteDrop(key);
        }
        else
            hookQueue().push(key, std::move(ev), nowMs);
    }
    reportDrops(key, nowMs, 10000);   // a full queue is never silent: the first drop is logged at once, then once per 10 s
}

void EluRouter::queuePlayerEnter(bool creating, MapKey key, uint64_t nowMs, bool instanced, fl::HandleData player,
                                 std::vector<EventValue> mapArgs, std::vector<EventValue> serverArgs, uint32_t event,
                                 uint32_t serverEvent) const
{
    // MAP_PLAYER_ENTER runs inside Map::AddPlayerToMap, before a logging-in player is in the core's player table: the
    // event is queued with the player as its subject and resolved by GUID at the drain (a player that left the map by then
    // is dropped: the event is about this map).
    auto push = [&](EluKind kind, uint32_t entry, uint32_t ev, std::vector<EventValue> args) {
        queueHook(creating, key, nowMs, kind, entry, ev, std::move(args), player, true, false);
    };
    if (event)
    {
        if (listening(EluKind::Map, key.map, event, key))
            push(EluKind::Map, key.map, event, mapArgs);
        if (instanced && listening(EluKind::Instance, key.instance, event, key))
            push(EluKind::Instance, key.instance, event, mapArgs);
    }
    if (serverEvent && listening(EluKind::Server, 0, serverEvent, key))
        push(EluKind::Server, 0, serverEvent, std::move(serverArgs));
}

size_t EluRouter::drainHooks(MapKey key) const
{
    if (!hookQueue().anyPending())
    {
        reportDrops(key, steadyNowMs(), 0);
        return 0;
    }
    std::vector<DeferredEvent> events = hookQueue().take(key);
    size_t ran = 0;
    for (auto& ev : events)
        if (runQueued(key, ev))
            ++ran;
    reportDrops(key, steadyNowMs(), 0);   // what was dropped at a bound since the last report
    return ran;
}

size_t EluRouter::drainWorldHooks(std::function<bool(MapKey)> const& ownsState) const
{
    if (!hookQueue().anyPending())
        return 0;
    size_t ran = 0;
    for (MapKey const& key : hookQueue().keys())
        if (!ownsState || !ownsState(key))
            ran += drainHooks(key);
    return ran;
}

bool EluRouter::itemUse(std::optional<MapKey> playerMap, uint32_t entry, EventValue player, EventValue item,
                        EventValue target) const
{
    bool cancel = false;
    std::optional<MapScope> scope;
    if (playerMap)
        scope.emplace(*playerMap);
    // E4: an item belongs to its owner's map; its events run in that map's state.
    std::shared_ptr<fl::Engine> e = engine(EluKind::Item, playerMap);
    if (!e)
        return false;
    bool const useListener = e->listeningEluna(EluKind::Item, entry, 2);
    bool const helloListener = e->listeningEluna(EluKind::ItemGossip, entry, 1);
    if (!useListener && !helloListener)
        return false;
    // The core still uses the item after the hook returns: a handler may not destroy it (Player:RemoveItem refuses).
    fl::Guid itemGuid;
    if (auto const* h = std::get_if<fl::HandleData>(&item.v))
        itemGuid = h->guid;
    fl::ItemUseScope const itemScope(itemGuid, entry);
    if (useListener)
        cancel = e->emitEluna(EluKind::Item, entry, 2, {player, item, std::move(target)}).cancel;   // false = cancel the cast
    if (helloListener)
        cancel = e->emitEluna(EluKind::ItemGossip, entry, 1, {std::move(player), std::move(item)}).cancel || cancel;
    return cancel;
}
}  // namespace flh
