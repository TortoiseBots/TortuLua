// Shared helpers of the engine-side Eluna bindings (src/engine/elu). The bindings are game-free: every game action
// goes through Host::gameCall or a named Host helper, so the same code is unit-tested on FakeHost and used by the
// module. Errors and argument checks follow the published Eluna API (names, argument order, return values).
#pragma once

#include "engine/Elu.h"
#include "engine/Engine.h"
#include "engine/Handle.h"
#include "engine/Safe.h"

#include <lauxlib.h>
#include <lua.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flu
{
using fl::EventValue;

constexpr unsigned kindBit(fl::ObjKind k) { return 1u << static_cast<unsigned>(k); }
constexpr unsigned kObject = kindBit(fl::ObjKind::Player) | kindBit(fl::ObjKind::Creature) |
                             kindBit(fl::ObjKind::GameObject) | kindBit(fl::ObjKind::Item) |
                             kindBit(fl::ObjKind::Map) | kindBit(fl::ObjKind::Group) | kindBit(fl::ObjKind::Guild) |
                             kindBit(fl::ObjKind::Quest) | kindBit(fl::ObjKind::Corpse);
constexpr unsigned kWorldObject = kindBit(fl::ObjKind::Player) | kindBit(fl::ObjKind::Creature) |
                                  kindBit(fl::ObjKind::GameObject) | kindBit(fl::ObjKind::Corpse);
constexpr unsigned kUnit = kindBit(fl::ObjKind::Player) | kindBit(fl::ObjKind::Creature);
constexpr unsigned kPlayer = kindBit(fl::ObjKind::Player);
constexpr unsigned kCreature = kindBit(fl::ObjKind::Creature);
constexpr unsigned kGameObject = kindBit(fl::ObjKind::GameObject);
constexpr unsigned kItem = kindBit(fl::ObjKind::Item);
constexpr unsigned kMap = kindBit(fl::ObjKind::Map);
constexpr unsigned kGroup = kindBit(fl::ObjKind::Group);
constexpr unsigned kGuild = kindBit(fl::ObjKind::Guild);
constexpr unsigned kQuest = kindBit(fl::ObjKind::Quest);
constexpr unsigned kSpell = kindBit(fl::ObjKind::Spell);
constexpr unsigned kAura = kindBit(fl::ObjKind::Aura);
constexpr unsigned kSpellInfo = kindBit(fl::ObjKind::SpellInfo);
constexpr unsigned kPacket = kindBit(fl::ObjKind::Packet);
constexpr unsigned kCorpse = kindBit(fl::ObjKind::Corpse);
constexpr unsigned kBattleGround = kindBit(fl::ObjKind::BattleGround);
constexpr unsigned kVehicle = kindBit(fl::ObjKind::Vehicle);
constexpr unsigned kBot = kindBit(fl::ObjKind::Bot);

inline fl::Engine& eng(lua_State* L) { return *fl::Engine::from(L); }
inline fl::Host& host(lua_State* L) { return eng(L).host(); }

// The object at `idx` (default 1), checked against `kinds`. Unless `alive` is false (the five cached Object methods),
// the object must still exist and the calling thread must be allowed to resolve it, else the call raises
// "<Class> no longer exists" / "object is on another map".
fl::HandleData const& self(lua_State* L, unsigned kinds, bool alive = true, int idx = 1);

// Raises when the object behind `h` may not be touched by this thread or is gone (access check first, then lookup).
void requireLive(lua_State* L, fl::HandleData const& h);

// A second object argument: same checks, no cached-method exception.
fl::HandleData const& objArg(lua_State* L, int idx, unsigned kinds, char const* what);

// An argument that must be a handle of one of `kinds` but is not resolved (a WorldPacket, a Spell): its kind is the
// whole guarantee, the host answers the rest.
fl::HandleData const& kindArg(lua_State* L, int idx, unsigned kinds, char const* what);

// Raised when the calling thread may not read world-owned social state (a Group or Guild method) now: the world
// thread with the maps idle. A map-thread handler gets this error instead of a racy read.
void requireWorldRead(lua_State* L, char const* display);

// Host::gameCall, raising "<display>: not available" when the core does not provide the op.
void gameCall(lua_State* L, fl::HandleData const& object, char const* op, std::vector<EventValue> const& args,
              std::vector<EventValue>& out, char const* display);
// The host answered a refusal with a leading `false` (a creature without AI or template, an unknown entry or data
// guid): raises "<display>: <why>". A read whose legitimate answer happens to be false is not passed here.
void requireHostOk(lua_State* L, std::vector<EventValue> const& out, char const* display, char const* why);

// Argument readers. They raise a Lua error naming the argument on a bad type or range.
uint32_t argUInt(lua_State* L, int idx, char const* what);
// The docs' 0..255 (bag and slot) and 0..65535 (a skill id or step) argument ranges.
uint32_t argU8(lua_State* L, int idx, char const* what);
uint32_t argU16(lua_State* L, int idx, char const* what);
int32_t argInt(lua_State* L, int idx, char const* what);
int64_t argI64(lua_State* L, int idx, char const* what);
float argFloat(lua_State* L, int idx, char const* what);
double argFinite(lua_State* L, int idx, char const* what);
std::string argStr(lua_State* L, int idx, char const* what);
bool optBool(lua_State* L, int idx, bool def);
// A required boolean (a documented argument with no default).
bool argBool(lua_State* L, int idx, char const* what);
int32_t optInt(lua_State* L, int idx, int32_t def, char const* what);
float optFloat(lua_State* L, int idx, float def, char const* what);
// A guid argument: the 32-hex string of Object:GetGUID(), or an integer (the low 64 bits, high half 0: `*legacyLow`).
bool argGuid(lua_State* L, int idx, fl::Guid& out, bool* legacyLow = nullptr);
uint64_t optUInt(lua_State* L, int idx, uint64_t def);

// Pushes the host results and returns how many values were pushed.
int pushResults(lua_State* L, std::vector<EventValue> const& out);

// Pushes a handle, or nil when the operation produced none.
void pushHandleOrNil(lua_State* L, std::optional<fl::HandleData> const& h);

// A convenience for a read method: no args, one result.
int readOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args = {});
// A convenience for an action method: no results.
int actionOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args = {});
// A read whose result is a list of object handles (Group:GetMembers): pushes them as a table.
int listOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args = {});
// A read whose host answer is -1 (the object cannot be answered for: an unknown spell, a missing template), 0 or 1
// (the documented boolean): raises on -1 and pushes the boolean. A plain `false` cannot carry the third state.
int triStateBool(lua_State* L, unsigned kinds, char const* op, char const* display, char const* why,
                 std::vector<EventValue> args = {});

// The first handle a host op returned, if any.
std::optional<fl::HandleData> handleResult(std::vector<EventValue> const& out);
// A read whose result is an object (or nil).
int readHandleOp(lua_State* L, unsigned kinds, char const* op, char const* display, std::vector<EventValue> args = {});

// Registers `regs` in one class table.
void addMethods(lua_State* L, char const* className, luaL_Reg const* regs);

// E5: sends the WorldPacket argument at `idx` to `target` (Player, Group or Guild).
void sendPacketArg(lua_State* L, fl::HandleData const& target, int idx, char const* display, bool ignorePlayersInBg = false,
                   uint64_t ignore = 0);
// The same to one guild rank (Guild:SendPacketToRanked).
void sendPacketToRankArg(lua_State* L, fl::HandleData const& target, int idx, uint32_t rank, char const* display);

// Pushes the docs cancel closure of a freshly registered event handler (the id travels in its upvalue).
int pushHandlerId(lua_State* L, uint64_t id);

// The once-per-registration notes of a Register* call (core hook, Classic 1.60 stub, unknown id).
void noteEventGaps(lua_State* L, fl::EluKind kind, uint32_t event);

// Pushes a table of handles (the result of Group:GetMembers / Guild:GetMembers).
int pushHandleTable(lua_State* L, std::vector<fl::HandleData> const& list);

// The per-family registrations (called by registerEluna).
void registerGlobals(lua_State* L);
void registerObjectMethods(lua_State* L);
void registerWorldObjectMethods(lua_State* L);
void registerMapMethods(lua_State* L);
void registerUnitMethods(lua_State* L);
void registerPlayerMethods(lua_State* L);
void registerCreatureMethods(lua_State* L);
void registerGameObjectMethods(lua_State* L);
void registerGossipMethods(lua_State* L);
void registerItemMethods(lua_State* L);
void registerGroupMethods(lua_State* L);
void registerGuildMethods(lua_State* L);
void registerQuestMethods(lua_State* L);
void registerSpellMethods(lua_State* L);
void registerAuraMethods(lua_State* L);
// Long tail: the SpellInfo class (a documented spell entry; GetSpellInfo and Aura/Spell:GetSpellInfo make one).
void registerSpellInfoMethods(lua_State* L);
// E5: the corpse, battleground, packet and vehicle classes and the methods of systems Classic 1.60 lacks.
void registerCorpseMethods(lua_State* L);
void registerBattleGroundMethods(lua_State* L);
void registerPacketMethods(lua_State* L);
// Global CreatePacket(opcode, size).
int createPacket(lua_State* L);
void registerVehicleMethods(lua_State* L);
// Our bot API (B1 read + events, B2 orders): the Bot class methods, and the globals registerGlobals installs
// (elu/EluBot.cpp). The B2 order entry points answer `test-mode` when the state is not an Eluna mode state.
void registerBotMethods(lua_State* L);
int pIsBotApiAvailable(lua_State* L);
int pGetBots(lua_State* L);
int pGetBotByName(lua_State* L);
int pGetBotByGUID(lua_State* L);
int pGetBotsOfOwner(lua_State* L);
int pGetBotsOfMaster(lua_State* L);
int pGetBotsInGroup(lua_State* L);
int pRegisterBotEvent(lua_State* L);
int pClearBotEvents(lua_State* L);
int pBotsInterrupt(lua_State* L);
int pBotCommanderPlace(lua_State* L);
void registerStubMethods(lua_State* L);
// A method of a system Classic 1.60 does not have: raises "<Method>: not available on Classic 1.60".
int classicStubMethod(lua_State* L);
// The same error for a method whose registration is a named function (the name is not an upvalue then).
int classicStubMethodFor(lua_State* L, char const* name);
void addStubMethod(lua_State* L, char const* className, char const* name);
// A documented call the pinned core has no API for: raises "<Method>: not available on this core".
int coreStubMethod(lua_State* L);
void addCoreStubMethod(lua_State* L, char const* className, char const* name);
// Installs the E3 tables (ForeverData in eluna mode, Data in test mode) and RegisterCommand/WorldDBQueryAsync & co.
void registerGameGlobals(lua_State* L);
}  // namespace flu
