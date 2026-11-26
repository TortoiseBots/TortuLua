// Shared helpers of the Eluna bindings: object resolution (existence, cross-map), argument checks and host dispatch.
#include "engine/elu/EluBind.h"

#include "engine/Api.h"
#include "engine/Elu.h"
#include "engine/GuidText.h"
#include "engine/Text.h"

#include <cctype>
#include <exception>

namespace flu
{
namespace
{
char const* classNameOf(fl::HandleData const& h) { return fl::eluClassName(h.kind); }
}  // namespace

fl::HandleData const& self(lua_State* L, unsigned kinds, bool alive, int idx)
{
    fl::HandleData const* h = fl::toHandleAny(L, idx);
    if (!h)
        luaL_typeerror(L, idx, "Eluna object");
    if (!(kinds & kindBit(h->kind)))
        return luaL_error(L, "this method does not work on a %s", classNameOf(*h)), *h;
    if (alive)
        requireLive(L, *h);
    return *h;
}

void requireLive(lua_State* L, fl::HandleData const& h)
{
    fl::Host& hh = host(L);
    if (h.removed)
        luaL_error(L, "%s no longer exists", classNameOf(h));   // a REMOVE event's handle: never resolves
    // The access check comes FIRST: a map thread must not even look a stale or foreign handle up (the lookup itself
    // reads another map's object store). A handle whose object left its map answers "no longer exists".
    switch (hh.checkAccess(h))
    {
        case fl::Access::Ok: break;
        case fl::Access::Gone: luaL_error(L, "%s no longer exists", classNameOf(h)); break;
        case fl::Access::OtherMap: luaL_error(L, "object is on another map"); break;
    }
    fl::ObjectInfo info;
    if (!hh.lookup(h, &info))
        luaL_error(L, "%s no longer exists", classNameOf(h));
}

void requireWorldRead(lua_State* L, char const* display)
{
    if (host(L).worldOnlyReadOk())
        return;
    luaL_error(L, "%s is only available on the world thread (server events, timed events, commands), "
                  "not from a map-thread handler",
               display);
}

fl::HandleData const& objArg(lua_State* L, int idx, unsigned kinds, char const* what)
{
    fl::HandleData const* h = fl::toHandleAny(L, idx);
    if (!h)
        luaL_typeerror(L, idx, what);
    if (!(kinds & kindBit(h->kind)))
        luaL_argerror(L, idx, (std::string(what) + " is not of the expected kind").c_str());
    requireLive(L, *h);
    return *h;
}

fl::HandleData const& kindArg(lua_State* L, int idx, unsigned kinds, char const* what)
{
    fl::HandleData const* h = fl::toHandleAny(L, idx);
    if (!h)
        luaL_typeerror(L, idx, what);
    if (!(kinds & kindBit(h->kind)))
        luaL_argerror(L, idx, (std::string(what) + " is not of the expected kind").c_str());
    return *h;
}

void gameCall(lua_State* L, fl::HandleData const& object, char const* op, std::vector<EventValue> const& args,
              std::vector<EventValue>& out, char const* display)
{
    if (object.removed)
        luaL_error(L, "%s no longer exists", classNameOf(object));
    if (!host(L).gameCall(object, op, args, out))
        luaL_error(L, "%s: not available on this server", display);
}

uint32_t argUInt(lua_State* L, int idx, char const* what)
{
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 0xFFFFFFFFll)
        luaL_error(L, "%s must be an integer 0..4294967295", what);
    return static_cast<uint32_t>(v);
}

uint32_t argU8(lua_State* L, int idx, char const* what)
{
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 255)
        luaL_error(L, "%s must be an integer 0..255", what);
    return static_cast<uint32_t>(v);
}

uint32_t argU16(lua_State* L, int idx, char const* what)
{
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 65535)
        luaL_error(L, "%s must be an integer 0..65535", what);
    return static_cast<uint32_t>(v);
}

int32_t argInt(lua_State* L, int idx, char const* what)
{
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < -2147483647ll || v > 2147483647ll)
        luaL_error(L, "%s must be an integer -2147483647..2147483647", what);
    return static_cast<int32_t>(v);
}

int64_t argI64(lua_State* L, int idx, char const* what)
{
    (void)what;
    return static_cast<int64_t>(luaL_checkinteger(L, idx));
}

float argFloat(lua_State* L, int idx, char const* what)
{
    lua_Number v = luaL_checknumber(L, idx);
    if (!(v == v))
        luaL_error(L, "%s must not be NaN", what);
    return static_cast<float>(v);
}

bool argGuid(lua_State* L, int idx, fl::Guid& out, bool* legacyLow)
{
    if (legacyLow)
        *legacyLow = false;
    if (lua_type(L, idx) == LUA_TSTRING)
    {
        size_t n = 0;
        char const* s = lua_tolstring(L, idx, &n);
        if (fl::parseGuidText(std::string_view(s, n), out))
            return true;
        luaL_argerror(L, idx, "guid must be an Object:GetGUID() value");
    }
    if (lua_isinteger(L, idx))
    {
        lua_Integer v = lua_tointeger(L, idx);
        out.hi = 0;
        out.lo = static_cast<uint64_t>(v);
        if (legacyLow)
            *legacyLow = true;
        return true;
    }
    luaL_typeerror(L, idx, "guid");
    return false;
}

std::string argStr(lua_State* L, int idx, char const* what)
{
    size_t n = 0;
    char const* s = luaL_checklstring(L, idx, &n);
    (void)what;
    if (n > 256 * 1024)
        luaL_error(L, "%s is longer than 256 KB", what);
    return std::string(s, n);
}

bool optBool(lua_State* L, int idx, bool def)
{
    if (lua_isnoneornil(L, idx))
        return def;
    luaL_checktype(L, idx, LUA_TBOOLEAN);
    return lua_toboolean(L, idx) != 0;
}

bool argBool(lua_State* L, int idx, char const* what)
{
    (void)what;
    luaL_checktype(L, idx, LUA_TBOOLEAN);
    return lua_toboolean(L, idx) != 0;
}

void requireHostOk(lua_State* L, std::vector<EventValue> const& out, char const* display, char const* why)
{
    if (out.empty())
        return;
    if (auto const* ok = std::get_if<bool>(&out[0].v))
        if (!*ok)
            luaL_error(L, "%s: %s", display, why);
}

int32_t optInt(lua_State* L, int idx, int32_t def, char const* what)
{
    if (lua_isnoneornil(L, idx))
        return def;
    return argInt(L, idx, what);
}

uint64_t optUInt(lua_State* L, int idx, uint64_t def)
{
    if (lua_isnoneornil(L, idx))
        return def;
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0)
        luaL_error(L, "argument %d must not be negative", idx);
    return static_cast<uint64_t>(v);
}

int pushResults(lua_State* L, std::vector<EventValue> const& out)
{
    if (!lua_checkstack(L, static_cast<int>(out.size()) + 2))
        luaL_error(L, "stack overflow");
    for (auto const& v : out)
        fl::pushValue(L, v);
    return static_cast<int>(out.size());
}

int triStateBool(lua_State* L, unsigned kinds, char const* op, char const* display, char const* why,
                 std::vector<EventValue> args)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    int64_t answer = 0;
    if (!out.empty())
    {
        if (auto const* n = std::get_if<int64_t>(&out[0].v))
            answer = *n;
        else if (auto const* b = std::get_if<bool>(&out[0].v))
            answer = *b ? 1 : 0;
    }
    if (answer < 0)
        luaL_error(L, "%s: %s", display, why);
    lua_pushboolean(L, answer != 0);
    return 1;
}

void pushHandleOrNil(lua_State* L, std::optional<fl::HandleData> const& h)
{
    if (!h)
        lua_pushnil(L);
    else
        fl::pushHandle(L, *h);
}

int readOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    return pushResults(L, out);
}

int actionOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    return 0;
}

std::optional<fl::HandleData> handleResult(std::vector<EventValue> const& out)
{
    if (out.empty())
        return std::nullopt;
    if (auto const* h = std::get_if<fl::HandleData>(&out[0].v))
        return *h;
    return std::nullopt;
}

int pushHandleTable(lua_State* L, std::vector<fl::HandleData> const& list)
{
    if (!lua_checkstack(L, static_cast<int>(list.size()) + 2))
        luaL_error(L, "stack overflow");
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 0;
    for (auto const& h : list)
    {
        fl::pushHandle(L, h);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

int listOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    std::vector<fl::HandleData> list;
    list.reserve(out.size());
    for (auto const& v : out)
        if (auto const* o = std::get_if<fl::HandleData>(&v.v))
            list.push_back(*o);
    return pushHandleTable(L, list);
}

int readHandleOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    pushHandleOrNil(L, handleResult(out));
    return 1;
}

int cancelHandler(lua_State* L)
{
    uint64_t id = static_cast<uint64_t>(lua_tointeger(L, lua_upvalueindex(1)));
    eng(L).removeEluHandler(id);
    return 0;
}

void sendPacketArg(lua_State* L, fl::HandleData const& target, int idx, char const* display, bool ignorePlayersInBg,
                   uint64_t ignore)
{
    fl::HandleData const& packet = kindArg(L, idx, kPacket, "packet");
    fl::Engine::PacketValue* value = eng(L).packet(packet.guid.lo);
    if (!value)
        luaL_error(L, "%s: WorldPacket no longer exists", display);
    if (!host(L).sendPacket(target, value->opcode, value->bytes, ignorePlayersInBg, ignore))
        luaL_error(L, "%s: not available on this server", display);
}

void sendPacketToRankArg(lua_State* L, fl::HandleData const& target, int idx, uint32_t rank, char const* display)
{
    fl::HandleData const& packet = kindArg(L, idx, kPacket, "packet");
    fl::Engine::PacketValue* value = eng(L).packet(packet.guid.lo);
    if (!value)
        luaL_error(L, "%s: WorldPacket no longer exists", display);
    if (!host(L).sendPacketToRanked(target, value->opcode, value->bytes, rank))
        luaL_error(L, "%s: not available on this server", display);
}

int pushHandlerId(lua_State* L, uint64_t id)
{
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_pushcclosure(L, fl::safe<cancelHandler>, 1);
    return 1;   // docs: Register*Event returns the cancel function
}

namespace
{
// Events whose driving hook the core does not have (spec section 6): they register, and say so once.
char const* hookGapFor(fl::Config const& cfg, fl::EluKind kind, uint32_t event)
{
    bool const c1 = fl::hasCoreHook(cfg, fl::CoreHook::AllCreature);
    bool const c2 = fl::hasCoreHook(cfg, fl::CoreHook::AllGameObject);
    bool const creatureEvents = fl::hasCoreHook(cfg, fl::CoreHook::CreatureEvents);
    bool const gameObjectUse = fl::hasCoreHook(cfg, fl::CoreHook::GameObjectUse);
    bool const gossip = fl::hasCoreHook(cfg, fl::CoreHook::GossipSelect);
    bool const itemUse = fl::hasCoreHook(cfg, fl::CoreHook::ItemUse);
    switch (kind)
    {
        case fl::EluKind::Creature:
            // The hooks are notify only (the module never replaces an AI): ADD/REMOVE and ENTER_COMBAT, LEAVE_COMBAT,
            // DIED, SPAWN for creatures without a ScriptName (RESET has no hook: it stays a ScriptName event). Every other creature event, and these for a creature with
            // a ScriptName, fire only from the ScriptName forever_lua (noteEventGaps says so).
            if (event == 30)
                return "C9";
            if (event == 36 || event == 37)
                return c1 ? nullptr : "C1";
            if (event == 1 || event == 2 || event == 4 || event == 5)
                return creatureEvents ? nullptr : "C1";
            return nullptr;
        case fl::EluKind::GameObject:
            if (event == 3)
                return "C9";
            if (event == 12 || event == 13)
                return c2 ? nullptr : "C2";
            if (event == 14)
                return gameObjectUse ? nullptr : "C2";
            return nullptr;   // ScriptName forever_lua_gameobject (noteEventGaps says so)
        case fl::EluKind::Item:
            // C3 (PR #378) adds the use of every item only; quest accept, expire and remove still need the item's
            // ScriptName forever_lua_item, in every core. DUMMY_EFFECT needs C9.
            if (event == 1)
                return "C9";
            return event == 2 && !itemUse ? "C3" : nullptr;
        case fl::EluKind::PlayerGossip: return event == 1 ? nullptr : (gossip ? nullptr : "C3");
        case fl::EluKind::ItemGossip: return (event == 1 ? itemUse : gossip) ? nullptr : "C3";
        case fl::EluKind::Server:
            switch (event)
            {
                case 24: return "C8";
                case 31: return c1 ? nullptr : "C1";
                case 32: return c2 ? nullptr : "C2";
                case 34:
                case 35: return "C7";
                default: return nullptr;
            }
        case fl::EluKind::Player:
            switch (event)
            {
                case 42: return fl::hasCoreHook(cfg, fl::CoreHook::PlayerCommand) ? nullptr : "C4";
                case 18:
                case 19:
                case 20:
                case 21:
                case 22: return "C6";
                case 23:
                case 29:
                case 31:
                case 33:
                case 34:
                case 36:
                case 37:
                case 38:
                case 39:
                case 40:
                case 41:
                case 43:
                case 44:
                case 46:
                case 48:
                case 49: return "C5";
                default: return nullptr;
            }
        case fl::EluKind::CreatureGossip:
        case fl::EluKind::GameObjectGossip: return nullptr;
        case fl::EluKind::Group: return event == 6 ? "C10" : nullptr;
        case fl::EluKind::Guild: return nullptr;
        case fl::EluKind::BattleGround:
            return nullptr;   // E5: the pinned core has BattlegroundScript/BattlegroundMapScript, so C12 is not needed
        case fl::EluKind::Map:
        case fl::EluKind::Instance:
            // LOAD, CREATURE_CREATE, GAMEOBJECT_CREATE and CHECK_ENCOUNTER need InstanceScript forwarding (C11).
            return (event == 2 || event == 5 || event == 6 || event == 7) ? "C11" : nullptr;
        case fl::EluKind::Packet: return nullptr;   // registered here, wired with the core's packet hooks in E5
        case fl::EluKind::Bot: return nullptr;      // our own API: the bot snapshot needs no core hook
    }
    return nullptr;
}

// Events of systems Classic 1.60 does not have: they register, and say so once (spec section 7).
char const* classicStubFor(fl::EluKind kind, uint32_t event)
{
    if (kind == fl::EluKind::Guild && (event == 9 || event == 11))
        return "no guild bank on Classic 1.60";
    if (kind == fl::EluKind::GameObject && (event == 7 || event == 8))
        return "no destructible game objects on Classic 1.60";
    if (kind == fl::EluKind::Player && (event == 45 || event == 50))
        return event == 45 ? "no achievements on Classic 1.60" : "no dungeon finder on Classic 1.60";
    return nullptr;
}
}  // namespace

void noteEventGaps(lua_State* L, fl::EluKind kind, uint32_t event)
{
    std::string const family = std::to_string(int(kind));
    if (!fl::eluEventSpec(kind, event) &&
        eng(L).noteHookGap("elu-event-" + family + "-" + std::to_string(event)))
        eng(L).host().log(fl::LogLevel::Warn, std::string("ForeverLua: event ") + fl::eluKindName(kind) + " " +
                                                   std::to_string(event) +
                                                   " is not in the Eluna documentation; registered anyway");
    if (char const* hook = hookGapFor(eng(L).config(), kind, event))
    {
        std::string name = fl::eluEventSpec(kind, event) ? fl::eluEventSpec(kind, event)->name : "?";
        if (eng(L).noteHookGap("elu-hook-" + family + "-" + std::to_string(event)))
        {
            // The ScriptName binding is the interim path for the three families C1-C3 replace; the other hooks have none.
            std::string const interim =
                std::string(hook) == "C1" || std::string(hook) == "C2" || std::string(hook) == "C3"
                    ? " (interim: the core's ScriptName forever_lua for a creature, forever_lua_gameobject, "
                      "forever_lua_item)"
                    : std::string();
            eng(L).host().log(fl::LogLevel::Warn, std::string("event ") + fl::eluKindName(kind) + " " + name +
                                                       " needs core hook " + hook + interim);
        }
    }
    if (kind == fl::EluKind::Creature || kind == fl::EluKind::GameObject)
    {
        // No core hook replaces an AI, so the events that need the AI exist only for the ScriptName path; the hooked
        // events also fire there (and the hook skips a creature that has a ScriptName, so nothing fires twice).
        bool const creature = kind == fl::EluKind::Creature;
        bool const hooked = creature ? (event == 1 || event == 2 || event == 4 || event == 5 || event == 36 || event == 37 ||
                                        event == 30)
                                     : (event == 12 || event == 13 || event == 14 || event == 3);
        if (!hooked && eng(L).noteHookGap("elu-ai-script-" + family + "-" + std::to_string(event)))
        {
            std::string name = fl::eluEventSpec(kind, event) ? fl::eluEventSpec(kind, event)->name : "?";
            eng(L).host().log(fl::LogLevel::Warn,
                              std::string("event ") + fl::eluKindName(kind) + " " + name +
                                  " fires only for " + (creature ? "creatures whose ScriptName is forever_lua"
                                                                  : "game objects whose ScriptName is forever_lua_gameobject") +
                                  ": the core's all-" + (creature ? "creature" : "game object") +
                                  " hooks only notify and cannot replace an AI");
        }
    }
    if (kind == fl::EluKind::Item && (event == 3 || event == 4 || event == 5) &&
        eng(L).noteHookGap("elu-item-script-" + family + "-" + std::to_string(event)))
    {
        std::string name = fl::eluEventSpec(kind, event) ? fl::eluEventSpec(kind, event)->name : "?";
        eng(L).host().log(fl::LogLevel::Warn, std::string("event item ") + name +
                                                  " fires for items whose ScriptName is forever_lua_item: the core has no "
                                                  "all-item hook for it (C3 adds the item use only)");
    }
    if (kind == fl::EluKind::Server && (event == 17 || event == 18 || event == 21 || event == 22 || event == 23) &&
        eng(L).noteHookGap("elu-server-map-events"))
        eng(L).host().log(fl::LogLevel::Warn, "ForeverLua: the server map events (17, 18, 21, 22, 23) fire for world (continent) "
                                              "maps only; dungeon and raid maps need core hook C11 (instance script forwarding)");
    if (char const* stub = classicStubFor(kind, event))
    {
        std::string name = fl::eluEventSpec(kind, event) ? fl::eluEventSpec(kind, event)->name : "?";
        if (eng(L).noteHookGap("elu-stub-" + family + "-" + std::to_string(event)))
            eng(L).host().log(fl::LogLevel::Warn, std::string("event ") + fl::eluKindName(kind) + " " + name +
                                                       ": not available on Classic 1.60 (" + stub + ")");
    }
}

void addMethods(lua_State* L, char const* className, luaL_Reg const* regs)
{
    lua_getfield(L, LUA_REGISTRYINDEX, (std::string("Eluna.class.") + className).c_str());
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    luaL_setfuncs(L, regs, 0);
    lua_pop(L, 1);
}
}  // namespace flu
