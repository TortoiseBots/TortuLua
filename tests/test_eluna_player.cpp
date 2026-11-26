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

// ---- E7 long tail, round 2: the documented reads ----

namespace
{
// Stores a second player (for the methods that take one) through its own event handler.
void storeOther(PlayerRig& rig, fl::HandleData const& other)
{
    rig.ok("RegisterPlayerEvent(4, function(ev, player) other = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 4, {fl::EventValue(other)});
}

// Every read of the round, in the call form the docs give (the receiver is the stored player).
std::vector<std::string> const& readCalls()
{
    static std::vector<std::string> const calls = {
        "CanBlock()",
        "CanCompleteRepeatableQuest(5)",
        "CanFly()",
        "CanParry()",
        "CanShareQuest(5)",
        "CanSpeak()",
        "CanUninviteFromGroup(stored:GetGUID())",
        "GetChampioningFaction()",
        "GetDbcLocale()",
        "GetDifficulty()",
        "GetDrunkValue()",
        "GetGroupInvite()",
        "GetGuildRank()",
        "GetHealthBonusFromStamina()",
        "GetInGameTime()",
        "GetMailCount()",
        "GetManaBonusFromIntellect()",
        "GetNearbyGameObject()",
        "GetNextRandomRaidMember(30)",
        "GetOriginalGroup()",
        "GetOriginalSubGroup()",
        "GetQuestLevel(5)",
        "GetRecruiterId()",
        "GetSelectedPlayer()",
        "GetSelectedUnit()",
        "GetShieldBlockValue()",
        "GetSubGroup()",
        "GetXPRestBonus(100)",
        "HasAtLoginFlag(4)",
        "HasPendingBind()",
        "HasTalent(133, 0)",
        "HasTitle(7)",
        "InArena()",
        "InBattlegroundQueue()",
        "IsAcceptingWhispers()",
        "IsARecruiter()",
        "IsGMChat()",
        "IsGMVisible()",
        "IsGroupVisibleFor(other)",
        "IsInSameGroupWith(other)",
        "IsInSameRaidWith(other)",
        "IsNeverVisible()",
        "IsOutdoorPvPActive()",
        "ResetTalentsCost()"};
    return calls;
}
}  // namespace

TEST_CASE("eluna player reads, round 2: every method reaches the host and a gone player raises")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(101);
    fl::HandleData mob = FakeHost::creature(102, 0, 0, 800);
    fl::HandleData other = FakeHost::player(103);
    fl::ObjectInfo info;
    info.name = "Reads";
    rig.host.add(p, info);
    info.entry = 800;
    rig.host.add(mob, info);
    info.entry = 0;
    rig.host.add(other, info);
    storeHandles(rig, p, mob);
    storeOther(rig, other);

    for (std::string const& call : readCalls())
    {
        INFO("call: stored:" << call);
        CHECK(rig.run("stored:" + call).ok);
    }

    rig.host.remove(p);
    for (std::string const& call : readCalls())
    {
        INFO("call: stored:" << call);
        fl::CallResult r = rig.run("stored:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Player no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna player reads, round 2: the values flow through the fake player state")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(111);
    fl::HandleData mob = FakeHost::creature(112, 0, 0, 900);
    fl::HandleData other = FakeHost::player(113);
    fl::ObjectInfo info;
    info.name = "ReadValues";
    rig.host.add(p, info);
    info.entry = 900;
    rig.host.add(mob, info);
    info.entry = 0;
    rig.host.add(other, info);
    storeHandles(rig, p, mob);
    storeOther(rig, other);
    FakeHost::FakePlayer& state = rig.host.playerState(p);

    state.canBlock = true;
    state.canCompleteRepeatable = true;
    state.canFly = true;
    state.canParry = true;
    state.canShare = true;
    CHECK(rig.ok("return tostring(stored:CanBlock()) .. tostring(stored:CanCompleteRepeatableQuest(5)) .. "
                 "tostring(stored:CanFly()) .. tostring(stored:CanParry()) .. tostring(stored:CanShareQuest(5))") ==
          "truetruetruetruetrue");
    state.canSpeak = false;
    CHECK(rig.ok("return tostring(stored:CanSpeak())") == "false");

    state.partyResult = 7;
    CHECK(rig.ok("return stored:CanUninviteFromGroup(stored:GetGUID())") == "7");

    state.championing = 72;
    state.dbcLocale = 2;
    state.difficulty = 1;
    state.drunk = 30;
    CHECK(rig.ok("return stored:GetChampioningFaction() .. ',' .. stored:GetDbcLocale() .. ',' .. "
                 "stored:GetDifficulty() .. ',' .. stored:GetDrunkValue()") == "72,2,1,30");

    state.groupInvite = 700;
    state.originalGroup = 701;
    state.originalSubGroup = 3;
    state.guildRank = 4;
    state.subGroup = 2;
    CHECK(rig.ok("return tostring(stored:GetGroupInvite()) .. ',' .. tostring(stored:GetOriginalGroup()) .. ',' .. "
                 "stored:GetOriginalSubGroup()") == "Guid-0-700,Guid-0-701,3");
    CHECK(rig.ok("return stored:GetGuildRank() .. ',' .. stored:GetSubGroup()") == "4,2");
    state.groupInvite = 0;
    CHECK(rig.ok("return tostring(stored:GetGroupInvite())") == "nil");

    state.healthBonus = 12.5;
    state.manaBonus = 15.0;
    state.inGameTime = 4242;
    state.mailCount = 3;
    state.shieldBlockValue = 47;
    state.resetTalentsCost = 50000;
    CHECK(rig.ok("return stored:GetHealthBonusFromStamina() .. ',' .. stored:GetManaBonusFromIntellect()") ==
          "12.5,15.0");
    CHECK(rig.ok("return stored:GetInGameTime() .. ',' .. stored:GetMailCount() .. ',' .. stored:GetShieldBlockValue()") ==
          "4242,3,47");
    CHECK(rig.ok("return stored:ResetTalentsCost()") == "50000");

    state.nearbyGameObject = 910;
    state.nextRaidMember = 111;
    state.selectedPlayer = 113;
    state.selectedUnitCreature = 112;
    CHECK(rig.ok("return stored:GetNearbyGameObject():GetGUIDLow()") == "910");
    CHECK(rig.ok("return stored:GetNextRandomRaidMember(10):GetGUIDLow()") == "111");
    CHECK(rig.ok("return stored:GetSelectedPlayer():GetGUIDLow()") == "113");
    CHECK(rig.ok("return stored:GetSelectedUnit():GetGUIDLow()") == "112");
    state.selectedPlayer = 0;
    CHECK(rig.ok("return tostring(stored:GetSelectedPlayer())") == "nil");

    state.questLevel = 40;
    state.recruiterId = 9;
    state.xpRestBonus = 250;
    CHECK(rig.ok("return stored:GetQuestLevel(5) .. ',' .. stored:GetRecruiterId() .. ',' .. "
                 "stored:GetXPRestBonus(100)") == "40,9,250");

    state.atLoginFlags = 6;
    state.pendingBind = true;
    state.talents.insert({133, 0});
    state.titles.insert(7);
    CHECK(rig.ok("return tostring(stored:HasAtLoginFlag(4)) .. tostring(stored:HasAtLoginFlag(16))") == "truefalse");
    CHECK(rig.ok("return tostring(stored:HasPendingBind())") == "true");
    CHECK(rig.ok("return tostring(stored:HasTalent(133, 0)) .. tostring(stored:HasTalent(133, 1))") == "truefalse");
    CHECK(rig.ok("return tostring(stored:HasTitle(7)) .. tostring(stored:HasTitle(8))") == "truefalse");

    state.inArena = true;
    state.inBgQueue = true;
    state.acceptWhispers = true;
    state.isRecruiter = true;
    state.gmChat = true;
    state.gmVisible = false;
    state.neverVisible = true;
    state.outdoorPvp = true;
    CHECK(rig.ok("return tostring(stored:InArena()) .. tostring(stored:InBattlegroundQueue())") == "truetrue");
    CHECK(rig.ok("return tostring(stored:IsAcceptingWhispers()) .. tostring(stored:IsARecruiter())") == "truetrue");
    CHECK(rig.ok("return tostring(stored:IsGMChat()) .. tostring(stored:IsGMVisible())") == "truefalse");
    CHECK(rig.ok("return tostring(stored:IsNeverVisible()) .. tostring(stored:IsOutdoorPvPActive())") == "truetrue");

    // The three "with" reads answer for the player argument, not for the receiver alone.
    state.groupMates.insert(other.guid.lo);
    state.raidMates.insert(other.guid.lo);
    state.visibleFor.insert(other.guid.lo);
    CHECK(rig.ok("return tostring(stored:IsInSameGroupWith(other)) .. tostring(stored:IsInSameRaidWith(other)) .. "
                 "tostring(stored:IsGroupVisibleFor(other))") == "truetruetrue");
    state.groupMates.clear();
    state.raidMates.clear();
    state.visibleFor.clear();
    state.groupMates.insert(mob.guid.lo);
    CHECK(rig.ok("return tostring(stored:IsInSameGroupWith(other)) .. tostring(stored:IsInSameRaidWith(other))") ==
          "falsefalse");
}

TEST_CASE("eluna player reads, round 2: a bad argument raises before the host is called")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(121);
    fl::HandleData mob = FakeHost::creature(122, 0, 0, 1000);
    fl::HandleData other = FakeHost::player(123);
    fl::ObjectInfo info;
    info.name = "ReadArgs";
    rig.host.add(p, info);
    info.entry = 1000;
    rig.host.add(mob, info);
    info.entry = 0;
    rig.host.add(other, info);
    storeHandles(rig, p, mob);
    storeOther(rig, other);

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };
    size_t const callsBefore = rig.host.gameCalls.size();

    CHECK(errorOf("stored:GetXPRestBonus(-1)").find("xp must be an integer 0..4294967295") != std::string::npos);
    CHECK(errorOf("stored:GetQuestLevel('x')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:HasTalent(1, 300)").find("spec must be an integer 0..255") != std::string::npos);
    CHECK(errorOf("stored:HasTitle(1.5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:HasAtLoginFlag(-1)").find("flag must be an integer 0..4294967295") != std::string::npos);
    CHECK(errorOf("stored:CanUninviteFromGroup('nope')").find("guid must be an Object:GetGUID() value") !=
          std::string::npos);
    CHECK(errorOf("stored:CanUninviteFromGroup(1.5)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:GetNextRandomRaidMember('x')").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:IsInSameGroupWith(5)").find("player expected") != std::string::npos);
    CHECK(errorOf("stored:IsInSameRaidWith(mob)").find("player is not of the expected kind") != std::string::npos);
    CHECK(errorOf("stored:IsGroupVisibleFor()").find("bad argument") != std::string::npos);
    CHECK(rig.host.gameCalls.size() == callsBefore);   // nothing reached the host

    // A Player-only read is not on the Creature metatable at all.
    CHECK(errorOf("mob:CanBlock()").find("attempt to call a nil value") != std::string::npos);
}

TEST_CASE("eluna player reads, round 2: the calls the core has no API for are registered stubs")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(131);
    fl::ObjectInfo info;
    info.name = "CoreStubs";
    rig.host.add(p, info);
    storeHandles(rig, p, FakeHost::creature(132, 0, 0, 1100));

    for (std::string const& name : {"GetChatTag", "GetFreeTalentPoints", "GetLifetimeKills", "GetMailItem",
                                    "GetPhaseMaskForSpawn", "IsHonorOrXPTarget", "IsVisibleForPlayer"})
    {
        INFO("method: " << name);
        fl::CallResult r = rig.run("return stored:" + name + "()");
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find(name + ": not available on this core") != std::string::npos);
    }
}

// ---- E7 long tail, round 2: the writes ----

namespace
{
// Every write of the round, in the call form the docs give. The two item forms read an item out of the inventory.
std::vector<std::string> const& writeCalls()
{
    static std::vector<std::string> const calls = {
        "SendCinematicStart(2)",
        "SendMovieStart(3)",
        "SendUpdateWorldState(22, 5)",
        "SendSpiritResurrect()",
        "SetAcceptWhispers(true)",
        "SetAtLoginFlag(4)",
        "SetDrunkValue(30)",
        "SetFactionForRace(1)",
        "SetGameMaster(true)",
        "SetGender(1)",
        "SetGMChat(true)",
        "SetGMVisible(false)",
        "SetPvPDeath(true)",
        "SpawnBones()",
        "KillPlayer()",
        "ResetTalents()",
        "DurabilityLoss(stored:GetItemByPos(1, 3), 5)",
        "DurabilityLossAll(5)",
        "DurabilityPointLossForEquipSlot(3)",
        "DurabilityPointsLoss(stored:GetItemByPos(1, 3), 10)",
        "DurabilityPointsLossAll(10)",
        "DurabilityRepair(3)"};
    return calls;
}
}  // namespace

TEST_CASE("eluna player writes, round 2: every method reaches the host and a gone player raises")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(141);
    fl::ObjectInfo info;
    info.name = "Writes";
    rig.host.add(p, info);
    storeHandles(rig, p, FakeHost::creature(142, 0, 0, 1200));
    rig.host.playerState(p).items.push_back(FakeHost::FakeItem{9101, 2589, 1, 3});

    for (std::string const& call : writeCalls())
    {
        INFO("call: stored:" << call);
        CHECK(rig.run("stored:" + call).ok);
    }

    rig.host.remove(p);
    for (std::string const& call : writeCalls())
    {
        INFO("call: stored:" << call);
        fl::CallResult r = rig.run("stored:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Player no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna player writes, round 2: the arguments reach the fake player state")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(151);
    fl::ObjectInfo info;
    info.name = "WriteValues";
    rig.host.add(p, info);
    storeHandles(rig, p, FakeHost::creature(152, 0, 0, 1300));
    FakeHost::FakePlayer& state = rig.host.playerState(p);
    state.items.push_back(FakeHost::FakeItem{9201, 2589, 1, 3});

    // the sends carry their documented arguments
    std::size_t const from = state.actions.size();
    rig.ok("stored:SendCinematicStart(2)");
    rig.ok("stored:SendMovieStart(3)");
    rig.ok("stored:SendUpdateWorldState(22, 5)");
    rig.ok("stored:SendUpdateWorldState(22, 6, true)");
    rig.ok("stored:SendSpiritResurrect()");
    REQUIRE(state.actions.size() == from + 5);
    CHECK(state.actions[from].first == "player.sendCinematicStart");
    CHECK(std::get<int64_t>(state.actions[from].second[0].v) == 2);
    CHECK(state.actions[from + 1].first == "player.sendMovieStart");
    CHECK(std::get<int64_t>(state.actions[from + 1].second[0].v) == 3);
    CHECK(state.actions[from + 2].first == "player.sendUpdateWorldState");
    CHECK(std::get<int64_t>(state.actions[from + 2].second[0].v) == 22);
    CHECK(std::get<int64_t>(state.actions[from + 2].second[1].v) == 5);
    CHECK(std::get<bool>(state.actions[from + 2].second[2].v) == false);
    CHECK(std::get<bool>(state.actions[from + 3].second[2].v) == true);
    CHECK(state.actions[from + 4].first == "player.sendSpiritResurrect");

    // the setters write the documented value
    rig.ok("stored:SetAcceptWhispers(true)");
    CHECK(state.acceptWhispers);
    rig.ok("stored:SetAtLoginFlag(4)");
    CHECK(state.atLoginFlags == 4);
    rig.ok("stored:SetDrunkValue(30)");
    CHECK(state.drunk == 30);
    rig.ok("stored:SetFactionForRace(1)");
    CHECK(state.factionRace == 1);
    rig.ok("stored:SetGameMaster(false)");
    CHECK_FALSE(state.gameMaster);
    rig.ok("stored:SetGender(1)");
    CHECK(state.gender == 1);
    rig.ok("stored:SetGMChat(true)");
    CHECK(state.gmChat);
    rig.ok("stored:SetGMVisible(false)");
    CHECK_FALSE(state.gmVisible);
    rig.ok("stored:SetPvPDeath(true)");
    CHECK(state.pvpDeath);

    // the remaining actions
    rig.ok("stored:SpawnBones()");
    CHECK(state.bonesSpawned);
    rig.ok("stored:KillPlayer()");
    CHECK(state.killed);
    state.resetTalentsResult = false;
    CHECK(rig.ok("return tostring(stored:ResetTalents(true))") == "false");
    CHECK(state.resetTalentsCalls == 1);
    CHECK(state.resetTalentsNoCost);

    // ResetTalents: paid reset requires enough money and deducts cost; noCost=true resets free
    state.resetTalentsResult = true;
    state.resetTalentsCalls = 0;
    state.resetTalentsCost = 10000;
    state.money = 9999;
    CHECK(rig.ok("return tostring(stored:ResetTalents())") == "false");
    CHECK(state.resetTalentsCalls == 0);
    CHECK(state.money == 9999);
    CHECK(rig.ok("return tostring(stored:ResetTalents(false))") == "false");
    CHECK(state.resetTalentsCalls == 0);
    CHECK(state.money == 9999);
    CHECK(rig.ok("return tostring(stored:ResetTalents(true))") == "true");
    CHECK(state.resetTalentsCalls == 1);
    CHECK(state.resetTalentsNoCost);
    CHECK(state.money == 9999);
    state.money = 12500;
    CHECK(rig.ok("return tostring(stored:ResetTalents(false))") == "true");
    CHECK(state.resetTalentsCalls == 2);
    CHECK_FALSE(state.resetTalentsNoCost);
    CHECK(state.money == 2500);

    // SetFactionForRace: unknown race is refused and leaves factionRace untouched
    state.factionRace = 3;
    CHECK(rig.run("stored:SetFactionForRace(0)").ok == false);
    CHECK(rig.run("stored:SetFactionForRace(99)").ok == false);
    CHECK(state.factionRace == 3);
    rig.ok("stored:SetFactionForRace(11)");
    CHECK(state.factionRace == 11);

    // durability: each call reaches the host with its arguments (percent clamped to [0, 1])
    rig.ok("stored:DurabilityLoss(stored:GetItemByPos(1, 3), 0.5)");
    rig.ok("stored:DurabilityLossAll(0.75, false)");
    rig.ok("stored:DurabilityPointLossForEquipSlot(3)");
    rig.ok("stored:DurabilityPointsLoss(stored:GetItemByPos(1, 3), 12)");
    rig.ok("stored:DurabilityPointsLossAll(13)");
    rig.ok("stored:DurabilityRepair(4, false, 0.5)");
    rig.ok("stored:DurabilityLoss(stored:GetItemByPos(1, 3), -0.5)");
    rig.ok("stored:DurabilityLoss(stored:GetItemByPos(1, 3), 5.5)");
    rig.ok("stored:DurabilityLossAll(-0.2, true)");
    rig.ok("stored:DurabilityLossAll(7.5, false)");
    REQUIRE(state.durabilities.size() == 10);
    CHECK(state.durabilities[0].op == "player.durabilityLoss");
    CHECK(state.durabilities[0].itemLow == 9201);
    CHECK(state.durabilities[0].percent == 0.5);
    CHECK(state.durabilities[1].percent == 0.75);
    CHECK_FALSE(state.durabilities[1].inventory);
    CHECK(state.durabilities[2].slot == 3);
    CHECK(state.durabilities[3].points == 12);
    CHECK(state.durabilities[4].points == 13);
    CHECK(state.durabilities[4].inventory);
    CHECK(state.durabilities[5].pos == 4);
    CHECK_FALSE(state.durabilities[5].takeCost);
    CHECK(state.durabilities[5].discount == 0.5);
    CHECK(state.durabilities[6].percent == 0.0);
    CHECK(state.durabilities[7].percent == 1.0);
    CHECK(state.durabilities[8].percent == 0.0);
    CHECK(state.durabilities[8].inventory);
    CHECK(state.durabilities[9].percent == 1.0);
    CHECK_FALSE(state.durabilities[9].inventory);
}

TEST_CASE("eluna player writes, round 2: a bad argument raises before the host is called")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(161);
    fl::ObjectInfo info;
    info.name = "WriteArgs";
    rig.host.add(p, info);
    storeHandles(rig, p, FakeHost::creature(162, 0, 0, 1400));
    rig.host.playerState(p).items.push_back(FakeHost::FakeItem{9301, 2589, 1, 3});
    rig.ok("ownedItem = stored:GetItemByPos(1, 3)");

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };
    size_t const callsBefore = rig.host.gameCalls.size();

    CHECK(errorOf("stored:SetGameMaster('yes')").find("boolean expected") != std::string::npos);
    CHECK(errorOf("stored:SetDrunkValue(256)").find("value must be an integer 0..255") != std::string::npos);
    CHECK(errorOf("stored:SetGender(-1)").find("gender must be an integer 0..1") != std::string::npos);
    CHECK(errorOf("stored:SetGender(2)").find("gender must be an integer 0..1") != std::string::npos);
    CHECK(errorOf("stored:SetGender(256)").find("gender must be an integer 0..1") != std::string::npos);
    CHECK(errorOf("stored:SendMovieStart(-1)").find("movieId must be an integer 0..4294967295") != std::string::npos);
    CHECK(errorOf("stored:SendUpdateWorldState(1)").find("bad argument") != std::string::npos);
    CHECK(errorOf("stored:DurabilityRepair(70000)").find("pos must be an integer 0..65535") != std::string::npos);
    CHECK(errorOf("stored:DurabilityRepair(4, false, 0/0)").find("discount must not be NaN") != std::string::npos);
    CHECK(errorOf("stored:DurabilityRepairAll(false, 0/0)").find("discount must not be NaN") != std::string::npos);
    CHECK(errorOf("stored:DurabilityLoss(5, 1)").find("item expected") != std::string::npos);
    CHECK(errorOf("stored:DurabilityLoss(ownedItem, 0/0)").find("percent must be a finite number") !=
          std::string::npos);
    CHECK(errorOf("stored:DurabilityLoss(ownedItem, 1/0)").find("percent must be a finite number") !=
          std::string::npos);
    CHECK(errorOf("stored:DurabilityLoss(ownedItem, -1/0)").find("percent must be a finite number") !=
          std::string::npos);
    CHECK(errorOf("stored:DurabilityLossAll(0/0)").find("percent must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:DurabilityLossAll(1/0)").find("percent must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:DurabilityLossAll(-1/0)").find("percent must be a finite number") != std::string::npos);
    CHECK(errorOf("stored:DurabilityPointsLoss(mob, 1)").find("item is not of the expected kind") != std::string::npos);
    CHECK(rig.host.gameCalls.size() == callsBefore);   // nothing reached the host
}

TEST_CASE("eluna player writes, round 2: a durability write ignores an item the receiver does not own")
{
    PlayerRig rig;
    fl::HandleData p = FakeHost::player(171);
    fl::HandleData other = FakeHost::player(172);
    fl::ObjectInfo info;
    info.name = "Owned";
    rig.host.add(p, info);
    rig.host.add(other, info);
    storeHandles(rig, p, FakeHost::creature(173, 0, 0, 1500));
    storeOther(rig, other);
    FakeHost::FakePlayer& state = rig.host.playerState(p);
    FakeHost::FakePlayer& otherState = rig.host.playerState(other);
    state.items.push_back(FakeHost::FakeItem{9401, 2589, 1, 3});
    otherState.items.push_back(FakeHost::FakeItem{9402, 2589, 1, 3});

    std::size_t const ownFrom = state.durabilities.size();
    rig.ok("stored:DurabilityLoss(other:GetItemByPos(1, 3), 10)");
    rig.ok("stored:DurabilityPointsLoss(other:GetItemByPos(1, 3), 10)");
    CHECK(state.durabilities.size() == ownFrom);     // the other player's item is not the receiver's to damage
    CHECK(otherState.durabilities.empty());
    rig.ok("stored:DurabilityLoss(stored:GetItemByPos(1, 3), 10)");
    REQUIRE(state.durabilities.size() == ownFrom + 1);
    CHECK(state.durabilities.back().itemLow == 9401);
}
