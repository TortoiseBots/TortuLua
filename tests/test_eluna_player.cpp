// E5 long tail, round 1: the documented Player methods added in this round (docs Player/index.html). The fake world
// carries a small player state (FakeHost::FakePlayer) so a value can be written through one method and read back
// through another; every method is also called on a gone player, which must raise the family's stale-handle error, and
// with a bad argument, which must raise before anything reaches the host.
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

struct PlayerRig : Rig
{
    PlayerRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

// Stores a player and a creature in the Lua state through their own events, so the methods can take them as arguments.
void storeHandles(PlayerRig& rig, fl::HandleData const& p, fl::HandleData const& mob)
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
        "SetCoinage(100)",
        "GetXP()",
        "GetXPForNextLevel()",
        "GetRestBonus()",
        "SetRestBonus(12.5)",
        "IsRested()",
        "GetHonorPoints()",
        "SetHonorPoints(10)",
        "ModifyHonorPoints(-5)",
        "GetReputation(59)",
        "SetReputation(59, 100)",
        "GetReputationRank(59)",
        "GetSkillValue(164)",
        "GetMaxSkillValue(164)",
        "GetPureSkillValue(164)",
        "GetPureMaxSkillValue(164)",
        "GetBaseSkillValue(164)",
        "GetSkillPermBonusValue(164)",
        "GetSkillTempBonusValue(164)",
        "HasSkill(164)",
        "SetSkill(164, 1, 75, 75)",
        "ResetSpellCooldown(133)",
        "HasSpellCooldown(133)",
        "GetSpellCooldownDelay(133)",
        "ResetAllCooldowns()",
        "ResetTypeCooldowns(4)",
        "GetItemByPos(1, 3)",
        "GetItemByEntry(2589)",
        "GetItemByGUID(0)",
        "GetEquippedItemBySlot(4)",
        "EquipItem(1234, 7)",
        "CanEquipItem(1234, 7)",
        "CanUseItem(1234)",
        "GetQuestRewardStatus(5)",
        "IncompleteQuest(6)",
        "FailQuest(6)",
        "RemoveQuest(6)",
        "RemoveActiveQuest(6)",
        "RemoveRewardedQuest(6)",
        "CanCompleteQuest(7)",
        "CanRewardQuest(8)",
        "RewardQuest(8)",
        "AreaExploredOrEventHappens(9)",
        "KilledMonsterCredit(10)",
        "GroupEventHappens(11, stored)",
        "TalkedToCreature(12, mob)",
        "HasQuestForGO(13)",
        "HasQuestForItem(14)",
        "GetReqKillOrCastCurrentCount(15, 16)",
        "HasReceivedQuestReward(5)",
        "IsInGroup()",
        "IsInGuild()",
        "IsFlying()",
        "IsMoving()",
        "IsFalling()",
        "IsInWater()",
        "IsTaxiCheater()",
        "SetTaxiCheat(false)",
        "InBattleground()",
        "GetBattlegroundId()",
        "GetBattlegroundTypeId()",
        "IsAFK()",
        "IsDND()",
        "ToggleAFK()",
        "ToggleDND()",
        "SetFFA(false)",
        "IsImmuneToDamage()",
        "IsImmuneToEnvironmentalDamage()",
        "SetPvP(false)",
        "Say('hello', 0)",
        "Yell('hello', 0)",
        "Whisper('hello', 0, stored)",
        "TextEmote('hello')",
        "SendShowBank(mob)",
        "SendListInventory(mob)",
        "SendTrainerList(mob)",
        "SendTaxiMenu(mob)",
        "SetBindPoint(1.5, 2.5, 3.5, 1, 12)",
        "SummonPlayer(stored)",
        "GetGuildName()",
        "GetDbLocaleIndex()",
        "GetPlayerIP()",
        "GetLatency()",
        "GetTotalPlayedTime()",
        "GetLevelPlayedTime()",
        "LogoutPlayer(false)",
        "DurabilityRepairAll(false, 0.5, true)",
    };
    return calls;
}
}  // namespace

TEST_CASE("eluna player long tail: every method of the round reaches the host and a gone player raises")
{
    PlayerRig rig;
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

    // The family's stale-handle rule: the same calls on a gone player raise before anything reaches the host.
    rig.host.remove(p);
    for (std::string const& call : roundCalls())
    {
        INFO("call: stored:" << call);
        fl::CallResult r = rig.run("stored:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Player no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna player long tail: the values flow both ways through the fake player state")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(81);
    fl::HandleData mob = FakeHost::creature(82, 0, 0, 600);
    fl::ObjectInfo info;
    info.name = "Values";
    rig.host.add(p, info);
    info.entry = 600;
    rig.host.add(mob, info);
    storeHandles(rig, p, mob);
    FakeHost::FakePlayer& state = rig.host.playerState(p);

    // money
    rig.ok("stored:SetCoinage(12345)");
    CHECK(rig.ok("return stored:GetCoinage()") == "12345");
    rig.ok("stored:ModifyMoney(-345)");
    CHECK(rig.ok("return stored:GetCoinage()") == "12000");
    CHECK(state.money == 12000);

    // XP, rest
    state.xp = 400;
    state.xpNext = 900;
    CHECK(rig.ok("return stored:GetXP() .. ',' .. stored:GetXPForNextLevel()") == "400,900");
    rig.ok("stored:SetRestBonus(122.5)");
    CHECK(rig.ok("return stored:GetRestBonus()") == "122.5");
    state.rested = true;
    CHECK(rig.ok("return tostring(stored:IsRested())") == "true");

    // honor
    rig.ok("stored:SetHonorPoints(777)");
    CHECK(rig.ok("return stored:GetHonorPoints()") == "777");
    rig.ok("stored:ModifyHonorPoints(-77)");
    CHECK(rig.ok("return stored:GetHonorPoints()") == "700");

    // reputation
    state.reputation[59] = 4200;
    state.repRank = 6;
    CHECK(rig.ok("return stored:GetReputation(59) .. ',' .. stored:GetReputationRank(59)") == "4200,6");
    rig.ok("stored:SetReputation(59, -500)");
    CHECK(rig.ok("return stored:GetReputation(59)") == "-500");

    // skills, both ways and every getter
    rig.ok("stored:SetSkill(164, 4, 275, 300)");
    CHECK(rig.ok("return stored:GetSkillValue(164) .. ',' .. stored:GetMaxSkillValue(164)") == "275,300");
    CHECK(rig.ok("return tostring(stored:HasSkill(164))") == "true");
    state.skills[164].perm = 5;
    state.skills[164].temp = 3;
    CHECK(rig.ok("return stored:GetPureSkillValue(164) .. ',' .. stored:GetPureMaxSkillValue(164)") == "267,292");
    CHECK(rig.ok("return stored:GetBaseSkillValue(164) .. ',' .. stored:GetSkillPermBonusValue(164) .. ',' .. "
                 "stored:GetSkillTempBonusValue(164)") == "272,5,3");

    // cooldowns
    state.spellCooldowns = {133};
    state.spellCooldownDelay[133] = 1500;
    CHECK(rig.ok("return tostring(stored:HasSpellCooldown(133)) .. ',' .. stored:GetSpellCooldownDelay(133)") ==
          "true,1500");
    rig.ok("stored:ResetSpellCooldown(133, false)");
    CHECK(rig.ok("return tostring(stored:HasSpellCooldown(133))") == "false");
    REQUIRE(state.resetSpells.size() == 1);
    CHECK(state.resetSpells[0].first == 133);
    CHECK_FALSE(state.resetSpells[0].second);
    rig.ok("stored:ResetTypeCooldowns(4)");
    REQUIRE(state.resetTypes.size() == 1);
    CHECK(state.resetTypes[0].first == 4);
    CHECK(state.resetTypes[0].second);
    rig.ok("stored:ResetAllCooldowns()");
    CHECK(state.resetAll == 1);

    // items: by position, by entry, by guid, equipped, and EquipItem in both argument forms
    state.items.push_back(FakeHost::FakeItem{9001, 2589, 1, 3});
    state.items.push_back(FakeHost::FakeItem{9002, 2590, 0, 4});
    state.equipped.insert(4);
    CHECK(rig.ok("return stored:GetItemByPos(1, 3):GetGUIDLow()") == "9001");
    CHECK(rig.ok("return stored:GetItemByEntry(2589):GetGUIDLow()") == "9001");
    CHECK(rig.ok("local it = stored:GetItemByEntry(2590) return stored:GetItemByGUID(it:GetGUID()):GetGUIDLow()") ==
          "9002");
    CHECK(rig.ok("return stored:GetEquippedItemBySlot(4):GetGUIDLow()") == "9002");
    CHECK(rig.ok("return tostring(stored:GetItemByPos(9, 9))") == "nil");
    CHECK(rig.ok("return tostring(tonumber(stored:EquipItem(1234, 7):GetGUIDLow()) >= 9000)") == "true");
    CHECK(state.items.back().entry == 1234);
    CHECK(state.equipped.count(7) == 1);
    // the same item handed back as a handle is equipped again, not created twice
    std::size_t const itemsBefore = state.items.size();
    CHECK(rig.ok("local it = stored:GetItemByPos(1, 3) return stored:EquipItem(it, 8):GetGUIDLow()") == "9001");
    CHECK(state.items.size() == itemsBefore);
    CHECK(state.equipped.count(8) == 1);
    state.canEquip = true;
    state.canUse = true;
    CHECK(rig.ok("return tostring(stored:CanEquipItem(1234, 7))") == "true");
    CHECK(rig.ok("return tostring(stored:CanUseItem(1234))") == "true");

    // quests
    state.rewarded = {5};
    state.completed = {7};
    state.canReward = true;
    state.hasQuestForGO = true;
    state.hasQuestForItem = true;
    state.reqKillCount = 3;
    CHECK(rig.ok("return tostring(stored:GetQuestRewardStatus(5))") == "true");
    CHECK(rig.ok("return tostring(stored:HasReceivedQuestReward(5))") == "true");
    CHECK(rig.ok("return tostring(stored:CanCompleteQuest(7))") == "true");
    CHECK(rig.ok("return tostring(stored:CanRewardQuest(8))") == "true");
    CHECK(rig.ok("return tostring(stored:HasQuestForGO(13))") == "true");
    CHECK(rig.ok("return tostring(stored:HasQuestForItem(14))") == "true");
    CHECK(rig.ok("return stored:GetReqKillOrCastCurrentCount(15, 16)") == "3");
    rig.ok("stored:IncompleteQuest(6)");
    CHECK(state.activeQuests.count(6) == 1);
    rig.ok("stored:FailQuest(6)");
    CHECK(state.activeQuests.count(6) == 0);
    rig.ok("stored:RewardQuest(8)");
    CHECK(state.rewarded.count(8) == 1);
    rig.ok("stored:RemoveRewardedQuest(8)");
    CHECK(state.rewarded.count(8) == 0);
    // RemoveQuest clears both halves of a quest's state.
    state.activeQuests.insert(20);
    state.rewarded.insert(20);
    rig.ok("stored:RemoveQuest(20)");
    CHECK(state.activeQuests.count(20) == 0);
    CHECK(state.rewarded.count(20) == 0);

    // state flags
    state.inGroup = true;
    state.inGuild = true;
    state.flying = true;
    state.moving = true;
    state.falling = true;
    state.inWater = true;
    state.taxiCheat = true;
    state.inBg = true;
    state.bgId = 33;
    state.bgTypeId = 2;
    state.afk = true;
    state.dnd = true;
    state.immune = true;
    state.envImmune = true;
    CHECK(rig.ok("return tostring(stored:IsInGroup()) .. tostring(stored:IsInGuild()) .. tostring(stored:IsFlying())") ==
          "truetruetrue");
    CHECK(rig.ok("return tostring(stored:IsMoving()) .. tostring(stored:IsFalling()) .. tostring(stored:IsInWater())") ==
          "truetruetrue");
    CHECK(rig.ok("return tostring(stored:IsTaxiCheater())") == "true");
    CHECK(rig.ok("return tostring(stored:InBattleground()) .. ',' .. stored:GetBattlegroundId() .. ',' .. "
                 "stored:GetBattlegroundTypeId()") == "true,33,2");
    CHECK(rig.ok("return tostring(stored:IsAFK()) .. tostring(stored:IsDND())") == "truetrue");
    CHECK(rig.ok("return tostring(stored:IsImmuneToDamage()) .. tostring(stored:IsImmuneToEnvironmentalDamage())") ==
          "truetrue");
    rig.ok("stored:ToggleAFK()");
    rig.ok("stored:ToggleDND()");
    CHECK_FALSE(state.afk);
    CHECK_FALSE(state.dnd);
    rig.ok("stored:SetTaxiCheat(false)");
    CHECK_FALSE(state.taxiCheat);
    rig.ok("stored:SetFFA(true)");
    CHECK(state.ffa);
    rig.ok("stored:SetPvP(true)");
    CHECK(state.pvp);

    // chat and UI sends carry the docs arguments
    std::size_t const chatFrom = state.actions.size();
    rig.ok("stored:Say('hello', 1)");
    REQUIRE(state.actions.size() == chatFrom + 1);
    CHECK(state.actions[chatFrom].first == "player.say");
    REQUIRE(state.actions[chatFrom].second.size() == 2);
    CHECK(std::get<std::string>(state.actions[chatFrom].second[0].v) == "hello");
    CHECK(std::get<int64_t>(state.actions[chatFrom].second[1].v) == 1);
    rig.ok("stored:Yell('out', 0)");
    rig.ok("stored:Whisper('psst', 0, stored)");
    REQUIRE(state.actions.size() == chatFrom + 3);
    CHECK(state.actions[chatFrom + 2].first == "player.whisper");
    REQUIRE(state.actions[chatFrom + 2].second.size() == 3);
    auto const* receiver = std::get_if<fl::HandleData>(&state.actions[chatFrom + 2].second[2].v);
    REQUIRE(receiver);
    CHECK(receiver->guid.lo == p.guid.lo);
    rig.ok("stored:TextEmote('wave')");
    rig.ok("stored:SendShowBank(mob)");
    rig.ok("stored:SendListInventory(mob)");
    rig.ok("stored:SendTrainerList(mob)");
    rig.ok("stored:SendTaxiMenu(mob)");
    CHECK(state.actions.back().first == "player.sendTaxiMenu");
    CHECK(std::get<fl::HandleData>(state.actions.back().second[0].v).guid.lo == mob.guid.lo);

    // bind, summon, misc
    rig.ok("stored:SetBindPoint(1.5, 2.5, 3.5, 1, 12)");
    CHECK(state.bindSet);
    CHECK(state.bindX == 1.5);
    CHECK(state.bindMap == 1);
    CHECK(state.bindArea == 12);
    rig.ok("stored:SummonPlayer(stored)");
    CHECK(state.actions.back().first == "player.summonPlayer");
    state.guildName = "Guild";
    state.ip = "ip-sample";
    state.latency = 42;
    state.dbLocale = -1;
    state.totalPlayed = 3600;
    state.levelPlayed = 60;
    CHECK(rig.ok("return stored:GetGuildName() .. ',' .. stored:GetPlayerIP()") == "Guild,ip-sample");
    CHECK(rig.ok("return stored:GetLatency() .. ',' .. stored:GetDbLocaleIndex()") == "42,-1");
    CHECK(rig.ok("return stored:GetTotalPlayedTime() .. ',' .. stored:GetLevelPlayedTime()") == "3600,60");
    rig.ok("stored:LogoutPlayer(false)");
    CHECK(state.loggedOut);
    CHECK_FALSE(state.logoutSave);
    rig.ok("stored:DurabilityRepairAll(false, 0.5, true)");
    CHECK(state.repaired);
    CHECK_FALSE(state.repairTakeCost);
    CHECK(state.repairDiscount == 0.5);
    CHECK(state.repairGuildBank);
}

TEST_CASE("eluna player long tail: a bad argument raises before the host is called")
{
    PlayerRig rig;
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

    CHECK(errorOf("stored:SetCoinage(-1)").find("copperAmt must be an integer 0..4294967295") != std::string::npos);
    CHECK(errorOf("stored:SetCoinage('x')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetSkill(1, 2, 3, 70000)").find("maxVal must be an integer 0..65535") != std::string::npos);
    CHECK(errorOf("stored:GetItemByPos(256, 0)").find("bag must be an integer 0..255") != std::string::npos);
    CHECK(errorOf("stored:GetItemByPos(0, -1)").find("slot must be an integer 0..255") != std::string::npos);
    CHECK(errorOf("stored:Say(true, 0)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetTaxiCheat('yes')").find("boolean expected") != std::string::npos);
    CHECK(errorOf("stored:TalkedToCreature(1, stored)").find("creature is not of the expected kind") !=
          std::string::npos);
    CHECK(errorOf("stored:GroupEventHappens(1, 5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:SetReputation(1, 2.5)").find("bad argument") != std::string::npos);
    CHECK(rig.host.gameCalls.size() == callsBefore);   // nothing reached the host

    // A Player-only method is not on the Creature metatable at all (inheritance runs the other way).
    CHECK(errorOf("mob:SetCoinage(1)").find("attempt to call a nil value") != std::string::npos);
}
