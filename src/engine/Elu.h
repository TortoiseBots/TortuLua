// Eluna mode (E0/E1) of the engine: class metatables over the handle data, the Register* event registry, the timed
// events, the error ring and the ElunaQuery userdata. The API names and argument orders come from Eluna's published
// documentation (https://elunaluaengine.github.io/, docs/ELUNA-CLEANROOM.md); nothing here is copied from its source.
#pragma once

#include "engine/Types.h"
#include "engine/Value.h"

#include <lua.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fl
{
// The Register* families: the nine of E1, plus the group, guild, map, instance and packet families of E2.
enum class EluKind : uint8_t
{
    Player,
    Creature,
    GameObject,
    Item,
    Server,
    PlayerGossip,
    CreatureGossip,
    GameObjectGossip,
    ItemGossip,
    Group,
    Guild,
    Map,
    Instance,
    Packet,
    BattleGround,  // E5: RegisterBGEvent (docs: state MAP, parameters event, bg, bgId, instanceId[, winner])
    Bot            // our bot API: RegisterBotEvent (world state only, ids 1-9)
};

char const* eluKindName(EluKind kind);

// What the handlers of one synchronous Eluna event decided.
struct EventResult
{
    bool handled = false;    // a handler returned true ("stop normal action")
    bool cancel = false;     // a handler returned false where false cancels the action
    bool hasValue = false;   // a handler returned a new value (XP, money, damage, delay, skill ...)
    int64_t value = 0;
    bool hasText = false;    // a handler returned a new chat text
    std::string text;
    uint32_t calls = 0;      // handlers run
};

// One entry of the error ring: the last 50 distinct script errors.
struct ScriptError
{
    std::string script;
    int line = 0;
    std::string message;
    uint64_t count = 0;
    uint64_t firstMs = 0;
    uint64_t lastMs = 0;
};

// How a handler's return values are folded into the hook result.
enum class EluReturn : uint8_t
{
    None,          // ignored
    StopOnTrue,    // true = handled
    CancelOnFalse, // false = cancel
    ValueOnFirst,  // a number = new value
    StopAndValue,  // true = handled, second number = new value
    TextOnSecond,  // false = cancel, second string = new text
};

// What a Register*Event family knows about one event id: the docs name and the return convention.
struct EluEventSpec
{
    char const* name;
    EluReturn ret;
};

// Null when this core does not define that event id.
EluEventSpec const* eluEventSpec(EluKind kind, uint32_t event);

// Eluna class name for a handle kind ("Player", "Creature", ...), or null.
char const* eluClassName(ObjKind kind);
// Pushes the Eluna metatable name for the kind onto the stack; false when the kind has no class.
bool eluPushClassMetatable(lua_State* L, ObjKind kind);

// Installs the class metatables (Object > WorldObject > Unit > Player|Creature, WorldObject > GameObject,
// Object > Item, plus Map/Group/Guild/Quest) and the Eluna globals. Called instead of registerApi() in eluna mode.
void registerEluna(lua_State* L);
void registerElunaClasses(lua_State* L);

// Installs the ForeverData table (E3) under `name`: "ForeverData" in eluna mode, the toolkit's "Data" in test mode.
void registerForeverData(lua_State* L, char const* name);

// Pushes a handle with the Eluna metatable of its kind.
void eluPushHandle(lua_State* L, HandleData const& data);

// The profile row of one registered handler (E3): calls, time spent and where it was registered.
struct HandlerProfile
{
    uint64_t id = 0;
    EluKind kind = EluKind::Player;
    uint32_t event = 0;
    uint32_t entry = 0;
    std::string where = {};
    uint64_t calls = 0;
    uint64_t totalMs = 0;
    uint64_t maxMs = 0;
    bool disabled = false;
};

// Adds a game-side method to a class table (a binder may extend the classes after registerElunaClasses).
void eluAddMethod(lua_State* L, char const* className, char const* name, lua_CFunction fn);

// Pushes an ElunaQuery userdata (the result of WorldDBQuery/CharDBQuery/AuthDBQuery).
void eluPushQuery(lua_State* L, DbRows rows);
// The ElunaQuery methods and its metatable.
void registerQueryMethods(lua_State* L);
}  // namespace fl
