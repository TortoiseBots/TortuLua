// Eluna Player methods of E1 (docs: Player/index.html), gossip included through elu/EluGossip.cpp.
#include "engine/elu/EluBind.h"

#include "engine/GuidText.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cmath>

namespace flu
{
namespace
{
using fl::EventValue;

double durabilityPercentArg(lua_State* L, int idx)
{
    lua_Number const v = luaL_checknumber(L, idx);
    if (!std::isfinite(v))
        luaL_error(L, "percent must be a finite number");
    return std::clamp(static_cast<double>(v), 0.0, 1.0);
}

int pSendBroadcastMessage(lua_State* L)
{
    std::string msg = argStr(L, 2, "message");
    return actionOp(L, kPlayer, "player.broadcast", "SendBroadcastMessage", {EventValue(msg)});
}
int pSendNotification(lua_State* L)
{
    std::string msg = argStr(L, 2, "message");
    return actionOp(L, kPlayer, "player.notification", "SendNotification", {EventValue(msg)});
}
int pGetAccountId(lua_State* L) { return readOp(L, kPlayer, "player.getAccountId", "GetAccountId"); }
int pGetAccountName(lua_State* L) { return readOp(L, kPlayer, "player.getAccountName", "GetAccountName"); }
int pGetGMRank(lua_State* L) { return readOp(L, kPlayer, "player.getGMRank", "GetGMRank"); }
int pIsGM(lua_State* L) { return readOp(L, kPlayer, "player.isGM", "IsGM"); }
int pGetTeam(lua_State* L) { return readOp(L, kPlayer, "player.getTeam", "GetTeam"); }
int pGetCoinage(lua_State* L) { return readOp(L, kPlayer, "player.getCoinage", "GetCoinage"); }
int pModifyMoney(lua_State* L)
{
    return actionOp(L, kPlayer, "player.modifyMoney", "ModifyMoney", {EventValue(argI64(L, 2, "copperAmt"))});
}
int pAddItem(lua_State* L)
{
    return readHandleOp(L, kPlayer, "player.addItem", "AddItem",
                        {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry"))),
                         EventValue(static_cast<int64_t>(optUInt(L, 3, 1)))});
}
int pRemoveItem(lua_State* L)
{
    std::vector<EventValue> args;
    bool blocked = false;
    if (fl::toHandleAny(L, 2))
    {
        args.emplace_back(objArg(L, 2, kItem, "item"));
        if (auto const* h = std::get_if<fl::HandleData>(&args.back().v))
            blocked = fl::ItemUseScope::blocks(&h->guid, 0);
    }
    else
    {
        uint32_t const entry = static_cast<uint32_t>(argUInt(L, 2, "entry"));
        args.emplace_back(static_cast<int64_t>(entry));
        blocked = fl::ItemUseScope::blocks(nullptr, entry);
    }
    args.emplace_back(static_cast<int64_t>(optUInt(L, 3, 1)));
    if (blocked)
    {
        // The core still uses this item after the hook returns: destroying it here would be a use after free.
        if (eng(L).noteHookGap("elu-item-use-destroy"))
            eng(L).host().log(fl::LogLevel::Warn,
                              "ForeverLua: Player:RemoveItem on the item being used was refused inside an item USE "
                              "handler (the core still uses the item); remove it from a CreateLuaEvent timer instead");
        return 0;
    }
    return actionOp(L, kPlayer, "player.removeItem", "RemoveItem", args);
}
int pHasItem(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasItem", "HasItem",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "itemId"))),
                   EventValue(static_cast<int64_t>(optUInt(L, 3, 1))), EventValue(optBool(L, 4, false))});
}
int pGetItemCount(lua_State* L)
{
    return readOp(L, kPlayer, "player.getItemCount", "GetItemCount",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry"))), EventValue(optBool(L, 3, false))});
}
int pTeleport(lua_State* L)
{
    return actionOp(L, kPlayer, "player.teleport", "Teleport",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "mapId"))), EventValue(argFloat(L, 3, "x")),
                     EventValue(argFloat(L, 4, "y")), EventValue(argFloat(L, 5, "z")),
                     EventValue(argFloat(L, 6, "orientation"))});
}
int pGiveXP(lua_State* L)
{
    std::vector<EventValue> args{EventValue(static_cast<int64_t>(optUInt(L, 2, 0)))};
    if (lua_isnoneornil(L, 3))
        args.emplace_back();
    else
        args.emplace_back(objArg(L, 3, kUnit, "victim"));
    return actionOp(L, kPlayer, "player.giveXP", "GiveXP", args);
}
int pLearnSpell(lua_State* L)
{
    return actionOp(L, kPlayer, "player.learnSpell", "LearnSpell",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))});
}
int pRemoveSpell(lua_State* L)
{
    return actionOp(L, kPlayer, "player.removeSpell", "RemoveSpell",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pHasSpell(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasSpell", "HasSpell",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))});
}
int pGetGroup(lua_State* L) { return readHandleOp(L, kPlayer, "player.getGroup", "GetGroup"); }
int pGetGuild(lua_State* L) { return readHandleOp(L, kPlayer, "player.getGuild", "GetGuild"); }
int pGetGuildId(lua_State* L) { return readOp(L, kPlayer, "player.getGuildId", "GetGuildId"); }
int pGetQuestStatus(lua_State* L)
{
    return readOp(L, kPlayer, "player.getQuestStatus", "GetQuestStatus",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pAddQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.addQuest", "AddQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pCompleteQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.completeQuest", "CompleteQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pHasQuest(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasQuest", "HasQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pSaveToDB(lua_State* L) { return actionOp(L, kPlayer, "player.saveToDB", "SaveToDB"); }
int pKickPlayer(lua_State* L) { return actionOp(L, kPlayer, "player.kick", "KickPlayer"); }
int pResurrectPlayer(lua_State* L)
{
    return actionOp(L, kPlayer, "player.resurrect", "ResurrectPlayer",
                    {EventValue(static_cast<double>(luaL_optnumber(L, 2, 100.0))), EventValue(optBool(L, 3, false))});
}
int pGetSelection(lua_State* L) { return readHandleOp(L, kPlayer, "player.getSelection", "GetSelection"); }
int pIsAlliance(lua_State* L) { return readOp(L, kPlayer, "player.isAlliance", "IsAlliance"); }
// E5: Player:GetCorpse (docs Corpse/index.html) and Player:SendPacket (docs Player/SendPacket.html).
int pGetCorpse(lua_State* L) { return readHandleOp(L, kPlayer, "player.getCorpse", "GetCorpse"); }
int pSendPacket(lua_State* L)
{
    fl::HandleData const& h = self(L, kPlayer);
    sendPacketArg(L, h, 2, "SendPacket");
    return 0;
}
int pIsHorde(lua_State* L) { return readOp(L, kPlayer, "player.isHorde", "IsHorde"); }

// ---- E5 long tail, round 1: the most used documented Player methods (docs Player/index.html). Each binding only
// checks the docs' argument types and ranges and hands the op to the host; the host owns the core call.

int pSetCoinage(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setCoinage", "SetCoinage",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "copperAmt")))});
}
int pGetXP(lua_State* L) { return readOp(L, kPlayer, "player.getXP", "GetXP"); }
int pGetXPForNextLevel(lua_State* L) { return readOp(L, kPlayer, "player.getXPForNextLevel", "GetXPForNextLevel"); }
int pGetRestBonus(lua_State* L) { return readOp(L, kPlayer, "player.getRestBonus", "GetRestBonus"); }
int pSetRestBonus(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setRestBonus", "SetRestBonus",
                    {EventValue(static_cast<double>(argFloat(L, 2, "restBonus")))});
}
int pIsRested(lua_State* L) { return readOp(L, kPlayer, "player.isRested", "IsRested"); }
int pGetHonorPoints(lua_State* L) { return readOp(L, kPlayer, "player.getHonorPoints", "GetHonorPoints"); }
int pSetHonorPoints(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setHonorPoints", "SetHonorPoints",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "honorPoints")))});
}
int pModifyHonorPoints(lua_State* L)
{
    return actionOp(L, kPlayer, "player.modifyHonorPoints", "ModifyHonorPoints",
                    {EventValue(static_cast<int64_t>(argInt(L, 2, "amount")))});
}
int pGetReputation(lua_State* L)
{
    return readOp(L, kPlayer, "player.getReputation", "GetReputation",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "faction")))});
}
int pSetReputation(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setReputation", "SetReputation",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "factionId"))),
                     EventValue(static_cast<int64_t>(argInt(L, 3, "reputationValue")))});
}
int pGetReputationRank(lua_State* L)
{
    return readOp(L, kPlayer, "player.getReputationRank", "GetReputationRank",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "faction")))});
}

// --- skills ---
int pGetSkillValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getSkillValue", "GetSkillValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pGetMaxSkillValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getMaxSkillValue", "GetMaxSkillValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pGetPureSkillValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getPureSkillValue", "GetPureSkillValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pGetPureMaxSkillValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getPureMaxSkillValue", "GetPureMaxSkillValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pGetBaseSkillValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getBaseSkillValue", "GetBaseSkillValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pGetSkillPermBonusValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getSkillPermBonusValue", "GetSkillPermBonusValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pGetSkillTempBonusValue(lua_State* L)
{
    return readOp(L, kPlayer, "player.getSkillTempBonusValue", "GetSkillTempBonusValue",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pHasSkill(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasSkill", "HasSkill",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "skill")))});
}
int pSetSkill(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setSkill", "SetSkill",
                    {EventValue(static_cast<int64_t>(argU16(L, 2, "id"))),
                     EventValue(static_cast<int64_t>(argU16(L, 3, "step"))),
                     EventValue(static_cast<int64_t>(argU16(L, 4, "currVal"))),
                     EventValue(static_cast<int64_t>(argU16(L, 5, "maxVal")))});
}

// --- spells and cooldowns ---
int pResetSpellCooldown(lua_State* L)
{
    return actionOp(L, kPlayer, "player.resetSpellCooldown", "ResetSpellCooldown",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId"))), EventValue(optBool(L, 3, true))});
}
int pHasSpellCooldown(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasSpellCooldown", "HasSpellCooldown",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))});
}
int pGetSpellCooldownDelay(lua_State* L)
{
    return readOp(L, kPlayer, "player.getSpellCooldownDelay", "GetSpellCooldownDelay",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId")))});
}
int pResetAllCooldowns(lua_State* L) { return actionOp(L, kPlayer, "player.resetAllCooldowns", "ResetAllCooldowns"); }
int pResetTypeCooldowns(lua_State* L)
{
    return actionOp(L, kPlayer, "player.resetTypeCooldowns", "ResetTypeCooldowns",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "category"))), EventValue(optBool(L, 3, true))});
}

// --- items and inventory ---
int pGetItemByPos(lua_State* L)
{
    return readHandleOp(L, kPlayer, "player.getItemByPos", "GetItemByPos",
                        {EventValue(static_cast<int64_t>(argU8(L, 2, "bag"))),
                         EventValue(static_cast<int64_t>(argU8(L, 3, "slot")))});
}
int pGetItemByEntry(lua_State* L)
{
    return readHandleOp(L, kPlayer, "player.getItemByEntry", "GetItemByEntry",
                        {EventValue(static_cast<int64_t>(argUInt(L, 2, "entryId")))});
}
int pGetItemByGUID(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readHandleOp(L, kPlayer, "player.getItemByGUID", "GetItemByGUID", {EventValue(fl::guidText(guid))});
}
int pGetEquippedItemBySlot(lua_State* L)
{
    return readHandleOp(L, kPlayer, "player.getEquippedItemBySlot", "GetEquippedItemBySlot",
                        {EventValue(static_cast<int64_t>(argU8(L, 2, "slot")))});
}
// The "item or its entry" argument of EquipItem, CanEquipItem and CanUseItem: an Item handle resolves through the
// host (the item the caller was handed), a number is the item entry.
std::vector<EventValue> itemOrEntry(lua_State* L, int idx)
{
    std::vector<EventValue> args;
    if (fl::toHandleAny(L, idx))
        args.emplace_back(objArg(L, idx, kItem, "item"));
    else
        args.emplace_back(static_cast<int64_t>(argUInt(L, idx, "entry")));
    return args;
}
int pEquipItem(lua_State* L)
{
    std::vector<EventValue> args = itemOrEntry(L, 2);
    args.emplace_back(static_cast<int64_t>(argU8(L, 3, "slot")));
    return readHandleOp(L, kPlayer, "player.equipItem", "EquipItem", args);
}
int pCanEquipItem(lua_State* L)
{
    std::vector<EventValue> args = itemOrEntry(L, 2);
    args.emplace_back(static_cast<int64_t>(argU8(L, 3, "slot")));
    return readOp(L, kPlayer, "player.canEquipItem", "CanEquipItem", args);
}
int pCanUseItem(lua_State* L)
{
    return readOp(L, kPlayer, "player.canUseItem", "CanUseItem", itemOrEntry(L, 2));
}

// --- quests ---
int pGetQuestRewardStatus(lua_State* L)
{
    return readOp(L, kPlayer, "player.getQuestRewardStatus", "GetQuestRewardStatus",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pIncompleteQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.incompleteQuest", "IncompleteQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pFailQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.failQuest", "FailQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pRemoveQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.removeQuest", "RemoveQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pRemoveActiveQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.removeActiveQuest", "RemoveActiveQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pRemoveRewardedQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.removeRewardedQuest", "RemoveRewardedQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pCanCompleteQuest(lua_State* L)
{
    return readOp(L, kPlayer, "player.canCompleteQuest", "CanCompleteQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pCanRewardQuest(lua_State* L)
{
    return readOp(L, kPlayer, "player.canRewardQuest", "CanRewardQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pRewardQuest(lua_State* L)
{
    return actionOp(L, kPlayer, "player.rewardQuest", "RewardQuest",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pAreaExploredOrEventHappens(lua_State* L)
{
    return actionOp(L, kPlayer, "player.areaExploredOrEventHappens", "AreaExploredOrEventHappens",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "quest")))});
}
int pKilledMonsterCredit(lua_State* L)
{
    return actionOp(L, kPlayer, "player.killedMonsterCredit", "KilledMonsterCredit",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pGroupEventHappens(lua_State* L)
{
    return actionOp(L, kPlayer, "player.groupEventHappens", "GroupEventHappens",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "quest"))),
                     EventValue(objArg(L, 3, kWorldObject, "object"))});
}
int pTalkedToCreature(lua_State* L)
{
    return actionOp(L, kPlayer, "player.talkedToCreature", "TalkedToCreature",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry"))),
                     EventValue(objArg(L, 3, kCreature, "creature"))});
}
int pHasQuestForGO(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasQuestForGO", "HasQuestForGO",
                  {EventValue(static_cast<int64_t>(argInt(L, 2, "entry")))});
}
int pHasQuestForItem(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasQuestForItem", "HasQuestForItem",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "entry")))});
}
int pGetReqKillOrCastCurrentCount(lua_State* L)
{
    return readOp(L, kPlayer, "player.getReqKillOrCastCurrentCount", "GetReqKillOrCastCurrentCount",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "quest"))),
                   EventValue(static_cast<int64_t>(argInt(L, 3, "entry")))});
}
int pHasReceivedQuestReward(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasReceivedQuestReward", "HasReceivedQuestReward",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}

// --- state ---
int pIsInGroup(lua_State* L) { return readOp(L, kPlayer, "player.isInGroup", "IsInGroup"); }
int pIsInGuild(lua_State* L) { return readOp(L, kPlayer, "player.isInGuild", "IsInGuild"); }
int pIsFlying(lua_State* L) { return readOp(L, kPlayer, "player.isFlying", "IsFlying"); }
int pIsMoving(lua_State* L) { return readOp(L, kPlayer, "player.isMoving", "IsMoving"); }
int pIsFalling(lua_State* L) { return readOp(L, kPlayer, "player.isFalling", "IsFalling"); }
int pIsInWater(lua_State* L) { return readOp(L, kPlayer, "player.isInWater", "IsInWater"); }
int pIsTaxiCheater(lua_State* L) { return readOp(L, kPlayer, "player.isTaxiCheater", "IsTaxiCheater"); }
int pSetTaxiCheat(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setTaxiCheat", "SetTaxiCheat", {EventValue(optBool(L, 2, true))});
}
int pInBattleground(lua_State* L) { return readOp(L, kPlayer, "player.inBattleground", "InBattleground"); }
int pGetBattlegroundId(lua_State* L) { return readOp(L, kPlayer, "player.getBattlegroundId", "GetBattlegroundId"); }
int pGetBattlegroundTypeId(lua_State* L)
{
    return readOp(L, kPlayer, "player.getBattlegroundTypeId", "GetBattlegroundTypeId");
}
int pIsAFK(lua_State* L) { return readOp(L, kPlayer, "player.isAFK", "IsAFK"); }
int pIsDND(lua_State* L) { return readOp(L, kPlayer, "player.isDND", "IsDND"); }
int pToggleAFK(lua_State* L) { return actionOp(L, kPlayer, "player.toggleAFK", "ToggleAFK"); }
int pToggleDND(lua_State* L) { return actionOp(L, kPlayer, "player.toggleDND", "ToggleDND"); }
int pSetFFA(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setFFA", "SetFFA", {EventValue(optBool(L, 2, true))});
}
int pIsImmuneToDamage(lua_State* L) { return readOp(L, kPlayer, "player.isImmuneToDamage", "IsImmuneToDamage"); }
int pIsImmuneToEnvironmentalDamage(lua_State* L)
{
    return readOp(L, kPlayer, "player.isImmuneToEnvironmentalDamage", "IsImmuneToEnvironmentalDamage");
}

// --- chat ---
int pSay(lua_State* L)
{
    return actionOp(L, kPlayer, "player.say", "Say",
                    {EventValue(argStr(L, 2, "text")), EventValue(static_cast<int64_t>(argUInt(L, 3, "lang")))});
}
int pYell(lua_State* L)
{
    return actionOp(L, kPlayer, "player.yell", "Yell",
                    {EventValue(argStr(L, 2, "text")), EventValue(static_cast<int64_t>(argUInt(L, 3, "lang")))});
}
int pWhisper(lua_State* L)
{
    // docs: Whisper(text, lang, receiver, guid); the receiver is a Player, the guid the non-TrinityCore form of it.
    std::vector<EventValue> args{EventValue(argStr(L, 2, "text")),
                                 EventValue(static_cast<int64_t>(argUInt(L, 3, "lang")))};
    if (!lua_isnoneornil(L, 4))
    {
        if (fl::toHandleAny(L, 4))
            args.emplace_back(objArg(L, 4, kPlayer, "receiver"));
        else
        {
            fl::Guid guid;
            argGuid(L, 4, guid);
            args.emplace_back(fl::guidText(guid));
        }
    }
    else if (!lua_isnoneornil(L, 5))
    {
        fl::Guid guid;
        argGuid(L, 5, guid);
        args.emplace_back(fl::guidText(guid));
    }
    return actionOp(L, kPlayer, "player.whisper", "Whisper", args);
}
int pTextEmote(lua_State* L)
{
    return actionOp(L, kPlayer, "player.textEmote", "TextEmote", {EventValue(argStr(L, 2, "emoteText"))});
}

// --- UI sends ---
int pSendShowBank(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendShowBank", "SendShowBank",
                    {EventValue(objArg(L, 2, kWorldObject, "sender"))});
}
int pSendListInventory(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendListInventory", "SendListInventory",
                    {EventValue(objArg(L, 2, kWorldObject, "sender"))});
}
int pSendTrainerList(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendTrainerList", "SendTrainerList",
                    {EventValue(objArg(L, 2, kCreature, "sender"))});
}
int pSendTaxiMenu(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendTaxiMenu", "SendTaxiMenu",
                    {EventValue(objArg(L, 2, kCreature, "sender"))});
}

// --- bind and summon ---
int pSetBindPoint(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setBindPoint", "SetBindPoint",
                    {EventValue(static_cast<double>(argFloat(L, 2, "x"))),
                     EventValue(static_cast<double>(argFloat(L, 3, "y"))),
                     EventValue(static_cast<double>(argFloat(L, 4, "z"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 5, "mapId"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 6, "areaId")))});
}
int pSummonPlayer(lua_State* L)
{
    return actionOp(L, kPlayer, "player.summonPlayer", "SummonPlayer",
                    {EventValue(objArg(L, 2, kUnit, "summoner"))});
}

// --- misc ---
int pGetGuildName(lua_State* L) { return readOp(L, kPlayer, "player.getGuildName", "GetGuildName"); }
int pGetDbLocaleIndex(lua_State* L) { return readOp(L, kPlayer, "player.getDbLocaleIndex", "GetDbLocaleIndex"); }
int pGetPlayerIP(lua_State* L) { return readOp(L, kPlayer, "player.getPlayerIP", "GetPlayerIP"); }
int pGetLatency(lua_State* L) { return readOp(L, kPlayer, "player.getLatency", "GetLatency"); }
int pGetTotalPlayedTime(lua_State* L) { return readOp(L, kPlayer, "player.getTotalPlayedTime", "GetTotalPlayedTime"); }
int pGetLevelPlayedTime(lua_State* L) { return readOp(L, kPlayer, "player.getLevelPlayedTime", "GetLevelPlayedTime"); }
int pLogoutPlayer(lua_State* L)
{
    return actionOp(L, kPlayer, "player.logoutPlayer", "LogoutPlayer", {EventValue(optBool(L, 2, true))});
}
int pDurabilityRepairAll(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityRepairAll", "DurabilityRepairAll",
                    {EventValue(optBool(L, 2, true)),
                     EventValue(static_cast<double>(optFloat(L, 3, 1.0f, "discount"))),
                     EventValue(optBool(L, 4, false))});
}

// ---- E7 long tail, round 2: the documented reads that the core answers directly (docs Player/index.html). None of them
// changes the player; each checks the docs' argument types and hands the op to the host.

int pCanBlock(lua_State* L) { return readOp(L, kPlayer, "player.canBlock", "CanBlock"); }
int pCanCompleteRepeatableQuest(lua_State* L)
{
    return readOp(L, kPlayer, "player.canCompleteRepeatableQuest", "CanCompleteRepeatableQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pCanFly(lua_State* L) { return readOp(L, kPlayer, "player.canFly", "CanFly"); }
int pCanParry(lua_State* L) { return readOp(L, kPlayer, "player.canParry", "CanParry"); }
int pCanShareQuest(lua_State* L)
{
    return readOp(L, kPlayer, "player.canShareQuest", "CanShareQuest",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pCanSpeak(lua_State* L) { return readOp(L, kPlayer, "player.canSpeak", "CanSpeak"); }
int pCanUninviteFromGroup(lua_State* L)
{
    fl::Guid guid;
    argGuid(L, 2, guid);
    return readOp(L, kPlayer, "player.canUninviteFromGroup", "CanUninviteFromGroup", {EventValue(fl::guidText(guid))});
}
int pGetChampioningFaction(lua_State* L)
{
    return readOp(L, kPlayer, "player.getChampioningFaction", "GetChampioningFaction");
}
int pGetDbcLocale(lua_State* L) { return readOp(L, kPlayer, "player.getDbcLocale", "GetDbcLocale"); }
int pGetDifficulty(lua_State* L) { return readOp(L, kPlayer, "player.getDifficulty", "GetDifficulty"); }
int pGetDrunkValue(lua_State* L) { return readOp(L, kPlayer, "player.getDrunkValue", "GetDrunkValue"); }
int pGetGroupInvite(lua_State* L) { return readHandleOp(L, kPlayer, "player.getGroupInvite", "GetGroupInvite"); }
int pGetGuildRank(lua_State* L) { return readOp(L, kPlayer, "player.getGuildRank", "GetGuildRank"); }
int pGetHealthBonusFromStamina(lua_State* L)
{
    return readOp(L, kPlayer, "player.getHealthBonusFromStamina", "GetHealthBonusFromStamina");
}
int pGetInGameTime(lua_State* L) { return readOp(L, kPlayer, "player.getInGameTime", "GetInGameTime"); }
int pGetMailCount(lua_State* L) { return readOp(L, kPlayer, "player.getMailCount", "GetMailCount"); }
int pGetManaBonusFromIntellect(lua_State* L)
{
    return readOp(L, kPlayer, "player.getManaBonusFromIntellect", "GetManaBonusFromIntellect");
}
int pGetNearbyGameObject(lua_State* L)
{
    return readHandleOp(L, kPlayer, "player.getNearbyGameObject", "GetNearbyGameObject");
}
int pGetNextRandomRaidMember(lua_State* L)
{
    return readHandleOp(L, kPlayer, "player.getNextRandomRaidMember", "GetNextRandomRaidMember",
                        {EventValue(static_cast<double>(argFloat(L, 2, "radius")))});
}
int pGetOriginalGroup(lua_State* L) { return readHandleOp(L, kPlayer, "player.getOriginalGroup", "GetOriginalGroup"); }
int pGetOriginalSubGroup(lua_State* L) { return readOp(L, kPlayer, "player.getOriginalSubGroup", "GetOriginalSubGroup"); }
int pGetQuestLevel(lua_State* L)
{
    return readOp(L, kPlayer, "player.getQuestLevel", "GetQuestLevel",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}
int pGetRecruiterId(lua_State* L) { return readOp(L, kPlayer, "player.getRecruiterId", "GetRecruiterId"); }
int pGetSelectedPlayer(lua_State* L) { return readHandleOp(L, kPlayer, "player.getSelectedPlayer", "GetSelectedPlayer"); }
int pGetSelectedUnit(lua_State* L) { return readHandleOp(L, kPlayer, "player.getSelectedUnit", "GetSelectedUnit"); }
int pGetShieldBlockValue(lua_State* L) { return readOp(L, kPlayer, "player.getShieldBlockValue", "GetShieldBlockValue"); }
int pGetSubGroup(lua_State* L) { return readOp(L, kPlayer, "player.getSubGroup", "GetSubGroup"); }
int pGetXPRestBonus(lua_State* L)
{
    return readOp(L, kPlayer, "player.getXPRestBonus", "GetXPRestBonus",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "xp")))});
}
int pHasAtLoginFlag(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasAtLoginFlag", "HasAtLoginFlag",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "flag")))});
}
int pHasPendingBind(lua_State* L) { return readOp(L, kPlayer, "player.hasPendingBind", "HasPendingBind"); }
int pHasTalent(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasTalent", "HasTalent",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "spellId"))),
                   EventValue(static_cast<int64_t>(argU8(L, 3, "spec")))});
}
int pHasTitle(lua_State* L)
{
    return readOp(L, kPlayer, "player.hasTitle", "HasTitle",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "bitIndex")))});
}
int pInArena(lua_State* L) { return readOp(L, kPlayer, "player.inArena", "InArena"); }
int pInBattlegroundQueue(lua_State* L)
{
    return readOp(L, kPlayer, "player.inBattlegroundQueue", "InBattlegroundQueue", {EventValue(optBool(L, 2, false))});
}
int pIsAcceptingWhispers(lua_State* L)
{
    return readOp(L, kPlayer, "player.isAcceptingWhispers", "IsAcceptingWhispers");
}
int pIsARecruiter(lua_State* L) { return readOp(L, kPlayer, "player.isARecruiter", "IsARecruiter"); }
int pIsGMChat(lua_State* L) { return readOp(L, kPlayer, "player.isGMChat", "IsGMChat"); }
int pIsGMVisible(lua_State* L) { return readOp(L, kPlayer, "player.isGMVisible", "IsGMVisible"); }
int pIsGroupVisibleFor(lua_State* L)
{
    return readOp(L, kPlayer, "player.isGroupVisibleFor", "IsGroupVisibleFor",
                  {EventValue(objArg(L, 2, kPlayer, "player"))});
}
int pIsInSameGroupWith(lua_State* L)
{
    return readOp(L, kPlayer, "player.isInSameGroupWith", "IsInSameGroupWith",
                  {EventValue(objArg(L, 2, kPlayer, "player"))});
}
int pIsInSameRaidWith(lua_State* L)
{
    return readOp(L, kPlayer, "player.isInSameRaidWith", "IsInSameRaidWith",
                  {EventValue(objArg(L, 2, kPlayer, "player"))});
}
int pIsNeverVisible(lua_State* L) { return readOp(L, kPlayer, "player.isNeverVisible", "IsNeverVisible"); }
int pIsOutdoorPvPActive(lua_State* L) { return readOp(L, kPlayer, "player.isOutdoorPvPActive", "IsOutdoorPvPActive"); }
int pResetTalentsCost(lua_State* L) { return readOp(L, kPlayer, "player.resetTalentsCost", "ResetTalentsCost"); }

// ---- E7 long tail, round 2: the documented writes that only change the receiver (no group or guild member, rank or
// bank is touched). A write the docs give a return value (ResetTalents) uses readOp so the answer reaches the script.

int pSendCinematicStart(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendCinematicStart", "SendCinematicStart",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "cinematicId")))});
}
int pSendMovieStart(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendMovieStart", "SendMovieStart",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "movieId")))});
}
int pSendUpdateWorldState(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendUpdateWorldState", "SendUpdateWorldState",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "variable"))),
                     EventValue(static_cast<int64_t>(argUInt(L, 3, "value"))), EventValue(optBool(L, 4, false))});
}
int pSendSpiritResurrect(lua_State* L)
{
    return actionOp(L, kPlayer, "player.sendSpiritResurrect", "SendSpiritResurrect");
}
int pSetAcceptWhispers(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setAcceptWhispers", "SetAcceptWhispers", {EventValue(argBool(L, 2, "on"))});
}
int pSetAtLoginFlag(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setAtLoginFlag", "SetAtLoginFlag",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "flag")))});
}
int pSetDrunkValue(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setDrunkValue", "SetDrunkValue",
                    {EventValue(static_cast<int64_t>(argU8(L, 2, "value")))});
}
int pSetFactionForRace(lua_State* L)
{
    uint32_t const race = argU8(L, 2, "race");
    fl::HandleData const& h = self(L, kPlayer);
    std::vector<EventValue> out;
    gameCall(L, h, "player.setFactionForRace", {EventValue(static_cast<int64_t>(race))}, out, "SetFactionForRace");
    requireHostOk(L, out, "SetFactionForRace", "unknown race");
    return 0;
}
int pSetGameMaster(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setGameMaster", "SetGameMaster", {EventValue(argBool(L, 2, "on"))});
}
int pSetGender(lua_State* L)
{
    lua_Integer const gender = luaL_checkinteger(L, 2);
    if (gender < 0 || gender > 1)
        luaL_error(L, "gender must be an integer 0..1");
    return actionOp(L, kPlayer, "player.setGender", "SetGender", {EventValue(static_cast<int64_t>(gender))});
}
int pSetGMChat(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setGMChat", "SetGMChat", {EventValue(argBool(L, 2, "on"))});
}
int pSetGMVisible(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setGMVisible", "SetGMVisible", {EventValue(argBool(L, 2, "on"))});
}
int pSetPvPDeath(lua_State* L)
{
    return actionOp(L, kPlayer, "player.setPvPDeath", "SetPvPDeath", {EventValue(argBool(L, 2, "on"))});
}
int pSpawnBones(lua_State* L) { return actionOp(L, kPlayer, "player.spawnBones", "SpawnBones"); }
int pKillPlayer(lua_State* L) { return actionOp(L, kPlayer, "player.killPlayer", "KillPlayer"); }
int pResetTalents(lua_State* L)
{
    return readOp(L, kPlayer, "player.resetTalents", "ResetTalents", {EventValue(optBool(L, 2, false))});
}

// --- durability ---
int pDurabilityLoss(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityLoss", "DurabilityLoss",
                    {EventValue(objArg(L, 2, kItem, "item")), EventValue(durabilityPercentArg(L, 3))});
}
int pDurabilityLossAll(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityLossAll", "DurabilityLossAll",
                    {EventValue(durabilityPercentArg(L, 2)), EventValue(optBool(L, 3, true))});
}
int pDurabilityPointLossForEquipSlot(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityPointLossForEquipSlot", "DurabilityPointLossForEquipSlot",
                    {EventValue(static_cast<int64_t>(argU8(L, 2, "slot")))});
}
int pDurabilityPointsLoss(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityPointsLoss", "DurabilityPointsLoss",
                    {EventValue(objArg(L, 2, kItem, "item")), EventValue(static_cast<int64_t>(argInt(L, 3, "points")))});
}
int pDurabilityPointsLossAll(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityPointsLossAll", "DurabilityPointsLossAll",
                    {EventValue(static_cast<int64_t>(argInt(L, 2, "points"))), EventValue(optBool(L, 3, true))});
}
int pDurabilityRepair(lua_State* L)
{
    return actionOp(L, kPlayer, "player.durabilityRepair", "DurabilityRepair",
                    {EventValue(static_cast<int64_t>(argU16(L, 2, "pos"))), EventValue(optBool(L, 3, true)),
                     EventValue(static_cast<double>(optFloat(L, 4, 1.0f, "discount")))});
}
}  // namespace

void registerPlayerMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"SendBroadcastMessage", fl::safe<pSendBroadcastMessage>},
        {"SendNotification", fl::safe<pSendNotification>},
        {"GetAccountId", fl::safe<pGetAccountId>},
        {"GetAccountName", fl::safe<pGetAccountName>},
        {"GetGMRank", fl::safe<pGetGMRank>},
        {"IsGM", fl::safe<pIsGM>},
        {"GetTeam", fl::safe<pGetTeam>},
        {"GetCoinage", fl::safe<pGetCoinage>},
        {"ModifyMoney", fl::safe<pModifyMoney>},
        {"AddItem", fl::safe<pAddItem>},
        {"RemoveItem", fl::safe<pRemoveItem>},
        {"HasItem", fl::safe<pHasItem>},
        {"GetItemCount", fl::safe<pGetItemCount>},
        {"Teleport", fl::safe<pTeleport>},
        {"GiveXP", fl::safe<pGiveXP>},
        {"LearnSpell", fl::safe<pLearnSpell>},
        {"RemoveSpell", fl::safe<pRemoveSpell>},
        {"HasSpell", fl::safe<pHasSpell>},
        {"GetGroup", fl::safe<pGetGroup>},
        {"GetGuild", fl::safe<pGetGuild>},
        {"GetGuildId", fl::safe<pGetGuildId>},
        {"GetQuestStatus", fl::safe<pGetQuestStatus>},
        {"AddQuest", fl::safe<pAddQuest>},
        {"CompleteQuest", fl::safe<pCompleteQuest>},
        {"HasQuest", fl::safe<pHasQuest>},
        {"SaveToDB", fl::safe<pSaveToDB>},
        {"KickPlayer", fl::safe<pKickPlayer>},
        {"ResurrectPlayer", fl::safe<pResurrectPlayer>},
        {"GetSelection", fl::safe<pGetSelection>},
        {"IsAlliance", fl::safe<pIsAlliance>},
        {"IsHorde", fl::safe<pIsHorde>},
        {"GetCorpse", fl::safe<pGetCorpse>},
        {"SendPacket", fl::safe<pSendPacket>},
        // E5 long tail, round 1.
        {"SetCoinage", fl::safe<pSetCoinage>},
        {"GetXP", fl::safe<pGetXP>},
        {"GetXPForNextLevel", fl::safe<pGetXPForNextLevel>},
        {"GetRestBonus", fl::safe<pGetRestBonus>},
        {"SetRestBonus", fl::safe<pSetRestBonus>},
        {"IsRested", fl::safe<pIsRested>},
        {"GetHonorPoints", fl::safe<pGetHonorPoints>},
        {"SetHonorPoints", fl::safe<pSetHonorPoints>},
        {"ModifyHonorPoints", fl::safe<pModifyHonorPoints>},
        {"GetReputation", fl::safe<pGetReputation>},
        {"SetReputation", fl::safe<pSetReputation>},
        {"GetReputationRank", fl::safe<pGetReputationRank>},
        {"GetSkillValue", fl::safe<pGetSkillValue>},
        {"GetMaxSkillValue", fl::safe<pGetMaxSkillValue>},
        {"GetPureSkillValue", fl::safe<pGetPureSkillValue>},
        {"GetPureMaxSkillValue", fl::safe<pGetPureMaxSkillValue>},
        {"GetBaseSkillValue", fl::safe<pGetBaseSkillValue>},
        {"GetSkillPermBonusValue", fl::safe<pGetSkillPermBonusValue>},
        {"GetSkillTempBonusValue", fl::safe<pGetSkillTempBonusValue>},
        {"HasSkill", fl::safe<pHasSkill>},
        {"SetSkill", fl::safe<pSetSkill>},
        {"ResetSpellCooldown", fl::safe<pResetSpellCooldown>},
        {"HasSpellCooldown", fl::safe<pHasSpellCooldown>},
        {"GetSpellCooldownDelay", fl::safe<pGetSpellCooldownDelay>},
        {"ResetAllCooldowns", fl::safe<pResetAllCooldowns>},
        {"ResetTypeCooldowns", fl::safe<pResetTypeCooldowns>},
        {"GetItemByPos", fl::safe<pGetItemByPos>},
        {"GetItemByEntry", fl::safe<pGetItemByEntry>},
        {"GetItemByGUID", fl::safe<pGetItemByGUID>},
        {"GetEquippedItemBySlot", fl::safe<pGetEquippedItemBySlot>},
        {"EquipItem", fl::safe<pEquipItem>},
        {"CanEquipItem", fl::safe<pCanEquipItem>},
        {"CanUseItem", fl::safe<pCanUseItem>},
        {"GetQuestRewardStatus", fl::safe<pGetQuestRewardStatus>},
        {"IncompleteQuest", fl::safe<pIncompleteQuest>},
        {"FailQuest", fl::safe<pFailQuest>},
        {"RemoveQuest", fl::safe<pRemoveQuest>},
        {"RemoveActiveQuest", fl::safe<pRemoveActiveQuest>},
        {"RemoveRewardedQuest", fl::safe<pRemoveRewardedQuest>},
        {"CanCompleteQuest", fl::safe<pCanCompleteQuest>},
        {"CanRewardQuest", fl::safe<pCanRewardQuest>},
        {"RewardQuest", fl::safe<pRewardQuest>},
        {"AreaExploredOrEventHappens", fl::safe<pAreaExploredOrEventHappens>},
        {"KilledMonsterCredit", fl::safe<pKilledMonsterCredit>},
        {"GroupEventHappens", fl::safe<pGroupEventHappens>},
        {"TalkedToCreature", fl::safe<pTalkedToCreature>},
        {"HasQuestForGO", fl::safe<pHasQuestForGO>},
        {"HasQuestForItem", fl::safe<pHasQuestForItem>},
        {"GetReqKillOrCastCurrentCount", fl::safe<pGetReqKillOrCastCurrentCount>},
        {"HasReceivedQuestReward", fl::safe<pHasReceivedQuestReward>},
        {"IsInGroup", fl::safe<pIsInGroup>},
        {"IsInGuild", fl::safe<pIsInGuild>},
        {"IsFlying", fl::safe<pIsFlying>},
        {"IsMoving", fl::safe<pIsMoving>},
        {"IsFalling", fl::safe<pIsFalling>},
        {"IsInWater", fl::safe<pIsInWater>},
        {"IsTaxiCheater", fl::safe<pIsTaxiCheater>},
        {"SetTaxiCheat", fl::safe<pSetTaxiCheat>},
        {"InBattleground", fl::safe<pInBattleground>},
        {"GetBattlegroundId", fl::safe<pGetBattlegroundId>},
        {"GetBattlegroundTypeId", fl::safe<pGetBattlegroundTypeId>},
        {"IsAFK", fl::safe<pIsAFK>},
        {"IsDND", fl::safe<pIsDND>},
        {"ToggleAFK", fl::safe<pToggleAFK>},
        {"ToggleDND", fl::safe<pToggleDND>},
        {"SetFFA", fl::safe<pSetFFA>},
        {"IsImmuneToDamage", fl::safe<pIsImmuneToDamage>},
        {"IsImmuneToEnvironmentalDamage", fl::safe<pIsImmuneToEnvironmentalDamage>},
        {"Say", fl::safe<pSay>},
        {"Yell", fl::safe<pYell>},
        {"Whisper", fl::safe<pWhisper>},
        {"TextEmote", fl::safe<pTextEmote>},
        {"SendShowBank", fl::safe<pSendShowBank>},
        {"SendListInventory", fl::safe<pSendListInventory>},
        {"SendTrainerList", fl::safe<pSendTrainerList>},
        {"SendTaxiMenu", fl::safe<pSendTaxiMenu>},
        {"SetBindPoint", fl::safe<pSetBindPoint>},
        {"SummonPlayer", fl::safe<pSummonPlayer>},
        {"GetGuildName", fl::safe<pGetGuildName>},
        {"GetDbLocaleIndex", fl::safe<pGetDbLocaleIndex>},
        {"GetPlayerIP", fl::safe<pGetPlayerIP>},
        {"GetLatency", fl::safe<pGetLatency>},
        {"GetTotalPlayedTime", fl::safe<pGetTotalPlayedTime>},
        {"GetLevelPlayedTime", fl::safe<pGetLevelPlayedTime>},
        {"LogoutPlayer", fl::safe<pLogoutPlayer>},
        {"DurabilityRepairAll", fl::safe<pDurabilityRepairAll>},
        // E7 long tail, round 2: the reads.
        {"CanBlock", fl::safe<pCanBlock>},
        {"CanCompleteRepeatableQuest", fl::safe<pCanCompleteRepeatableQuest>},
        {"CanFly", fl::safe<pCanFly>},
        {"CanParry", fl::safe<pCanParry>},
        {"CanShareQuest", fl::safe<pCanShareQuest>},
        {"CanSpeak", fl::safe<pCanSpeak>},
        {"CanUninviteFromGroup", fl::safe<pCanUninviteFromGroup>},
        {"GetChampioningFaction", fl::safe<pGetChampioningFaction>},
        {"GetDbcLocale", fl::safe<pGetDbcLocale>},
        {"GetDifficulty", fl::safe<pGetDifficulty>},
        {"GetDrunkValue", fl::safe<pGetDrunkValue>},
        {"GetGroupInvite", fl::safe<pGetGroupInvite>},
        {"GetGuildRank", fl::safe<pGetGuildRank>},
        {"GetHealthBonusFromStamina", fl::safe<pGetHealthBonusFromStamina>},
        {"GetInGameTime", fl::safe<pGetInGameTime>},
        {"GetMailCount", fl::safe<pGetMailCount>},
        {"GetManaBonusFromIntellect", fl::safe<pGetManaBonusFromIntellect>},
        {"GetNearbyGameObject", fl::safe<pGetNearbyGameObject>},
        {"GetNextRandomRaidMember", fl::safe<pGetNextRandomRaidMember>},
        {"GetOriginalGroup", fl::safe<pGetOriginalGroup>},
        {"GetOriginalSubGroup", fl::safe<pGetOriginalSubGroup>},
        {"GetQuestLevel", fl::safe<pGetQuestLevel>},
        {"GetRecruiterId", fl::safe<pGetRecruiterId>},
        {"GetSelectedPlayer", fl::safe<pGetSelectedPlayer>},
        {"GetSelectedUnit", fl::safe<pGetSelectedUnit>},
        {"GetShieldBlockValue", fl::safe<pGetShieldBlockValue>},
        {"GetSubGroup", fl::safe<pGetSubGroup>},
        {"GetXPRestBonus", fl::safe<pGetXPRestBonus>},
        {"HasAtLoginFlag", fl::safe<pHasAtLoginFlag>},
        {"HasPendingBind", fl::safe<pHasPendingBind>},
        {"HasTalent", fl::safe<pHasTalent>},
        {"HasTitle", fl::safe<pHasTitle>},
        {"InArena", fl::safe<pInArena>},
        {"InBattlegroundQueue", fl::safe<pInBattlegroundQueue>},
        {"IsAcceptingWhispers", fl::safe<pIsAcceptingWhispers>},
        {"IsARecruiter", fl::safe<pIsARecruiter>},
        {"IsGMChat", fl::safe<pIsGMChat>},
        {"IsGMVisible", fl::safe<pIsGMVisible>},
        {"IsGroupVisibleFor", fl::safe<pIsGroupVisibleFor>},
        {"IsInSameGroupWith", fl::safe<pIsInSameGroupWith>},
        {"IsInSameRaidWith", fl::safe<pIsInSameRaidWith>},
        {"IsNeverVisible", fl::safe<pIsNeverVisible>},
        {"IsOutdoorPvPActive", fl::safe<pIsOutdoorPvPActive>},
        {"ResetTalentsCost", fl::safe<pResetTalentsCost>},
        // E7 long tail, round 2: the writes.
        {"SendCinematicStart", fl::safe<pSendCinematicStart>},
        {"SendMovieStart", fl::safe<pSendMovieStart>},
        {"SendUpdateWorldState", fl::safe<pSendUpdateWorldState>},
        {"SendSpiritResurrect", fl::safe<pSendSpiritResurrect>},
        {"SetAcceptWhispers", fl::safe<pSetAcceptWhispers>},
        {"SetAtLoginFlag", fl::safe<pSetAtLoginFlag>},
        {"SetDrunkValue", fl::safe<pSetDrunkValue>},
        {"SetFactionForRace", fl::safe<pSetFactionForRace>},
        {"SetGameMaster", fl::safe<pSetGameMaster>},
        {"SetGender", fl::safe<pSetGender>},
        {"SetGMChat", fl::safe<pSetGMChat>},
        {"SetGMVisible", fl::safe<pSetGMVisible>},
        {"SetPvPDeath", fl::safe<pSetPvPDeath>},
        {"SpawnBones", fl::safe<pSpawnBones>},
        {"KillPlayer", fl::safe<pKillPlayer>},
        {"ResetTalents", fl::safe<pResetTalents>},
        {"DurabilityLoss", fl::safe<pDurabilityLoss>},
        {"DurabilityLossAll", fl::safe<pDurabilityLossAll>},
        {"DurabilityPointLossForEquipSlot", fl::safe<pDurabilityPointLossForEquipSlot>},
        {"DurabilityPointsLoss", fl::safe<pDurabilityPointsLoss>},
        {"DurabilityPointsLossAll", fl::safe<pDurabilityPointsLossAll>},
        {"DurabilityRepair", fl::safe<pDurabilityRepair>},
        {nullptr, nullptr}};
    addMethods(L, "Player", regs);
}
}  // namespace flu
