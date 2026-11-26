// E5 Creature long tail, round 1: the most used documented Creature methods (docs Creature/index.html). The fake
// world carries a small creature state (FakeHost::FakeCreature) so a value can be written through one method and read
// back through another; every method is also called on a gone creature, which must raise the family's stale-handle
// error, and with a bad argument, which must raise before anything reaches the host.
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

struct CreatureRig : Rig
{
    CreatureRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

// Stores a player and a creature in the Lua state through their own events, so the methods can take them as arguments.
void storeHandles(CreatureRig& rig, fl::HandleData const& p, fl::HandleData const& mob)
{
    rig.ok("RegisterPlayerEvent(3, function(ev, player) stored = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    rig.ok("RegisterCreatureEvent(1, 5, function(ev, creature) mob = creature end)");
    rig.e().emitEluna(fl::EluKind::Creature, 1, 5, {fl::EventValue(mob)});
}

// The fake creature a test starts from: a known waypoint path, an entry UpdateEntry knows, a known spell and one known
// item entry, so the "happy path" loop below never hits a deliberate refusal.
void prepare(FakeHost& host, fl::HandleData const& mob)
{
    FakeHost::FakeCreature& c = host.creatureState(mob);
    c.waypointPath = 7;
    c.knownPaths = {7};
    c.knownEntries = {600};
    c.knownDataLow = {1234};
    c.knownItems = {77};
    c.serverSpells = {1};
    c.aiName = "AggressorAI";
    c.hasTemplate = true;
}

// Every method of the round, in the call form the docs give (the receiver is the stored creature).
std::vector<std::string> const& roundCalls()
{
    static std::vector<std::string> const calls = {
        // AI and combat
        "AttackStart(stored)",
        "SelectVictim()",
        "CallAssistance()",
        "CallForHelp(20)",
        "FleeToGetAssistance()",
        "SetNoCallAssistance(true)",
        "SetNoSearchAssistance(true)",
        "HasSearchedAssistance()",
        "IsInEvadeMode()",
        "SetInCombatWithZone()",
        "GetAggroRange(stored)",
        "GetAttackDistance(stored)",
        "CanAssistTo(stored, stored)",
        "CanStartAttack(stored)",
        "IsTargetableForAttack()",
        "IsTappedBy(stored)",
        "HasLootRecipient()",
        "GetLootRecipient()",
        "GetLootRecipientGroup()",
        // threat
        "AddThreat(stored, 10)",
        "GetThreat(stored)",
        "ClearThreat(stored)",
        "ClearThreatList()",
        "ResetAllThreat()",
        "FixateTarget(stored)",
        "ClearFixate()",
        "GetAITargets()",
        "GetAITargetsCount()",
        // state
        "IsCivilian()",
        "IsGuard()",
        "IsRacialLeader()",
        "IsTrigger()",
        "IsDungeonBoss()",
        "IsRegeneratingHealth()",
        "SetRegeneratingHealth(false)",
        "CanFly()",
        "CanSwim()",
        "SetHover(true)",
        "SetDisableGravity(true)",
        "IsReputationGainDisabled()",
        "SetDisableReputationGain(true)",
        "GetShieldBlockValue()",
        "SetWalk(true)",
        // spawn and movement
        "GetWaypointPath()",
        "GetCurrentWaypointId()",
        "MoveWaypoint()",
        "GetDefaultMovementType()",
        "SetDefaultMovementType(0)",
        "GetWanderRadius()",
        "SetWanderRadius(5)",
        "SetHomePosition(1, 2, 3, 4)",
        "GetRespawnDelay()",
        "GetCorpseDelay()",
        "RemoveCorpse()",
        "SetDeathState(0)",
        "SetEquipmentSlots(0, 0, 0)",
        // identity
        "GetCreatureFamily()",
        "GetNPCFlags()",
        "SetNPCFlags(1)",
        "GetExtraFlags()",
        "GetRank()",
        "GetDBTableGUIDLow()",
        "GetAIName()",
        "GetScriptId()",
        "UpdateEntry(600)",
        "UpdateEntry(600, 1234)",
        // spells
        "HasSpell(1)",
        "HasSpellCooldown(1)",
        "HasCategoryCooldown(1)",
        "GetCreatureSpellCooldownDelay(1)",
        // loot mode and quests
        "GetLootMode()",
        "HasLootMode(1)",
        "SetLootMode(1)",
        "AddLootMode(2)",
        "RemoveLootMode(2)",
        "ResetLootMode()",
        "HasQuest(100)",
        "CanCompleteQuest(100)",
        "RemoveFromWorld()",
    };
    return calls;
}
}  // namespace

TEST_CASE("eluna creature long tail: every method of the round reaches the host and a gone creature raises")
{
    CreatureRig rig;
    fl::HandleData p = FakeHost::player(71);
    fl::HandleData mob = FakeHost::creature(72, 0, 0, 600);
    fl::ObjectInfo info;
    info.name = "Longtail";
    rig.host.add(p, info);
    info.entry = 600;
    rig.host.add(mob, info);
    prepare(rig.host, mob);
    storeHandles(rig, p, mob);

    for (std::string const& call : roundCalls())
    {
        INFO("call: mob:" << call);
        CHECK(rig.run("mob:" + call).ok);
    }

    // The core writes a spawn through a blocking WorldDatabase transaction: SaveToDB raises instead.
    fl::CallResult save = rig.run("mob:SaveToDB()");
    REQUIRE_FALSE(save.ok);
    CHECK(save.error.find("not available on this server") != std::string::npos);

    // The family's stale-handle rule: the same calls on a gone creature raise before anything reaches the host.
    rig.host.remove(mob);
    for (std::string const& call : roundCalls())
    {
        INFO("call: mob:" << call);
        fl::CallResult r = rig.run("mob:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Creature no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna creature long tail: the values flow both ways through the fake creature state")
{
    CreatureRig rig;
    fl::HandleData p = FakeHost::player(81);
    fl::HandleData mob = FakeHost::creature(82, 0, 0, 610);
    fl::ObjectInfo info;
    info.name = "Values";
    rig.host.add(p, info);
    info.entry = 610;
    rig.host.add(mob, info);
    prepare(rig.host, mob);
    storeHandles(rig, p, mob);
    FakeHost::FakeCreature& c = rig.host.creatureState(mob);

    // AI and combat: the op reaches the host with its arguments
    rig.ok("mob:AttackStart(stored)");
    CHECK(c.attackStarts == 1);
    CHECK(c.attackStartTargetLow == p.guid.lo);
    CHECK(rig.ok("return tostring(mob:SelectVictim())") == "nil");   // no victim: nil
    c.victimLow = p.guid.lo;
    CHECK(rig.ok("return tostring(mob:SelectVictim() ~= nil)") == "true");   // the selected unit is the answer
    rig.ok("mob:CallAssistance()");
    CHECK(c.selectVictims == 2);
    CHECK(c.callAssistances == 1);
    rig.ok("mob:CallForHelp(12.5)");
    CHECK(c.callForHelps == 1);
    CHECK(c.callForHelpRadius == 12.5);
    rig.ok("mob:FleeToGetAssistance()");
    CHECK(c.flees == 1);
    rig.ok("mob:SetNoCallAssistance(false)");
    CHECK_FALSE(c.noCallAssistance);
    rig.ok("mob:SetNoSearchAssistance(true)");
    CHECK(c.noSearchAssistance);
    c.searchedAssistance = true;
    CHECK(rig.ok("return tostring(mob:HasSearchedAssistance())") == "true");
    c.evadeMode = true;
    CHECK(rig.ok("return tostring(mob:IsInEvadeMode())") == "true");
    rig.ok("mob:SetInCombatWithZone()");
    CHECK(c.zoneCombats == 1);
    c.aggroRange = 20.5;
    c.attackDistance = 4.0;
    CHECK(rig.ok("return mob:GetAggroRange(stored)") == "20.5");
    CHECK(rig.ok("return mob:GetAttackDistance(stored)") == "4.0");
    CHECK(c.rangeTargetLow == p.guid.lo);
    c.canAssist = true;
    CHECK(rig.ok("return tostring(mob:CanAssistTo(stored, stored, false))") == "true");
    CHECK(c.assistFriendLow == p.guid.lo);
    CHECK_FALSE(c.assistCheckFaction);
    c.canStartAttack = true;
    CHECK(rig.ok("return tostring(mob:CanStartAttack(stored, false))") == "true");
    CHECK_FALSE(c.canStartAttackForce);
    c.targetable = false;
    CHECK(rig.ok("return tostring(mob:IsTargetableForAttack())") == "false");
    c.targetable = true;
    CHECK(rig.ok("return tostring(mob:IsTargetableForAttack(false))") == "true");
    CHECK_FALSE(c.targetableCheckFakeDeath);   // the optional flag reaches the core call
    c.tapped = true;
    CHECK(rig.ok("return tostring(mob:IsTappedBy(stored))") == "true");
    CHECK(c.lastTappedLow == p.guid.lo);

    // loot recipient: the first player of the tap list, and that player's group
    CHECK(rig.ok("return tostring(mob:HasLootRecipient())") == "false");
    c.tapList = {p.guid.lo};
    c.tapGroups = {555};
    CHECK(rig.ok("return tostring(mob:HasLootRecipient())") == "true");
    rig.ok("rec = mob:GetLootRecipient()");
    CHECK(rig.ok("return tostring(rec ~= nil)") == "true");
    CHECK(rig.ok("return rec:GetGUIDLow()") == std::to_string(p.guid.lo));
    CHECK(rig.ok("return tostring(mob:GetLootRecipientGroup() ~= nil)") == "true");

    // threat
    rig.ok("mob:AddThreat(stored, 100, 0, 1)");   // spell 1 is one the fake server knows
    CHECK(c.threat[p.guid.lo] == 100.0);
    CHECK(rig.ok("return mob:GetThreat(stored)") == "100.0");
    c.threatOrder = {p.guid.lo};
    CHECK(rig.ok("return tostring(#mob:GetAITargets())") == "1");
    CHECK(rig.ok("return mob:GetAITargetsCount()") == "1");
    rig.ok("mob:FixateTarget(stored)");
    CHECK(c.fixateLow == p.guid.lo);
    rig.ok("mob:ClearFixate()");
    CHECK(c.clearFixates == 1);
    CHECK(c.fixateLow == 0);
    rig.ok("mob:ResetAllThreat()");
    CHECK(c.resetAllThreats == 1);
    CHECK(rig.ok("return mob:GetThreat(stored)") == "0.0");
    rig.ok("mob:ClearThreat(stored)");
    CHECK(c.threat.empty());
    rig.ok("mob:AddThreat(stored, 5)");
    rig.ok("mob:ClearThreatList()");
    CHECK(c.clearThreatLists == 1);
    CHECK(c.threat.empty());

    // state flags
    c.civilian = c.guard = c.racialLeader = c.trigger = c.dungeonBoss = true;
    CHECK(rig.ok("return tostring(mob:IsCivilian()) .. tostring(mob:IsGuard()) .. "
                 "tostring(mob:IsRacialLeader()) .. tostring(mob:IsTrigger()) .. tostring(mob:IsDungeonBoss())") ==
          "truetruetruetruetrue");
    CHECK(rig.ok("return tostring(mob:IsRegeneratingHealth())") == "true");
    rig.ok("mob:SetRegeneratingHealth(false)");
    CHECK_FALSE(c.regenerating);
    CHECK(rig.ok("return tostring(mob:IsRegeneratingHealth())") == "false");
    c.canFly = c.canSwim = c.reputationDisabled = true;
    CHECK(rig.ok("return tostring(mob:CanFly()) .. tostring(mob:CanSwim()) .. "
                 "tostring(mob:IsReputationGainDisabled())") == "truetruetrue");
    rig.ok("mob:SetDisableReputationGain(false)");
    CHECK_FALSE(c.reputationDisabled);
    rig.ok("mob:SetHover(true)");
    rig.ok("mob:SetDisableGravity(true)");
    CHECK(c.hover);
    CHECK(c.disableGravity);
    rig.ok("mob:SetHover(false)");
    rig.ok("mob:SetDisableGravity(false)");
    CHECK_FALSE(c.hover);
    CHECK_FALSE(c.disableGravity);
    rig.ok("mob:SetWalk(true)");
    CHECK(c.walk);
    c.shieldBlockValue = 42;
    CHECK(rig.ok("return mob:GetShieldBlockValue()") == "42");

    // spawn and movement
    CHECK(rig.ok("return mob:GetWaypointPath()") == "7");
    c.currentWaypoint = 3;
    CHECK(rig.ok("return mob:GetCurrentWaypointId()") == "3");
    rig.ok("mob:MoveWaypoint()");
    CHECK(c.moveWaypoints == 1);
    rig.ok("mob:SetDefaultMovementType(2)");
    CHECK(rig.ok("return mob:GetDefaultMovementType()") == "2");
    rig.ok("mob:SetWanderRadius(12.5)");
    CHECK(c.wanderRadius == 12.5);
    CHECK(rig.ok("return mob:GetWanderRadius()") == "12.5");
    rig.ok("mob:SetHomePosition(1.5, -2.5, 3.5, 4.5)");
    CHECK(c.homeX == 1.5);
    CHECK(c.homeY == -2.5);
    CHECK(c.homeZ == 3.5);
    CHECK(c.homeO == 4.5);
    c.respawnDelay = 300;
    c.corpseDelay = 60;
    CHECK(rig.ok("return mob:GetRespawnDelay() .. ',' .. mob:GetCorpseDelay()") == "300,60");
    rig.ok("mob:RemoveCorpse()");
    CHECK(c.corpseRemovals == 1);
    rig.ok("mob:RemoveFromWorld()");
    CHECK(c.removeFromWorlds == 1);
    rig.ok("mob:SetDeathState(1)");
    CHECK(c.deathState == 1);
    rig.ok("mob:SetDeathState(3)");
    CHECK(c.deathState == 3);

    // identity and gear
    rig.ok("mob:SetEquipmentSlots(77, 0, 0)");
    CHECK(c.equipMain == 77);
    CHECK(c.equipOff == 0);
    c.family = 1;
    c.extraFlags = 2;
    c.rank = 1;
    c.npcFlags = 0x80;
    c.dbGuidLow = 4242;
    c.scriptId = 55;
    CHECK(rig.ok("return mob:GetCreatureFamily() .. ',' .. mob:GetExtraFlags() .. ',' .. mob:GetRank()") == "1,2,1");
    CHECK(rig.ok("return mob:GetNPCFlags()") == "128");
    rig.ok("mob:SetNPCFlags(16)");
    CHECK(c.npcFlags == 16);
    CHECK(rig.ok("return mob:GetDBTableGUIDLow() .. ',' .. mob:GetScriptId()") == "4242,55");
    CHECK(rig.ok("return mob:GetAIName()") == "AggressorAI");
    rig.ok("mob:UpdateEntry(600, 1234)");
    CHECK(c.entry == 600);

    // spells: the creature's own spell list and the core's spell history
    c.spells = {200};
    c.spellCooldowns = {201};
    c.categoryCooldowns = {202};
    c.spellCooldownDelay[203] = 1500;
    c.serverSpells = {200, 201, 202, 203};
    CHECK(rig.ok("return tostring(mob:HasSpell(200)) .. tostring(mob:HasSpell(999))") == "truefalse");
    CHECK(rig.ok("return tostring(mob:HasSpellCooldown(201)) .. tostring(mob:HasSpellCooldown(200))") == "truefalse");
    CHECK(rig.ok("return tostring(mob:HasCategoryCooldown(202)) .. "
                 "tostring(mob:HasCategoryCooldown(200))") == "truefalse");
    CHECK(rig.ok("return mob:GetCreatureSpellCooldownDelay(203)") == "1500");

    // loot mode and quests
    CHECK(rig.ok("return mob:GetLootMode()") == "1");
    CHECK(rig.ok("return tostring(mob:HasLootMode(1))") == "true");
    rig.ok("mob:AddLootMode(2)");
    CHECK(c.lootMode == 3);
    rig.ok("mob:RemoveLootMode(2)");
    CHECK(c.lootMode == 1);
    rig.ok("mob:SetLootMode(8)");
    CHECK(c.lootMode == 8);
    rig.ok("mob:ResetLootMode()");
    CHECK(c.lootMode == 1);
    c.quests = {100};
    CHECK(rig.ok("return tostring(mob:HasQuest(100)) .. tostring(mob:HasQuest(101))") == "truefalse");
    // the end relation is separate from the starter relation
    c.involvedQuests = {102};
    CHECK(rig.ok("return tostring(mob:CanCompleteQuest(102)) .. tostring(mob:CanCompleteQuest(100))") == "truefalse");
}

TEST_CASE("eluna creature: RemoveFromWorld refuses a creature a player owns")
{
    CreatureRig rig;
    fl::HandleData mob = FakeHost::creature(112, 0, 0, 900);
    fl::HandleData pet = FakeHost::pet(113, 0, 0, 299);
    fl::ObjectInfo mobInfo, petInfo;
    mobInfo.name = "Plain";
    petInfo.name = "Wolf";
    petInfo.entry = 299;
    rig.host.add(mob, mobInfo);
    rig.host.add(pet, petInfo);
    prepare(rig.host, mob);
    rig.host.creatureState(pet).isPet = true;

    rig.ok("RegisterCreatureEvent(1, 5, function(ev, c) mob = c end)");
    rig.ok("RegisterCreatureEvent(299, 5, function(ev, c) pet = c end)");
    rig.e().emitEluna(fl::EluKind::Creature, 1, 5, {fl::EventValue(mob)});
    rig.e().emitEluna(fl::EluKind::Creature, 299, 5, {fl::EventValue(pet)});

    // A pet handle is refused; nothing is queued for removal and the handle's world object data survives.
    rig.ok("pet:Data().kept = true");
    fl::CallResult petCall = rig.run("pet:RemoveFromWorld()");
    REQUIRE_FALSE(petCall.ok);
    CHECK(petCall.error.find("RemoveFromWorld: a creature owned by a player") != std::string::npos);
    CHECK(rig.host.creatureState(pet).removeFromWorlds == 0);
    CHECK(rig.ok("return tostring(pet:Data().kept)") == "true");

    // A creature a player controls without a pet guid (a guardian, minion or charm) is refused the same way.
    rig.host.creatureState(mob).isPet = true;
    fl::CallResult ownedCall = rig.run("mob:RemoveFromWorld()");
    REQUIRE_FALSE(ownedCall.ok);
    CHECK(ownedCall.error.find("RemoveFromWorld: a creature owned by a player") != std::string::npos);
    CHECK(rig.host.creatureState(mob).removeFromWorlds == 0);
    rig.host.creatureState(mob).isPet = false;

    // An ordinary world creature is still removed and its data pruned.
    rig.ok("mob:Data().kept = true");
    rig.ok("mob:RemoveFromWorld()");
    CHECK(rig.host.creatureState(mob).removeFromWorlds == 1);
    rig.host.add(mob, mobInfo);
    CHECK(rig.ok("return tostring(mob:Data().kept)") == "nil");
}

TEST_CASE("eluna creature long tail: a bad argument raises before the host is called")
{
    CreatureRig rig;
    fl::HandleData p = FakeHost::player(91);
    fl::HandleData mob = FakeHost::creature(92, 0, 0, 700);
    fl::ObjectInfo info;
    info.name = "Args";
    rig.host.add(p, info);
    info.entry = 700;
    rig.host.add(mob, info);
    prepare(rig.host, mob);
    storeHandles(rig, p, mob);

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };
    size_t const callsBefore = rig.host.gameCalls.size();

    // radii: no negative, no NaN or infinity, nothing past the core's maximum range
    CHECK(errorOf("mob:CallForHelp(-1)").find("radius must not be negative") != std::string::npos);
    CHECK(errorOf("mob:CallForHelp(1/0)").find("radius must be a finite number") != std::string::npos);
    CHECK(errorOf("mob:CallForHelp(1000)").find("radius must be at most") != std::string::npos);
    CHECK(errorOf("mob:SetWanderRadius(-0.5)").find("distance must not be negative") != std::string::npos);
    CHECK(errorOf("mob:SetWanderRadius(0/0)").find("distance must be a finite number") != std::string::npos);
    CHECK(errorOf("mob:SetHomePosition(1, 2, 3, 1e300)").find("o must be a finite number") != std::string::npos);
    CHECK(errorOf("mob:SetHomePosition(20000, 2, 3, 0)").find("x must be within") != std::string::npos);
    CHECK(errorOf("mob:SetHomePosition(1, -17067, 3, 0)").find("y must be within") != std::string::npos);
    CHECK(errorOf("mob:SetHomePosition(1, 2, 1e9, 0)").find("z must be within") != std::string::npos);
    // threat: a number, and a school mask that is really a mask
    CHECK(errorOf("mob:AddThreat(stored, 0/0)").find("threat must be a finite number") != std::string::npos);
    CHECK(errorOf("mob:AddThreat(stored, 10, 128)").find("schoolMask has bits that are not") != std::string::npos);
    CHECK(errorOf("mob:AddThreat(stored, 10, 0, 4294967297)").find("spell must be an integer 0..4294967295") !=
          std::string::npos);   // would wrap to the valid spell 1 in the core's 32-bit id
    // enums and indices
    CHECK(errorOf("mob:SetDefaultMovementType(3)").find("type must be an integer 0..2") != std::string::npos);
    CHECK(errorOf("mob:SetDeathState(5)").find("deathState must be an integer 0..4") != std::string::npos);
    CHECK(errorOf("mob:SetNPCFlags(-1)").find("flags must be an integer 0..4294967295") != std::string::npos);
    CHECK(errorOf("mob:SetLootMode(65536)").find("lootMode must be an integer 0..65535") != std::string::npos);
    CHECK(errorOf("mob:AddLootMode(-1)").find("lootMode must be an integer 0..65535") != std::string::npos);
    CHECK(errorOf("mob:SetDisableGravity()").find("bad argument") != std::string::npos);
    CHECK(errorOf("mob:CanCompleteQuest(-1)").find("questId must be an integer 0..4294967295") != std::string::npos);
    // a handle of the wrong kind
    CHECK(errorOf("mob:AttackStart(5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("mob:CanAssistTo(5, stored)").find("bad argument") != std::string::npos);
    CHECK(rig.host.gameCalls.size() == callsBefore);   // nothing reached the host
}

TEST_CASE("eluna creature long tail: the host's refusals raise with the method's own message")
{
    CreatureRig rig;
    fl::HandleData p = FakeHost::player(101);
    fl::HandleData mob = FakeHost::creature(102, 0, 0, 800);
    fl::ObjectInfo info;
    info.name = "Refusals";
    rig.host.add(p, info);
    info.entry = 800;
    rig.host.add(mob, info);
    prepare(rig.host, mob);
    storeHandles(rig, p, mob);
    FakeHost::FakeCreature& c = rig.host.creatureState(mob);

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };

    c.hasAi = false;
    CHECK(errorOf("mob:AttackStart(stored)").find("AttackStart: the creature has no AI") != std::string::npos);
    CHECK(errorOf("mob:SelectVictim()").find("SelectVictim: the creature has no AI") != std::string::npos);
    c.hasAi = true;
    // creature-AI-only calls on a pet or a player-controlled creature
    c.isPet = true;
    CHECK(errorOf("mob:FleeToGetAssistance()").find("is a pet or is controlled by a player") != std::string::npos);
    CHECK(errorOf("mob:SetInCombatWithZone()").find("is a pet or is controlled by a player") != std::string::npos);
    CHECK(errorOf("mob:UpdateEntry(600)").find("or the creature is a pet") != std::string::npos);
    c.isPet = false;
    // a setter the getter would not read back, and an ALIVE state on a dead creature
    c.regenForbidden = true;
    CHECK(errorOf("mob:SetRegeneratingHealth(true)").find("template does not let it regenerate health") !=
          std::string::npos);
    rig.ok("mob:SetRegeneratingHealth(false)");
    c.regenForbidden = false;
    c.healthZero = true;
    CHECK(errorOf("mob:SetDeathState(0)").find("use 4, JUST_RESPAWNED") != std::string::npos);
    rig.ok("mob:SetDeathState(4)");
    c.healthZero = false;
    // CORPSE / DEAD on a living creature only store the state (full health, no corpse timer)
    c.deathState = 0;
    CHECK(errorOf("mob:SetDeathState(2)").find("2/3 (CORPSE, DEAD) on a living creature") != std::string::npos);
    CHECK(errorOf("mob:SetDeathState(3)").find("use 1, JUST_DIED") != std::string::npos);
    CHECK(c.deathState == 0);
    // an unknown spell id: the core's cooldown lookup by id asserts
    CHECK(errorOf("mob:HasSpellCooldown(9999)").find("the spell is not one the server knows") != std::string::npos);
    // a pet follows its owner: a spawn default movement type is refused
    c.isPet = true;
    CHECK(errorOf("mob:SetDefaultMovementType(1)").find("is a pet or is controlled by a player") != std::string::npos);
    c.isPet = false;
    // an entry whose template has no usable model would leave the creature half switched
    c.knownEntries.insert(601);
    c.noModelEntries = {601};
    CHECK(errorOf("mob:UpdateEntry(601)").find("has no usable model") != std::string::npos);
    CHECK(errorOf("mob:SetEquipmentSlots(9999, 0, 0)").find("an item entry is not one the server knows") !=
          std::string::npos);
    CHECK(errorOf("mob:UpdateEntry(9999)").find("is not one the server's creature tables hold") != std::string::npos);
    CHECK(errorOf("mob:UpdateEntry(600, 9999)").find("is not one the server's creature tables hold") != std::string::npos);
    c.waypointPath = 0;
    CHECK(errorOf("mob:MoveWaypoint()").find("waypoint path is not loaded") != std::string::npos);
    c.hasTemplate = false;
    CHECK(errorOf("mob:GetCreatureFamily()").find("template is not on the server") != std::string::npos);
    CHECK(errorOf("mob:GetRank()").find("template is not on the server") != std::string::npos);
    c.hasTemplate = true;
    CHECK(errorOf("mob:HasCategoryCooldown(9999)").find("the spell is not one the server knows") !=
          std::string::npos);
    CHECK(errorOf("mob:GetCreatureSpellCooldownDelay(9999)").find("the spell is not one the server knows") !=
          std::string::npos);
    CHECK(errorOf("mob:AddThreat(stored, 10, 1)").find("needs the spell entry") != std::string::npos);
    CHECK(errorOf("mob:AddThreat(stored, 10, 0, 9999)").find("needs the spell entry") != std::string::npos);
}

TEST_CASE("eluna creature: the four documented calls the core cannot answer are honest stubs")
{
    CreatureRig rig;
    fl::HandleData mob = FakeHost::creature(112, 0, 0, 900);
    fl::ObjectInfo info;
    info.name = "Stubs";
    rig.host.add(mob, info);
    prepare(rig.host, mob);
    storeHandles(rig, FakeHost::player(111), mob);

    // Each name is in the class table (the call resolves) and raises the documented core error.
    for (std::string const& call : {"CanAggro()", "CanWalk()", "IsDamageEnoughForLootingAndReward()",
                                    "SetAggroEnabled(true)"})
    {
        INFO("call: mob:" << call);
        fl::CallResult r = rig.run("mob:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("not available on this core") != std::string::npos);
    }
}
