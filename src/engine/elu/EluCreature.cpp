// Eluna Creature methods of E1 (docs: Creature/index.html) and the most used ones of the long tail round 1.
// Everything goes through Host::gameCall; the binding only checks the object, the argument types and the documented
// ranges. The host owns every core call (src/host/EluWorld.cpp).
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

#include <cfloat>
#include <cmath>

namespace flu
{
namespace
{
using fl::EventValue;

// The core's own maximum range (MAX_VISIBILITY_DISTANCE): a radius past it reaches across maps and overflows the
// grid search, so the radius arguments of this family stop there.
constexpr double kMaxRadius = 533.333;

// The core's MapManager::IsValidMapCoord limit (MAP_HALFSIZE - 0.5): a home position past it sends the creature's
// evade path to a point no map holds.
constexpr double kMaxCoord = 17066.16;

double argCoord(lua_State* L, int idx, char const* what)
{
    double const v = argFinite(L, idx, what);
    if (std::fabs(v) > kMaxCoord)
        luaL_error(L, "%s must be within +-17066.16", what);
    return v;
}

double argRadius(lua_State* L, int idx, char const* what)
{
    double const v = argFinite(L, idx, what);
    if (v < 0.0)
        luaL_error(L, "%s must not be negative", what);
    if (v > kMaxRadius)
        luaL_error(L, "%s must be at most 533.333", what);
    return v;
}

// The core's MovementGeneratorType values a spawn can store as its default (idle, random, waypoint):
// MAX_DB_MOTION_TYPE is 3.
uint32_t defaultMovementTypeArg(lua_State* L, int idx)
{
    uint32_t const type = argUInt(L, idx, "type");
    if (type >= 3)
        luaL_error(L, "type must be an integer 0..2 (idle, random or waypoint)");
    return type;
}

// The core's DeathState enum: ALIVE 0, JUST_DIED 1, CORPSE 2, DEAD 3, JUST_RESPAWNED 4.
uint32_t deathStateArg(lua_State* L, int idx)
{
    uint32_t const state = argUInt(L, idx, "deathState");
    if (state >= 5)
        luaL_error(L, "deathState must be an integer 0..4");
    return state;
}

// A SpellSchoolMask: the seven school bits 0x01..0x40.
uint32_t schoolMaskArg(lua_State* L, int idx)
{
    uint64_t const mask = optUInt(L, idx, 0);
    if (mask > 0x7Fu)
        luaL_error(L, "schoolMask has bits that are not SpellSchoolMask values");
    return static_cast<uint32_t>(mask);
}

int cGetScriptName(lua_State* L) { return readOp(L, kCreature, "creature.getScriptName", "GetScriptName"); }
int cIsElite(lua_State* L) { return readOp(L, kCreature, "creature.isElite", "IsElite"); }
int cIsBoss(lua_State* L) { return readOp(L, kCreature, "creature.isBoss", "IsBoss"); }
int cIsWorldBoss(lua_State* L) { return readOp(L, kCreature, "creature.isWorldBoss", "IsWorldBoss"); }
int cGetHomePosition(lua_State* L) { return readOp(L, kCreature, "creature.getHomePosition", "GetHomePosition"); }
int cRespawn(lua_State* L) { return actionOp(L, kCreature, "creature.respawn", "Respawn"); }
int cDespawnOrUnsummon(lua_State* L)
{
    return actionOp(L, kCreature, "creature.despawn", "DespawnOrUnsummon",
                    {EventValue(static_cast<int64_t>(optUInt(L, 2, 0)))});
}
int cSetReactState(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setReactState", "SetReactState",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "state")))});
}
int cGetAITarget(lua_State* L)
{
    return readHandleOp(L, kCreature, "creature.getAITarget", "GetAITarget",
                        {EventValue(static_cast<int64_t>(optUInt(L, 2, 0))), EventValue(optBool(L, 3, false)),
                         EventValue(static_cast<int64_t>(optUInt(L, 4, 0))),
                         EventValue(static_cast<double>(luaL_optnumber(L, 5, 0.0))),
                         EventValue(argI64(L, 6, "aura"))});
}
int cGetSpawnId(lua_State* L) { return readOp(L, kCreature, "creature.getSpawnId", "GetSpawnId"); }
int cSetRespawnDelay(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setRespawnDelay", "SetRespawnDelay",
                    {EventValue(static_cast<int64_t>(optUInt(L, 2, 0)))});
}

// ---- long tail round 1: AI and combat control ----

// An action whose op the host can refuse (a creature without AI, an unknown entry, an unloaded waypoint path): the
// host answers a leading `false` and the call raises "<display>: <why>".
int checkedAction(lua_State* L, unsigned kinds, char const* op, char const* display, char const* why,
                  std::vector<EventValue> args)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    requireHostOk(L, out, display, why);
    return 0;
}

// A read of an object (or nil) whose op the host can refuse the same way.
int checkedHandleRead(lua_State* L, unsigned kinds, char const* op, char const* display, char const* why)
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, {}, out, display);
    requireHostOk(L, out, display, why);
    pushHandleOrNil(L, handleResult(out));
    return 1;
}

// A read whose op the host can refuse the same way.
int checkedRead(lua_State* L, unsigned kinds, char const* op, char const* display, char const* why,
                std::vector<EventValue> args = {})
{
    fl::HandleData const& h = self(L, kinds);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    requireHostOk(L, out, display, why);
    return pushResults(L, out);
}

int cAttackStart(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return checkedAction(L, kCreature, "creature.attackStart", "AttackStart",
                         "the creature has no AI, or the target is not on its map", {EventValue(target)});
}
// The core's own victim selection sends a creature without a target into evade mode through its AI, so a creature
// without AI is refused; the selected victim (or nil) is the answer.
int cSelectVictim(lua_State* L)
{
    return checkedHandleRead(L, kCreature, "creature.selectVictim", "SelectVictim", "the creature has no AI");
}
int cCallAssistance(lua_State* L) { return actionOp(L, kCreature, "creature.callAssistance", "CallAssistance"); }
int cCallForHelp(lua_State* L)
{
    double const radius = argRadius(L, 2, "radius");
    return actionOp(L, kCreature, "creature.callForHelp", "CallForHelp", {EventValue(radius)});
}
int cFleeToGetAssistance(lua_State* L)
{
    return checkedAction(L, kCreature, "creature.fleeToGetAssistance", "FleeToGetAssistance",
                         "the creature is a pet or is controlled by a player", {});
}
int cSetNoCallAssistance(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setNoCallAssistance", "SetNoCallAssistance",
                    {EventValue(optBool(L, 2, true))});
}
int cSetNoSearchAssistance(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setNoSearchAssistance", "SetNoSearchAssistance",
                    {EventValue(optBool(L, 2, true))});
}
int cHasSearchedAssistance(lua_State* L)
{
    return readOp(L, kCreature, "creature.hasSearchedAssistance", "HasSearchedAssistance");
}
int cIsInEvadeMode(lua_State* L) { return readOp(L, kCreature, "creature.isInEvadeMode", "IsInEvadeMode"); }
int cSetInCombatWithZone(lua_State* L)
{
    return checkedAction(L, kCreature, "creature.setInCombatWithZone", "SetInCombatWithZone",
                         "the creature is a pet or is controlled by a player", {});
}
int cGetAggroRange(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return readOp(L, kCreature, "creature.getAggroRange", "GetAggroRange", {EventValue(target)});
}
int cGetAttackDistance(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return readOp(L, kCreature, "creature.getAttackDistance", "GetAttackDistance", {EventValue(target)});
}
int cCanAssistTo(lua_State* L)
{
    fl::HandleData const& friend_ = objArg(L, 2, kUnit, "friend");
    fl::HandleData const& enemy = objArg(L, 3, kUnit, "enemy");
    return readOp(L, kCreature, "creature.canAssistTo", "CanAssistTo",
                  {EventValue(friend_), EventValue(enemy), EventValue(optBool(L, 4, true))});
}
int cCanStartAttack(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return readOp(L, kCreature, "creature.canStartAttack", "CanStartAttack",
                  {EventValue(target), EventValue(optBool(L, 3, true))});
}
int cIsTappedBy(lua_State* L)
{
    fl::HandleData const& player = objArg(L, 2, kPlayer, "player");
    return readOp(L, kCreature, "creature.isTappedBy", "IsTappedBy", {EventValue(player)});
}
// The core's Unit::isTargetableForAttack(checkFakeDeath): alive, not flagged unattackable and not feigning death.
int cIsTargetableForAttack(lua_State* L)
{
    return readOp(L, kCreature, "creature.isTargetableForAttack", "IsTargetableForAttack",
                  {EventValue(optBool(L, 2, true))});
}
int cHasLootRecipient(lua_State* L) { return readOp(L, kCreature, "creature.hasLootRecipient", "HasLootRecipient"); }
int cGetLootRecipient(lua_State* L)
{
    return readHandleOp(L, kCreature, "creature.getLootRecipient", "GetLootRecipient");
}
int cGetLootRecipientGroup(lua_State* L)
{
    return readHandleOp(L, kCreature, "creature.getLootRecipientGroup", "GetLootRecipientGroup");
}

// ---- threat ----
int cAddThreat(lua_State* L)
{
    fl::HandleData const& victim = objArg(L, 2, kUnit, "victim");
    double const threat = argFinite(L, 3, "threat");
    uint32_t const schoolMask = schoolMaskArg(L, 4);
    uint32_t const spell = lua_isnoneornil(L, 5) ? 0u : argUInt(L, 5, "spell");
    return checkedAction(L, kCreature, "creature.addThreat", "AddThreat",
                         "the threat needs the spell entry this core takes (an unknown spell id, or a schoolMask "
                         "without one)",
                         {EventValue(victim), EventValue(threat), EventValue(static_cast<int64_t>(schoolMask)),
                          EventValue(static_cast<int64_t>(spell))});
}
int cGetThreat(lua_State* L)
{
    fl::HandleData const& victim = objArg(L, 2, kUnit, "victim");
    return readOp(L, kCreature, "creature.getThreat", "GetThreat", {EventValue(victim)});
}
int cClearThreat(lua_State* L)
{
    fl::HandleData const& victim = objArg(L, 2, kUnit, "victim");
    return actionOp(L, kCreature, "creature.clearThreat", "ClearThreat", {EventValue(victim)});
}
int cClearThreatList(lua_State* L) { return actionOp(L, kCreature, "creature.clearThreatList", "ClearThreatList"); }
int cResetAllThreat(lua_State* L) { return actionOp(L, kCreature, "creature.resetAllThreat", "ResetAllThreat"); }
int cFixateTarget(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return actionOp(L, kCreature, "creature.fixateTarget", "FixateTarget", {EventValue(target)});
}
int cClearFixate(lua_State* L) { return actionOp(L, kCreature, "creature.clearFixate", "ClearFixate"); }
int cGetAITargets(lua_State* L) { return listOp(L, kCreature, "creature.getAITargets", "GetAITargets"); }
int cGetAITargetsCount(lua_State* L)
{
    return readOp(L, kCreature, "creature.getAITargetsCount", "GetAITargetsCount");
}

// ---- state ----
int cIsCivilian(lua_State* L) { return readOp(L, kCreature, "creature.isCivilian", "IsCivilian"); }
int cIsGuard(lua_State* L) { return readOp(L, kCreature, "creature.isGuard", "IsGuard"); }
int cIsRacialLeader(lua_State* L) { return readOp(L, kCreature, "creature.isRacialLeader", "IsRacialLeader"); }
int cIsTrigger(lua_State* L) { return readOp(L, kCreature, "creature.isTrigger", "IsTrigger"); }
int cIsDungeonBoss(lua_State* L) { return readOp(L, kCreature, "creature.isDungeonBoss", "IsDungeonBoss"); }
int cIsRegeneratingHealth(lua_State* L)
{
    return readOp(L, kCreature, "creature.isRegeneratingHealth", "IsRegeneratingHealth");
}
int cSetRegeneratingHealth(lua_State* L)
{
    // the core also reads the template's own regeneration setting, which no script can change: enabling it on a
    // template that forbids it is refused rather than reported as done
    return checkedAction(L, kCreature, "creature.setRegeneratingHealth", "SetRegeneratingHealth",
                         "the creature's template does not let it regenerate health", {EventValue(optBool(L, 2, true))});
}
int cCanFly(lua_State* L) { return readOp(L, kCreature, "creature.canFly", "CanFly"); }
int cCanSwim(lua_State* L) { return readOp(L, kCreature, "creature.canSwim", "CanSwim"); }
int cSetHover(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setHover", "SetHover", {EventValue(optBool(L, 2, true))});
}
int cSetDisableGravity(lua_State* L)
{
    // docs: Creature:SetDisableGravity( disable ), no default
    return actionOp(L, kCreature, "creature.setDisableGravity", "SetDisableGravity",
                    {EventValue(argBool(L, 2, "disable"))});
}
int cIsReputationGainDisabled(lua_State* L)
{
    return readOp(L, kCreature, "creature.isReputationGainDisabled", "IsReputationGainDisabled");
}
int cSetDisableReputationGain(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setDisableReputationGain", "SetDisableReputationGain",
                    {EventValue(optBool(L, 2, true))});
}
int cGetShieldBlockValue(lua_State* L)
{
    return readOp(L, kCreature, "creature.getShieldBlockValue", "GetShieldBlockValue");
}
int cSetWalk(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setWalk", "SetWalk", {EventValue(optBool(L, 2, true))});
}

// ---- spawn and movement ----
int cGetWaypointPath(lua_State* L) { return readOp(L, kCreature, "creature.getWaypointPath", "GetWaypointPath"); }
int cGetCurrentWaypointId(lua_State* L)
{
    return readOp(L, kCreature, "creature.getCurrentWaypointId", "GetCurrentWaypointId");
}
int cMoveWaypoint(lua_State* L)
{
    return checkedAction(L, kCreature, "creature.moveWaypoint", "MoveWaypoint",
                         "the creature's waypoint path is not loaded", {});
}
int cGetDefaultMovementType(lua_State* L)
{
    return readOp(L, kCreature, "creature.getDefaultMovementType", "GetDefaultMovementType");
}
int cSetDefaultMovementType(lua_State* L)
{
    uint32_t const type = defaultMovementTypeArg(L, 2);
    return checkedAction(L, kCreature, "creature.setDefaultMovementType", "SetDefaultMovementType",
                         "the creature is a pet or is controlled by a player",
                         {EventValue(static_cast<int64_t>(type))});
}
int cGetWanderRadius(lua_State* L)
{
    return readOp(L, kCreature, "creature.getWanderRadius", "GetWanderRadius");
}
int cSetWanderRadius(lua_State* L)
{
    double const distance = argRadius(L, 2, "distance");
    return actionOp(L, kCreature, "creature.setWanderRadius", "SetWanderRadius", {EventValue(distance)});
}
int cSetHomePosition(lua_State* L)
{
    double const x = argCoord(L, 2, "x");
    double const y = argCoord(L, 3, "y");
    double const z = argCoord(L, 4, "z");
    double const o = argFinite(L, 5, "o");
    return checkedAction(L, kCreature, "creature.setHomePosition", "SetHomePosition",
                         "the position is not a valid map coordinate",
                         {EventValue(x), EventValue(y), EventValue(z), EventValue(o)});
}
int cGetRespawnDelay(lua_State* L) { return readOp(L, kCreature, "creature.getRespawnDelay", "GetRespawnDelay"); }
int cGetCorpseDelay(lua_State* L) { return readOp(L, kCreature, "creature.getCorpseDelay", "GetCorpseDelay"); }
int cRemoveCorpse(lua_State* L) { return actionOp(L, kCreature, "creature.removeCorpse", "RemoveCorpse"); }
// The core removes objects through its deferred removal list (WorldObject::AddObjectToRemoveList); the world object data
// of the handle is dropped with it, like GameObject:RemoveFromWorld.
int cRemoveFromWorld(lua_State* L)
{
    fl::HandleData const& h = self(L, kCreature);
    std::vector<EventValue> out;
    gameCall(L, h, "creature.removeFromWorld", {}, out, "RemoveFromWorld");
    eng(L).removeWorldObjectData(h);
    return 0;
}
int cSetDeathState(lua_State* L)
{
    uint32_t const state = deathStateArg(L, 2);
    return checkedAction(L, kCreature, "creature.setDeathState", "SetDeathState",
                         "deathState 0 (ALIVE) would leave a dead creature alive at 0 health (use 4, JUST_RESPAWNED), and "
                         "2/3 (CORPSE, DEAD) on a living creature would leave it at full health with no corpse "
                         "timer (use 1, JUST_DIED)",
                         {EventValue(static_cast<int64_t>(state))});
}
int cSetEquipmentSlots(lua_State* L)
{
    uint32_t const mainHand = argUInt(L, 2, "main_hand");
    uint32_t const offHand = argUInt(L, 3, "off_hand");
    uint32_t const ranged = argUInt(L, 4, "ranged");
    return checkedAction(L, kCreature, "creature.setEquipmentSlots", "SetEquipmentSlots",
                         "an item entry is not one the server knows",
                         {EventValue(static_cast<int64_t>(mainHand)), EventValue(static_cast<int64_t>(offHand)),
                          EventValue(static_cast<int64_t>(ranged))});
}

// ---- identity and gear ----
int cGetCreatureFamily(lua_State* L)
{
    return checkedRead(L, kCreature, "creature.getCreatureFamily", "GetCreatureFamily",
                       "the creature's template is not on the server");
}
int cGetNPCFlags(lua_State* L) { return readOp(L, kCreature, "creature.getNPCFlags", "GetNPCFlags"); }
int cSetNPCFlags(lua_State* L)
{
    // NPCFlags is a 32-bit mask in this core
    return actionOp(L, kCreature, "creature.setNPCFlags", "SetNPCFlags",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "flags")))});
}
int cGetExtraFlags(lua_State* L)
{
    return checkedRead(L, kCreature, "creature.getExtraFlags", "GetExtraFlags",
                       "the creature's template is not on the server");
}
int cGetRank(lua_State* L)
{
    return checkedRead(L, kCreature, "creature.getRank", "GetRank", "the creature's template is not on the server");
}
int cGetDBTableGUIDLow(lua_State* L)
{
    return readOp(L, kCreature, "creature.getDBTableGUIDLow", "GetDBTableGUIDLow");
}
int cGetAIName(lua_State* L) { return readOp(L, kCreature, "creature.getAIName", "GetAIName"); }
int cGetScriptId(lua_State* L) { return readOp(L, kCreature, "creature.getScriptId", "GetScriptId"); }
int cUpdateEntry(lua_State* L)
{
    uint32_t const entry = argUInt(L, 2, "entry");
    uint64_t const dataGUIDLow = optUInt(L, 3, 0);
    return checkedAction(L, kCreature, "creature.updateEntry", "UpdateEntry",
                         "the entry (or dataGUIDLow) is not one the server's creature tables hold, has no usable model, or the "
                         "creature is a pet",
                         {EventValue(static_cast<int64_t>(entry)), EventValue(static_cast<int64_t>(dataGUIDLow))});
}

// ---- spells and cooldowns ----
int cHasSpell(lua_State* L)
{
    return readOp(L, kCreature, "creature.hasSpell", "HasSpell",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "spell")))});
}
int cHasSpellCooldown(lua_State* L)
{
    // the core's cooldown lookup by id asserts on an unknown spell: the host answers 1 / 0 / -1
    return triStateBool(L, kCreature, "creature.hasSpellCooldown", "HasSpellCooldown",
                        "the spell is not one the server knows",
                        {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))});
}
int cHasCategoryCooldown(lua_State* L)
{
    return triStateBool(L, kCreature, "creature.hasCategoryCooldown", "HasCategoryCooldown",
                        "the spell is not one the server knows",
                        {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))});
}
int cGetCreatureSpellCooldownDelay(lua_State* L)
{
    return checkedRead(L, kCreature, "creature.getSpellCooldownDelay", "GetCreatureSpellCooldownDelay",
                       "the spell is not one the server knows",
                       {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellID")))});
}

// ---- loot mode ----
int cGetLootMode(lua_State* L) { return readOp(L, kCreature, "creature.getLootMode", "GetLootMode"); }
int cHasLootMode(lua_State* L)
{
    return readOp(L, kCreature, "creature.hasLootMode", "HasLootMode",
                  {EventValue(static_cast<int64_t>(argU16(L, 2, "lootMode")))});
}
int cSetLootMode(lua_State* L)
{
    return actionOp(L, kCreature, "creature.setLootMode", "SetLootMode",
                    {EventValue(static_cast<int64_t>(argU16(L, 2, "lootMode")))});
}
int cAddLootMode(lua_State* L)
{
    return actionOp(L, kCreature, "creature.addLootMode", "AddLootMode",
                    {EventValue(static_cast<int64_t>(argU16(L, 2, "lootMode")))});
}
int cRemoveLootMode(lua_State* L)
{
    return actionOp(L, kCreature, "creature.removeLootMode", "RemoveLootMode",
                    {EventValue(static_cast<int64_t>(argU16(L, 2, "lootMode")))});
}
int cResetLootMode(lua_State* L) { return actionOp(L, kCreature, "creature.resetLootMode", "ResetLootMode"); }

// ---- quests ----
int cHasQuest(lua_State* L)
{
    return readOp(L, kCreature, "creature.hasQuest", "HasQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
// The core keeps two quest relations: hasQuest (the creature starts the quest) and hasInvolvedQuest (the creature ends
// it), the second being the documented CanCompleteQuest.
int cCanCompleteQuest(lua_State* L)
{
    return readOp(L, kCreature, "creature.canCompleteQuest", "CanCompleteQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}

// The core writes the spawn through a synchronous WorldDatabase transaction, which must not run on a game thread.
int cSaveToDB(lua_State* L)
{
    return luaL_error(L, "SaveToDB: not available on this server (the write blocks the game thread)");
}
}  // namespace

void registerCreatureMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetScriptName", fl::safe<cGetScriptName>},
        {"IsElite", fl::safe<cIsElite>},
        {"IsBoss", fl::safe<cIsBoss>},
        {"IsWorldBoss", fl::safe<cIsWorldBoss>},
        {"GetHomePosition", fl::safe<cGetHomePosition>},
        {"Respawn", fl::safe<cRespawn>},
        {"DespawnOrUnsummon", fl::safe<cDespawnOrUnsummon>},
        {"SetReactState", fl::safe<cSetReactState>},
        {"GetAITarget", fl::safe<cGetAITarget>},
        {"GetSpawnId", fl::safe<cGetSpawnId>},
        {"SetRespawnDelay", fl::safe<cSetRespawnDelay>},
        // long tail round 1
        {"AttackStart", fl::safe<cAttackStart>},          {"SelectVictim", fl::safe<cSelectVictim>},
        {"CallAssistance", fl::safe<cCallAssistance>},    {"CallForHelp", fl::safe<cCallForHelp>},
        {"FleeToGetAssistance", fl::safe<cFleeToGetAssistance>},
        {"SetNoCallAssistance", fl::safe<cSetNoCallAssistance>},
        {"SetNoSearchAssistance", fl::safe<cSetNoSearchAssistance>},
        {"HasSearchedAssistance", fl::safe<cHasSearchedAssistance>},
        {"IsInEvadeMode", fl::safe<cIsInEvadeMode>},      {"SetInCombatWithZone", fl::safe<cSetInCombatWithZone>},
        {"GetAggroRange", fl::safe<cGetAggroRange>},      {"GetAttackDistance", fl::safe<cGetAttackDistance>},
        {"CanAssistTo", fl::safe<cCanAssistTo>},          {"CanStartAttack", fl::safe<cCanStartAttack>},
        {"IsTargetableForAttack", fl::safe<cIsTargetableForAttack>},
        {"IsTappedBy", fl::safe<cIsTappedBy>},            {"HasLootRecipient", fl::safe<cHasLootRecipient>},
        {"GetLootRecipient", fl::safe<cGetLootRecipient>},
        {"GetLootRecipientGroup", fl::safe<cGetLootRecipientGroup>},
        {"AddThreat", fl::safe<cAddThreat>},              {"GetThreat", fl::safe<cGetThreat>},
        {"ClearThreat", fl::safe<cClearThreat>},          {"ClearThreatList", fl::safe<cClearThreatList>},
        {"ResetAllThreat", fl::safe<cResetAllThreat>},    {"FixateTarget", fl::safe<cFixateTarget>},
        {"ClearFixate", fl::safe<cClearFixate>},          {"GetAITargets", fl::safe<cGetAITargets>},
        {"GetAITargetsCount", fl::safe<cGetAITargetsCount>},
        {"IsCivilian", fl::safe<cIsCivilian>},            {"IsGuard", fl::safe<cIsGuard>},
        {"IsRacialLeader", fl::safe<cIsRacialLeader>},    {"IsTrigger", fl::safe<cIsTrigger>},
        {"IsDungeonBoss", fl::safe<cIsDungeonBoss>},
        {"IsRegeneratingHealth", fl::safe<cIsRegeneratingHealth>},
        {"SetRegeneratingHealth", fl::safe<cSetRegeneratingHealth>},
        {"CanFly", fl::safe<cCanFly>},                    {"CanSwim", fl::safe<cCanSwim>},
        {"SetHover", fl::safe<cSetHover>},                {"SetDisableGravity", fl::safe<cSetDisableGravity>},
        {"IsReputationGainDisabled", fl::safe<cIsReputationGainDisabled>},
        {"SetDisableReputationGain", fl::safe<cSetDisableReputationGain>},
        {"GetShieldBlockValue", fl::safe<cGetShieldBlockValue>}, {"SetWalk", fl::safe<cSetWalk>},
        {"GetWaypointPath", fl::safe<cGetWaypointPath>},
        {"GetCurrentWaypointId", fl::safe<cGetCurrentWaypointId>},
        {"MoveWaypoint", fl::safe<cMoveWaypoint>},
        {"GetDefaultMovementType", fl::safe<cGetDefaultMovementType>},
        {"SetDefaultMovementType", fl::safe<cSetDefaultMovementType>},
        {"GetWanderRadius", fl::safe<cGetWanderRadius>},  {"SetWanderRadius", fl::safe<cSetWanderRadius>},
        {"SetHomePosition", fl::safe<cSetHomePosition>},  {"GetRespawnDelay", fl::safe<cGetRespawnDelay>},
        {"GetCorpseDelay", fl::safe<cGetCorpseDelay>},    {"RemoveCorpse", fl::safe<cRemoveCorpse>},
        {"RemoveFromWorld", fl::safe<cRemoveFromWorld>},
        {"SetDeathState", fl::safe<cSetDeathState>},      {"SetEquipmentSlots", fl::safe<cSetEquipmentSlots>},
        {"GetCreatureFamily", fl::safe<cGetCreatureFamily>}, {"GetNPCFlags", fl::safe<cGetNPCFlags>},
        {"SetNPCFlags", fl::safe<cSetNPCFlags>},          {"GetExtraFlags", fl::safe<cGetExtraFlags>},
        {"GetRank", fl::safe<cGetRank>},                  {"GetDBTableGUIDLow", fl::safe<cGetDBTableGUIDLow>},
        {"GetAIName", fl::safe<cGetAIName>},              {"GetScriptId", fl::safe<cGetScriptId>},
        {"UpdateEntry", fl::safe<cUpdateEntry>},          {"HasSpell", fl::safe<cHasSpell>},
        {"HasSpellCooldown", fl::safe<cHasSpellCooldown>},
        {"HasCategoryCooldown", fl::safe<cHasCategoryCooldown>},
        {"GetCreatureSpellCooldownDelay", fl::safe<cGetCreatureSpellCooldownDelay>},
        {"GetLootMode", fl::safe<cGetLootMode>},          {"HasLootMode", fl::safe<cHasLootMode>},
        {"SetLootMode", fl::safe<cSetLootMode>},          {"AddLootMode", fl::safe<cAddLootMode>},
        {"RemoveLootMode", fl::safe<cRemoveLootMode>},    {"ResetLootMode", fl::safe<cResetLootMode>},
        {"HasQuest", fl::safe<cHasQuest>},                {"CanCompleteQuest", fl::safe<cCanCompleteQuest>},
        {"SaveToDB", fl::safe<cSaveToDB>},
        {nullptr, nullptr}};
    addMethods(L, "Creature", regs);
}
}  // namespace flu
