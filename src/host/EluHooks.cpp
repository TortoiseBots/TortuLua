// The ScriptMgr hooks of Eluna mode (E1): Player, Server, Item, and the creature/gameobject AI scripts that carry the
// creature and gameobject event families. The hooks only build the docs argument lists and hand them to
// Engine::emitEluna; the return values (XP, money, damage, chat text, delays, handled flags) flow back through
// EventResult. Every hook first asks whether any handler is bound (cheap), so test mode pays a relaxed load.
#include "CoreHooks.h"
#include "EluRouter.h"
#include "HookQueue.h"
#include "GameAccess.h"
#include "Module.h"

#include "fl_core_hooks.h"

#include "engine/GuidText.h"

#include "Channel.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "GossipDef.h"
#include "Guild.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "RBAC.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "Weather.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace
{
using fl::EluKind;
using fl::EventResult;
using fl::EventValue;
using fl::HandleData;

fl::Engine* eng() { return flh::engine(); }

EventValue num(int64_t v) { return EventValue(v); }
EventValue dbl(double v) { return EventValue(v); }
EventValue txt(std::string const& s) { return EventValue(s); }
EventValue flag(bool b) { return EventValue(b); }
EventValue obj(WorldObject const* o)
{
    if (!o || !o->IsInWorld())
        return EventValue();
    return EventValue(flh::handleFor(o));
}
EventValue itemValue(Item* item)
{
    if (!item)
        return EventValue();
    flh::rememberItem(item);
    return EventValue(flh::handleForItem(item));
}
EventValue questValue(uint32 id)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Quest;
    h.guid.lo = id;
    return EventValue(h);
}

// The routing (family, map scope, unique-creature merge, return folding) is flh::EluRouter, which the unit tests drive.
// This file only turns core objects into plain values and calls it.
flh::EluRouter const& router()
{
    static flh::EluRouter const r(&flh::eventEngine);
    return r;
}

// E6: the event dispatch that exists only with the core hooks C1-C4 (CoreHooks.cpp, unit-tested on the fake world).
flh::HookDispatch const& hooks()
{
    static flh::HookDispatch const h(&flh::eventEngine);
    return h;
}

bool listening(EluKind kind, uint32_t entry, uint32_t event) { return router().listening(kind, entry, event); }
bool listeningIn(EluKind kind, uint32_t entry, uint32_t event, std::optional<flh::MapKey> map)
{
    return router().listening(kind, entry, event, map);
}

// A hook on the world thread (login, server events, chat): no map scope.
EventResult fire(EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> args)
{
    return router().fire(kind, entry, event, std::move(args));
}

// A player hook that runs on a map thread resolves only objects of that player's own map.
std::optional<flh::MapKey> playerMap(Player const* p)
{
    if (!p)
        return std::nullopt;
    return flh::MapKey{p->GetMapId(), p->GetInstanceId()};
}

EventResult fireP(Player const* p, uint32_t event, std::vector<EventValue> args)
{
    return router().firePlayer(playerMap(p), event, std::move(args));
}

flh::Subject subjectOf(WorldObject const* o)
{
    return flh::Subject{flh::handleFor(o), o->GetEntry()};
}

// E4/E5 final check: the core is still creating this object's map (MapManager::CreateMap holds its exclusive map lock
// while it loads a battleground's or an instance's grids, and registers the map, SetWeakPtr, only afterwards). No Lua may
// run then: the event is queued and replayed on the map's thread once the map is registered.
bool mapCreating(Map const* map) { return map && map->GetWeakPtr().expired(); }

uint64_t steadyMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// E6: when a core hook of a creature / game object runs relative to its map's creation (see flh::HookWhen). The hooks pass
// only a handle (GUID) on: the remove hooks run after the object left the world and the core frees it right after.
flh::HookWhen hookWhen(WorldObject const* o)
{
    Map const* map = o->FindMap();
    flh::HookWhen when;
    when.creating = mapCreating(map);
    when.nowMs = map ? steadyMs() : 0;
    // The Map the object is attached to right now, read in the hook itself: a transport that crosses maps fires its REMOVE
    // for the map it leaves and its ADD for the map it enters, each routed by that key (never by what GetMapId says later).
    if (map)
        when.map = flh::MapKey{map->GetId(), map->GetInstanceId()};
    return when;
}

// A creature / game object event: queued while its map is being created, appended to the map's queue while that waits
// for its replay (the order is kept), dispatched otherwise.
EventResult objectEvent(WorldObject* me, EluKind kind, uint32_t event, std::vector<EventValue> args)
{
    Map* map = me->FindMap();
    flh::Subject const s = subjectOf(me);
    if (!map)
        return kind == EluKind::Creature ? router().creature(s, event, std::move(args))
                                         : router().gameObject(s, event, std::move(args));
    flh::MapKey const key{map->GetId(), map->GetInstanceId()};
    return router().mapObjectEvent(mapCreating(map), key, steadyMs(), kind, s.entry, event, std::move(args), s.handle);
}

// The Lua form of a guid is its 128 bits as text (engine/GuidText.h), the same value Object:GetGUID() returns.
EventValue guidText(ObjectGuid const& g) { return EventValue(fl::guidText(flh::fromGuid(g))); }
EventValue groupValue(Group const* g)
{
    if (!g)
        return EventValue();
    fl::HandleData h;
    h.kind = fl::ObjKind::Group;
    h.guid = flh::fromGuid(g->GetGUID());
    return EventValue(h);
}
EventValue guildValue(Guild const* g)
{
    if (!g)
        return EventValue();
    fl::HandleData h;
    h.kind = fl::ObjKind::Guild;
    h.guid.lo = g->GetId();
    return EventValue(h);
}

void gap(std::string const& event, char const* hook)
{
    fl::Engine* e = eng();
    if (e && e->noteHookGap(event + hook))
        TC_LOG_WARN("forever.lua", "ForeverLua: event {} needs core hook {}", event, hook);
}

class forever_lua_player_elu : public PlayerScript
{
public:
    forever_lua_player_elu() : PlayerScript("forever_lua_player_elu") { }

    void OnCreate(Player* player) override
    {
        if (listening(EluKind::Player, 0, 1))
            fire(EluKind::Player, 0, 1, {obj(player)});
    }

    void OnDelete(ObjectGuid guid, uint32) override
    {
        if (!listening(EluKind::Player, 0, 2))
            return;
        if (guid.IsPlayer())
            fire(EluKind::Player, 0, 2,
                 {num(static_cast<int64_t>(guid.GetCounter()))});
    }

    void OnLogin(Player* player, bool firstLogin) override
    {
        flh::notePlayerMap(player);   // the map threads' record of where this player is (AccessRules.h)
        if (listening(EluKind::Player, 0, 3))
            fire(EluKind::Player, 0, 3, {obj(player)});
        if (firstLogin && listening(EluKind::Player, 0, 30))
            fire(EluKind::Player, 0, 30, {obj(player)});
    }

    void OnLogout(Player* player) override
    {
        if (listening(EluKind::Player, 0, 4))
            fire(EluKind::Player, 0, 4, {obj(player)});
        // The player leaves the world: its object-bound timed events go with it, and the map record is dropped.
        if (fl::Engine* e = flh::engine())
            e->removeLuaEventsOf(flh::handleFor(player));
        flh::forgetPlayerMap(player);
    }

    void OnSpellCast(Player* player, Spell* spell, bool skipCheck) override
    {
        if (!listening(EluKind::Player, 0, 5))
            return;
        // The docs pass the Spell object: a transient handle, valid inside this call (E2).
        EventValue spellValue;
        if (spell)
            spellValue = EventValue(flh::handleForSpell(spell));
        fireP(player, 5, {obj(player), spellValue, flag(skipCheck)});
    }

    void OnPVPKill(Player* killer, Player* killed) override
    {
        if (listening(EluKind::Player, 0, 6))
            fireP(killer, 6, {obj(killer), obj(killed)});
    }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        if (listening(EluKind::Player, 0, 7))
            fireP(killer, 7, {obj(killer), obj(killed)});
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (listening(EluKind::Player, 0, 8))
            fireP(killed, 8, {obj(killer), obj(killed)});
    }

    void OnDuelRequest(Player* target, Player* challenger) override
    {
        if (listening(EluKind::Player, 0, 9))
            fireP(target, 9, {obj(target), obj(challenger)});
    }

    void OnDuelStart(Player* player1, Player* player2) override
    {
        if (listening(EluKind::Player, 0, 10))
            fireP(player1, 10, {obj(player1), obj(player2)});
    }

    void OnDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
    {
        if (listening(EluKind::Player, 0, 11))
            fireP(winner, 11, {obj(winner), obj(loser), num(type)});
    }

    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
    {
        if (!listening(EluKind::Player, 0, 12))
            return;
        EventResult r = fireP(player, 12, {obj(player), num(amount), obj(victim)});
        if (r.hasValue)
            amount = static_cast<uint32>(std::clamp<int64_t>(r.value, 0, 0xFFFFFFFFll));
    }

    void OnLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (listening(EluKind::Player, 0, 13))
            fireP(player, 13, {obj(player), num(oldLevel)});
    }

    void OnMoneyChanged(Player* player, int64& amount) override
    {
        if (!listening(EluKind::Player, 0, 14))
            return;
        EventResult r = fireP(player, 14, {obj(player), num(amount)});
        if (r.hasValue)
            amount = r.value;
    }

    void OnReputationChange(Player* player, uint32 factionId, int32& standing, bool incremental) override
    {
        if (!listening(EluKind::Player, 0, 15))
            return;
        EventResult r = fireP(player, 15, {obj(player), num(factionId), num(standing), flag(incremental)});
        if (r.hasValue)
            standing = static_cast<int32>(r.value);
    }

    void OnFreeTalentPointsChanged(Player* player, uint32 points) override
    {
        if (listening(EluKind::Player, 0, 16))
            fireP(player, 16, {obj(player), num(points)});
    }

    void OnTalentsReset(Player* player, bool involuntarily) override
    {
        if (listening(EluKind::Player, 0, 17))
            fireP(player, 17, {obj(player), flag(involuntarily)});
    }

    void Chat(Player* player, uint32 type, uint32 lang, std::string& msg)
    {
        if (!listening(EluKind::Player, 0, 18))
            return;
        EventResult r = fire(EluKind::Player, 0, 18, {obj(player), txt(msg), num(type), num(lang)});
        if (r.hasText)
            msg = r.text;
        if (r.cancel)
            gap("player.on_chat blocking", "C6");
    }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg) override { Chat(player, type, lang, msg); }
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (!listening(EluKind::Player, 0, 19))
            return;
        EventResult r = fire(EluKind::Player, 0, 19, {obj(player), txt(msg), num(type), num(lang), obj(receiver)});
        if (r.hasText)
            msg = r.text;
        if (r.cancel)
            gap("player.on_whisper blocking", "C6");
    }
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Group* group) override
    {
        if (!listening(EluKind::Player, 0, 20))
            return;
        EventResult r = fire(EluKind::Player, 0, 20, {obj(player), txt(msg), num(type), num(lang), groupValue(group)});
        if (r.hasText)
            msg = r.text;
        if (r.cancel)
            gap("player.on_group_chat blocking", "C6");
    }
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Guild* guild) override
    {
        if (!listening(EluKind::Player, 0, 21))
            return;
        EventResult r = fire(EluKind::Player, 0, 21, {obj(player), txt(msg), num(type), num(lang), guildValue(guild)});
        if (r.hasText)
            msg = r.text;
        if (r.cancel)
            gap("player.on_guild_chat blocking", "C6");
    }
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Channel* channel) override
    {
        if (!listening(EluKind::Player, 0, 22))
            return;
        // The Channel class is a later stage: the docs' last argument is the channel's name here.
        EventResult r = fire(EluKind::Player, 0, 22,
                             {obj(player), txt(msg), num(type), num(lang), txt(channel ? channel->GetName() : std::string())});
        if (r.hasText)
            msg = r.text;
        if (r.cancel)
            gap("player.on_channel_chat blocking", "C6");
    }

    void OnTextEmote(Player* player, uint32 textEmote, uint32 emoteNum, ObjectGuid guid) override
    {
        if (listening(EluKind::Player, 0, 24))
            fireP(player, 24, {obj(player), num(textEmote), num(emoteNum), guidText(guid)});
    }

    void OnSave(Player* player) override
    {
        if (listening(EluKind::Player, 0, 25))
            fireP(player, 25, {obj(player)});
    }

    void OnBindToInstance(Player* player, Difficulty difficulty, uint32 mapId, bool permanent, uint8) override
    {
        if (listening(EluKind::Player, 0, 26))
            fireP(player, 26, {obj(player), num(difficulty), num(mapId), flag(permanent)});
    }

    void OnUpdateZone(Player* player, uint32 newZone, uint32 newArea) override
    {
        if (listening(EluKind::Player, 0, 27))
            fireP(player, 27, {obj(player), num(newZone), num(newArea)});
        if (listening(EluKind::Player, 0, 47))
            fireP(player, 47, {obj(player), num(newArea), num(newArea)});
    }

    void OnMapChanged(Player* player) override
    {
        flh::notePlayerMap(player);
        // A map without a first-update hook of its own (dungeons and raids at this core) replays what its creation
        // queued at the first player enter (this runs after the map was registered, outside the core's map lock).
        if (flh::creationQueue().anyPending())
            if (Map* map = player->FindMap())
                if (!flh::awaitingFirstUpdate(map->GetId(), map->GetInstanceId()))
                    router().replayCreation(flh::MapKey{map->GetId(), map->GetInstanceId()});
        // Queued, not fired here: this runs inside Map::AddPlayerToMap, before the core puts a logging-in player into its
        // player table, so a handler's GetName would find no player (HookDispatch::playerMapChanged).
        hooks().playerMapChanged(flh::handleFor(player), hookWhen(player));
    }

    void OnLootItem(Player* player, Item* item) override
    {
        if (!listening(EluKind::Player, 0, 32))
            return;
        fireP(player, 32, {obj(player), itemValue(item), num(item ? item->GetCount() : 0)});
    }

    void OnPlayerRepop(Player* player) override
    {
        if (listening(EluKind::Player, 0, 35))
            fireP(player, 35, {obj(player)});
    }

    void OnQuestStatusChange(Player* player, uint32 questId) override
    {
        if (listening(EluKind::Player, 0, 51))
            fireP(player, 51,
                 {obj(player), num(questId), num(player ? player->GetQuestStatus(questId) : 0)});
    }

#if FL_CORE_HOOK_C3_GOSSIP
    // PR #377: the core drops a gossip select whose source is a player or an item; this hook is the only way to answer
    // it, and it runs only for those two sources. A player menu is keyed by its menu id, an item menu by the item entry;
    // the docs' object argument is the source itself.
    void OnGossipSelect(Player* player, ObjectGuid source, uint32 menuId, uint32 listId, std::string const& code) override
    {
        if (!player)
            return;
        fl::Engine* const e = eng();
        if (!e || e->config().mode != fl::EngineMode::Eluna ||
            !fl::hasCoreHook(e->config(), fl::CoreHook::GossipSelect))
            return;
        uint32 sender = 0, action = 0;
        if (player->PlayerTalkClass)
        {
            sender = player->PlayerTalkClass->GetGossipOptionSender(listId);
            action = player->PlayerTalkClass->GetGossipOptionAction(listId);
        }
        std::optional<std::string> codeText;
        if (!code.empty())
            codeText = code;
        std::optional<flh::MapKey> const scope = playerMap(player);
        if (source.IsPlayer())
        {
            Player* sourcePlayer = ObjectAccessor::FindConnectedPlayer(source);
            if (!sourcePlayer)
                return;   // the menu source is gone: nothing may answer for it
            hooks().playerGossipSelect(scope, menuId, obj(player), obj(sourcePlayer), sender, action, codeText);
        }
        else if (source.IsItem())
        {
            // Only the player's own item can have sent the menu. The script still checks the menu and the item it sent
            // (the PR's contract): this hook only proves the item exists and is the player's.
            Item* item = player->GetItemByGuid(source);
            if (!item || !listeningIn(EluKind::ItemGossip, item->GetEntry(), 2, scope))
                return;
            flh::rememberItem(item);
            hooks().itemGossipSelect(scope, item->GetEntry(), obj(player), itemValue(item), sender, action, codeText,
                                     menuId);
        }
    }
#endif

#if FL_CORE_HOOK_C3_ITEM_USE
    // PR #378: before the item's own ItemScript::OnUse, for every item. true = the script stopped the use.
    bool OnItemUse(Player* player, Item* item, SpellCastTargets const& targets, ObjectGuid) override
    {
        fl::Engine* const e = eng();
        if (!e || e->config().mode != fl::EngineMode::Eluna || !player || !item)
            return false;   // test mode has no Eluna item handlers; do not touch the item store
        // Only an item with a handler is worth the item store (a global mutex on the map threads).
        uint32 const entry = item->GetEntry();
        std::optional<flh::MapKey> const scope = playerMap(player);
        if (!listeningIn(EluKind::Item, entry, 2, scope) && !listeningIn(EluKind::ItemGossip, entry, 1, scope))
            return false;
        flh::rememberItem(item);
        EventValue target;
        if (Unit* unit = targets.GetUnitTarget())
            target = obj(unit);
        return hooks().itemUse(scope, item->GetEntry(), obj(player), itemValue(item), target);
    }
#endif

#if FL_CORE_HOOK_C4
    // PR #379: before the core looks the command up and before any RBAC/permission check, with the text without its
    // leading '.' or '!'. The module's own RegisterCommand claims are security-checked here because the core's check no
    // longer runs first; the effective level is max(1, minSecurity), as in E3 (the core command slots still require GM).
    bool OnCommand(Player* player, std::string_view command) override
    {
        fl::Engine* e = eng();
        if (!player || !e || e->config().mode != fl::EngineMode::Eluna)
            return false;
        std::string const text(command);
        if (hooks().playerCommand(playerMap(player), obj(player), text))
            return true;
        int const security = player->GetSession() ? int(player->GetSession()->GetSecurity()) : 0;
        flh::LuaCommandClaim const claim = flh::claimLuaCommand(e->luaCommands(), text, security);
        WorldSession* session = player->GetSession();
        // E3's gate was the core's RBAC permission of the .lua slot plus minSecurity; the hook runs before both, so the
        // engine repeats both here (a security number alone is not the RBAC grant).
        if (!claim.mine || !claim.allowed || !session || !session->HasPermission(rbac::RBAC_PERM_COMMAND_GM))
            return false;   // a denied claim falls through to the core slot, which answers "no permission"
        fl::CommandResult r = e->runLuaCommand(claim.index, claim.tail, security, flh::handleFor(player), claim.name);
        if (r.status == fl::CommandStatus::Refused || r.status == fl::CommandStatus::NoHandler)
            return false;   // the engine could not run it (re-entrant call, command replaced): let the core's slot decide
        ChatHandler chat(player->GetSession());
        for (std::string const& line : r.lines)
            chat.SendSysMessage(line);
        return true;
    }
#endif
};

class forever_lua_unit_elu : public UnitScript
{
public:
    forever_lua_unit_elu() : UnitScript("forever_lua_unit_elu") { }

    // Creature DAMAGE_TAKEN (event 9). This core has no CreatureAI::DamageTaken; the UnitScript damage hook is the
    // right place and carries the same (creature, attacker, damage) payload. The entry handlers and the unique ones
    // (RegisterUniqueCreatureEvent: guid + instance) both run.
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        Creature* creature = victim ? victim->ToCreature() : nullptr;
        if (!creature)
            return;
        flh::Subject const me = subjectOf(creature);
        if (!router().listening(EluKind::Creature, me, 9))
            return;
        EventResult r = router().creatureDamageTaken(me, obj(attacker), damage);
        if (r.hasValue)
            damage = static_cast<uint32>(std::clamp<int64_t>(r.value, 0, 0xFFFFFFFFll));
    }
};

class forever_lua_item_elu : public ItemScript
{
public:
    forever_lua_item_elu() : ItemScript("forever_lua_item") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& targets, ObjectGuid) override
    {
#if FL_CORE_HOOK_C3_ITEM_USE
        // PlayerScript::OnItemUse already fired the item USE event and the item gossip HELLO for this item (it runs
        // before every ItemScript); firing them again here would double every handler. This ItemScript stays registered
        // for the events C3 does not cover (quest accept, expire, remove).
        (void)player;
        (void)item;
        (void)targets;
        return false;
#else
        uint32 const entry = item->GetEntry();
        std::optional<flh::MapKey> const map = playerMap(player);
        if (!listeningIn(EluKind::Item, entry, 2, map) && !listeningIn(EluKind::ItemGossip, entry, 1, map))
            return false;
        EventValue target;
        if (Unit* unit = targets.GetUnitTarget())
            target = obj(unit);
        // docs: the item USE event, then the item gossip HELLO; false from either cancels the spell cast (the core's
        // true = handled/cancel).
        return router().itemUse(playerMap(player), entry, obj(player), itemValue(item), target);
#endif
    }

    bool OnQuestAccept(Player* player, Item* item, Quest const* quest) override
    {
        if (!listeningIn(EluKind::Item, item->GetEntry(), 3, playerMap(player)))
            return false;
        EventResult r = router().fireIn(playerMap(player), EluKind::Item, item->GetEntry(), 3,
                                        {obj(player), itemValue(item), questValue(quest->GetQuestId())});
        return r.handled;
    }

    bool OnExpire(Player* player, ItemTemplate const* proto) override
    {
        if (!listeningIn(EluKind::Item, proto->GetId(), 4, playerMap(player)))
            return false;
        EventResult r = router().fireIn(playerMap(player), EluKind::Item, proto->GetId(), 4,
                                        {obj(player), num(proto->GetId())});
        return r.handled;
    }

    bool OnRemove(Player* player, Item* item) override
    {
        bool const listen = listeningIn(EluKind::Item, item->GetEntry(), 5, playerMap(player));
        EventResult r;
        if (listen)
            r = router().fireIn(playerMap(player), EluKind::Item, item->GetEntry(), 5, {obj(player), itemValue(item)});
        flh::forgetItem(item->GetGUID());
        return r.handled;
    }
};

class forever_lua_creature_elu : public CreatureScript
{
public:
    forever_lua_creature_elu() : CreatureScript("forever_lua") { }
    CreatureAI* GetAI(Creature* creature) const override;
};

class forever_lua_creature_ai_elu : public ScriptedAI
{
public:
    explicit forever_lua_creature_ai_elu(Creature* creature) : ScriptedAI(creature) { }

    // The entry handlers and the unique-creature ones (guid + instance); the router runs them inside this creature's map
    // scope.
    bool any(uint32 event) const { return router().listening(EluKind::Creature, subjectOf(me), event); }
    EventResult go(uint32 event, std::vector<EventValue> args) { return objectEvent(me, EluKind::Creature, event, std::move(args)); }

    void JustEngagedWith(Unit* who) override
    {
        if (any(1))
            go(1, {obj(me), obj(who)});
        ScriptedAI::JustEngagedWith(who);
    }
    void EnterEvadeMode(EvadeReason why) override
    {
        if (any(2) && go(2, {obj(me)}).handled)
            return;
        ScriptedAI::EnterEvadeMode(why);
    }
    void KilledUnit(Unit* victim) override
    {
        if (any(3))
            go(3, {obj(me), obj(victim)});
        ScriptedAI::KilledUnit(victim);
    }
    void JustDied(Unit* killer) override
    {
        if (any(4))
            go(4, {obj(me), obj(killer)});
        ScriptedAI::JustDied(killer);
    }
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        if (any(5))
            go(5, {obj(me)});
    }
    void MovementInform(uint32 type, uint32 id) override
    {
        if (any(6))
            go(6, {obj(me), num(type), num(id)});
        ScriptedAI::MovementInform(type, id);
    }
    void UpdateAI(uint32 diff) override
    {
        if (any(7) && go(7, {obj(me), num(diff)}).handled)
            return;
        UpdateVictim();
        me->DoMeleeAttackIfReady();
    }
    void ReceiveEmote(Player* player, uint32 emoteId) override
    {
        if (any(8))
            go(8, {obj(me), obj(player), num(emoteId)});
        ScriptedAI::ReceiveEmote(player, emoteId);
    }
    void JustStartedThreateningMe(Unit* who) override
    {
        if (any(10) && go(10, {obj(me), obj(who)}).handled)
            return;
        ScriptedAI::JustStartedThreateningMe(who);
    }
    void OwnerAttacked(Unit* target) override
    {
        if (any(12))
            go(12, {obj(me), obj(target)});
        ScriptedAI::OwnerAttacked(target);
    }
    void OwnerAttackedBy(Unit* attacker) override
    {
        if (any(13))
            go(13, {obj(me), obj(attacker)});
        ScriptedAI::OwnerAttackedBy(attacker);
    }
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (any(14))
            go(14, {obj(me), obj(caster), num(spellInfo ? spellInfo->Id : 0)});
        ScriptedAI::SpellHit(caster, spellInfo);
    }
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (any(15))
            go(15, {obj(me), obj(target), num(spellInfo ? spellInfo->Id : 0)});
        ScriptedAI::SpellHitTarget(target, spellInfo);
    }
    void JustSummoned(Creature* summon) override
    {
        if (any(19))
            go(19, {obj(me), obj(summon)});
        ScriptedAI::JustSummoned(summon);
    }
    void SummonedCreatureDespawn(Creature* summon) override
    {
        if (any(20))
            go(20, {obj(me), obj(summon)});
        ScriptedAI::SummonedCreatureDespawn(summon);
    }
    void SummonedCreatureDies(Creature* summon, Unit* killer) override
    {
        if (any(21))
            go(21, {obj(me), obj(summon), obj(killer)});
        ScriptedAI::SummonedCreatureDies(summon, killer);
    }
    void IsSummonedBy(WorldObject* summoner) override
    {
        if (any(22))
            go(22, {obj(me), obj(summoner)});
        ScriptedAI::IsSummonedBy(summoner);
    }
    void Reset() override
    {
        if (any(23))
            go(23, {obj(me)});
        ScriptedAI::Reset();
    }
    void JustReachedHome() override
    {
        if (any(24))
            go(24, {obj(me)});
        ScriptedAI::JustReachedHome();
    }
    void CorpseRemoved(uint32& respawnDelay) override
    {
        if (any(26))
        {
            EventResult r = go(26, {obj(me), num(respawnDelay)});
            if (r.hasValue)
                respawnDelay = static_cast<uint32>(std::clamp<int64_t>(r.value, 0, 0xFFFFFFFFll));
        }
        ScriptedAI::CorpseRemoved(respawnDelay);
    }
    void MoveInLineOfSight(Unit* who) override
    {
        if (any(27) && go(27, {obj(me), obj(who)}).handled)
            return;
        ScriptedAI::MoveInLineOfSight(who);
    }
    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (any(31))
            go(31, {obj(player), obj(me), questValue(quest->GetQuestId())});
        ScriptedAI::OnQuestAccept(player, quest);
    }
    void OnQuestReward(Player* player, Quest const* quest, LootItemType type, uint32 opt) override
    {
        if (any(34))
            go(34, {obj(player), obj(me), questValue(quest->GetQuestId()), num(opt)});
        ScriptedAI::OnQuestReward(player, quest, type, opt);
    }
    Optional<QuestGiverStatus> GetDialogStatus(Player const* player) override
    {
        if (any(35))
            go(35, {obj(player), obj(me)});   // docs: DIALOG_STATUS (event, player, creature)
        return ScriptedAI::GetDialogStatus(player);
    }
    bool OnGossipHello(Player* player) override
    {
        if (router().listening(EluKind::CreatureGossip, subjectOf(me), 1))
        {
            // The CreatureGossip family (not the creature combat events): HELLO (event, player, object).
            EventResult r = router().creatureGossipHello(subjectOf(me), obj(player));
            if (!r.cancel)
                return true;   // docs: return false to do the default action
        }
        return ScriptedAI::OnGossipHello(player);
    }
    bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
    {
        return gossip(player, menuId, gossipListId, nullptr);
    }
    bool OnGossipSelectCode(Player* player, uint32 menuId, uint32 gossipListId, char const* code) override
    {
        return gossip(player, menuId, gossipListId, code);
    }
    bool gossip(Player* player, uint32 menuId, uint32 gossipListId, char const* code)
    {
        if (!router().listening(EluKind::CreatureGossip, subjectOf(me), 2))
            return ScriptedAI::OnGossipSelect(player, menuId, gossipListId);
        uint32 sender = 0, action = 0;
        if (player && player->PlayerTalkClass)
        {
            sender = player->PlayerTalkClass->GetGossipOptionSender(gossipListId);
            action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        }
        std::optional<std::string> codeText;
        if (code)
            codeText = std::string(code);
        EventResult r = router().creatureGossipSelect(subjectOf(me), obj(player), sender, action, codeText, menuId);
        return !r.cancel;
    }
};

CreatureAI* forever_lua_creature_elu::GetAI(Creature* creature) const
{
    return new forever_lua_creature_ai_elu(creature);
}

#if FL_CORE_HOOK_C1
// Karrz1/TrinityCore PR "AllCreatureScript": notify-only hooks for every creature (pets included). The module never
// replaces, wraps or owns a creature AI: a creature that has a ScriptName keeps its own AI untouched (our forever_lua
// ScriptName AI fires its Lua events itself, a C++ script keeps its class), and the notifications below skip it
// (GetScriptId() != 0) so nothing fires twice. With no Lua handler registered every method returns at once and the
// server behaves as the plain core. Every hook hands the dispatch a handle (a GUID), never the pointer: the remove hooks
// run after the creature left the world, JustDied may run with the creature already out of the world, and an event that
// must wait (the map is still being created) is queued by GUID.
class forever_lua_all_creature_elu final : public AllCreatureScript
{
public:
    forever_lua_all_creature_elu() : AllCreatureScript("forever_lua_all_creature") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (creature)
            hooks().creatureAdded(subjectOf(creature), hookWhen(creature));
    }
    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (creature)
            hooks().creatureRemoved(subjectOf(creature), hookWhen(creature));
    }
#if FL_CORE_HOOK_C1_EVENTS
    void OnCreatureEngage(Creature* creature, Unit* target) override
    {
        if (creature && !creature->GetScriptId())
            hooks().creatureEngaged(subjectOf(creature), 0, obj(target), hookWhen(creature));
    }
    // Fires on every combat end of a living creature in the world (not only an evade): docs LEAVE_COMBAT.
    void OnCreatureLeaveCombat(Creature* creature) override
    {
        if (creature && !creature->GetScriptId())
            hooks().creatureLeftCombat(subjectOf(creature), 0, hookWhen(creature));
    }
    // May run with the creature out of the world (its AI despawned it): the handle then resolves to nothing.
    void OnCreatureJustDied(Creature* creature, Unit* killer) override
    {
        if (creature && !creature->GetScriptId())
            hooks().creatureDied(subjectOf(creature), 0, obj(killer), hookWhen(creature));
    }
    // A living creature that appeared after its spawn or respawn: docs SPAWN.
    void OnCreatureAppear(Creature* creature) override
    {
        if (creature && !creature->GetScriptId())
            hooks().creatureAppeared(subjectOf(creature), 0, hookWhen(creature));
    }
#endif
};
#endif

class forever_lua_gameobject_elu : public GameObjectScript
{
public:
    class AI : public GameObjectAI
    {
    public:
        explicit AI(GameObject* go) : GameObjectAI(go) { }
        // Every game object event runs inside this object's map scope (the router sets it).
        bool any(uint32 event) const { return router().listening(EluKind::GameObject, subjectOf(me), event); }
        EventResult fireGo(uint32 event, std::vector<EventValue> args)
        {
            return objectEvent(me, EluKind::GameObject, event, std::move(args));
        }
        void UpdateAI(uint32 diff) override
        {
            if (any(1))
                fireGo(1, {obj(me), num(diff)});
        }
        void InitializeAI() override
        {
            // The core's InitializeAI calls Reset(), which fires SPAWN: firing it here as well would run every SPAWN
            // handler twice for one spawn.
            GameObjectAI::InitializeAI();
        }
        void Reset() override
        {
            GameObjectAI::Reset();
            if (any(2))
                fireGo(2, {obj(me)});
        }
        Optional<QuestGiverStatus> GetDialogStatus(Player const* player) override
        {
            if (any(6))
                fireGo(6, {obj(player), obj(me)});   // docs: DIALOG_STATUS (event, player, go)
            return GameObjectAI::GetDialogStatus(player);
        }
        void OnQuestAccept(Player* player, Quest const* quest) override
        {
            if (any(4))
                fireGo(4, {obj(player), obj(me), questValue(quest->GetQuestId())});
            GameObjectAI::OnQuestAccept(player, quest);
        }
        void OnQuestReward(Player* player, Quest const* quest, LootItemType type, uint32 opt) override
        {
            if (any(5))
                fireGo(5, {obj(player), obj(me), questValue(quest->GetQuestId()), num(opt)});
            GameObjectAI::OnQuestReward(player, quest, type, opt);
        }
        void Destroyed(WorldObject* attacker, uint32 eventId) override
        {
            if (any(7))
                fireGo(7, {obj(me), obj(attacker)});
            GameObjectAI::Destroyed(attacker, eventId);
        }
        void Damaged(WorldObject* attacker, uint32 eventId) override
        {
            if (any(8))
                fireGo(8, {obj(me), obj(attacker)});
            GameObjectAI::Damaged(attacker, eventId);
        }
        void OnLootStateChanged(uint32 state, Unit* unit) override
        {
            if (any(9))
                fireGo(9, {obj(me), num(state), obj(unit)});
            GameObjectAI::OnLootStateChanged(state, unit);
        }
        void OnStateChanged(uint32 state) override
        {
            if (any(10))
                fireGo(10, {obj(me), num(state)});
            GameObjectAI::OnStateChanged(state);
        }
        bool OnGossipHello(Player* player) override
        {
            if (any(14))
                if (EventResult r = fireGo(14, {obj(me), obj(player)}); r.handled)
                    return true;
            if (router().listening(EluKind::GameObjectGossip, subjectOf(me), 1))
            {
                EventResult r = router().gameObjectGossipHello(subjectOf(me), obj(player));
                return !r.cancel;
            }
            return GameObjectAI::OnGossipHello(player);
        }
        bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
        {
            return gossip(player, menuId, gossipListId, nullptr);
        }
        bool OnGossipSelectCode(Player* player, uint32 menuId, uint32 gossipListId, char const* code) override
        {
            return gossip(player, menuId, gossipListId, code);
        }
        bool gossip(Player* player, uint32 menuId, uint32 gossipListId, char const* code)
        {
            if (!router().listening(EluKind::GameObjectGossip, subjectOf(me), 2))
                return GameObjectAI::OnGossipSelect(player, menuId, gossipListId);
            uint32 sender = 0, action = 0;
            if (player && player->PlayerTalkClass)
            {
                sender = player->PlayerTalkClass->GetGossipOptionSender(gossipListId);
                action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            }
            std::optional<std::string> codeText;
            if (code)
                codeText = std::string(code);
            EventResult r = router().gameObjectGossipSelect(subjectOf(me), obj(player), sender, action, codeText, menuId);
            return !r.cancel;
        }
    };
    forever_lua_gameobject_elu() : GameObjectScript("forever_lua_gameobject") { }
    GameObjectAI* GetAI(GameObject* go) const override { return new AI(go); }
};

#if FL_CORE_HOOK_C2
// Karrz1/TrinityCore PR "AllGameObjectScript": notify-only hooks for every game object (no AI is replaced; see
// forever_lua_all_creature_elu). OnGameObjectRemoveWorld runs after the object left the world (a transport fires
// add/remove at every map crossing): only its handle goes on.
class forever_lua_all_gameobject_elu final : public AllGameObjectScript
{
public:
    forever_lua_all_gameobject_elu() : AllGameObjectScript("forever_lua_all_gameobject") { }

    void OnGameObjectAddWorld(GameObject* go) override
    {
        if (go)
            hooks().gameObjectAdded(subjectOf(go), hookWhen(go));
    }
    void OnGameObjectRemoveWorld(GameObject* go) override
    {
        if (go)
            hooks().gameObjectRemoved(subjectOf(go), hookWhen(go));
    }
#if FL_CORE_HOOK_C2_USE
    // After the AI's OnGossipHello (also when it handled the use, then the object may already be out of the world).
    void OnGameObjectUse(GameObject* go, Player* player) override
    {
        if (go && !go->GetScriptId())
            hooks().gameObjectUsed(subjectOf(go), 0, obj(player), hookWhen(go));
    }
#endif
};
#endif

class forever_lua_server_elu : public WorldScript
{
public:
    forever_lua_server_elu() : WorldScript("forever_lua_server_elu") { }
    void OnOpenStateChange(bool open) override
    {
        if (listening(EluKind::Server, 0, 8))
            fire(EluKind::Server, 0, 8, {flag(open)});
    }
    void OnConfigLoad(bool reload) override
    {
        if (listening(EluKind::Server, 0, 9))
            fire(EluKind::Server, 0, 9, {flag(reload)});
    }
    void OnShutdownInitiate(ShutdownExitCode code, ShutdownMask mask) override
    {
        if (listening(EluKind::Server, 0, 11))
            fire(EluKind::Server, 0, 11, {num(code), num(mask)});
    }
    void OnShutdownCancel() override
    {
        if (listening(EluKind::Server, 0, 12))
            fire(EluKind::Server, 0, 12, {});
    }
    void OnUpdate(uint32 diff) override
    {
        // World tick: queues of maps the core lists now are sealed (a later creation of the key is a new instance); a
        // queue that waited longer than a minute (a map that never got a player or an update) is dropped, logged per map.
        if (flh::creationQueue().anyPending())
            flh::creationQueue().sweep(
                steadyMs(), 60000,
                [](flh::MapKey k) { return sMapMgr->FindMap(k.map, k.instance) != nullptr; },
                [](flh::MapKey k, size_t n) {
                    TC_LOG_WARN("forever.lua", "ForeverLua: map {} instance {}: {} events queued during its creation were "
                                "dropped (the map got no update or player in time)", k.map, k.instance, n);
                });
        // E6: the queued creature / game object events of the core hooks. A map with a state of its own drains them in its
        // own update; the others (MapStates off, dungeons and raids) here, in the world update, where the maps are idle.
        if (flh::hookQueue().anyPending())
        {
            router().drainWorldHooks([](flh::MapKey k) { return flh::mapOwnsState(k.map, k.instance); });
            flh::hookQueue().sweep(steadyMs(), 60000, [](flh::MapKey k, size_t n) {
                TC_LOG_WARN("forever.lua", "ForeverLua: map {} instance {}: {} queued core-hook events were dropped (the "
                            "map did not update in time)", k.map, k.instance, n);
            });
        }
        if (listening(EluKind::Server, 0, 13))
            fire(EluKind::Server, 0, 13, {num(diff)});
    }
    void OnStartup() override
    {
        if (listening(EluKind::Server, 0, 14))
            fire(EluKind::Server, 0, 14, {});
    }
    void OnShutdown() override
    {
        if (listening(EluKind::Server, 0, 15))
            fire(EluKind::Server, 0, 15, {});
    }
};

class forever_lua_server_socket_elu : public ServerScript
{
public:
    forever_lua_server_socket_elu() : ServerScript("forever_lua_server_socket_elu") { }
    void OnNetworkStart() override
    {
        if (listening(EluKind::Server, 0, 1))
            fire(EluKind::Server, 0, 1, {});
    }
    void OnNetworkStop() override
    {
        if (listening(EluKind::Server, 0, 2))
            fire(EluKind::Server, 0, 2, {});
    }

    // E5 packet events (docs RegisterPacketEvent: WORLD state, arguments event, packet, player). The core gives the hook
    // a copy of the packet: the script can read it, never change the one on the wire (spec section 3, read-only copy).
    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        packetEvent(session, packet, 5, "on_receive");
    }
    void OnPacketSend(WorldSession* session, WorldPacket& packet) override
    {
        packetEvent(session, packet, 7, "on_send");
    }

    static void packetEvent(WorldSession* session, WorldPacket& packet, uint32_t event, char const* which)
    {
        fl::Engine* e = eng();
        uint32_t const opcode = packet.GetOpcode();
        if (!e || !router().listening(EluKind::Packet, opcode, event))
            return;
        EventValue playerValue;
        if (session)
            if (Player* p = session->GetPlayer())
                playerValue = obj(p);
        // H4 (E4/E5 review): the copy is made, dispatched and freed inside the world state's Lua lock, never in the hook
        // (this runs on any map thread, at the same time as other packets and as the world thread's own Lua).
        std::vector<uint8_t> bytes(packet.data(), packet.data() + packet.size());
        EventResult r = e->emitPacketEvent(opcode, event, bytes, playerValue);
        if (r.cancel && e->noteHookGap(std::string("packet-readonly-") + which))
            TC_LOG_WARN("forever.lua",
                        "ForeverLua: packet {} event: the copy is read-only, a returned false cannot block the packet",
                        which);
    }
};

class forever_lua_addon_elu : public PlayerScript
{
public:
    forever_lua_addon_elu() : PlayerScript("forever_lua_addon_elu") { }
    void OnAddonMessageSent(Player* player, uint32 type, std::string const& prefix, std::string const& text,
                            Player* receiver) override
    {
        addon(player, type, prefix, text, receiver ? obj(receiver) : EventValue());
    }
    void OnAddonMessageSent(Player* player, uint32 type, std::string const& prefix, std::string const& text,
                            Group* group) override
    {
        (void)group;
        addon(player, type, prefix, text, EventValue());
    }
    static void addon(Player* player, uint32 type, std::string const& prefix, std::string const& text, EventValue target)
    {
        if (!listening(EluKind::Server, 0, 30))
            return;
        EventResult r = fire(EluKind::Server, 0, 30, {obj(player), num(type), txt(prefix), txt(text), std::move(target)});
        if (r.cancel)
            gap("server.on_addon_message", "a veto hook");
    }
};

class forever_lua_weather_elu : public WeatherScript
{
public:
    forever_lua_weather_elu() : WeatherScript("forever_lua_weather_elu") { }
    void OnChange(Weather* weather, WeatherState state, float grade) override
    {
        if (listening(EluKind::Server, 0, 25))
            fire(EluKind::Server, 0, 25, {num(weather ? weather->GetZone() : 0), num(state), dbl(grade)});
    }
};
}  // namespace

void AddSC_forever_lua_social();
void AddSC_forever_lua_maps();
void AddSC_forever_lua_battlegrounds();

void AddSC_forever_lua_eluna()
{
    new forever_lua_player_elu();
    new forever_lua_unit_elu();
    new forever_lua_item_elu();
    new forever_lua_creature_elu();
    new forever_lua_gameobject_elu();
    new forever_lua_server_elu();
    new forever_lua_server_socket_elu();
    new forever_lua_addon_elu();
    new forever_lua_weather_elu();
#if FL_CORE_HOOK_C1
    new forever_lua_all_creature_elu();
#endif
#if FL_CORE_HOOK_C2
    new forever_lua_all_gameobject_elu();
#endif
    AddSC_forever_lua_social();   // E2: GroupScript and GuildScript
    AddSC_forever_lua_maps();          // E2: one WorldMapScript/InstanceMapScript per map
    AddSC_forever_lua_battlegrounds(); // E5: one BattlegroundMapScript per battleground/arena map (C12 not needed)
}
