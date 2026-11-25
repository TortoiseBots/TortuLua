// ForeverLua host side: the ScriptMgr scripts. The rule for every hook: look at the atomic "anyone listening" flag
// first (the core's built-in bots are Players, so with no handler bound that flag is the whole cost per bot), then
// either emit() on the world thread or post() a plain snapshot from a map thread.
#include "Commands.h"
#include "GameAccess.h"
#include "Module.h"

#include "Chat.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Unit.h"

#include <memory>

namespace
{
using flh::engine;

fl::EventValue guidValue(ObjectGuid const& g) { return fl::EventValue(g.ToString()); }

class forever_lua_world : public WorldScript
{
public:
    forever_lua_world() : WorldScript("forever_lua_world") { }

    // The first tick creates the state on the world thread (the engine records this thread as its owner), loads the
    // scripts and fires server.start.
    void OnBeforeMapsUpdate(uint32 /*diff*/) override
    {
        if (!_started)
        {
            _started = true;
            flh::startEngine();
        }
        // The maps are about to update on their threads: a handle may only be resolved by the thread updating its own
        // map from here on (Eluna mode's cross-map rule).
        flh::setMapsIdle(false);
        // One Lua window per world tick, shared by every emit() of that tick (review N4).
        if (fl::Engine* e = engine())
            e->beginTick();
    }

    // After the maps: no map thread is running, so the queued snapshots are drained and the timers run here.
    void OnUpdate(uint32 /*diff*/) override
    {
        flh::setMapsIdle(true);
        if (flh::settings().engine.mode == fl::EngineMode::Eluna)
        {
            flh::refreshPlayerMaps();     // the truth of this tick: where every player is, for the map threads' access checks
            flh::deferred().drain();      // spawns/commands a map thread asked for run here, on the world thread
        }
        if (flh::takeReloadRequest())
            flh::reloadEngine();
        // The bot API's tick: the provider's snapshot, its event diff and (B2) the queued orders, world thread, before
        // the Lua tick so a handler registered by a bot event still runs in the same tick's queue drain.
        flh::pumpBots();
        if (fl::Engine* e = engine())
            e->tick();
        // A Lua command registered, or a reload that changed the command list, this tick (possibly from inside a chat
        // command): the core's command map is rebuilt now, outside every command.
        flh::applyCommandMapChange();
    }

    void OnShutdown() override { flh::stopEngine(); }

private:
    bool _started = false;
};

class forever_lua_player : public PlayerScript
{
public:
    forever_lua_player() : PlayerScript("forever_lua_player") { }

    void OnLogin(Player* player, bool firstLogin) override
    {
        fl::Engine* e = engine();
        if (!e || !e->listening("player.login"))
            return;
        fl::QueuedEvent ev;
        ev.name = "player.login";
        ev.isBot = flh::isBot(player);
        ev.args.emplace_back(flh::handleFor(player));
        ev.args.emplace_back(firstLogin);
        ev.args.emplace_back(ev.isBot);
        e->emit(std::move(ev));
    }

    // The Player is going away: the handler gets plain strings, not a handle.
    void OnLogout(Player* player) override
    {
        fl::Engine* e = engine();
        if (!e || !e->listening("player.logout"))
            return;
        fl::QueuedEvent ev;
        ev.name = "player.logout";
        ev.isBot = flh::isBot(player);
        ev.args.emplace_back(player->GetGUID().ToString());
        ev.args.emplace_back(player->GetName());
        ev.args.emplace_back(ev.isBot);
        e->emit(std::move(ev));
    }

    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg) override { Chat(player, type, msg); }
    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Player* /*receiver*/) override { Chat(player, type, msg); }
    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Group* /*group*/) override { Chat(player, type, msg); }

private:
    static void Chat(Player* player, uint32 type, std::string const& msg)
    {
        fl::Engine* e = engine();
        if (!e || !e->listening("player.chat"))
            return;
        char const* name = ChatName(type);
        if (!name)
            return;
        fl::QueuedEvent ev;
        ev.name = "player.chat";
        ev.isBot = flh::isBot(player);
        ev.args.emplace_back(flh::handleFor(player));
        ev.args.emplace_back(name);
        ev.args.emplace_back(msg.size() > 1024 ? msg.substr(0, 1024) : msg);
        e->emit(std::move(ev));
    }

    // say, yell, whisper, party, raid only.
    static char const* ChatName(uint32 type)
    {
        switch (type)
        {
            case CHAT_MSG_SAY: return "say";
            case CHAT_MSG_YELL: return "yell";
            case CHAT_MSG_WHISPER: return "whisper";
            case CHAT_MSG_PARTY:
            case CHAT_MSG_PARTY_LEADER: return "party";
            case CHAT_MSG_RAID:
            case CHAT_MSG_RAID_LEADER: return "raid";
            default: return nullptr;
        }
    }
};

class forever_lua_unit : public UnitScript
{
public:
    forever_lua_unit() : UnitScript("forever_lua_unit") { }

    // Map thread: only a plain snapshot goes into the queue; the world thread runs the handlers after the maps.
    void OnUnitDeath(Unit* victim, Unit* killer) override
    {
        fl::Engine* e = engine();
        if (!e || !e->listening() || !victim)
            return;
        Creature* c = victim->ToCreature();
        if (!c || !e->listening("creature.death"))
            return;
        auto snap = std::make_shared<fl::EventTable>();
        auto put = [&](char const* k, fl::EventValue v) { snap->fields.emplace_back(k, std::move(v)); };
        put("guid", guidValue(c->GetGUID()));
        put("entry", int64_t(c->GetEntry()));
        put("spawnId", int64_t(c->GetSpawnId()));
        put("map", int64_t(c->GetMapId()));
        put("instance", int64_t(c->GetInstanceId()));
        put("x", double(c->GetPositionX()));
        put("y", double(c->GetPositionY()));
        put("z", double(c->GetPositionZ()));
        if (killer)
            put("killer", guidValue(killer->GetGUID()));
        fl::QueuedEvent ev;
        ev.name = "creature.death";
        ev.hasEntry = true;
        ev.entry = c->GetEntry();
        ev.args.emplace_back(snap);
        ev.args.emplace_back(flh::handleFor(c));
        if (killer && (killer->IsPlayer() || killer->IsCreature()))
            ev.args.emplace_back(flh::handleFor(killer));
        else
            ev.args.emplace_back();
        e->post(std::move(ev));
    }
};
}  // namespace

void AddSC_forever_lua_eluna();

void AddSC_forever_lua()
{
    // Off by default: nothing is registered with the ScriptMgr unless ForeverLua.Enable = 1. The commands are
    // registered either way so that `.lua status` can say why the module is off.
    new forever_lua_commands();
    if (!flh::settings().enabled)
        return;
    new forever_lua_world();
    new forever_lua_player();
    new forever_lua_unit();
    // E0/E1: the Eluna event families (player, server, item, creature/gameobject AI, gossip). The hooks are cheap when
    // no Eluna handler is bound, so they are registered in both modes; in test mode nothing ever dispatches to them.
    AddSC_forever_lua_eluna();
}
