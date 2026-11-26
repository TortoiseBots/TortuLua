// Eluna classes over the handle data (E0): one metatable per class with inheritance by __index chains, the event
// spec table of E1 (docs names and return conventions), and the Register*/Clear* registry plumbing.
//
// Object > WorldObject > Unit > Player|Creature, WorldObject > GameObject, Object > Item, plus Map, Group, Guild and
// Quest. A handle holds {kind, guid, map, instance} and is re-resolved by the host on every method call.
#include "engine/Elu.h"

#include "engine/Api.h"
#include "engine/Engine.h"
#include "engine/Handle.h"
#include "engine/Safe.h"
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cstring>
#include <string>

namespace fl
{
namespace
{
char const* const kClassPrefix = "Eluna.class.";
char const* const kMetaPrefix = "Eluna.";

std::string classRegName(char const* name) { return std::string(kClassPrefix) + name; }
std::string metaName(char const* name) { return std::string(kMetaPrefix) + name; }

void createClass(lua_State* L, char const* name, char const* parent)
{
    lua_newtable(L);   // the method table of this class
    if (parent)
    {
        lua_newtable(L);   // its metatable: __index -> the parent's method table
        lua_getfield(L, LUA_REGISTRYINDEX, classRegName(parent).c_str());
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
    }
    lua_setfield(L, LUA_REGISTRYINDEX, classRegName(name).c_str());

    luaL_newmetatable(L, metaName(name).c_str());
    std::string title = std::string("Eluna ") + name;
    lua_pushlstring(L, title.data(), title.size());
    lua_setfield(L, -2, "__name");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");   // scripts can neither read nor swap it
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "__handle");
    lua_getfield(L, LUA_REGISTRYINDEX, classRegName(name).c_str());
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

int eluEq(lua_State* L)
{
    HandleData const* a = toHandleAny(L, 1);
    HandleData const* b = toHandleAny(L, 2);
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}

int eluToString(lua_State* L)
{
    HandleData const* h = toHandleAny(L, 1);
    if (!h)
    {
        lua_pushliteral(L, "nil");
        return 1;
    }
    std::string s = Engine::from(L)->host().guidToString(h->guid);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int eluNewIndex(lua_State* L)
{
    return luaL_error(L, "Eluna objects are read-only");
}
}  // namespace

char const* eluKindName(EluKind kind)
{
    switch (kind)
    {
        case EluKind::Player: return "player";
        case EluKind::Creature: return "creature";
        case EluKind::GameObject: return "gameobject";
        case EluKind::Item: return "item";
        case EluKind::Server: return "server";
        case EluKind::PlayerGossip: return "player gossip";
        case EluKind::CreatureGossip: return "creature gossip";
        case EluKind::GameObjectGossip: return "gameobject gossip";
        case EluKind::ItemGossip: return "item gossip";
        case EluKind::Group: return "group";
        case EluKind::Guild: return "guild";
        case EluKind::Map: return "map";
        case EluKind::Instance: return "instance";
        case EluKind::Packet: return "packet";
        case EluKind::BattleGround: return "battleground";
        case EluKind::Bot: return "bot";
    }
    return "event";
}

char const* eluClassName(ObjKind kind)
{
    switch (kind)
    {
        case ObjKind::Player: return "Player";
        case ObjKind::Creature: return "Creature";
        case ObjKind::GameObject: return "GameObject";
        case ObjKind::Map: return "Map";
        case ObjKind::Item: return "Item";
        case ObjKind::Group: return "Group";
        case ObjKind::Guild: return "Guild";
        case ObjKind::Quest: return "Quest";
        case ObjKind::Spell: return "Spell";
        case ObjKind::Aura: return "Aura";
        case ObjKind::Query: return "ElunaQuery";
        case ObjKind::Packet: return "WorldPacket";
        case ObjKind::Corpse: return "Corpse";
        case ObjKind::BattleGround: return "BattleGround";
        case ObjKind::Vehicle: return "Vehicle";
        case ObjKind::Bot: return "Bot";
        case ObjKind::SpellInfo: return "SpellInfo";
        case ObjKind::AuraEffect: return "AuraEffect";
        case ObjKind::ElunaProcInfo: return "ElunaProcInfo";
    }
    return "Object";
}

bool eluPushClassMetatable(lua_State* L, ObjKind kind)
{
    char const* name = eluClassName(kind);
    if (!name)
        return false;
    if (luaL_getmetatable(L, metaName(name).c_str()) == LUA_TNIL)
    {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void eluPushHandle(lua_State* L, HandleData const& data)
{
    void* ud = lua_newuserdatauv(L, sizeof(HandleData), 0);
    new (ud) HandleData(data);
    if (!eluPushClassMetatable(L, data.kind))
    {
        lua_pop(L, 1);
        lua_pushnil(L);
        return;
    }
    lua_setmetatable(L, -2);
}

void eluAddMethod(lua_State* L, char const* className, char const* name, lua_CFunction fn)
{
    lua_getfield(L, LUA_REGISTRYINDEX, classRegName(className).c_str());
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

void registerElunaClasses(lua_State* L)
{
    createClass(L, "Object", nullptr);
    createClass(L, "WorldObject", "Object");
    createClass(L, "Unit", "WorldObject");
    createClass(L, "Player", "Unit");
    createClass(L, "Creature", "Unit");
    createClass(L, "GameObject", "WorldObject");
    createClass(L, "Item", "Object");
    createClass(L, "Map", nullptr);
    createClass(L, "Group", nullptr);
    createClass(L, "Guild", nullptr);
    createClass(L, "Quest", nullptr);
    createClass(L, "Spell", nullptr);
    createClass(L, "Aura", nullptr);
    createClass(L, "ElunaQuery", nullptr);
    createClass(L, "WorldPacket", nullptr);   // E5: CreatePacket and the packet events
    createClass(L, "Corpse", "WorldObject");   // E5: Player:GetCorpse
    createClass(L, "BattleGround", nullptr);   // E5: RegisterBGEvent
    createClass(L, "Vehicle", nullptr);        // E5: Classic 1.60 has no vehicles; only the section 7 stubs exist
    createClass(L, "Bot", nullptr);            // our bot API: a read-only handle over the bot snapshot
    createClass(L, "SpellInfo", nullptr);      // long tail: a documented spell entry (GetSpellInfo, Aura/Spell:GetSpellInfo)
    createClass(L, "AuraEffect", nullptr);     // long tail: a transient aura effect of the running call
    createClass(L, "ElunaProcInfo", nullptr);  // long tail: a transient proc event of the running call

    // __eq/__tostring/__newindex on every class metatable.
    for (char const* name : {"Object", "WorldObject", "Unit", "Player", "Creature", "GameObject", "Item", "Map", "Group",
                             "Guild", "Quest", "Spell", "Aura", "ElunaQuery", "WorldPacket", "Corpse", "BattleGround",
                             "Vehicle", "Bot", "SpellInfo", "AuraEffect", "ElunaProcInfo"})
    {
        luaL_getmetatable(L, metaName(name).c_str());
        lua_pushcfunction(L, safe<eluEq>);
        lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, safe<eluToString>);
        lua_setfield(L, -2, "__tostring");
        lua_pushcfunction(L, safe<eluNewIndex>);
        lua_setfield(L, -2, "__newindex");
        lua_pop(L, 1);
    }
}

void registerEluna(lua_State* L)
{
    registerElunaClasses(L);
    flu::registerObjectMethods(L);
    flu::registerWorldObjectMethods(L);
    flu::registerMapMethods(L);
    flu::registerUnitMethods(L);
    flu::registerPlayerMethods(L);
    flu::registerCreatureMethods(L);
    flu::registerGameObjectMethods(L);
    flu::registerGossipMethods(L);
    flu::registerItemMethods(L);
    flu::registerGroupMethods(L);
    flu::registerGuildMethods(L);
    flu::registerQuestMethods(L);
    flu::registerSpellMethods(L);
    flu::registerAuraMethods(L);
    flu::registerSpellInfoMethods(L);
    flu::registerAuraEffectMethods(L);
    flu::registerProcInfoMethods(L);
    flu::registerCorpseMethods(L);
    flu::registerBattleGroundMethods(L);
    flu::registerPacketMethods(L);
    flu::registerVehicleMethods(L);
    flu::registerBotMethods(L);
    flu::registerStubMethods(L);
    registerQueryMethods(L);
    flu::registerGlobals(L);
    flu::registerGameGlobals(L);
    registerCommon(L);   // print and require
}

// ---------------------------------------------------------------------------------------------------------------
// Event specs: the docs names and the return convention of every E1 event id (docs/ELUNA-CLEANROOM.md).

namespace
{
struct SpecRow
{
    uint32_t id;
    EluEventSpec spec;
};

constexpr SpecRow kServer[] = {
    {1, {"NETWORK_START", EluReturn::None}},
    {2, {"NETWORK_STOP", EluReturn::None}},
    {3, {"SOCKET_OPEN", EluReturn::None}},
    {4, {"SOCKET_CLOSE", EluReturn::None}},
    {5, {"PACKET_RECEIVE", EluReturn::None}},
    {6, {"PACKET_RECEIVE_UNKNOWN", EluReturn::None}},
    {7, {"PACKET_SEND", EluReturn::None}},
    {8, {"OPEN_STATE_CHANGE", EluReturn::None}},
    {9, {"CONFIG_LOAD", EluReturn::None}},
    {11, {"SHUTDOWN_INIT", EluReturn::None}},
    {12, {"SHUTDOWN_CANCEL", EluReturn::None}},
    {13, {"UPDATE", EluReturn::None}},
    {14, {"STARTUP", EluReturn::None}},
    {15, {"SHUTDOWN", EluReturn::None}},
    {16, {"LUA_STATE_CLOSE", EluReturn::None}},
    {17, {"MAP_CREATE", EluReturn::None}},
    {18, {"MAP_DESTROY", EluReturn::None}},
    {19, {"GRID_LOAD", EluReturn::None}},
    {20, {"GRID_UNLOAD", EluReturn::None}},
    {21, {"MAP_PLAYER_ENTER", EluReturn::None}},
    {22, {"MAP_PLAYER_LEAVE", EluReturn::None}},
    {23, {"MAP_UPDATE", EluReturn::None}},
    {24, {"TRIGGER_ON_TRIGGER", EluReturn::StopOnTrue}},
    {25, {"WEATHER_CHANGE", EluReturn::None}},
    {26, {"AUCTION_ADD", EluReturn::None}},
    {27, {"AUCTION_REMOVE", EluReturn::None}},
    {28, {"AUCTION_SUCCESSFUL", EluReturn::None}},
    {29, {"AUCTION_EXPIRE", EluReturn::None}},
    {30, {"ADDON_ON_MESSAGE", EluReturn::CancelOnFalse}},
    {31, {"DELETE_CREATURE", EluReturn::None}},
    {32, {"DELETE_GAMEOBJECT", EluReturn::None}},
    {33, {"LUA_STATE_OPEN", EluReturn::None}},
    {34, {"GAME_EVENT_START", EluReturn::None}},
    {35, {"GAME_EVENT_STOP", EluReturn::None}},
};

constexpr SpecRow kPlayer[] = {
    {1, {"CHARACTER_CREATE", EluReturn::None}},
    {2, {"CHARACTER_DELETE", EluReturn::None}},
    {3, {"LOGIN", EluReturn::None}},
    {4, {"LOGOUT", EluReturn::None}},
    {5, {"SPELL_CAST", EluReturn::None}},
    {6, {"KILL_PLAYER", EluReturn::None}},
    {7, {"KILL_CREATURE", EluReturn::None}},
    {8, {"KILLED_BY_CREATURE", EluReturn::None}},
    {9, {"DUEL_REQUEST", EluReturn::None}},
    {10, {"DUEL_START", EluReturn::None}},
    {11, {"DUEL_END", EluReturn::None}},
    {12, {"GIVE_XP", EluReturn::ValueOnFirst}},
    {13, {"LEVEL_CHANGE", EluReturn::None}},
    {14, {"MONEY_CHANGE", EluReturn::ValueOnFirst}},
    {15, {"REPUTATION_CHANGE", EluReturn::ValueOnFirst}},
    {16, {"TALENTS_CHANGE", EluReturn::None}},
    {17, {"TALENTS_RESET", EluReturn::None}},
    {18, {"CHAT", EluReturn::TextOnSecond}},
    {19, {"WHISPER", EluReturn::TextOnSecond}},
    {20, {"GROUP_CHAT", EluReturn::TextOnSecond}},
    {21, {"GUILD_CHAT", EluReturn::TextOnSecond}},
    {22, {"CHANNEL_CHAT", EluReturn::TextOnSecond}},
    {23, {"EMOTE", EluReturn::None}},
    {24, {"TEXT_EMOTE", EluReturn::None}},
    {25, {"SAVE", EluReturn::None}},
    {26, {"BIND_TO_INSTANCE", EluReturn::None}},
    {27, {"UPDATE_ZONE", EluReturn::None}},
    {28, {"MAP_CHANGE", EluReturn::None}},
    {29, {"EQUIP", EluReturn::None}},
    {30, {"FIRST_LOGIN", EluReturn::None}},
    {31, {"CAN_USE_ITEM", EluReturn::ValueOnFirst}},
    {32, {"LOOT_ITEM", EluReturn::None}},
    {33, {"ENTER_COMBAT", EluReturn::None}},
    {34, {"LEAVE_COMBAT", EluReturn::None}},
    {35, {"REPOP", EluReturn::None}},
    {36, {"RESURRECT", EluReturn::None}},
    {37, {"LOOT_MONEY", EluReturn::None}},
    {38, {"QUEST_ABANDON", EluReturn::None}},
    {39, {"LEARN_TALENTS", EluReturn::None}},
    {40, {"ENVIRONMENTAL_DEATH", EluReturn::None}},
    {41, {"TRADE_ACCEPT", EluReturn::CancelOnFalse}},
    {42, {"COMMAND", EluReturn::CancelOnFalse}},
    {43, {"SKILL_CHANGE", EluReturn::ValueOnFirst}},
    {44, {"LEARN_SPELL", EluReturn::None}},
    {45, {"ACHIEVEMENT_COMPLETE", EluReturn::None}},
    {46, {"DISCOVER_TAXI", EluReturn::None}},
    {47, {"UPDATE_AREA", EluReturn::None}},
    {48, {"CAN_INIT_TRADE", EluReturn::CancelOnFalse}},
    {49, {"CAN_SEND_MAIL", EluReturn::CancelOnFalse}},
    {50, {"CAN_JOIN_LFG", EluReturn::CancelOnFalse}},
    {51, {"QUEST_STATUS_CHANGED", EluReturn::None}},
};

constexpr SpecRow kCreature[] = {
    {1, {"ENTER_COMBAT", EluReturn::StopOnTrue}},
    {2, {"LEAVE_COMBAT", EluReturn::StopOnTrue}},
    {3, {"TARGET_DIED", EluReturn::StopOnTrue}},
    {4, {"DIED", EluReturn::StopOnTrue}},
    {5, {"SPAWN", EluReturn::StopOnTrue}},
    {6, {"REACH_WP", EluReturn::StopOnTrue}},
    {7, {"AIUPDATE", EluReturn::StopOnTrue}},
    {8, {"RECEIVE_EMOTE", EluReturn::StopOnTrue}},
    {9, {"DAMAGE_TAKEN", EluReturn::StopAndValue}},
    {10, {"PRE_COMBAT", EluReturn::StopOnTrue}},
    {12, {"OWNER_ATTACKED", EluReturn::StopOnTrue}},
    {13, {"OWNER_ATTACKED_AT", EluReturn::StopOnTrue}},
    {14, {"HIT_BY_SPELL", EluReturn::StopOnTrue}},
    {15, {"SPELL_HIT_TARGET", EluReturn::StopOnTrue}},
    {19, {"JUST_SUMMONED_CREATURE", EluReturn::StopOnTrue}},
    {20, {"SUMMONED_CREATURE_DESPAWN", EluReturn::StopOnTrue}},
    {21, {"SUMMONED_CREATURE_DIED", EluReturn::StopOnTrue}},
    {22, {"SUMMONED", EluReturn::StopOnTrue}},
    {23, {"RESET", EluReturn::None}},
    {24, {"REACH_HOME", EluReturn::StopOnTrue}},
    {26, {"CORPSE_REMOVED", EluReturn::StopAndValue}},
    {27, {"MOVE_IN_LOS", EluReturn::StopOnTrue}},
    {30, {"DUMMY_EFFECT", EluReturn::None}},
    {31, {"QUEST_ACCEPT", EluReturn::StopOnTrue}},
    {34, {"QUEST_REWARD", EluReturn::StopOnTrue}},
    {35, {"DIALOG_STATUS", EluReturn::None}},
    {36, {"ADD", EluReturn::None}},
    {37, {"REMOVE", EluReturn::None}},
};

constexpr SpecRow kGameObject[] = {
    {1, {"AIUPDATE", EluReturn::None}},
    {2, {"SPAWN", EluReturn::None}},
    {3, {"DUMMY_EFFECT", EluReturn::StopOnTrue}},
    {4, {"QUEST_ACCEPT", EluReturn::StopOnTrue}},
    {5, {"QUEST_REWARD", EluReturn::StopOnTrue}},
    {6, {"DIALOG_STATUS", EluReturn::None}},
    {7, {"DESTROYED", EluReturn::None}},
    {8, {"DAMAGED", EluReturn::None}},
    {9, {"LOOT_STATE_CHANGE", EluReturn::None}},
    {10, {"GO_STATE_CHANGED", EluReturn::None}},
    {12, {"ADD", EluReturn::None}},
    {13, {"REMOVE", EluReturn::None}},
    {14, {"USE", EluReturn::StopOnTrue}},
};

constexpr SpecRow kItem[] = {
    {1, {"DUMMY_EFFECT", EluReturn::None}},
    {2, {"USE", EluReturn::CancelOnFalse}},
    {3, {"QUEST_ACCEPT", EluReturn::StopOnTrue}},
    {4, {"EXPIRE", EluReturn::StopOnTrue}},
    {5, {"REMOVE", EluReturn::StopOnTrue}},
};

constexpr SpecRow kGossip[] = {
    {1, {"HELLO", EluReturn::CancelOnFalse}},
    {2, {"SELECT", EluReturn::CancelOnFalse}},
};

// RegisterGroupEvent (docs table order: add, invite, remove, leader change, disband, create).
constexpr SpecRow kGroup[] = {
    {1, {"MEMBER_ADD", EluReturn::None}},
    {2, {"MEMBER_INVITE", EluReturn::None}},
    {3, {"MEMBER_REMOVE", EluReturn::None}},
    {4, {"LEADER_CHANGE", EluReturn::None}},
    {5, {"DISBAND", EluReturn::None}},
    {6, {"CREATE", EluReturn::None}},
};

// RegisterGuildEvent: the docs' money events can return the new amount.
constexpr SpecRow kGuild[] = {
    {1, {"ADD_MEMBER", EluReturn::None}},
    {2, {"REMOVE_MEMBER", EluReturn::None}},
    {3, {"MOTD_CHANGE", EluReturn::None}},
    {4, {"INFO_CHANGE", EluReturn::None}},
    {5, {"CREATE", EluReturn::None}},
    {6, {"DISBAND", EluReturn::None}},
    {7, {"MONEY_WITHDRAW", EluReturn::ValueOnFirst}},
    {8, {"MONEY_DEPOSIT", EluReturn::ValueOnFirst}},
    {9, {"ITEM_MOVE", EluReturn::None}},
    {10, {"EVENT", EluReturn::None}},
    {11, {"BANK_EVENT", EluReturn::None}},
};

// RegisterMapEvent / RegisterInstanceEvent share the docs table.
constexpr SpecRow kMap[] = {
    {1, {"INITIALIZE", EluReturn::None}},
    {2, {"LOAD", EluReturn::None}},
    {3, {"UPDATE", EluReturn::None}},
    {4, {"PLAYER_ENTER", EluReturn::None}},
    {5, {"CREATURE_CREATE", EluReturn::None}},
    {6, {"GAMEOBJECT_CREATE", EluReturn::None}},
    {7, {"CHECK_ENCOUNTER", EluReturn::None}},
};

// RegisterPacketEvent: read-only copies of the core's packets (E5).
constexpr SpecRow kPacket[] = {
    {5, {"PACKET_RECEIVE", EluReturn::CancelOnFalse}},
    {7, {"PACKET_SEND", EluReturn::CancelOnFalse}},
};

// RegisterBotEvent: our own API (the maintainers' bot API specification section 3). A handler's return value means nothing.
constexpr SpecRow kBot[] = {
    {1, {"BOT_ADDED", EluReturn::None}},
    {2, {"BOT_REMOVED", EluReturn::None}},
    {3, {"ORDER_GIVEN", EluReturn::None}},
    {4, {"ORDER_DONE", EluReturn::None}},
    {5, {"ORDER_FAILED", EluReturn::None}},
    {6, {"BOT_DIED", EluReturn::None}},
    {7, {"WIPE", EluReturn::None}},
    {8, {"RUN_STEP", EluReturn::None}},
    {9, {"RUN_STOPPED", EluReturn::None}},
    {10, {"RUN_LOST", EluReturn::None}},
};

// RegisterBGEvent: the docs table (START, END, CREATE, PRE_DESTROY; state MAP).
constexpr SpecRow kBattleGround[] = {
    {1, {"START", EluReturn::None}},
    {2, {"END", EluReturn::None}},
    {3, {"CREATE", EluReturn::None}},
    {4, {"PRE_DESTROY", EluReturn::None}},
};

template <size_t N>
EluEventSpec const* findSpec(SpecRow const (&rows)[N], uint32_t event)
{
    for (auto const& r : rows)
        if (r.id == event)
            return &r.spec;
    return nullptr;
}
}  // namespace

EluEventSpec const* eluEventSpec(EluKind kind, uint32_t event)
{
    switch (kind)
    {
        case EluKind::Server: return findSpec(kServer, event);
        case EluKind::Player: return findSpec(kPlayer, event);
        case EluKind::Creature: return findSpec(kCreature, event);
        case EluKind::GameObject: return findSpec(kGameObject, event);
        case EluKind::Item: return findSpec(kItem, event);
        case EluKind::PlayerGossip:
        case EluKind::CreatureGossip:
        case EluKind::GameObjectGossip:
        case EluKind::ItemGossip: return findSpec(kGossip, event);
        case EluKind::Group: return findSpec(kGroup, event);
        case EluKind::Guild: return findSpec(kGuild, event);
        case EluKind::Map:
        case EluKind::Instance: return findSpec(kMap, event);
        case EluKind::Packet: return findSpec(kPacket, event);
        case EluKind::BattleGround: return findSpec(kBattleGround, event);
        case EluKind::Bot: return findSpec(kBot, event);
    }
    return nullptr;
}
}  // namespace fl
