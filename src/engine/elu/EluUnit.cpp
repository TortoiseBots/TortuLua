// Eluna Unit methods of E1 (docs: Unit/index.html). Everything goes through Host::gameCall; the binding only checks
// the object, the argument types and the documented optional values.
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"

#include <lauxlib.h>
#include <lua.h>

#include <cfloat>
#include <cmath>

namespace flu
{
namespace
{
using fl::EventValue;

// The core's UnitState bits that are defined (Unit.h UNIT_STATE_ALL_STATE_SUPPORTED): 0x2000, 0x80000 and 0x40000000 and up are
// not states, so AddUnitState/ClearUnitState refuse them instead of writing meaningless bits into the unit.
constexpr uint32_t kUnitStateSupported = 0x3FF7DFFFu;

uint32_t unitStateArg(lua_State* L, int idx)
{
    uint32_t const state = argUInt(L, idx, "state");
    if ((state & ~kUnitStateSupported) != 0)
        luaL_error(L, "state has bits that are not UnitState values");
    return state;
}

// The core's LocaleConstant: 0..8, then 10 and 11 (9 is LOCALE_none); anything else indexes past the name table.
int64_t localeArg(lua_State* L, int idx)
{
    int32_t const locale = optInt(L, idx, 0, "locale");
    if (locale < 0 || locale > 11 || locale == 9)
        luaL_error(L, "locale must be a LocaleConstant (0..8, 10 or 11)");
    return locale;
}

// The nine UnitMoveType values (walk, run, run back, swim, swim back, turn rate, flight, flight back, pitch rate).
uint32_t moveTypeArg(lua_State* L, int idx)
{
    uint32_t const type = argUInt(L, idx, "type");
    if (type >= 9)
        luaL_error(L, "type must be an integer 0..8");
    return type;
}

int uGetLevel(lua_State* L) { return readOp(L, kUnit, "unit.getLevel", "GetLevel"); }
int uSetLevel(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setLevel", "SetLevel",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "level")))});
}
int uGetHealth(lua_State* L) { return readOp(L, kUnit, "unit.getHealth", "GetHealth"); }
int uSetHealth(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setHealth", "SetHealth",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "health")))});
}
int uGetMaxHealth(lua_State* L) { return readOp(L, kUnit, "unit.getMaxHealth", "GetMaxHealth"); }
int uGetHealthPct(lua_State* L) { return readOp(L, kUnit, "unit.getHealthPct", "GetHealthPct"); }
int uGetPower(lua_State* L)
{
    // docs: Unit:GetPower([type]), type -1 (the default) = the unit's current power type
    return readOp(L, kUnit, "unit.getPower", "GetPower", {EventValue(int64_t(optInt(L, 2, -1, "type")))});
}
int uSetPower(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setPower", "SetPower",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "amount"))), EventValue(int64_t(optInt(L, 3, -1, "type")))});
}
int uIsAlive(lua_State* L) { return readOp(L, kUnit, "unit.isAlive", "IsAlive"); }
int uIsDead(lua_State* L) { return readOp(L, kUnit, "unit.isDead", "IsDead"); }
int uIsInCombat(lua_State* L) { return readOp(L, kUnit, "unit.isInCombat", "IsInCombat"); }
int uGetVictim(lua_State* L) { return readHandleOp(L, kUnit, "unit.getVictim", "GetVictim"); }
int uAttack(lua_State* L)
{
    fl::HandleData const& who = objArg(L, 2, kUnit, "who");
    return actionOp(L, kUnit, "unit.attack", "Attack", {EventValue(who), EventValue(optBool(L, 3, false))});
}
int uAttackStop(lua_State* L) { return readOp(L, kUnit, "unit.attackStop", "AttackStop"); }
int uCastSpell(lua_State* L)
{
    std::vector<EventValue> args;
    if (lua_isnoneornil(L, 2))
        args.emplace_back();
    else
        args.emplace_back(objArg(L, 2, kUnit, "target"));
    args.emplace_back(static_cast<int64_t>(argUInt(L, 3, "spell")));
    args.emplace_back(optBool(L, 4, false));
    return actionOp(L, kUnit, "unit.castSpell", "CastSpell", args);
}
int uCastCustomSpell(lua_State* L)
{
    std::vector<EventValue> args;
    if (lua_isnoneornil(L, 2))
        args.emplace_back();
    else
        args.emplace_back(objArg(L, 2, kUnit, "target"));
    args.emplace_back(static_cast<int64_t>(argUInt(L, 3, "spell")));
    args.emplace_back(optBool(L, 4, false));
    for (int i = 5; i <= 7; ++i)
        args.emplace_back(lua_isnoneornil(L, i) ? EventValue() : EventValue(int64_t(argInt(L, i, "basepoint"))));
    if (lua_isnoneornil(L, 8))
        args.emplace_back();
    else
        args.emplace_back(objArg(L, 8, kItem, "castItem"));
    args.emplace_back(lua_isnoneornil(L, 9) ? EventValue(int64_t(0)) : EventValue(argI64(L, 9, "originalCaster")));
    return actionOp(L, kUnit, "unit.castCustomSpell", "CastCustomSpell", args);
}
int uAddAura(lua_State* L)
{
    uint32_t spell = argUInt(L, 2, "spell");
    std::vector<EventValue> args{EventValue(static_cast<int64_t>(spell))};
    if (lua_isnoneornil(L, 3))
        args.emplace_back();
    else
        args.emplace_back(objArg(L, 3, kUnit, "target"));
    return readHandleOp(L, kUnit, "unit.addAura", "AddAura", args);
}
int uRemoveAura(lua_State* L)
{
    return actionOp(L, kUnit, "unit.removeAura", "RemoveAura",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "spell")))});
}
int uHasAura(lua_State* L)
{
    return readOp(L, kUnit, "unit.hasAura", "HasAura",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "spell")))});
}
int uKill(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return actionOp(L, kUnit, "unit.kill", "Kill", {EventValue(target), EventValue(optBool(L, 3, true))});
}
int uDealDamage(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    std::vector<EventValue> args{EventValue(target), EventValue(static_cast<int64_t>(optUInt(L, 3, 0))),
                                 EventValue(optBool(L, 4, true)), EventValue(int64_t(optInt(L, 5, 7, "school"))),
                                 EventValue(static_cast<int64_t>(optUInt(L, 6, 0)))};
    return actionOp(L, kUnit, "unit.dealDamage", "DealDamage", args);
}
int uMoveTo(lua_State* L)
{
    return actionOp(L, kUnit, "unit.moveTo", "MoveTo",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "id"))), EventValue(argFloat(L, 3, "x")),
                     EventValue(argFloat(L, 4, "y")), EventValue(argFloat(L, 5, "z")),
                     EventValue(optBool(L, 6, true))});
}
int uMoveFollow(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return actionOp(L, kUnit, "unit.moveFollow", "MoveFollow",
                    {EventValue(target), EventValue(double(luaL_optnumber(L, 3, 0))),
                     EventValue(double(luaL_optnumber(L, 4, 0)))});
}
int uMoveStop(lua_State* L) { return actionOp(L, kUnit, "unit.moveStop", "MoveStop"); }
int uMoveHome(lua_State* L) { return actionOp(L, kUnit, "unit.moveHome", "MoveHome"); }
int uSetFaction(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setFaction", "SetFaction",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "faction")))});
}
int uGetFaction(lua_State* L) { return readOp(L, kUnit, "unit.getFaction", "GetFaction"); }
int uIsHostileTo(lua_State* L)
{
    fl::HandleData const& other = objArg(L, 2, kUnit, "other");
    return readOp(L, kUnit, "unit.isHostileTo", "IsHostileTo", {EventValue(other)});
}
int uIsFriendlyTo(lua_State* L)
{
    fl::HandleData const& other = objArg(L, 2, kUnit, "other");
    return readOp(L, kUnit, "unit.isFriendlyTo", "IsFriendlyTo", {EventValue(other)});
}
int uSetDisplayId(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setDisplayId", "SetDisplayId",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "displayId")))});
}
int uEmote(lua_State* L)
{
    return actionOp(L, kUnit, "unit.emote", "Emote",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "emote")))});
}
int uGetOwner(lua_State* L) { return readHandleOp(L, kUnit, "unit.getOwner", "GetOwner"); }
int uGetClass(lua_State* L) { return readOp(L, kUnit, "unit.getClass", "GetClass"); }
int uGetRace(lua_State* L) { return readOp(L, kUnit, "unit.getRace", "GetRace"); }
int uGetGender(lua_State* L) { return readOp(L, kUnit, "unit.getGender", "GetGender"); }

int uSendUnitSay(lua_State* L)
{
    std::string msg = argStr(L, 2, "msg");
    return actionOp(L, kUnit, "unit.say", "SendUnitSay",
                    {EventValue(msg), EventValue(static_cast<int64_t>(argUInt(L, 3, "language")))});
}
int uSendUnitYell(lua_State* L)
{
    std::string msg = argStr(L, 2, "msg");
    return actionOp(L, kUnit, "unit.yell", "SendUnitYell",
                    {EventValue(msg), EventValue(static_cast<int64_t>(argUInt(L, 3, "language")))});
}
int uSendUnitWhisper(lua_State* L)
{
    std::string msg = argStr(L, 2, "msg");
    fl::HandleData const& receiver = objArg(L, 4, kPlayer, "receiver");
    return actionOp(L, kUnit, "unit.whisper", "SendUnitWhisper",
                    {EventValue(msg), EventValue(static_cast<int64_t>(argUInt(L, 3, "lang"))), EventValue(receiver),
                     EventValue(optBool(L, 5, false))});
}
// E5 long tail, round 1: Unit:SetPvP(apply) (docs Unit/SetPvP.html).
int uSetPvP(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setPvP", "SetPvP", {EventValue(optBool(L, 2, true))});
}

// ---- E5 long tail, round 1: the most used documented Unit methods (docs Unit/index.html). Each binding only checks
// the docs' argument types and ranges and hands the op to the host; the host owns the core call.

// stats and powers
int uGetMaxPower(lua_State* L)
{
    // docs: Unit:GetMaxPower([type]), type -1 (the default) = the unit's current power type
    return readOp(L, kUnit, "unit.getMaxPower", "GetMaxPower", {EventValue(int64_t(optInt(L, 2, -1, "type")))});
}
int uSetMaxPower(lua_State* L)
{
    // docs: Unit:SetMaxPower([type], maxPower), type -1 (the default) = the unit's current power type
    return actionOp(L, kUnit, "unit.setMaxPower", "SetMaxPower",
                    {EventValue(int64_t(optInt(L, 2, -1, "type"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 3, "maxPower")))});
}
int uGetPowerType(lua_State* L) { return readOp(L, kUnit, "unit.getPowerType", "GetPowerType"); }
int uSetPowerType(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setPowerType", "SetPowerType",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "type")))});
}
int uGetPowerPct(lua_State* L)
{
    return readOp(L, kUnit, "unit.getPowerPct", "GetPowerPct", {EventValue(int64_t(optInt(L, 2, -1, "type")))});
}
int uSetMaxHealth(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setMaxHealth", "SetMaxHealth",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "maxHealth")))});
}
int uGetStat(lua_State* L)
{
    uint32_t const stat = argUInt(L, 2, "statType");
    if (stat >= 5)   // the core's Stats enum: strength, agility, stamina, intellect, spirit
        luaL_error(L, "statType must be an integer 0..4");
    return readOp(L, kUnit, "unit.getStat", "GetStat", {EventValue(static_cast<int64_t>(stat))});
}
int uGetBaseSpellPower(lua_State* L)
{
    uint32_t const school = argUInt(L, 2, "spellSchool");
    if (school >= 7)   // the core's SpellSchools enum has seven schools
        luaL_error(L, "spellSchool must be an integer 0..6");
    return readOp(L, kUnit, "unit.getBaseSpellPower", "GetBaseSpellPower", {EventValue(static_cast<int64_t>(school))});
}
int uModifyPower(lua_State* L)
{
    // docs: Unit:ModifyPower(amount [, type]), type -1 (the default) = the unit's current power type
    return actionOp(L, kUnit, "unit.modifyPower", "ModifyPower",
                    {EventValue(int64_t(argInt(L, 2, "amount"))), EventValue(int64_t(optInt(L, 3, -1, "type")))});
}

// state and NPC flags
int uIsStandState(lua_State* L) { return readOp(L, kUnit, "unit.isStandState", "IsStandState"); }
int uSetStandState(lua_State* L)
{
    uint32_t const state = argU8(L, 2, "state");
    if (state >= 10)   // the core's UnitStandStateType: stand .. submerged, MAX_UNIT_STAND_STATE is 10
        luaL_error(L, "state must be an integer 0..9");
    return actionOp(L, kUnit, "unit.setStandState", "SetStandState", {EventValue(static_cast<int64_t>(state))});
}
int uIsMounted(lua_State* L) { return readOp(L, kUnit, "unit.isMounted", "IsMounted"); }
int uMount(lua_State* L)
{
    return actionOp(L, kUnit, "unit.mount", "Mount", {EventValue(static_cast<int64_t>(argUInt(L, 2, "displayId")))});
}
int uDismount(lua_State* L) { return actionOp(L, kUnit, "unit.dismount", "Dismount"); }
int uGetMountId(lua_State* L) { return readOp(L, kUnit, "unit.getMountId", "GetMountId"); }
int uIsStopped(lua_State* L) { return readOp(L, kUnit, "unit.isStopped", "IsStopped"); }
int uIsCasting(lua_State* L) { return readOp(L, kUnit, "unit.isCasting", "IsCasting"); }
int uIsFullHealth(lua_State* L) { return readOp(L, kUnit, "unit.isFullHealth", "IsFullHealth"); }
int uIsRooted(lua_State* L) { return readOp(L, kUnit, "unit.isRooted", "IsRooted"); }
int uHasUnitState(lua_State* L)
{
    return readOp(L, kUnit, "unit.hasUnitState", "HasUnitState",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "state")))});
}
int uAddUnitState(lua_State* L)
{
    return actionOp(L, kUnit, "unit.addUnitState", "AddUnitState",
                    {EventValue(static_cast<int64_t>(unitStateArg(L, 2)))});
}
int uClearUnitState(lua_State* L)
{
    return actionOp(L, kUnit, "unit.clearUnitState", "ClearUnitState",
                    {EventValue(static_cast<int64_t>(unitStateArg(L, 2)))});
}
int uIsInWater(lua_State* L) { return readOp(L, kUnit, "unit.isInWater", "IsInWater"); }
int uIsUnderWater(lua_State* L) { return readOp(L, kUnit, "unit.isUnderWater", "IsUnderWater"); }
int uIsPvPFlagged(lua_State* L) { return readOp(L, kUnit, "unit.isPvPFlagged", "IsPvPFlagged"); }
int uIsAttackingPlayer(lua_State* L) { return readOp(L, kUnit, "unit.isAttackingPlayer", "IsAttackingPlayer"); }
int uIsCharmed(lua_State* L) { return readOp(L, kUnit, "unit.isCharmed", "IsCharmed"); }
int uIsVendor(lua_State* L) { return readOp(L, kUnit, "unit.isVendor", "IsVendor"); }
int uIsQuestGiver(lua_State* L) { return readOp(L, kUnit, "unit.isQuestGiver", "IsQuestGiver"); }
int uIsTrainer(lua_State* L) { return readOp(L, kUnit, "unit.isTrainer", "IsTrainer"); }
int uIsBanker(lua_State* L) { return readOp(L, kUnit, "unit.isBanker", "IsBanker"); }
int uIsInnkeeper(lua_State* L) { return readOp(L, kUnit, "unit.isInnkeeper", "IsInnkeeper"); }
int uIsGossip(lua_State* L) { return readOp(L, kUnit, "unit.isGossip", "IsGossip"); }
int uIsSpiritHealer(lua_State* L) { return readOp(L, kUnit, "unit.isSpiritHealer", "IsSpiritHealer"); }
int uIsServiceProvider(lua_State* L) { return readOp(L, kUnit, "unit.isServiceProvider", "IsServiceProvider"); }

// speed and movement
int uGetSpeed(lua_State* L)
{
    return readOp(L, kUnit, "unit.getSpeed", "GetSpeed", {EventValue(static_cast<int64_t>(moveTypeArg(L, 2)))});
}
int uSetSpeed(lua_State* L)
{
    uint32_t const type = moveTypeArg(L, 2);
    double const rate = argFinite(L, 3, "rate");
    return actionOp(L, kUnit, "unit.setSpeed", "SetSpeed",
                    {EventValue(static_cast<int64_t>(type)), EventValue(rate), EventValue(optBool(L, 4, false))});
}
int uSetRooted(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setRooted", "SetRooted", {EventValue(optBool(L, 2, true))});
}
int uSetConfused(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setConfused", "SetConfused", {EventValue(optBool(L, 2, true))});
}
int uSetFeared(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setFeared", "SetFeared", {EventValue(optBool(L, 2, true))});
}
int uSetStunned(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setStunned", "SetStunned", {EventValue(optBool(L, 2, true))});
}
int uMoveRandom(lua_State* L)
{
    double const radius = argFinite(L, 2, "radius");
    if (radius < 0)
        luaL_error(L, "radius must not be negative");
    return actionOp(L, kUnit, "unit.moveRandom", "MoveRandom", {EventValue(radius)});
}
int uMoveChase(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    // An absent distance or angle stays absent: the core then chases at its normal melee range from any side, where a
    // zero would pin the unit onto the target's front.
    return actionOp(L, kUnit, "unit.moveChase", "MoveChase",
                    {EventValue(target), lua_isnoneornil(L, 3) ? EventValue() : EventValue(argFinite(L, 3, "dist")),
                     lua_isnoneornil(L, 4) ? EventValue() : EventValue(argFinite(L, 4, "angle"))});
}
int uMoveConfused(lua_State* L) { return actionOp(L, kUnit, "unit.moveConfused", "MoveConfused"); }
int uMoveFleeing(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return actionOp(L, kUnit, "unit.moveFleeing", "MoveFleeing",
                    {EventValue(target), EventValue(static_cast<int64_t>(optUInt(L, 3, 0)))});
}
int uMoveIdle(lua_State* L) { return actionOp(L, kUnit, "unit.moveIdle", "MoveIdle"); }
int uMoveJump(lua_State* L)
{
    double const x = argFinite(L, 2, "x");
    double const y = argFinite(L, 3, "y");
    double const z = argFinite(L, 4, "z");
    double const zSpeed = argFinite(L, 5, "zSpeed");
    double const maxHeight = argFinite(L, 6, "maxHeight");
    if (maxHeight < 0)
        luaL_error(L, "maxHeight must not be negative");
    return actionOp(L, kUnit, "unit.moveJump", "MoveJump",
                    {EventValue(x), EventValue(y), EventValue(z), EventValue(zSpeed), EventValue(maxHeight),
                     EventValue(static_cast<int64_t>(optUInt(L, 7, 0)))});
}
int uMoveExpire(lua_State* L)
{
    return actionOp(L, kUnit, "unit.moveExpire", "MoveExpire", {EventValue(optBool(L, 2, true))});
}
int uMoveClear(lua_State* L)
{
    return actionOp(L, kUnit, "unit.moveClear", "MoveClear", {EventValue(optBool(L, 2, true))});
}
int uNearTeleport(lua_State* L)
{
    return actionOp(L, kUnit, "unit.nearTeleport", "NearTeleport",
                    {EventValue(double(argFloat(L, 2, "x"))), EventValue(double(argFloat(L, 3, "y"))),
                     EventValue(double(argFloat(L, 4, "z"))), EventValue(double(argFloat(L, 5, "o")))});
}
int uGetMovementType(lua_State* L) { return readOp(L, kUnit, "unit.getMovementType", "GetMovementType"); }
int uSetFacing(lua_State* L)
{
    // the orientation is a float in radians (the core normalises it), not an integer
    return actionOp(L, kUnit, "unit.setFacing", "SetFacing", {EventValue(argFinite(L, 2, "orientation"))});
}
int uSetFacingToObject(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kWorldObject, "target");
    return actionOp(L, kUnit, "unit.setFacingToObject", "SetFacingToObject", {EventValue(target)});
}

// combat and spells
int uHealthBelowPct(lua_State* L)
{
    return readOp(L, kUnit, "unit.healthBelowPct", "HealthBelowPct",
                  {EventValue(double(argFloat(L, 2, "healthpct")))});
}
int uHealthAbovePct(lua_State* L)
{
    return readOp(L, kUnit, "unit.healthAbovePct", "HealthAbovePct",
                  {EventValue(double(argFloat(L, 2, "healthpct")))});
}
int uClearInCombat(lua_State* L) { return actionOp(L, kUnit, "unit.clearInCombat", "ClearInCombat"); }
int uSetInCombatWith(lua_State* L)
{
    fl::HandleData const& enemy = objArg(L, 2, kUnit, "enemy");
    return actionOp(L, kUnit, "unit.setInCombatWith", "SetInCombatWith", {EventValue(enemy)});
}
int uDealHeal(lua_State* L)
{
    fl::HandleData const& target = objArg(L, 2, kUnit, "target");
    return actionOp(L, kUnit, "unit.dealHeal", "DealHeal",
                    {EventValue(target), EventValue(static_cast<int64_t>(argUInt(L, 3, "spell"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 4, "amount"))), EventValue(optBool(L, 5, false))});
}
int uStopSpellCast(lua_State* L)
{
    return actionOp(L, kUnit, "unit.stopSpellCast", "StopSpellCast",
                    {EventValue(static_cast<int64_t>(optUInt(L, 2, 0)))});
}
int uInterruptSpell(lua_State* L)
{
    return actionOp(L, kUnit, "unit.interruptSpell", "InterruptSpell",
                    {EventValue(int64_t(argInt(L, 2, "spellType"))), EventValue(optBool(L, 3, true))});
}
int uGetCurrentSpell(lua_State* L)
{
    return readHandleOp(L, kUnit, "unit.getCurrentSpell", "GetCurrentSpell",
                        {EventValue(int64_t(argInt(L, 2, "spellType")))});
}

// auras
int uGetAura(lua_State* L)
{
    std::vector<EventValue> args{EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))};
    for (int i = 3; i <= 4; ++i)
        if (lua_isnoneornil(L, i))
            args.emplace_back();
        else
        {
            fl::Guid guid;
            argGuid(L, i, guid);
            args.emplace_back(fl::guidText(guid));
        }
    args.emplace_back(static_cast<int64_t>(optUInt(L, 5, 0)));
    return readHandleOp(L, kUnit, "unit.getAura", "GetAura", args);
}
int uGetOwnedAura(lua_State* L)
{
    std::vector<EventValue> args{EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))};
    for (int i = 3; i <= 4; ++i)
        if (lua_isnoneornil(L, i))
            args.emplace_back();
        else
        {
            fl::Guid guid;
            argGuid(L, i, guid);
            args.emplace_back(fl::guidText(guid));
        }
    args.emplace_back(static_cast<int64_t>(optUInt(L, 5, 0)));
    if (lua_isnoneornil(L, 6))
        args.emplace_back();
    else
        args.emplace_back(kindArg(L, 6, kAura, "exceptAura"));
    return readHandleOp(L, kUnit, "unit.getOwnedAura", "GetOwnedAura", args);
}
int uRemoveAllAuras(lua_State* L) { return actionOp(L, kUnit, "unit.removeAllAuras", "RemoveAllAuras"); }
int uRemoveArenaAuras(lua_State* L) { return actionOp(L, kUnit, "unit.removeArenaAuras", "RemoveArenaAuras"); }

// ownership and relations
int uGetCharmerGUID(lua_State* L) { return readOp(L, kUnit, "unit.getCharmerGuid", "GetCharmerGUID"); }
int uGetOwnerGUID(lua_State* L) { return readOp(L, kUnit, "unit.getOwnerGuid", "GetOwnerGUID"); }
int uGetControllerGUID(lua_State* L) { return readOp(L, kUnit, "unit.getControllerGuid", "GetControllerGUID"); }
int uGetCreatorGUID(lua_State* L) { return readOp(L, kUnit, "unit.getCreatorGuid", "GetCreatorGUID"); }
int uGetPetGUID(lua_State* L) { return readOp(L, kUnit, "unit.getPetGuid", "GetPetGUID"); }
int uSetOwnerGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return actionOp(L, kUnit, "unit.setOwnerGuid", "SetOwnerGUID", {EventValue(fl::guidText(guid))});
}
// The search radius: the core turns it into grid cells with a float-to-int conversion, so it must be finite.
double rangeArg(lua_State* L, int idx)
{
    return lua_isnoneornil(L, idx) ? 533.333 : argFinite(L, idx, "range");
}
int uGetFriendlyUnitsInRange(lua_State* L)
{
    return listOp(L, kUnit, "unit.getFriendlyUnitsInRange", "GetFriendlyUnitsInRange",
                  {EventValue(rangeArg(L, 2))});
}
int uGetUnfriendlyUnitsInRange(lua_State* L)
{
    return listOp(L, kUnit, "unit.getUnfriendlyUnitsInRange", "GetUnfriendlyUnitsInRange",
                  {EventValue(rangeArg(L, 2))});
}

// display and identity
int uGetDisplayId(lua_State* L) { return readOp(L, kUnit, "unit.getDisplayId", "GetDisplayId"); }
int uGetNativeDisplayId(lua_State* L) { return readOp(L, kUnit, "unit.getNativeDisplayId", "GetNativeDisplayId"); }
int uSetNativeDisplayId(lua_State* L)
{
    uint32_t const displayId = argUInt(L, 2, "displayId");
    fl::HandleData const& h = self(L, kUnit);
    std::vector<EventValue> out;
    gameCall(L, h, "unit.setNativeDisplayId", {EventValue(static_cast<int64_t>(displayId))}, out, "SetNativeDisplayId");
    // The core aborts later (Unit::GetCollisionHeight asserts the native display is in its tables), so the host
    // refuses an id the display tables do not hold and says so with `false`.
    if (!out.empty())
        if (auto const* ok = std::get_if<bool>(&out[0].v); ok && !*ok)
            luaL_error(L, "displayId %I is not a creature display the server knows", static_cast<lua_Integer>(displayId));
    return 0;
}
int uGetCreatureType(lua_State* L) { return readOp(L, kUnit, "unit.getCreatureType", "GetCreatureType"); }
int uGetClassAsString(lua_State* L)
{
    return readOp(L, kUnit, "unit.getClassAsString", "GetClassAsString",
                  {EventValue(localeArg(L, 2))});
}
int uGetRaceAsString(lua_State* L)
{
    return readOp(L, kUnit, "unit.getRaceAsString", "GetRaceAsString",
                  {EventValue(localeArg(L, 2))});
}
int uGetClassMask(lua_State* L) { return readOp(L, kUnit, "unit.getClassMask", "GetClassMask"); }
int uGetRaceMask(lua_State* L) { return readOp(L, kUnit, "unit.getRaceMask", "GetRaceMask"); }
int uSendChatMessageToPlayer(lua_State* L)
{
    // docs: Unit:SendChatMessageToPlayer(type, lang, msg, target), type a ChatMsg value 0..255
    fl::HandleData const& target = objArg(L, 5, kPlayer, "target");
    return actionOp(L, kUnit, "unit.sendChatMessageToPlayer", "SendChatMessageToPlayer",
                    {EventValue(static_cast<int64_t>(argU8(L, 2, "type"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 3, "lang"))), EventValue(argStr(L, 4, "msg")),
                     EventValue(target)});
}
int uPerformEmote(lua_State* L)
{
    return actionOp(L, kUnit, "unit.performEmote", "PerformEmote",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "emoteId")))});
}
int uEmoteState(lua_State* L)
{
    return actionOp(L, kUnit, "unit.emoteState", "EmoteState",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "emoteId")))});
}
int uIsMoving(lua_State* L) { return readOp(L, kUnit, "unit.isMoving", "IsMoving"); }
int uIsFlying(lua_State* L) { return readOp(L, kUnit, "unit.isFlying", "IsFlying"); }
int uIsVisible(lua_State* L) { return readOp(L, kUnit, "unit.isVisible", "IsVisible"); }
int uSetSheath(lua_State* L)
{
    uint32_t const state = argUInt(L, 2, "sheathState");
    if (state > 2)   // the core's SheathState enum: unarmed, melee, ranged
        luaL_error(L, "sheathState must be an integer 0..2");
    return actionOp(L, kUnit, "unit.setSheath", "SetSheath", {EventValue(static_cast<int64_t>(state))});
}
int uSetWaterWalk(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setWaterWalk", "SetWaterWalk", {EventValue(optBool(L, 2, true))});
}
int uSetSanctuary(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setSanctuary", "SetSanctuary", {EventValue(optBool(L, 2, true))});
}
int uDeMorph(lua_State* L) { return actionOp(L, kUnit, "unit.deMorph", "DeMorph"); }
int uRestoreDisplayId(lua_State* L) { return actionOp(L, kUnit, "unit.restoreDisplayId", "RestoreDisplayId"); }
int uRestoreFaction(lua_State* L) { return actionOp(L, kUnit, "unit.restoreFaction", "RestoreFaction"); }

// remaining Unit coverage
int uAddFlatStatModifier(lua_State* L)
{
    uint32_t const statType = argUInt(L, 2, "statType");
    if (statType >= 44)   // Unit::UnitMods: UNIT_MOD_END is 44
        luaL_error(L, "statType must be an integer 0..43");
    uint32_t const modType = argUInt(L, 3, "modType");
    if (modType > 1)   // BASE_VALUE = 0, TOTAL_VALUE = 1
        luaL_error(L, "modType must be 0 or 1");
    double const value = argFinite(L, 4, "value");
    return actionOp(L, kUnit, "unit.addFlatStatModifier", "AddFlatStatModifier",
                    {EventValue(static_cast<int64_t>(statType)), EventValue(static_cast<int64_t>(modType)),
                     EventValue(value), EventValue(optBool(L, 5, true))});
}
int uAddPctStatModifier(lua_State* L)
{
    uint32_t const statType = argUInt(L, 2, "statType");
    if (statType >= 44)
        luaL_error(L, "statType must be an integer 0..43");
    uint32_t const modType = argUInt(L, 3, "modType");
    if (modType > 1)   // BASE_PCT = 0, TOTAL_PCT = 1
        luaL_error(L, "modType must be 0 or 1");
    double const value = argFinite(L, 4, "value");
    return actionOp(L, kUnit, "unit.addPctStatModifier", "AddPctStatModifier",
                    {EventValue(static_cast<int64_t>(statType)), EventValue(static_cast<int64_t>(modType)),
                     EventValue(value)});
}
int uCanModifyStats(lua_State* L) { return readOp(L, kUnit, "unit.canModifyStats", "CanModifyStats"); }
int uCastSpellAoF(lua_State* L)
{
    double const x = argFinite(L, 2, "x");
    double const y = argFinite(L, 3, "y");
    double const z = argFinite(L, 4, "z");
    uint32_t const spell = argUInt(L, 5, "spell");
    return actionOp(L, kUnit, "unit.castSpellAoF", "CastSpellAoF",
                    {EventValue(x), EventValue(y), EventValue(z), EventValue(static_cast<int64_t>(spell)),
                     EventValue(optBool(L, 6, true))});
}
int uCountPctFromCurHealth(lua_State* L)
{
    double const pct = lua_isnoneornil(L, 2) ? 0.0 : argFinite(L, 2, "pct");
    return readOp(L, kUnit, "unit.countPctFromCurHealth", "CountPctFromCurHealth", {EventValue(pct)});
}
int uCountPctFromMaxHealth(lua_State* L)
{
    double const pct = lua_isnoneornil(L, 2) ? 0.0 : argFinite(L, 2, "pct");
    return readOp(L, kUnit, "unit.countPctFromMaxHealth", "CountPctFromMaxHealth", {EventValue(pct)});
}
int uDisableMelee(lua_State* L)
{
    fl::HandleData const& h = self(L, kUnit);
    std::vector<EventValue> out;
    gameCall(L, h, "unit.disableMelee", {EventValue(optBool(L, 2, true))}, out, "DisableMelee");
    requireHostOk(L, out, "DisableMelee", "only available on a creature");
    return 0;
}
int uGetCharmGUID(lua_State* L) { return readOp(L, kUnit, "unit.getCharmGuid", "GetCharmGUID"); }
int uGetControllerGUIDS(lua_State* L) { return readOp(L, kUnit, "unit.getControllerGuids", "GetControllerGUIDS"); }
int uGetCritterGUID(lua_State* L) { return readOp(L, kUnit, "unit.getCritterGuid", "GetCritterGUID"); }
int uGetStandState(lua_State* L) { return readOp(L, kUnit, "unit.getStandState", "GetStandState"); }
int uIsArmorer(lua_State* L) { return readOp(L, kUnit, "unit.isArmorer", "IsArmorer"); }
int uIsAuctioneer(lua_State* L) { return readOp(L, kUnit, "unit.isAuctioneer", "IsAuctioneer"); }
int uIsBattleMaster(lua_State* L) { return readOp(L, kUnit, "unit.isBattleMaster", "IsBattleMaster"); }
int uIsDying(lua_State* L) { return readOp(L, kUnit, "unit.isDying", "IsDying"); }
int uIsGuildMaster(lua_State* L) { return readOp(L, kUnit, "unit.isGuildMaster", "IsGuildMaster"); }
int uIsInAccessiblePlaceFor(lua_State* L)
{
    fl::HandleData const& obj = objArg(L, 2, kWorldObject, "obj");
    double const radius = lua_isnoneornil(L, 3) ? 0.0 : argFinite(L, 3, "radius");
    return readOp(L, kUnit, "unit.isInAccessiblePlaceFor", "IsInAccessiblePlaceFor",
                  {EventValue(obj), EventValue(radius)});
}
int uIsSpiritGuide(lua_State* L) { return readOp(L, kUnit, "unit.isSpiritGuide", "IsSpiritGuide"); }
int uIsSpiritService(lua_State* L) { return readOp(L, kUnit, "unit.isSpiritService", "IsSpiritService"); }
int uIsTabardDesigner(lua_State* L) { return readOp(L, kUnit, "unit.isTabardDesigner", "IsTabardDesigner"); }
int uIsTaxi(lua_State* L) { return readOp(L, kUnit, "unit.isTaxi", "IsTaxi"); }
int uRemoveBindSightAuras(lua_State* L) { return actionOp(L, kUnit, "unit.removeBindSightAuras", "RemoveBindSightAuras"); }
int uRemoveCharmAuras(lua_State* L) { return actionOp(L, kUnit, "unit.removeCharmAuras", "RemoveCharmAuras"); }
int uSendUnitEmote(lua_State* L)
{
    std::string const msg = argStr(L, 2, "msg");
    if (msg.size() > 255)
        luaL_error(L, "msg is longer than 255 bytes");
    std::vector<EventValue> args{EventValue(msg)};
    if (lua_isnoneornil(L, 3))
        args.emplace_back();
    else
        args.emplace_back(objArg(L, 3, kUnit, "receiver"));
    args.emplace_back(optBool(L, 4, false));
    return actionOp(L, kUnit, "unit.sendUnitEmote", "SendUnitEmote", args);
}
int uSetCanFly(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setCanFly", "SetCanFly", {EventValue(optBool(L, 2, true))});
}
int uSetCreatorGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return actionOp(L, kUnit, "unit.setCreatorGuid", "SetCreatorGUID", {EventValue(fl::guidText(guid))});
}
int uSetCritterGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return actionOp(L, kUnit, "unit.setCritterGuid", "SetCritterGUID", {EventValue(fl::guidText(guid))});
}
int uSetFFA(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setFFA", "SetFFA", {EventValue(optBool(L, 2, true))});
}
int uSetImmuneTo(lua_State* L)
{
    int32_t const immunity = argInt(L, 2, "immunity");
    if (immunity < 0 || immunity >= 37)   // Mechanics: MECHANIC_NONE .. MAX_MECHANIC - 1
        luaL_error(L, "immunity must be an integer 0..36");
    return actionOp(L, kUnit, "unit.setImmuneTo", "SetImmuneTo",
                    {EventValue(static_cast<int64_t>(immunity)), EventValue(optBool(L, 3, true))});
}
int uSetName(lua_State* L)
{
    fl::HandleData const& h = self(L, kUnit);
    std::string const name = argStr(L, 2, "name");
    if (name.size() > 64)
        luaL_error(L, "name is longer than 64 bytes");
    std::vector<EventValue> out;
    gameCall(L, h, "unit.setName", {EventValue(name)}, out, "SetName");
    requireHostOk(L, out, "SetName", "only available on a creature (not a pet)");
    return 0;
}
int uSetPetGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return actionOp(L, kUnit, "unit.setPetGuid", "SetPetGUID", {EventValue(fl::guidText(guid))});
}
int uSetVisible(lua_State* L)
{
    return actionOp(L, kUnit, "unit.setVisible", "SetVisible", {EventValue(optBool(L, 2, true))});
}
}  // namespace

void registerUnitMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetLevel", fl::safe<uGetLevel>},        {"SetLevel", fl::safe<uSetLevel>},
        {"GetHealth", fl::safe<uGetHealth>},      {"SetHealth", fl::safe<uSetHealth>},
        {"GetMaxHealth", fl::safe<uGetMaxHealth>}, {"GetHealthPct", fl::safe<uGetHealthPct>},
        {"GetPower", fl::safe<uGetPower>},        {"SetPower", fl::safe<uSetPower>},
        {"IsAlive", fl::safe<uIsAlive>},          {"IsDead", fl::safe<uIsDead>},
        {"IsInCombat", fl::safe<uIsInCombat>},    {"GetVictim", fl::safe<uGetVictim>},
        {"Attack", fl::safe<uAttack>},            {"AttackStop", fl::safe<uAttackStop>},
        {"CastSpell", fl::safe<uCastSpell>},      {"CastCustomSpell", fl::safe<uCastCustomSpell>},
        {"AddAura", fl::safe<uAddAura>},          {"RemoveAura", fl::safe<uRemoveAura>},
        {"HasAura", fl::safe<uHasAura>},          {"Kill", fl::safe<uKill>},
        {"DealDamage", fl::safe<uDealDamage>},    {"MoveTo", fl::safe<uMoveTo>},
        {"MoveFollow", fl::safe<uMoveFollow>},    {"MoveStop", fl::safe<uMoveStop>},
        {"MoveHome", fl::safe<uMoveHome>},        {"SetFaction", fl::safe<uSetFaction>},
        {"GetFaction", fl::safe<uGetFaction>},    {"IsHostileTo", fl::safe<uIsHostileTo>},
        {"IsFriendlyTo", fl::safe<uIsFriendlyTo>}, {"SetDisplayId", fl::safe<uSetDisplayId>},
        {"Emote", fl::safe<uEmote>},              {"GetOwner", fl::safe<uGetOwner>},
        {"GetClass", fl::safe<uGetClass>},        {"GetRace", fl::safe<uGetRace>},
        {"GetGender", fl::safe<uGetGender>},      {"SendUnitSay", fl::safe<uSendUnitSay>},
        {"SendUnitYell", fl::safe<uSendUnitYell>}, {"SendUnitWhisper", fl::safe<uSendUnitWhisper>},
        {"SetPvP", fl::safe<uSetPvP>},
        // E5 long tail, round 1
        {"GetMaxPower", fl::safe<uGetMaxPower>},          {"SetMaxPower", fl::safe<uSetMaxPower>},
        {"GetPowerType", fl::safe<uGetPowerType>},        {"SetPowerType", fl::safe<uSetPowerType>},
        {"GetPowerPct", fl::safe<uGetPowerPct>},          {"SetMaxHealth", fl::safe<uSetMaxHealth>},
        {"GetStat", fl::safe<uGetStat>},                  {"GetBaseSpellPower", fl::safe<uGetBaseSpellPower>},
        {"ModifyPower", fl::safe<uModifyPower>},
        {"IsStandState", fl::safe<uIsStandState>},        {"SetStandState", fl::safe<uSetStandState>},
        {"IsMounted", fl::safe<uIsMounted>},              {"Mount", fl::safe<uMount>},
        {"Dismount", fl::safe<uDismount>},                {"GetMountId", fl::safe<uGetMountId>},
        {"IsStopped", fl::safe<uIsStopped>},              {"IsCasting", fl::safe<uIsCasting>},
        {"IsFullHealth", fl::safe<uIsFullHealth>},        {"IsRooted", fl::safe<uIsRooted>},
        {"HasUnitState", fl::safe<uHasUnitState>},        {"AddUnitState", fl::safe<uAddUnitState>},
        {"ClearUnitState", fl::safe<uClearUnitState>},    {"IsInWater", fl::safe<uIsInWater>},
        {"IsUnderWater", fl::safe<uIsUnderWater>},        {"IsPvPFlagged", fl::safe<uIsPvPFlagged>},
        {"IsAttackingPlayer", fl::safe<uIsAttackingPlayer>}, {"IsCharmed", fl::safe<uIsCharmed>},
        {"IsVendor", fl::safe<uIsVendor>},                {"IsQuestGiver", fl::safe<uIsQuestGiver>},
        {"IsTrainer", fl::safe<uIsTrainer>},              {"IsBanker", fl::safe<uIsBanker>},
        {"IsInnkeeper", fl::safe<uIsInnkeeper>},          {"IsGossip", fl::safe<uIsGossip>},
        {"IsSpiritHealer", fl::safe<uIsSpiritHealer>},    {"IsServiceProvider", fl::safe<uIsServiceProvider>},
        {"GetSpeed", fl::safe<uGetSpeed>},                {"SetSpeed", fl::safe<uSetSpeed>},
        {"SetRooted", fl::safe<uSetRooted>},              {"SetConfused", fl::safe<uSetConfused>},
        {"SetFeared", fl::safe<uSetFeared>},              {"SetStunned", fl::safe<uSetStunned>},
        {"MoveRandom", fl::safe<uMoveRandom>},            {"MoveChase", fl::safe<uMoveChase>},
        {"MoveConfused", fl::safe<uMoveConfused>},        {"MoveFleeing", fl::safe<uMoveFleeing>},
        {"MoveIdle", fl::safe<uMoveIdle>},                {"MoveJump", fl::safe<uMoveJump>},
        {"MoveExpire", fl::safe<uMoveExpire>},            {"MoveClear", fl::safe<uMoveClear>},
        {"NearTeleport", fl::safe<uNearTeleport>},        {"GetMovementType", fl::safe<uGetMovementType>},
        {"SetFacing", fl::safe<uSetFacing>},              {"SetFacingToObject", fl::safe<uSetFacingToObject>},
        {"HealthBelowPct", fl::safe<uHealthBelowPct>},    {"HealthAbovePct", fl::safe<uHealthAbovePct>},
        {"ClearInCombat", fl::safe<uClearInCombat>},      {"SetInCombatWith", fl::safe<uSetInCombatWith>},
        {"DealHeal", fl::safe<uDealHeal>},                {"StopSpellCast", fl::safe<uStopSpellCast>},
        {"InterruptSpell", fl::safe<uInterruptSpell>},    {"GetCurrentSpell", fl::safe<uGetCurrentSpell>},
        {"GetAura", fl::safe<uGetAura>},                  {"GetOwnedAura", fl::safe<uGetOwnedAura>},
        {"RemoveAllAuras", fl::safe<uRemoveAllAuras>},    {"RemoveArenaAuras", fl::safe<uRemoveArenaAuras>},
        {"GetCharmerGUID", fl::safe<uGetCharmerGUID>},    {"GetOwnerGUID", fl::safe<uGetOwnerGUID>},
        {"GetControllerGUID", fl::safe<uGetControllerGUID>},
        {"GetCreatorGUID", fl::safe<uGetCreatorGUID>},    {"GetPetGUID", fl::safe<uGetPetGUID>},
        {"SetOwnerGUID", fl::safe<uSetOwnerGUID>},
        {"GetFriendlyUnitsInRange", fl::safe<uGetFriendlyUnitsInRange>},
        {"GetUnfriendlyUnitsInRange", fl::safe<uGetUnfriendlyUnitsInRange>},
        {"GetDisplayId", fl::safe<uGetDisplayId>},        {"GetNativeDisplayId", fl::safe<uGetNativeDisplayId>},
        {"SetNativeDisplayId", fl::safe<uSetNativeDisplayId>},
        {"GetCreatureType", fl::safe<uGetCreatureType>},  {"GetClassAsString", fl::safe<uGetClassAsString>},
        {"GetRaceAsString", fl::safe<uGetRaceAsString>},  {"GetClassMask", fl::safe<uGetClassMask>},
        {"GetRaceMask", fl::safe<uGetRaceMask>},
        {"SendChatMessageToPlayer", fl::safe<uSendChatMessageToPlayer>},
        {"PerformEmote", fl::safe<uPerformEmote>},        {"EmoteState", fl::safe<uEmoteState>},
        {"IsMoving", fl::safe<uIsMoving>},                {"IsFlying", fl::safe<uIsFlying>},
        {"IsVisible", fl::safe<uIsVisible>},              {"SetSheath", fl::safe<uSetSheath>},
        {"SetWaterWalk", fl::safe<uSetWaterWalk>},        {"SetSanctuary", fl::safe<uSetSanctuary>},
        {"DeMorph", fl::safe<uDeMorph>},                  {"RestoreDisplayId", fl::safe<uRestoreDisplayId>},
        {"RestoreFaction", fl::safe<uRestoreFaction>},
        {"AddFlatStatModifier", fl::safe<uAddFlatStatModifier>},
        {"AddPctStatModifier", fl::safe<uAddPctStatModifier>},
        {"CanModifyStats", fl::safe<uCanModifyStats>},
        {"CastSpellAoF", fl::safe<uCastSpellAoF>},
        {"CountPctFromCurHealth", fl::safe<uCountPctFromCurHealth>},
        {"CountPctFromMaxHealth", fl::safe<uCountPctFromMaxHealth>},
        {"DisableMelee", fl::safe<uDisableMelee>},
        {"GetCharmGUID", fl::safe<uGetCharmGUID>},
        {"GetControllerGUIDS", fl::safe<uGetControllerGUIDS>},
        {"GetCritterGUID", fl::safe<uGetCritterGUID>},
        {"GetStandState", fl::safe<uGetStandState>},
        {"IsArmorer", fl::safe<uIsArmorer>},
        {"IsAuctioneer", fl::safe<uIsAuctioneer>},
        {"IsBattleMaster", fl::safe<uIsBattleMaster>},
        {"IsDying", fl::safe<uIsDying>},
        {"IsGuildMaster", fl::safe<uIsGuildMaster>},
        {"IsInAccessiblePlaceFor", fl::safe<uIsInAccessiblePlaceFor>},
        {"IsSpiritGuide", fl::safe<uIsSpiritGuide>},
        {"IsSpiritService", fl::safe<uIsSpiritService>},
        {"IsTabardDesigner", fl::safe<uIsTabardDesigner>},
        {"IsTaxi", fl::safe<uIsTaxi>},
        {"RemoveBindSightAuras", fl::safe<uRemoveBindSightAuras>},
        {"RemoveCharmAuras", fl::safe<uRemoveCharmAuras>},
        {"SendUnitEmote", fl::safe<uSendUnitEmote>},
        {"SetCanFly", fl::safe<uSetCanFly>},
        {"SetCreatorGUID", fl::safe<uSetCreatorGUID>},
        {"SetCritterGUID", fl::safe<uSetCritterGUID>},
        {"SetFFA", fl::safe<uSetFFA>},
        {"SetImmuneTo", fl::safe<uSetImmuneTo>},
        {"SetName", fl::safe<uSetName>},
        {"SetPetGUID", fl::safe<uSetPetGUID>},
        {"SetVisible", fl::safe<uSetVisible>},
        {nullptr, nullptr}};
    addMethods(L, "Unit", regs);
}
}  // namespace flu
