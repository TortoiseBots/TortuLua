// E5 long tail, round 1: the documented Unit methods added in this round (docs Unit/index.html). The fake world
// carries a small unit state (FakeHost::FakeUnit) so a value can be written through one method and read back through
// another; every method is also called on a gone unit, which must raise the family's stale-handle error, and with a
// bad argument, which must raise before anything reaches the host.
#include "Support.h"

#include <string>
#include <vector>

using namespace fltest;

namespace
{
fl::Config elunaConfig(fl::Config c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    return c;
}

struct UnitRig : Rig
{
    UnitRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

// Stores a player and a creature in the Lua state through their own events, so the methods can take them as arguments.
void storeHandles(UnitRig& rig, fl::HandleData const& p, fl::HandleData const& mob)
{
    rig.ok("RegisterPlayerEvent(3, function(ev, player) stored = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    rig.ok("RegisterCreatureEvent(1, 5, function(ev, creature) mob = creature end)");
    rig.e().emitEluna(fl::EluKind::Creature, 1, 5, {fl::EventValue(mob)});
}

// Every method of the round, in the call form the docs give (the receiver is the stored player).
std::vector<std::string> const& roundCalls()
{
    static std::vector<std::string> const calls = {
        "GetMaxPower()",
        "SetMaxPower(0, 500)",
        "GetPowerType()",
        "SetPowerType(1)",
        "GetPowerPct(0)",
        "SetMaxHealth(2000)",
        "GetStat(1)",
        "GetBaseSpellPower(3)",
        "ModifyPower(10, 0)",
        "IsStandState()",
        "SetStandState(1)",
        "IsMounted()",
        "Mount(14337)",
        "Dismount()",
        "GetMountId()",
        "IsStopped()",
        "IsCasting()",
        "IsFullHealth()",
        "IsRooted()",
        "HasUnitState(1024)",
        "AddUnitState(1024)",
        "ClearUnitState(1024)",
        "IsInWater()",
        "IsUnderWater()",
        "IsPvPFlagged()",
        "IsAttackingPlayer()",
        "IsCharmed()",
        "IsVendor()",
        "IsQuestGiver()",
        "IsTrainer()",
        "IsBanker()",
        "IsInnkeeper()",
        "IsGossip()",
        "IsSpiritHealer()",
        "IsServiceProvider()",
        "GetSpeed(1)",
        "SetSpeed(1, 1.5)",
        "SetRooted(false)",
        "SetConfused(false)",
        "SetFeared(false)",
        "SetStunned(false)",
        "MoveRandom(10)",
        "MoveChase(mob)",
        "MoveConfused()",
        "MoveFleeing(mob)",
        "MoveIdle()",
        "MoveJump(1, 2, 3, 1, 2)",
        "MoveExpire(false)",
        "MoveClear(false)",
        "NearTeleport(1, 2, 3, 4)",
        "GetMovementType()",
        "SetFacing(1)",
        "SetFacingToObject(mob)",
        "HealthBelowPct(50)",
        "HealthAbovePct(50)",
        "ClearInCombat()",
        "SetInCombatWith(mob)",
        "DealHeal(mob, 1, 100)",
        "StopSpellCast(0)",
        "InterruptSpell(0)",
        "GetCurrentSpell(1)",
        "GetAura(1)",
        "GetOwnedAura(1)",
        "RemoveAllAuras()",
        "RemoveArenaAuras()",
        "GetCharmerGUID()",
        "GetOwnerGUID()",
        "GetControllerGUID()",
        "GetCreatorGUID()",
        "GetPetGUID()",
        "SetOwnerGUID(stored:GetGUID())",
        "GetFriendlyUnitsInRange()",
        "GetUnfriendlyUnitsInRange()",
        "GetDisplayId()",
        "GetNativeDisplayId()",
        "SetNativeDisplayId(100)",
        "GetCreatureType()",
        "GetClassAsString()",
        "GetRaceAsString()",
        "GetClassMask()",
        "GetRaceMask()",
        "SendChatMessageToPlayer(0, 0, 'hi', stored)",
        "PerformEmote(1)",
        "EmoteState(1)",
        "IsMoving()",
        "IsFlying()",
        "IsVisible()",
        "SetSheath(1)",
        "SetWaterWalk(false)",
        "SetSanctuary(false)",
        "DeMorph()",
        "RestoreDisplayId()",
        "RestoreFaction()",
        "AddFlatStatModifier(0, 1, 5)",
        "AddPctStatModifier(0, 1, 10)",
        "CanModifyStats()",
        "CastSpellAoF(1, 2, 3, 100)",
        "CountPctFromCurHealth(50)",
        "CountPctFromMaxHealth(50)",
        "GetCharmGUID()",
        "GetControllerGUIDS()",
        "GetCritterGUID()",
        "GetStandState()",
        "IsArmorer()",
        "IsAuctioneer()",
        "IsBattleMaster()",
        "IsDying()",
        "IsGuildMaster()",
        "IsInAccessiblePlaceFor(mob)",
        "IsSpiritGuide()",
        "IsSpiritService()",
        "IsTabardDesigner()",
        "IsTaxi()",
        "RemoveBindSightAuras()",
        "RemoveCharmAuras()",
        "SendUnitEmote('roars')",
        "SetCanFly(false)",
        "SetCreatorGUID(stored:GetGUID())",
        "SetCritterGUID(stored:GetGUID())",
        "SetFFA(false)",
        "SetImmuneTo(12, true)",
        "SetPetGUID(stored:GetGUID())",
        "SetVisible(true)",
    };
    return calls;
}
}  // namespace

TEST_CASE("eluna unit long tail: every method of the round reaches the host and a gone unit raises")
{
    UnitRig rig;
    fl::HandleData p = FakeHost::player(71);
    fl::HandleData mob = FakeHost::creature(72, 0, 0, 500);
    fl::ObjectInfo info;
    info.name = "Longtail";
    rig.host.add(p, info);
    info.entry = 500;
    rig.host.add(mob, info);
    storeHandles(rig, p, mob);

    for (std::string const& call : roundCalls())
    {
        INFO("call: stored:" << call);
        CHECK(rig.run("stored:" + call).ok);
    }

    // The family's stale-handle rule: the same calls on a gone unit raise before anything reaches the host.
    rig.host.remove(p);
    for (std::string const& call : roundCalls())
    {
        INFO("call: stored:" << call);
        fl::CallResult r = rig.run("stored:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Player no longer exists") != std::string::npos);
    }
    rig.host.remove(mob);
    {
        fl::CallResult r = rig.run("mob:SetName('Longtail')");
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Creature no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna unit long tail: the values flow both ways through the fake unit state")
{
    UnitRig rig;
    fl::HandleData p = FakeHost::player(81);
    fl::HandleData mob = FakeHost::creature(82, 0, 0, 600);
    fl::ObjectInfo info;
    info.name = "Values";
    rig.host.add(p, info);
    info.entry = 600;
    rig.host.add(mob, info);
    storeHandles(rig, p, mob);
    FakeHost::FakeUnit& state = rig.host.unitState(p);

    // powers and stats
    rig.ok("stored:SetMaxPower(0, 500)");
    CHECK(rig.ok("return stored:GetMaxPower(0)") == "500");
    rig.ok("stored:SetMaxPower(2, 80)");
    CHECK(rig.ok("return stored:GetMaxPower(2)") == "80");
    rig.ok("stored:SetPowerType(2)");
    CHECK(rig.ok("return stored:GetPowerType()") == "2");
    CHECK(rig.ok("return stored:GetMaxPower()") == "80");   // the current power type
    rig.ok("stored:ModifyPower(20)");
    CHECK(rig.ok("return stored:GetPowerPct()") == "25.0");
    rig.ok("stored:SetMaxHealth(1500)");
    CHECK(state.maxHealth == 1500);
    state.stats[1] = 12.5;
    state.baseSpellPower[3] = 42;
    CHECK(rig.ok("return stored:GetStat(1)") == "12.5");
    CHECK(rig.ok("return stored:GetBaseSpellPower(3)") == "42");

    // stand, mount
    CHECK(rig.ok("return tostring(stored:IsStandState())") == "true");
    rig.ok("stored:SetStandState(3)");
    CHECK(state.standState == 3);
    CHECK(rig.ok("return tostring(stored:IsStandState())") == "false");
    CHECK(rig.ok("return tostring(stored:IsMounted())") == "false");
    rig.ok("stored:Mount(14337)");
    CHECK(rig.ok("return tostring(stored:IsMounted()) .. ',' .. stored:GetMountId()") == "true,14337");
    rig.ok("stored:Dismount()");
    CHECK_FALSE(state.mounted);

    // unit states and flags
    rig.ok("stored:AddUnitState(1024)");
    CHECK(rig.ok("return tostring(stored:HasUnitState(1024))") == "true");
    rig.ok("stored:ClearUnitState(1024)");
    CHECK(rig.ok("return tostring(stored:HasUnitState(1024))") == "false");
    rig.ok("stored:SetRooted(true)");
    CHECK(rig.ok("return tostring(stored:IsRooted())") == "true");
    FakeHost::FakeUnit& mobState = rig.host.unitState(mob);
    state.stopped = true;
    state.casting = true;
    mobState.inWater = true;
    mobState.underWater = true;
    state.pvp = true;
    state.attackingPlayer = true;
    state.charmed = true;
    state.vendor = true;
    state.questGiver = true;
    state.trainer = true;
    state.banker = true;
    state.innkeeper = true;
    state.gossip = true;
    state.spiritHealer = true;
    state.serviceProvider = true;
    CHECK(rig.ok("return tostring(stored:IsStopped()) .. tostring(stored:IsCasting())") == "truetrue");
    // IsInWater/IsUnderWater are declared on Unit but a Player has its own (the Player round); the creature uses Unit's.
    CHECK(rig.ok("return tostring(mob:IsInWater()) .. tostring(mob:IsUnderWater())") == "truetrue");
    CHECK(rig.ok("return tostring(stored:IsPvPFlagged()) .. tostring(stored:IsAttackingPlayer()) .. "
                 "tostring(stored:IsCharmed())") == "truetruetrue");
    CHECK(rig.ok("return tostring(stored:IsVendor()) .. tostring(stored:IsQuestGiver()) .. "
                 "tostring(stored:IsTrainer()) .. tostring(stored:IsBanker())") == "truetruetruetrue");
    CHECK(rig.ok("return tostring(stored:IsInnkeeper()) .. tostring(stored:IsGossip()) .. "
                 "tostring(stored:IsSpiritHealer()) .. tostring(stored:IsServiceProvider())") == "truetruetruetrue");
    state.health = 1500;
    CHECK(rig.ok("return tostring(stored:IsFullHealth())") == "true");
    state.health = 100;
    CHECK(rig.ok("return tostring(stored:IsFullHealth())") == "false");

    // speed
    state.speed[1] = 7.0;
    CHECK(rig.ok("return stored:GetSpeed(1)") == "7.0");
    rig.ok("stored:SetSpeed(1, 1.5, true)");
    CHECK(state.speedRate[1] == 1.5);
    REQUIRE(state.forcedSpeed.size() == 1);
    CHECK(state.forcedSpeed[0] == 1);
    rig.ok("stored:SetConfused(true)");
    rig.ok("stored:SetFeared(true)");
    rig.ok("stored:SetStunned(true)");
    CHECK(state.controlled["unit.setConfused"]);
    CHECK(state.controlled["unit.setFeared"]);
    CHECK(state.controlled["unit.setStunned"]);

    // movement: the op and its arguments reach the host; the simple setters round-trip
    std::size_t const movesFrom = state.actions.size();
    rig.ok("stored:MoveRandom(10)");
    rig.ok("stored:MoveChase(mob, 5, 0.5)");
    rig.ok("stored:MoveChase(mob)");   // no distance and no angle stay absent (the core's own chase)
    rig.ok("stored:MoveConfused()");
    rig.ok("stored:MoveFleeing(mob, 2000)");
    rig.ok("stored:MoveIdle()");
    rig.ok("stored:MoveJump(1, 2, 3, 4, 5, 6)");
    rig.ok("stored:MoveExpire(false)");
    rig.ok("stored:MoveClear(true)");
    REQUIRE(state.actions.size() == movesFrom + 9);
    CHECK(state.actions[movesFrom].first == "unit.moveRandom");
    CHECK(std::get<double>(state.actions[movesFrom].second[0].v) == 10.0);
    CHECK(state.actions[movesFrom + 1].first == "unit.moveChase");
    CHECK(std::get<fl::HandleData>(state.actions[movesFrom + 1].second[0].v).guid.lo == mob.guid.lo);
    CHECK(std::get<double>(state.actions[movesFrom + 1].second[1].v) == 5.0);
    CHECK(std::get<double>(state.actions[movesFrom + 1].second[2].v) == 0.5);
    CHECK(state.actions[movesFrom + 2].first == "unit.moveChase");
    CHECK(std::holds_alternative<std::monostate>(state.actions[movesFrom + 2].second[1].v));
    CHECK(std::holds_alternative<std::monostate>(state.actions[movesFrom + 2].second[2].v));
    CHECK(state.actions[movesFrom + 6].first == "unit.moveJump");
    REQUIRE(state.actions[movesFrom + 6].second.size() == 6);
    CHECK(std::get<int64_t>(state.actions[movesFrom + 6].second[5].v) == 6);
    rig.ok("stored:NearTeleport(1.5, 2.5, 3.5, 4.5)");
    CHECK(state.teleported);
    CHECK(state.teleportX == 1.5);
    CHECK(state.teleportO == 4.5);
    state.movementType = 5;
    CHECK(rig.ok("return stored:GetMovementType()") == "5");
    rig.ok("stored:SetFacing(1.57)");   // radians, not an integer
    CHECK(state.facing == 1.57);
    rig.ok("stored:SetFacing(2)");
    CHECK(state.facing == 2.0);
    rig.ok("stored:SetFacingToObject(mob)");
    CHECK(state.facingObject);
    CHECK(state.facingObjectLow == mob.guid.lo);

    // combat and healing
    state.health = 400;
    CHECK(rig.ok("return tostring(stored:HealthBelowPct(50))") == "true");
    CHECK(rig.ok("return tostring(stored:HealthAbovePct(50))") == "false");
    state.inCombat = true;
    rig.ok("stored:ClearInCombat()");
    CHECK_FALSE(state.inCombat);
    rig.ok("stored:SetInCombatWith(mob)");
    CHECK(state.inCombat);
    CHECK(state.combatWithLow == mob.guid.lo);
    int64_t const mobHealth = rig.host.unitState(mob).health;
    rig.ok("stored:DealHeal(mob, 1, 100, true)");
    REQUIRE(state.heals.size() == 1);
    CHECK(state.heals[0].targetLow == mob.guid.lo);
    CHECK(state.heals[0].amount == 100);
    CHECK(state.heals[0].critical);
    CHECK(rig.host.unitState(mob).health == mobHealth + 100);
    rig.ok("stored:StopSpellCast(77)");
    CHECK(state.hasStoppedSpell);
    CHECK(state.stoppedSpell == 77);
    rig.ok("stored:InterruptSpell(2, false)");
    CHECK(state.interruptedType == 2);
    CHECK_FALSE(state.interruptDelayed);
    state.hasCurrentSpell = true;
    state.currentSpellId = 55;
    CHECK(rig.ok("return tostring(stored:GetCurrentSpell(1) ~= nil)") == "true");

    // auras
    state.auraSpell = 123;
    state.ownedAuraSpell = 456;
    CHECK(rig.ok("return tostring(stored:GetAura(123) ~= nil)") == "true");
    CHECK(rig.ok("return tostring(stored:GetAura(5) ~= nil)") == "false");
    CHECK(rig.ok("return tostring(stored:GetOwnedAura(456) ~= nil)") == "true");
    rig.ok("stored:RemoveAllAuras()");
    rig.ok("stored:RemoveArenaAuras()");
    CHECK(state.removeAllAuras == 1);
    CHECK(state.removeArenaAuras == 1);

    // ownership and relations
    state.charmerGuid = "000000000000000100000000000000aa";
    state.ownerGuid = "000000000000000100000000000000bb";
    state.controllerGuid = "000000000000000100000000000000cc";
    state.creatorGuid = "000000000000000100000000000000dd";
    state.petGuid = "000000000000000100000000000000ee";
    CHECK(rig.ok("return stored:GetCharmerGUID()") == state.charmerGuid);
    CHECK(rig.ok("return stored:GetOwnerGUID()") == state.ownerGuid);
    CHECK(rig.ok("return stored:GetControllerGUID()") == state.controllerGuid);
    CHECK(rig.ok("return stored:GetCreatorGUID()") == state.creatorGuid);
    CHECK(rig.ok("return stored:GetPetGUID()") == state.petGuid);
    std::string const ownGuid = rig.ok("return stored:GetGUID()");
    rig.ok("stored:SetOwnerGUID('" + ownGuid + "')");
    CHECK(state.ownerGuid == ownGuid);
    state.friendlyUnits = {mob};
    state.unfriendlyUnits = {p};
    CHECK(rig.ok("return #stored:GetFriendlyUnitsInRange(30)") == "1");
    CHECK(state.listRange == 30);
    CHECK(rig.ok("return #stored:GetUnfriendlyUnitsInRange()") == "1");
    CHECK(state.listRange == 533.333);

    // display and identity
    state.displayId = 99;
    state.nativeDisplayId = 98;
    state.creatureType = 3;
    state.classMask = 1;
    state.raceMask = 1024;
    CHECK(rig.ok("return stored:GetDisplayId() .. ',' .. stored:GetNativeDisplayId()") == "99,98");
    rig.ok("stored:SetNativeDisplayId(150)");
    CHECK(state.nativeDisplayId == 150);
    CHECK(state.displayId == 150);
    CHECK(rig.ok("return stored:GetNativeDisplayId()") == "150");
    CHECK(rig.ok("return stored:GetCreatureType()") == "3");
    state.classAsString = "Warrior";
    state.raceAsString = "Human";
    CHECK(rig.ok("return stored:GetClassAsString(2)") == "Warrior");
    CHECK(state.classLocale == 2);
    CHECK(rig.ok("return stored:GetRaceAsString()") == "Human");
    CHECK(state.raceLocale == 0);
    CHECK(rig.ok("return stored:GetClassMask() .. ',' .. stored:GetRaceMask()") == "1,1024");
    rig.ok("stored:SendChatMessageToPlayer(1, 7, 'hello', stored)");
    REQUIRE(state.chats.size() == 1);
    CHECK(state.chats[0].type == 1);
    CHECK(state.chats[0].lang == 7);
    CHECK(state.chats[0].msg == "hello");
    CHECK(state.chats[0].targetLow == p.guid.lo);
    rig.ok("stored:PerformEmote(4)");
    CHECK(state.emote == 4);
    rig.ok("stored:EmoteState(5)");
    CHECK(state.emoteState == 5);

    // visibility, movement flags and the restore helpers
    mobState.moving = true;
    mobState.flying = true;
    state.visible = false;
    // IsMoving/IsFlying are declared on Unit but a Player has its own (the Player round); the creature uses Unit's.
    CHECK(rig.ok("return tostring(mob:IsMoving()) .. tostring(mob:IsFlying())") == "truetrue");
    CHECK(rig.ok("return tostring(stored:IsVisible())") == "false");
    rig.ok("stored:SetSheath(2)");
    CHECK(state.sheath == 2);
    rig.ok("stored:SetWaterWalk(true)");
    CHECK(state.waterWalk);
    rig.ok("stored:SetSanctuary(true)");
    CHECK(state.sanctuary);
    rig.ok("stored:DeMorph()");
    rig.ok("stored:RestoreDisplayId()");
    rig.ok("stored:RestoreFaction()");
    CHECK(state.deMorphs == 1);
    CHECK(state.restoreDisplayId == 1);
    CHECK(state.restoreFaction == 1);

    // a creature inherits the same methods
    CHECK(rig.ok("return tostring(mob:IsMounted())") == "false");

    // remaining Unit coverage methods: values flow both ways
    state.canModifyStats = false;
    state.dying = true;
    state.armorer = state.auctioneer = state.battleMaster = state.guildMaster = true;
    state.spiritGuide = state.spiritService = state.tabardDesigner = state.taxi = true;
    state.standState = 4;
    state.charmGuid = "00000000000000010000000000000111";
    state.controllerGuids = "00000000000000010000000000000222";
    state.critterGuid = "00000000000000010000000000000333";
    CHECK(rig.ok("return tostring(stored:CanModifyStats()) .. ',' .. tostring(stored:IsDying())") == "false,true");
    CHECK(rig.ok("return stored:GetStandState()") == "4");
    CHECK(rig.ok("return tostring(stored:IsArmorer()) .. tostring(stored:IsAuctioneer()) .. "
                 "tostring(stored:IsBattleMaster()) .. tostring(stored:IsGuildMaster())") == "truetruetruetrue");
    CHECK(rig.ok("return tostring(stored:IsSpiritGuide()) .. tostring(stored:IsSpiritService()) .. "
                 "tostring(stored:IsTabardDesigner()) .. tostring(stored:IsTaxi())") == "truetruetruetrue");
    CHECK(rig.ok("return stored:GetCharmGUID() .. ',' .. stored:GetControllerGUIDS() .. ',' .. stored:GetCritterGUID()") ==
          state.charmGuid + "," + state.controllerGuids + "," + state.critterGuid);

    state.health = 200;
    state.maxHealth = 1000;
    CHECK(rig.ok("return stored:CountPctFromCurHealth(25) .. ',' .. stored:CountPctFromMaxHealth(50)") == "50,500");
    CHECK(rig.ok("return stored:CountPctFromCurHealth() .. ',' .. stored:CountPctFromMaxHealth()") == "0,0");

    rig.ok("stored:AddFlatStatModifier(2, 1, 15.5, false)");
    rig.ok("stored:AddPctStatModifier(3, 0, 12.5)");
    REQUIRE(state.statMods.size() == 2);
    CHECK(state.statMods[0].mod == 2);
    CHECK(state.statMods[0].type == 1);
    CHECK(state.statMods[0].value == 15.5);
    CHECK_FALSE(state.statMods[0].apply);
    CHECK_FALSE(state.statMods[0].isPct);
    CHECK(state.statMods[1].mod == 3);
    CHECK(state.statMods[1].type == 0);
    CHECK(state.statMods[1].value == 12.5);
    CHECK(state.statMods[1].isPct);

    rig.ok("stored:CastSpellAoF(10.5, 20.5, 30.5, 999)");
    rig.ok("stored:CastSpellAoF(1, 2, 3, 888, false)");
    REQUIRE(state.aofCasts.size() == 2);
    CHECK(state.aofCasts[0].spell == 999);
    CHECK(state.aofCasts[0].triggered);
    CHECK(state.aofCasts[1].spell == 888);
    CHECK_FALSE(state.aofCasts[1].triggered);

    rig.ok("mob:DisableMelee(true)");
    CHECK(mobState.meleeDisabled);
    rig.ok("mob:DisableMelee(false)");
    CHECK_FALSE(mobState.meleeDisabled);
    {
        fl::CallResult r = rig.run("stored:DisableMelee(true)");
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("DisableMelee: only available on a creature") != std::string::npos);
    }

    state.accessiblePlace = false;
    CHECK(rig.ok("return tostring(stored:IsInAccessiblePlaceFor(mob, 5.0))") == "false");
    CHECK(state.accessibleObjLow == mob.guid.lo);
    CHECK(state.accessibleRadius == 5.0);

    rig.ok("stored:RemoveBindSightAuras()");
    rig.ok("stored:RemoveCharmAuras()");
    CHECK(state.removeBindSightAuras == 1);
    CHECK(state.removeCharmAuras == 1);

    rig.ok("stored:SendUnitEmote('yawns', mob, true)");
    REQUIRE(state.unitEmotes.size() == 1);
    CHECK(state.unitEmotes[0].msg == "yawns");
    CHECK(state.unitEmotes[0].receiverLow == mob.guid.lo);
    CHECK(state.unitEmotes[0].bossEmote);
    CHECK(rig.run("stored:SendUnitEmote(string.rep('a', 255))").ok);

    rig.ok("stored:SetCanFly(true)");
    CHECK(state.canFly);
    rig.ok("stored:SetCreatorGUID('" + state.charmerGuid + "')");
    CHECK(state.creatorGuid == state.charmerGuid);
    rig.ok("stored:SetCritterGUID('" + state.ownerGuid + "')");
    CHECK(state.critterGuid == state.ownerGuid);
    rig.ok("stored:SetPetGUID('" + state.controllerGuid + "')");
    CHECK(state.petGuid == state.controllerGuid);

    rig.ok("mob:SetFFA(true)");
    CHECK(mobState.ffa);
    rig.ok("stored:SetFFA(true)");
    CHECK(state.ffa);

    rig.ok("stored:SetImmuneTo(12)");
    CHECK(state.immuneMechanics.count(12) == 1);
    rig.ok("stored:SetImmuneTo(12, false)");
    CHECK(state.immuneMechanics.empty());

    // SetName is creature-only (Player::SaveToDB persists GetName() into characters.name) and capped at 64 bytes.
    rig.ok("mob:SetName('Renamed')");
    CHECK(mobState.name == "Renamed");
    CHECK(rig.run("mob:SetName(string.rep('a', 64))").ok);
    {
        fl::CallResult r = rig.run("stored:SetName('Renamed')");
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("SetName: only available on a creature") != std::string::npos);
    }
    rig.ok("stored:SetVisible(false)");
    CHECK_FALSE(state.visible);
    rig.ok("stored:SetVisible()");
    CHECK(state.visible);
}

TEST_CASE("eluna unit long tail: a bad argument raises before the host is called")
{
    UnitRig rig;
    fl::HandleData p = FakeHost::player(91);
    fl::HandleData mob = FakeHost::creature(92, 0, 0, 700);
    fl::ObjectInfo info;
    info.name = "Args";
    rig.host.add(p, info);
    info.entry = 700;
    rig.host.add(mob, info);
    storeHandles(rig, p, mob);

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };
    size_t const callsBefore = rig.host.gameCalls.size();

    CHECK(errorOf("stored:SetMaxHealth(-1)").find("maxHealth must be an integer 0..4294967295") !=
          std::string::npos);
    CHECK(errorOf("stored:GetStat(5)").find("statType must be an integer 0..4") != std::string::npos);
    CHECK(errorOf("stored:GetBaseSpellPower(7)").find("spellSchool must be an integer 0..6") !=
          std::string::npos);
    CHECK(errorOf("stored:SetStandState(256)").find("state must be an integer 0..255") != std::string::npos);
    CHECK(errorOf("stored:SetSheath(3)").find("sheathState must be an integer 0..2") != std::string::npos);
    CHECK(errorOf("stored:SendChatMessageToPlayer(300, 0, 'x', stored)").find("type must be an integer 0..255") !=
          std::string::npos);
    CHECK(errorOf("stored:MoveChase(5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetFacingToObject(5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetOwnerGUID(1.5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetSpeed(1, 'x')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetMaxPower(0, -1)").find("maxPower must be an integer 0..4294967295") !=
          std::string::npos);
    // review round (third review, 2026-11-24): a value the core would index, shift or clamp with must be refused
    // here, before the host. Locale: the core indexes its name table with it.
    for (char const* call : {"stored:GetClassAsString(99)", "stored:GetClassAsString(-1)", "stored:GetClassAsString(9)",
                             "stored:GetRaceAsString(99)", "stored:GetRaceAsString(-1)", "stored:GetRaceAsString(12)"})
    {
        INFO("call: " << call);
        CHECK(errorOf(call).find("locale must be a LocaleConstant") != std::string::npos);
    }
    CHECK(errorOf("stored:SetStandState(10)").find("state must be an integer 0..9") != std::string::npos);
    // UnitState: 0x2000 (deprecated), 0x80000 (follow formation) and 0x40000000 up are not supported states
    for (char const* call : {"stored:AddUnitState(0x80000000)", "stored:AddUnitState(0x2000)",
                             "stored:ClearUnitState(0x40000000)", "stored:ClearUnitState(0x80000)"})
    {
        INFO("call: " << call);
        CHECK(errorOf(call).find("not UnitState values") != std::string::npos);
    }
    // speeds: nine move types, a finite rate
    CHECK(errorOf("stored:GetSpeed(9)").find("type must be an integer 0..8") != std::string::npos);
    CHECK(errorOf("stored:SetSpeed(9, 1.0)").find("type must be an integer 0..8") != std::string::npos);
    CHECK(errorOf("stored:SetSpeed(1, 1/0)").find("rate must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:SetSpeed(1, 0/0)").find("rate must") != std::string::npos);
    // movement: no negative radius or arc height, nothing non-finite, an orientation is a number
    CHECK(errorOf("stored:MoveRandom(-1)").find("radius must not be negative") != std::string::npos);
    CHECK(errorOf("stored:MoveRandom(1/0)").find("radius must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:MoveJump(1, 2, 3, 4, -0.5)").find("maxHeight must not be negative") != std::string::npos);
    CHECK(errorOf("stored:MoveJump(1, 2, 3, 1/0, 5)").find("zSpeed must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:MoveJump(1e300, 2, 3, 4, 5)").find("x must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:MoveChase(mob, 1/0)").find("dist must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:SetFacing(1/0)").find("orientation must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:SetFacing('x')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:GetFriendlyUnitsInRange(1/0)").find("range must be a finite number") != std::string::npos);
    // remaining Unit coverage argument checks
    CHECK(errorOf("stored:AddFlatStatModifier(44, 0, 1)").find("statType must be an integer 0..43") != std::string::npos);
    CHECK(errorOf("stored:AddFlatStatModifier(0, 2, 1)").find("modType must be 0 or 1") != std::string::npos);
    CHECK(errorOf("stored:AddFlatStatModifier(0, 0, 1/0)").find("value must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:AddPctStatModifier(44, 0, 1)").find("statType must be an integer 0..43") != std::string::npos);
    CHECK(errorOf("stored:AddPctStatModifier(0, 2, 1)").find("modType must be 0 or 1") != std::string::npos);
    CHECK(errorOf("stored:AddPctStatModifier(0, 0, 0/0)").find("value must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:CastSpellAoF(1/0, 2, 3, 1)").find("x must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:CountPctFromCurHealth(1/0)").find("pct must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:CountPctFromMaxHealth(0/0)").find("pct must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:IsInAccessiblePlaceFor(5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:IsInAccessiblePlaceFor(mob, 1/0)").find("radius must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:SendUnitEmote('x', 5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SendUnitEmote(string.rep('a', 256))").find("255") != std::string::npos);
    CHECK(errorOf("mob:SetName(string.rep('a', 65))").find("64") != std::string::npos);
    CHECK(errorOf("stored:SetImmuneTo(-1)").find("immunity must be an integer 0..36") != std::string::npos);
    CHECK(errorOf("stored:SetImmuneTo(37)").find("immunity must be an integer 0..36") != std::string::npos);
    CHECK(errorOf("stored:SetCreatorGUID('bad')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetCritterGUID('bad')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetPetGUID('bad')").find("bad argument") != std::string::npos);
    CHECK(rig.host.gameCalls.size() == callsBefore);   // nothing reached the host
}

TEST_CASE("eluna unit long tail: review round, edge values that are accepted and the display id the host refuses")
{
    UnitRig rig;
    fl::HandleData p = FakeHost::player(101);
    fl::HandleData mob = FakeHost::creature(102, 0, 0, 800);
    fl::ObjectInfo info;
    info.name = "Review";
    rig.host.add(p, info);
    info.entry = 800;
    rig.host.add(mob, info);
    storeHandles(rig, p, mob);
    FakeHost::FakeUnit& state = rig.host.unitState(p);

    // the last valid values pass
    rig.ok("stored:SetStandState(9)");
    CHECK(state.standState == 9);
    rig.ok("stored:AddUnitState(0x20000000)");
    CHECK(rig.ok("return tostring(stored:HasUnitState(0x20000000))") == "true");
    rig.ok("stored:ClearUnitState(0x20000000)");
    for (int locale : {0, 8, 10, 11})
    {
        rig.ok("stored:GetClassAsString(" + std::to_string(locale) + ")");
        CHECK(state.classLocale == locale);
    }
    rig.ok("stored:SetSpeed(8, 2.5)");
    CHECK(state.speedRate[8] == 2.5);
    rig.ok("stored:MoveJump(1, 2, 3, 4, 0.2)");   // a maximum below the core's 0.5 minimum reaches the host unchanged
    CHECK(std::get<double>(state.actions.back().second[4].v) == 0.2);
    rig.ok("stored:MoveRandom(0)");
    rig.ok("stored:MoveIdle()");

    // SetNativeDisplayId: the host answers false for an id its display tables do not hold, which raises
    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };
    state.nativeDisplayId = 98;
    CHECK(errorOf("stored:SetNativeDisplayId(0)").find("not a creature display") != std::string::npos);
    CHECK(errorOf("stored:SetNativeDisplayId(999999)").find("not a creature display") != std::string::npos);
    CHECK(state.nativeDisplayId == 98);   // refused, nothing changed
    rig.ok("stored:SetNativeDisplayId(150)");
    CHECK(state.nativeDisplayId == 150);
}
