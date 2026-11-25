// Our bot API, B1 (the maintainers' bot API specification sections 3-5): the read-only Bot class over the last bot snapshot, the
// B1 globals, RegisterBotEvent and ClearBotEvents. Our own extension: nothing here comes from Eluna's source. Reads use
// the snapshot the world thread took this tick (any state, any thread); a bot that is not in it is stale and raises
// "Bot no longer exists", a BOT_REMOVED handle never resolves again.
#include "engine/elu/EluBind.h"

#include "engine/BotHub.h"
#include "engine/Elu.h"
#include "engine/GuidText.h"
#include "engine/Handle.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flu
{
namespace
{
using fl::BotInfo;
using fl::BotSnapshot;
using fl::EventValue;
using fl::Guid;
using fl::HandleData;
using fl::toHandleAny;

// No provider (or one that answered available() == false): the bot API is not built into this server.
void requireBuilt(lua_State* L)
{
    if (!fl::botProvider()->available())
        luaL_error(L, "bots not built");
}

// The last snapshot (pinned), and the read that keeps the next one due within 60 s.
std::shared_ptr<BotSnapshot const> readSnapshot(lua_State* L)
{
    fl::botHub().noteRead(host(L).nowMs());
    return fl::botHub().snapshot();
}

HandleData botHandle(fl::Guid const& guid)
{
    HandleData h;
    h.kind = fl::ObjKind::Bot;
    h.guid = guid;
    return h;
}

// The bot behind a guid: an exact match, or (the legacy integer form) any bot with that low counter.
BotInfo const* findBot(BotSnapshot const& snap, fl::Guid const& guid, bool legacyLow = false)
{
    for (BotInfo const& bot : snap.bots)
        if (bot.player.guid == guid || (legacyLow && bot.player.guid.lo == guid.lo))
            return &bot;
    return nullptr;
}

void pushBot(lua_State* L, Guid const& guid) { fl::pushHandle(L, botHandle(guid)); }

// A Player|Group handle argument value: nil when the provider named none.
void pushPlayerOrNil(lua_State* L, HandleData const& player)
{
    if (player.guid == fl::Guid())
    {
        lua_pushnil(L);
        return;
    }
    HandleData h = player;
    h.kind = fl::ObjKind::Player;
    fl::pushHandle(L, h);
}

int pushBots(lua_State* L, std::vector<BotInfo const*> const& list)
{
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 0;
    for (BotInfo const* bot : list)
    {
        pushBot(L, bot->player.guid);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
}  // namespace

// ---- the B1 globals (declared in EluBind.h, installed by registerGlobals) ----
int pIsBotApiAvailable(lua_State* L)
{
    lua_pushboolean(L, fl::botProvider()->available());
    return 1;
}

int pGetBots(lua_State* L)
{
    requireBuilt(L);
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    std::vector<BotInfo const*> list;
    list.reserve(snap->bots.size());
    for (BotInfo const& bot : snap->bots)
        list.push_back(&bot);
    return pushBots(L, list);   // the snapshot is sorted by name
}

int pGetBotByName(lua_State* L)
{
    requireBuilt(L);
    std::string name = argStr(L, 1, "name");
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    for (BotInfo const& bot : snap->bots)
        if (bot.name == name)
        {
            pushBot(L, bot.player.guid);
            return 1;
        }
    lua_pushnil(L);
    return 1;
}

int pGetBotByGUID(lua_State* L)
{
    requireBuilt(L);
    fl::Guid guid;
    bool legacy = false;
    argGuid(L, 1, guid, &legacy);
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    if (BotInfo const* bot = findBot(*snap, guid, legacy))
    {
        pushBot(L, bot->player.guid);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// GetBotsOfOwner(player): the bots whose owner scope is the player's (one Battle.net account). Owner 0/unknown owns
// nothing (fail closed).
int pGetBotsOfOwner(lua_State* L)
{
    requireBuilt(L);
    HandleData const& player = objArg(L, 1, kPlayer, "player");
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    std::vector<BotInfo const*> list;
    uint64_t owner = 0;
    if (host(L).playerOwner(player, owner) && owner != 0)
        for (BotInfo const& bot : snap->bots)
            if (bot.owner != 0 && bot.owner == owner)
                list.push_back(&bot);
    return pushBots(L, list);
}

// GetBotsOfMaster(player): the bot's host master or its .tb master (commander) is that player.
int pGetBotsOfMaster(lua_State* L)
{
    requireBuilt(L);
    HandleData const& player = objArg(L, 1, kPlayer, "player");
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    std::vector<BotInfo const*> list;
    for (BotInfo const& bot : snap->bots)
        if (bot.master.guid == player.guid || bot.commander.guid == player.guid)
            list.push_back(&bot);
    return pushBots(L, list);
}

// GetBotsInGroup(group|player): the bots of that group (a player: the group it is in now).
int pGetBotsInGroup(lua_State* L)
{
    requireBuilt(L);
    HandleData const* h = toHandleAny(L, 1);
    if (!h || (h->kind != fl::ObjKind::Group && h->kind != fl::ObjKind::Player))
        return luaL_error(L, "GetBotsInGroup: expected a group or a player");
    requireLive(L, *h);
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    fl::Guid groupGuid;
    bool have = false;
    if (h->kind == fl::ObjKind::Group)
    {
        groupGuid = h->guid;
        have = true;
    }
    else
    {
        for (fl::GroupInfo const& group : snap->groups)
            for (fl::GroupMemberInfo const& member : group.members)
                if (member.guid == h->guid)
                {
                    groupGuid = group.group.guid;
                    have = true;
                    break;
                }
    }
    std::vector<BotInfo const*> list;
    if (have)
        for (BotInfo const& bot : snap->bots)
            if (bot.group.guid == groupGuid)
                list.push_back(&bot);
    return pushBots(L, list);
}

// RegisterBotEvent(event, function [, shots]): the docs cancel closure; the world state only.
int pRegisterBotEvent(lua_State* L)
{
    requireBuilt(L);
    if (eng(L).stateMapId() >= 0)
        return luaL_error(L, "RegisterBotEvent: only available in the world state");
    uint32_t event = argUInt(L, 1, "event");
    if (event < 1 || event > 9)
        return luaL_error(L, "RegisterBotEvent: unknown event id %d", static_cast<int>(event));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    uint32_t shots = static_cast<uint32_t>(optUInt(L, 3, 0));
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    uint64_t id =
        eng(L).addEluHandler(fl::EluKind::Bot, 0, ref, event, shots, std::nullopt, eng(L).where(L));
    if (id == 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "too many event handlers");
    }
    return pushHandlerId(L, id);
}

// ClearBotEvents([event]): all of them, or one id.
int pClearBotEvents(lua_State* L)
{
    requireBuilt(L);
    std::optional<uint32_t> event;
    if (!lua_isnoneornil(L, 1))
    {
        uint32_t id = argUInt(L, 1, "event_type");
        if (id < 1 || id > 9)
            return luaL_error(L, "ClearBotEvents: unknown event id %d", static_cast<int>(id));
        event = id;
    }
    eng(L).clearEluHandlers(fl::EluKind::Bot, std::nullopt, event);
    return 0;
}

// ---- the Bot class ----
namespace
{
// The Bot handle at index 1; a BOT_REMOVED handle never resolves.
HandleData const& botArg(lua_State* L)
{
    HandleData const* h = toHandleAny(L, 1);
    if (!h || h->kind != fl::ObjKind::Bot)
        luaL_error(L, "expected a Bot");
    requireBuilt(L);
    if (h->removed)
        luaL_error(L, "Bot no longer exists");
    return *h;
}

// The bot in the current snapshot; `pin` keeps the snapshot alive for the call.
BotInfo const& live(lua_State* L, std::shared_ptr<BotSnapshot const>& pin)
{
    HandleData const& h = botArg(L);
    pin = readSnapshot(L);
    BotInfo const* bot = findBot(*pin, h.guid);
    if (!bot)
        luaL_error(L, "Bot no longer exists");
    return *bot;
}

// GetGUID(): the handle's 128 bits as text. Never raises (a removed handle still answers).
int bGetGUID(lua_State* L)
{
    HandleData const* h = toHandleAny(L, 1);
    if (!h || h->kind != fl::ObjKind::Bot)
        return luaL_error(L, "expected a Bot");
    std::string s = host(L).guidToString(h->guid);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// IsValid(): in the last snapshot and not a BOT_REMOVED handle. Never raises.
int bIsValid(lua_State* L)
{
    HandleData const* h = toHandleAny(L, 1);
    if (!h || h->kind != fl::ObjKind::Bot)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (h->removed || !fl::botProvider()->available())
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::shared_ptr<BotSnapshot const> snap = readSnapshot(L);
    lua_pushboolean(L, findBot(*snap, h->guid) != nullptr ? 1 : 0);
    return 1;
}

int bGetName(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushlstring(L, bot.name.data(), bot.name.size());
    return 1;
}

int bGetLevel(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushinteger(L, static_cast<lua_Integer>(bot.level));
    return 1;
}

int bGetClass(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushinteger(L, static_cast<lua_Integer>(bot.cls));
    return 1;
}

int bGetRole(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushlstring(L, bot.role.data(), bot.role.size());
    return 1;
}

int bIsRoleForced(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushboolean(L, bot.roleForced ? 1 : 0);
    return 1;
}

int bGetSpec(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushlstring(L, bot.spec.data(), bot.spec.size());
    return 1;
}

int bHasEngine(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushboolean(L, bot.hasEngine ? 1 : 0);
    return 1;
}

int bGetEngineState(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushlstring(L, bot.engineState.data(), bot.engineState.size());
    return 1;
}

int bIsAlive(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushboolean(L, bot.alive ? 1 : 0);
    return 1;
}

int bIsInCombat(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushboolean(L, bot.inCombat ? 1 : 0);
    return 1;
}

int bGetMaster(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    pushPlayerOrNil(L, bot.master);
    return 1;
}

int bGetCommander(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    pushPlayerOrNil(L, bot.commander);
    return 1;
}

int bGetGroup(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    if (bot.group.guid == fl::Guid())
    {
        lua_pushnil(L);
        return 1;
    }
    HandleData h = bot.group;
    h.kind = fl::ObjKind::Group;
    fl::pushHandle(L, h);
    return 1;
}

// GetPlayer(): the bot's character as a Player handle; its methods apply the normal access rules.
int bGetPlayer(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    pushPlayerOrNil(L, bot.player);
    return 1;
}

// GetLastOrder(): seq, verb, result ("" = no order, or one still running).
int bGetLastOrder(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    lua_pushinteger(L, static_cast<lua_Integer>(bot.lastOrder.seq));
    lua_pushlstring(L, bot.lastOrder.verb.data(), bot.lastOrder.verb.size());
    lua_pushlstring(L, bot.lastOrder.result.data(), bot.lastOrder.result.size());
    return 3;
}

// GetRun(): nil, or {leader = Player, plan, state, step, steps, why}.
int bGetRun(lua_State* L)
{
    std::shared_ptr<BotSnapshot const> pin;
    BotInfo const& bot = live(L, pin);
    if (!bot.hasRun)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    pushPlayerOrNil(L, bot.run.leader);
    lua_setfield(L, -2, "leader");
    lua_pushlstring(L, bot.run.plan.data(), bot.run.plan.size());
    lua_setfield(L, -2, "plan");
    lua_pushlstring(L, bot.run.state.data(), bot.run.state.size());
    lua_setfield(L, -2, "state");
    lua_pushinteger(L, static_cast<lua_Integer>(bot.run.step));
    lua_setfield(L, -2, "step");
    lua_pushinteger(L, static_cast<lua_Integer>(bot.run.steps));
    lua_setfield(L, -2, "steps");
    lua_pushlstring(L, bot.run.why.data(), bot.run.why.size());
    lua_setfield(L, -2, "why");
    return 1;
}

// ---- B2: the order calls (the maintainers' bot API specification section 3) ----
// Every call hands ONE order over and answers the ticket, or nil, code, msg. Nothing here resolves a Player of the
// world: the bot's and the argument's guid travel as text and the provider resolves them on the world thread at its next
// tick, so a script anywhere (a command, a map state) can order a bot on any map (the engine's map rule).

// A test-mode script has no order entry point: BotsInterrupt and BotCommanderPlace are installed there and answer
// `test-mode`, so a script shared by both modes gets the reason instead of a nil call.
int testModeOrder(lua_State* L)
{
    lua_pushnil(L);
    lua_pushliteral(L, "test-mode");
    lua_pushliteral(L, "orders are not available in test mode");
    return 3;
}

// A refusal the engine decides itself (the spec's `bad-argument`): nothing was queued.
int badArgument(lua_State* L, std::string const& message)
{
    lua_pushnil(L);
    lua_pushliteral(L, "bad-argument");
    lua_pushlstring(L, message.data(), message.size());
    return 3;
}

// The order call's tail: the ticket the events carry, or the hub's refusal (rate-limited, queue-full).
int answerOrder(lua_State* L, fl::OrderRequest order)
{
    fl::BotHub::OrderPost const post = fl::botHub().postOrder(
        eng(L).orderDomain(), eng(L).dataNamespace(L), std::move(order), host(L).nowMs());
    if (post.ticket != 0)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(post.ticket));
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, post.code);
    lua_pushlstring(L, post.message.data(), post.message.size());
    return 3;
}

// The Bot handle at `idx`, checked against the last snapshot: a stale or BOT_REMOVED handle raises (section 5).
// `idx` must be a positive or absolute index: toHandleAny pushes the metatable, which moves a negative one.
fl::Guid botOrderArg(lua_State* L, int idx)
{
    fl::HandleData const* h = fl::toHandleAny(L, idx);
    if (!h || h->kind != fl::ObjKind::Bot)
    {
        luaL_argerror(L, idx, "expected a Bot");
        return fl::Guid();
    }
    requireBuilt(L);
    if (h->removed)
    {
        luaL_error(L, "Bot no longer exists");
        return fl::Guid();
    }
    std::shared_ptr<BotSnapshot const> pin = readSnapshot(L);
    if (!findBot(*pin, h->guid))
    {
        luaL_error(L, "Bot no longer exists");
        return fl::Guid();
    }
    return h->guid;
}

// A handle argument as its 32-hex guid text (no access check, no lookup: the object may be on any map).
std::string handleGuidText(lua_State* L, int idx, unsigned kinds, char const* what)
{
    fl::HandleData const* h = fl::toHandleAny(L, idx);
    if (!h || !(kinds & kindBit(h->kind)))
    {
        luaL_argerror(L, idx, (std::string(what) + " is not of the expected kind").c_str());
        return std::string();
    }
    if (h->removed)
    {
        luaL_error(L, "%s no longer exists", what);
        return std::string();
    }
    return fl::guidText(h->guid);
}

// A unit argument: a Unit handle (Player or Creature) or the 32-hex guid the spec allows (`attack <guid>`).
std::string unitGuidText(lua_State* L, int idx, char const* what)
{
    if (fl::toHandleAny(L, idx))
        return handleGuidText(L, idx, kUnit, what);
    if (lua_type(L, idx) != LUA_TSTRING)
    {
        luaL_typeerror(L, idx, what);
        return std::string();
    }
    fl::Guid guid;
    argGuid(L, idx, guid, nullptr);
    return fl::guidText(guid);
}

// The order of the calling handle's bot (the Bot methods).
int selfOrder(lua_State* L, char const* verb, std::string arg = std::string(), std::vector<std::string> words = {})
{
    fl::OrderRequest order;
    order.bot.kind = fl::ObjKind::Bot;
    order.bot.guid = botOrderArg(L, 1);
    order.verb = verb;
    order.arg = std::move(arg);
    order.words = std::move(words);
    return answerOrder(L, std::move(order));
}

bool isCrowdControlMark(std::string const& mark)
{
    return mark == "star" || mark == "circle" || mark == "diamond" || mark == "triangle" || mark == "moon" ||
           mark == "square" || mark == "cross";
}

// Follow([member]): the party member to follow (its guid), nothing = the master.
int bFollow(lua_State* L)
{
    std::string member;
    if (!lua_isnoneornil(L, 2))
        member = handleGuidText(L, 2, kPlayer, "member");
    return selfOrder(L, "follow", std::move(member));
}

int bCome(lua_State* L)
{
    double const x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_checknumber(L, 4);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return badArgument(L, "Come: the point must be finite");
    char point[128];
    std::snprintf(point, sizeof(point), "%.3f,%.3f,%.3f", x, y, z);
    return selfOrder(L, "come", point);
}

int bAttack(lua_State* L) { return selfOrder(L, "attack", unitGuidText(L, 2, "target")); }

// Pull(t[, sec[, "ranged" | "body"]]).
int bPull(lua_State* L)
{
    std::string const target = unitGuidText(L, 2, "target");
    std::vector<std::string> words;
    if (!lua_isnoneornil(L, 3))
    {
        int32_t const seconds = argInt(L, 3, "seconds");
        if (seconds < 0 || seconds > 60)
            return badArgument(L, "Pull: the seconds are 0..60");
        words.push_back(std::to_string(seconds));
    }
    if (!lua_isnoneornil(L, 4))
    {
        std::string const style = argStr(L, 4, "style");
        if (style != "ranged" && style != "body")
            return badArgument(L, "Pull: the style is ranged or body");
        words.push_back(style);
    }
    return selfOrder(L, "pull", target, std::move(words));
}

int bAssist(lua_State* L) { return selfOrder(L, "assist", handleGuidText(L, 2, kPlayer, "member")); }

int bStance(lua_State* L)
{
    std::string const word = argStr(L, 2, "stance");
    if (word.empty())
        return badArgument(L, "Stance: a stance word is required (near, tank, turnback, turn, behind)");
    return selfOrder(L, "stance", word);
}

// Engage([sec]): 0..60 seconds of pull window; nothing = the engine's own default.
int bEngage(lua_State* L)
{
    std::string seconds;
    if (!lua_isnoneornil(L, 2))
    {
        int32_t const value = argInt(L, 2, "seconds");
        if (value < 0 || value > 60)
            return badArgument(L, "Engage: the seconds are 0..60");
        seconds = std::to_string(value);
    }
    return selfOrder(L, "engage", std::move(seconds));
}

// SetToggle(aoe|autocc|sap|loot, bool): the toggle word is the order's verb, `on`/`off` its argument (the bot module's M3 form).
int bSetToggle(lua_State* L)
{
    std::string const toggle = argStr(L, 2, "toggle");
    if (toggle != "aoe" && toggle != "autocc" && toggle != "sap" && toggle != "loot")
        return badArgument(L, "SetToggle: the toggle is aoe, autocc, sap or loot");
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    bool const on = lua_toboolean(L, 3) != 0;
    return selfOrder(L, toggle.c_str(), on ? "on" : "off");
}

// CrowdControl(t, mark[, family]): the bot gets the mark; the provider passes its party's leader to ApplyCcOrder.
int bCrowdControl(lua_State* L)
{
    std::string const target = unitGuidText(L, 2, "target");
    std::string const mark = argStr(L, 3, "mark");
    if (!isCrowdControlMark(mark))
        return badArgument(L, "CrowdControl: the mark is star, circle, diamond, triangle, moon, square or cross");
    std::vector<std::string> words{mark};
    if (!lua_isnoneornil(L, 4))
        words.push_back(argStr(L, 4, "family"));
    return selfOrder(L, "cc", target, std::move(words));
}

// StartRun(plan[, fromStep]): the bot is the run's leader (the bot module's turtle run: the leader is a bot).
int bStartRun(lua_State* L)
{
    std::string const plan = argStr(L, 2, "plan");
    if (plan.empty())
        return badArgument(L, "StartRun: a plan name is required");
    std::vector<std::string> words;
    if (!lua_isnoneornil(L, 3))
    {
        int32_t const fromStep = argInt(L, 3, "fromStep");
        if (fromStep < 0)
            return badArgument(L, "StartRun: fromStep must not be negative");
        words.push_back(std::to_string(fromStep));
    }
    return selfOrder(L, "startrun", plan, std::move(words));
}

int bStopRun(lua_State* L)
{
    std::string why;
    if (!lua_isnoneornil(L, 2))
        why = argStr(L, 2, "why");
    return selfOrder(L, "stoprun", std::move(why));
}

// The verbs with no argument of their own.
int bStay(lua_State* L) { return selfOrder(L, "stay"); }
int bEndPull(lua_State* L) { return selfOrder(L, "endpull"); }
int bHold(lua_State* L) { return selfOrder(L, "hold"); }
int bFree(lua_State* L) { return selfOrder(L, "free"); }
int bRelease(lua_State* L) { return selfOrder(L, "release"); }
int bStop(lua_State* L) { return selfOrder(L, "stop"); }
int bFlee(lua_State* L) { return selfOrder(L, "flee"); }
int bGuard(lua_State* L) { return selfOrder(L, "guard"); }
int bRebuff(lua_State* L) { return selfOrder(L, "rebuff"); }
int bCorpseRun(lua_State* L) { return selfOrder(L, "corpserun"); }
int bClearCrowdControl(lua_State* L) { return selfOrder(L, "cc-clear"); }
int bPauseRun(lua_State* L) { return selfOrder(L, "pauserun"); }
int bResumeRun(lua_State* L) { return selfOrder(L, "resumerun"); }
}  // namespace

// ---- B2: the globals (installed by registerGlobals; test mode installs them as test-mode refusals) ----

// The most candidates one interrupt may name: a raid (40). The request's words are a C++ heap vector outside the Lua
// memory limit, so the table is deduped and bounded here.
constexpr size_t kMaxInterruptCandidates = 40;

// BotsInterrupt(bots, t): ask the party's interrupt for `t`. The candidates travel with the request (the first is its
// `bot`, all of them its words): the provider picks the one with a ready interrupt and names it in the verdict, so the
// ORDER events carry the bot that actually got the order. Identical handles are one candidate; more than 40 distinct
// candidates is `bad-argument`.
int pBotsInterrupt(lua_State* L)
{
    if (eng(L).config().mode != fl::EngineMode::Eluna)
        return testModeOrder(L);
    requireBuilt(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer const count = static_cast<lua_Integer>(lua_rawlen(L, 1));
    if (count <= 0)
        return badArgument(L, "BotsInterrupt: at least one bot is required");
    std::string const target = unitGuidText(L, 2, "target");
    std::vector<std::string> candidates;
    fl::Guid first;
    for (lua_Integer i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, 1, static_cast<int>(i));
        int const at = lua_absindex(L, -1);   // toHandleAny reads a positive index only (it pushes the metatable)
        fl::Guid const guid = botOrderArg(L, at);
        lua_pop(L, 1);
        if (i == 1)
            first = guid;
        std::string const text = fl::guidText(guid);
        if (std::find(candidates.begin(), candidates.end(), text) != candidates.end())
            continue;   // the same bot twice: one candidate
        if (candidates.size() == kMaxInterruptCandidates)
            return badArgument(L, "BotsInterrupt: at most 40 distinct candidate bots are allowed (a raid)");
        candidates.push_back(text);
    }
    fl::OrderRequest order;
    order.bot.kind = fl::ObjKind::Bot;
    order.bot.guid = first;
    order.verb = "interrupt";
    order.arg = target;
    order.words = std::move(candidates);
    return answerOrder(L, std::move(order));
}

// BotCommanderPlace(player, words): the chat form of a place/rally/fall back command, for that player's bots; the
// provider answers that player. `words` is the chat text, or a table of its words. The call names no bot: its ORDER
// events carry nil in the bot slot.
int pBotCommanderPlace(lua_State* L)
{
    if (eng(L).config().mode != fl::EngineMode::Eluna)
        return testModeOrder(L);
    requireBuilt(L);
    std::string const player = handleGuidText(L, 1, kPlayer, "player");
    std::vector<std::string> words;
    if (lua_istable(L, 2))
    {
        lua_Integer const count = static_cast<lua_Integer>(lua_rawlen(L, 2));
        for (lua_Integer i = 1; i <= count; ++i)
        {
            lua_rawgeti(L, 2, static_cast<int>(i));
            int const at = lua_absindex(L, -1);
            words.push_back(argStr(L, at, "word"));
            lua_pop(L, 1);
        }
    }
    else
    {
        std::string const text = argStr(L, 2, "words");
        size_t start = text.find_first_not_of(" \t\r\n");
        while (start != std::string::npos)
        {
            size_t const end = text.find_first_of(" \t\r\n", start);
            words.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            start = end == std::string::npos ? std::string::npos : text.find_first_not_of(" \t\r\n", end);
        }
    }
    if (words.empty())
        return badArgument(L, "BotCommanderPlace: the words are required (place, rally or fall back ...)");
    fl::OrderRequest order;
    order.bot.kind = fl::ObjKind::Bot;   // an empty guid: a party-level call names no bot
    order.verb = "place";
    order.arg = player;
    order.words = std::move(words);
    return answerOrder(L, std::move(order));
}

void registerBotMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetGUID", fl::safe<bGetGUID>},
        {"IsValid", fl::safe<bIsValid>},
        {"GetName", fl::safe<bGetName>},
        {"GetLevel", fl::safe<bGetLevel>},
        {"GetClass", fl::safe<bGetClass>},
        {"GetRole", fl::safe<bGetRole>},
        {"IsRoleForced", fl::safe<bIsRoleForced>},
        {"GetSpec", fl::safe<bGetSpec>},
        {"HasEngine", fl::safe<bHasEngine>},
        {"GetEngineState", fl::safe<bGetEngineState>},
        {"IsAlive", fl::safe<bIsAlive>},
        {"IsInCombat", fl::safe<bIsInCombat>},
        {"GetMaster", fl::safe<bGetMaster>},
        {"GetCommander", fl::safe<bGetCommander>},
        {"GetGroup", fl::safe<bGetGroup>},
        {"GetPlayer", fl::safe<bGetPlayer>},
        {"GetLastOrder", fl::safe<bGetLastOrder>},
        {"GetRun", fl::safe<bGetRun>},
        // B2: the orders of this bot (each answers a ticket, or nil, code, msg).
        {"Follow", fl::safe<bFollow>},
        {"Stay", fl::safe<bStay>},
        {"Come", fl::safe<bCome>},
        {"Attack", fl::safe<bAttack>},
        {"Pull", fl::safe<bPull>},
        {"EndPull", fl::safe<bEndPull>},
        {"Hold", fl::safe<bHold>},
        {"Free", fl::safe<bFree>},
        {"Assist", fl::safe<bAssist>},
        {"Release", fl::safe<bRelease>},
        {"Stop", fl::safe<bStop>},
        {"Flee", fl::safe<bFlee>},
        {"Guard", fl::safe<bGuard>},
        {"Stance", fl::safe<bStance>},
        {"Engage", fl::safe<bEngage>},
        {"Rebuff", fl::safe<bRebuff>},
        {"SetToggle", fl::safe<bSetToggle>},
        {"CorpseRun", fl::safe<bCorpseRun>},
        {"CrowdControl", fl::safe<bCrowdControl>},
        {"ClearCrowdControl", fl::safe<bClearCrowdControl>},
        {"StartRun", fl::safe<bStartRun>},
        {"StopRun", fl::safe<bStopRun>},
        {"PauseRun", fl::safe<bPauseRun>},
        {"ResumeRun", fl::safe<bResumeRun>},
        {nullptr, nullptr}};
    addMethods(L, "Bot", regs);
}

}  // namespace flu
