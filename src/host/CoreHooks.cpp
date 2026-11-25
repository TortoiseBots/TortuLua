#include "CoreHooks.h"

#include <cctype>
#include <utility>

namespace flh
{
using fl::EluKind;
using fl::EventResult;
using fl::EventValue;

namespace
{
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}  // namespace

LuaCommandClaim claimLuaCommand(std::vector<fl::Engine::LuaCommand> const& commands, std::string_view command,
                                int security)
{
    LuaCommandClaim out;
    out.security = security;
    size_t begin = 0;
    while (begin < command.size() && isSpace(command[begin]))
        ++begin;
    if (begin < command.size() && (command[begin] == '.' || command[begin] == '!'))
        ++begin;   // the hook promises the text without its prefix; a forwarded line may still carry it
    size_t end = begin;
    while (end < command.size() && !isSpace(command[end]))
        ++end;
    std::string const name = lower(command.substr(begin, end - begin));
    if (name.empty())
        return out;
    for (size_t i = 0; i < commands.size(); ++i)
    {
        if (lower(commands[i].name) != name)
            continue;
        out.mine = true;
        out.index = i;
        out.name = commands[i].name;
        out.required = std::max(1, commands[i].minSecurity);
        out.allowed = security >= out.required;
        size_t tail = end;
        while (tail < command.size() && isSpace(command[tail]))
            ++tail;
        out.tail.assign(command.substr(tail));
        return out;
    }
    return out;
}

// The subject as the hook saw it: its handle stamped with the hook-time map (HookWhen::map).
static Subject stamp(Subject s, HookWhen const& when)
{
    if (when.map)
    {
        s.handle.mapId = when.map->map;
        s.handle.instanceId = when.map->instance;
    }
    return s;
}

bool HookDispatch::has(fl::CoreHook hook) const
{
    std::shared_ptr<fl::Engine> e = _engine ? _engine(EluKind::Server, std::nullopt) : nullptr;
    // The whole dispatch is the Eluna mode's: in test mode the state has no Eluna handlers and the core must see no
    // extra work at all.
    return e && e->config().mode == fl::EngineMode::Eluna && fl::hasCoreHook(e->config(), hook);
}

// One creature/game object event of the subject's map. It never runs Lua here: the core forbids a synchronous kill,
// despawn or combat end from these hooks, so the event is queued by GUID (HookQueue, or the creation queue while the map
// is being created) and the map's update or the world update drains it. `gone` marks an event about an object that has
// left the world (REMOVE): it fires although the GUID no longer resolves.
void HookDispatch::object(HookWhen when, EluKind kind, Subject const& me, uint32_t event, std::vector<EventValue> args,
                          bool gone) const
{
    Subject s = me;
    if (when.map)
    {
        s.handle.mapId = when.map->map;       // the hook-time map, never the handle's or the drain-time one
        s.handle.instanceId = when.map->instance;
    }
    if (gone)
        s.handle.removed = true;              // a REMOVE delivers the GUID only: the handle never resolves
    // The arguments were built from the caller's handle: rebuild the subject's own slot from the stamped one.
    for (EventValue& a : args)
        if (auto* h = std::get_if<fl::HandleData>(&a.v))
            if (h->kind == s.handle.kind && h->guid == s.handle.guid)
                *h = s.handle;
    MapKey const key{s.handle.mapId, s.handle.instanceId};
    _router.queueHook(when.creating, key, when.nowMs, kind, s.entry, event, std::move(args), s.handle, !gone);
}

// A server event about this subject (on_world_delete_*): the world state, in the subject's map scope; queued the same way.
void HookDispatch::serverEvent(HookWhen when, Subject const& me, uint32_t event, bool gone) const
{
    Subject s = me;
    if (when.map)
    {
        s.handle.mapId = when.map->map;
        s.handle.instanceId = when.map->instance;
    }
    if (gone)
        s.handle.removed = true;
    MapKey const key{s.handle.mapId, s.handle.instanceId};
    _router.queueHook(when.creating, key, when.nowMs, EluKind::Server, 0, event, {EventValue(s.handle)}, std::nullopt,
                      false);
}

void HookDispatch::playerMapChanged(fl::HandleData const& player, HookWhen when) const
{
    std::shared_ptr<fl::Engine> e = _engine ? _engine(EluKind::Server, std::nullopt) : nullptr;
    if (!e || e->config().mode != fl::EngineMode::Eluna || !_router.listening(EluKind::Player, 0, 28))
        return;
    MapKey const key{player.mapId, player.instanceId};
    _router.queueHook(when.creating, key, when.nowMs, EluKind::Player, 0, 28, {EventValue(player)}, player, true, true);   // a map change follows the player
}

void HookDispatch::creatureAdded(Subject const& meIn, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (!has(fl::CoreHook::AllCreature) || !_router.listening(EluKind::Creature, me, 36))
        return;
    object(when, EluKind::Creature, me, 36, {EventValue(me.handle)});
}

void HookDispatch::creatureRemoved(Subject const& meIn, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (!has(fl::CoreHook::AllCreature))
        return;
    if (_router.listening(EluKind::Creature, me, 37))
        object(when, EluKind::Creature, me, 37, {EventValue(me.handle)}, true);
    MapKey const key{me.handle.mapId, me.handle.instanceId};
    if (_router.listening(EluKind::Server, 0, 31, key))
        serverEvent(when, me, 31, true);   // on_world_delete_creature
}

void HookDispatch::gameObjectAdded(Subject const& meIn, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (!has(fl::CoreHook::AllGameObject) || !_router.listening(EluKind::GameObject, me, 12))
        return;
    object(when, EluKind::GameObject, me, 12, {EventValue(me.handle)});
}

void HookDispatch::gameObjectRemoved(Subject const& meIn, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (!has(fl::CoreHook::AllGameObject))
        return;
    if (_router.listening(EluKind::GameObject, me, 13))
        object(when, EluKind::GameObject, me, 13, {EventValue(me.handle)}, true);
    MapKey const key{me.handle.mapId, me.handle.instanceId};
    if (_router.listening(EluKind::Server, 0, 32, key))
        serverEvent(when, me, 32, true);   // on_world_delete_gameobject
}

void HookDispatch::creatureEngaged(Subject const& meIn, uint32_t scriptId, EventValue target, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (scriptId != 0 || !has(fl::CoreHook::CreatureEvents) || !_router.listening(EluKind::Creature, me, 1))
        return;
    object(when, EluKind::Creature, me, 1, {EventValue(me.handle), std::move(target)});
}

void HookDispatch::creatureLeftCombat(Subject const& meIn, uint32_t scriptId, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (scriptId != 0 || !has(fl::CoreHook::CreatureEvents) || !_router.listening(EluKind::Creature, me, 2))
        return;
    object(when, EluKind::Creature, me, 2, {EventValue(me.handle)});
}

void HookDispatch::creatureDied(Subject const& meIn, uint32_t scriptId, EventValue killer, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (scriptId != 0 || !has(fl::CoreHook::CreatureEvents) || !_router.listening(EluKind::Creature, me, 4))
        return;
    object(when, EluKind::Creature, me, 4, {EventValue(me.handle), std::move(killer)});
}

void HookDispatch::creatureAppeared(Subject const& meIn, uint32_t scriptId, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (scriptId != 0 || !has(fl::CoreHook::CreatureEvents) || !_router.listening(EluKind::Creature, me, 5))
        return;
    object(when, EluKind::Creature, me, 5, {EventValue(me.handle)});
}

void HookDispatch::gameObjectUsed(Subject const& meIn, uint32_t scriptId, EventValue player, HookWhen when) const
{
    Subject const me = stamp(meIn, when);
    if (scriptId != 0 || !has(fl::CoreHook::GameObjectUse) || !_router.listening(EluKind::GameObject, me, 14))
        return;
    object(when, EluKind::GameObject, me, 14, {EventValue(me.handle), std::move(player)});
}

EventResult HookDispatch::playerGossipSelect(std::optional<MapKey> scope, uint32_t menuId, EventValue player,
                                             EventValue object, uint32_t sender, uint32_t intid,
                                             std::optional<std::string> const& code) const
{
    if (!has(fl::CoreHook::GossipSelect))
        return EventResult();
    return _router.playerGossipSelect(scope, menuId, std::move(player), std::move(object), sender, intid, code);
}

EventResult HookDispatch::itemGossipSelect(std::optional<MapKey> scope, uint32_t entry, EventValue player,
                                           EventValue item, uint32_t sender, uint32_t intid,
                                           std::optional<std::string> const& code, uint32_t menuId) const
{
    if (!has(fl::CoreHook::GossipSelect))
        return EventResult();
    return _router.itemGossipSelect(scope, entry, std::move(player), std::move(item), sender, intid, code, menuId);
}

bool HookDispatch::playerCommand(std::optional<MapKey> scope, EventValue player, std::string const& command) const
{
    if (!has(fl::CoreHook::PlayerCommand))
        return false;
    return _router.playerCommand(scope, std::move(player), command).cancel;
}

bool HookDispatch::itemUse(std::optional<MapKey> scope, uint32_t entry, EventValue player, EventValue item,
                           EventValue target) const
{
    if (!has(fl::CoreHook::ItemUse))
        return false;
    return _router.itemUse(scope, entry, std::move(player), std::move(item), std::move(target));
}
}  // namespace flh
