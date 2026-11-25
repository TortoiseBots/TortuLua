---@meta

---@class Player : Unit
local Player = {}

---@param entry any
---@param arg2 any
---@return any
function Player:AddItem(entry, arg2) end

---@param entry any
---@return any
function Player:AddQuest(entry) end

---@param quest any
---@return any
function Player:AreaExploredOrEventHappens(quest) end

---@param entry any
---@return any
function Player:CanCompleteQuest(entry) end

---@param arg1 any
---@param slot any
---@return any
function Player:CanEquipItem(arg1, slot) end

---@param entry any
---@return any
function Player:CanRewardQuest(entry) end

---@param arg1 any
---@return any
function Player:CanUseItem(arg1) end

---@param entry any
---@return any
function Player:CompleteQuest(entry) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function Player:DurabilityRepairAll(arg1, arg2, arg3) end

---@param arg1 any
---@param slot any
---@return any
function Player:EquipItem(arg1, slot) end

---@param entry any
---@return any
function Player:FailQuest(entry) end

---@return any
function Player:GetAccountId() end

---@return any
function Player:GetAccountName() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:GetActiveSpec() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:GetArenaPoints() end

---@param skill any
---@return any
function Player:GetBaseSkillValue(skill) end

---@return any
function Player:GetBattlegroundId() end

---@return any
function Player:GetBattlegroundTypeId() end

---@return any
function Player:GetCoinage() end

---@return any
function Player:GetCorpse() end

---@return any
function Player:GetDbLocaleIndex() end

---@param slot any
---@return any
function Player:GetEquippedItemBySlot(slot) end

---@return any
function Player:GetGMRank() end

---@return any
function Player:GetGroup() end

---@return any
function Player:GetGuild() end

---@return any
function Player:GetGuildId() end

---@return any
function Player:GetGuildName() end

---@return any
function Player:GetHonorPoints() end

---@param entryId any
---@return any
function Player:GetItemByEntry(entryId) end

---@param arg1 any
---@return any
function Player:GetItemByGUID(arg1) end

---@param bag any
---@param slot any
---@return any
function Player:GetItemByPos(bag, slot) end

---@param entry any
---@param arg2 any
---@return any
function Player:GetItemCount(entry, arg2) end

---@return any
function Player:GetLatency() end

---@return any
function Player:GetLevelPlayedTime() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:GetLFGRole() end

---@param skill any
---@return any
function Player:GetMaxSkillValue(skill) end

---@return any
function Player:GetPlayerIP() end

---@param skill any
---@return any
function Player:GetPureMaxSkillValue(skill) end

---@param skill any
---@return any
function Player:GetPureSkillValue(skill) end

---@param questId any
---@return any
function Player:GetQuestRewardStatus(questId) end

---@param questId any
---@return any
function Player:GetQuestStatus(questId) end

---@param faction any
---@return any
function Player:GetReputation(faction) end

---@param faction any
---@return any
function Player:GetReputationRank(faction) end

---@param quest any
---@param entry any
---@return any
function Player:GetReqKillOrCastCurrentCount(quest, entry) end

---@return any
function Player:GetRestBonus() end

---@return any
function Player:GetSelection() end

---@param skill any
---@return any
function Player:GetSkillPermBonusValue(skill) end

---@param skill any
---@return any
function Player:GetSkillTempBonusValue(skill) end

---@param skill any
---@return any
function Player:GetSkillValue(skill) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:GetSpecsCount() end

---@param spellId any
---@return any
function Player:GetSpellCooldownDelay(spellId) end

---@return any
function Player:GetTeam() end

---@return any
function Player:GetTotalPlayedTime() end

---@return any
function Player:GetXP() end

---@return any
function Player:GetXPForNextLevel() end

---@param arg1 any
---@param victim any
---@return any
function Player:GiveXP(arg1, victim) end

---@param source any
---@return any
function Player:GossipAddQuests(source) end

---@return any
function Player:GossipClearMenu() end

---@return any
function Player:GossipComplete() end

---@param icon any
---@param msg any
---@param sender any
---@param intid any
---@param arg5 any
---@param popup any
---@param arg7 any
---@return any
function Player:GossipMenuAddItem(icon, msg, sender, intid, arg5, popup, arg7) end

---@param npc_text any
---@param sender any
---@param menu_id any
---@return any
function Player:GossipSendMenu(npc_text, sender, menu_id) end

---@param quest any
---@param object any
---@return any
function Player:GroupEventHappens(quest, object) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:HasAchieved() end

---@param itemId any
---@param arg2 any
---@param arg3 any
---@return any
function Player:HasItem(itemId, arg2, arg3) end

---@param questId any
---@return any
function Player:HasQuest(questId) end

---@param entry any
---@return any
function Player:HasQuestForGO(entry) end

---@param entry any
---@return any
function Player:HasQuestForItem(entry) end

---@param questId any
---@return any
function Player:HasReceivedQuestReward(questId) end

---@param skill any
---@return any
function Player:HasSkill(skill) end

---@param spellId any
---@return any
function Player:HasSpell(spellId) end

---@param spellId any
---@return any
function Player:HasSpellCooldown(spellId) end

---@return any
function Player:InBattleground() end

---@param entry any
---@return any
function Player:IncompleteQuest(entry) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:InRandomLfgDungeon() end

---@return any
function Player:IsAFK() end

---@return any
function Player:IsAlliance() end

---@return any
function Player:IsDND() end

---@return any
function Player:IsFalling() end

---@return any
function Player:IsFlying() end

---@return any
function Player:IsGM() end

---@return any
function Player:IsHorde() end

---@return any
function Player:IsImmuneToDamage() end

---@return any
function Player:IsImmuneToEnvironmentalDamage() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:IsInArenaTeam() end

---@return any
function Player:IsInGroup() end

---@return any
function Player:IsInGuild() end

---@return any
function Player:IsInWater() end

---@return any
function Player:IsMoving() end

---@return any
function Player:IsRested() end

---@return any
function Player:IsTaxiCheater() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:IsUsingLfg() end

---@return any
function Player:KickPlayer() end

---@param entry any
---@return any
function Player:KilledMonsterCredit(entry) end

---@param spellId any
---@return any
function Player:LearnSpell(spellId) end

---@param arg1 any
---@return any
function Player:LogoutPlayer(arg1) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:ModifyArenaPoints() end

---@param amount any
---@return any
function Player:ModifyHonorPoints(amount) end

---@param copperAmt any
---@return any
function Player:ModifyMoney(copperAmt) end

---@param questId any
---@return any
function Player:RemoveActiveQuest(questId) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:RemoveArenaSpellCooldowns() end

---@param entry any
---@param arg2 any
---@return any
function Player:RemoveItem(entry, arg2) end

---@param entry any
---@return any
function Player:RemoveQuest(entry) end

---@param questId any
---@return any
function Player:RemoveRewardedQuest(questId) end

---@param entry any
---@return any
function Player:RemoveSpell(entry) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:ResetAchievements() end

---@return any
function Player:ResetAllCooldowns() end

---@param spellId any
---@param arg2 any
---@return any
function Player:ResetSpellCooldown(spellId, arg2) end

---@param category any
---@param arg2 any
---@return any
function Player:ResetTypeCooldowns(category, arg2) end

---@param arg1 any
---@param arg2 any
---@return any
function Player:ResurrectPlayer(arg1, arg2) end

---@param entry any
---@return any
function Player:RewardQuest(entry) end

---@return any
function Player:SaveToDB() end

---@param text any
---@param lang any
---@return any
function Player:Say(text, lang) end

---@param message any
---@return any
function Player:SendBroadcastMessage(message) end

---@param sender any
---@return any
function Player:SendListInventory(sender) end

---@param message any
---@return any
function Player:SendNotification(message) end

---@return any
function Player:SendPacket() end

---@param sender any
---@return any
function Player:SendShowBank(sender) end

---@param sender any
---@return any
function Player:SendTaxiMenu(sender) end

---@param sender any
---@return any
function Player:SendTrainerList(sender) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:SetAchievement() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:SetArenaPoints() end

---@param x any
---@param y any
---@param z any
---@param mapId any
---@param areaId any
---@return any
function Player:SetBindPoint(x, y, z, mapId, areaId) end

---@param copperAmt any
---@return any
function Player:SetCoinage(copperAmt) end

---@param arg1 any
---@return any
function Player:SetFFA(arg1) end

---@param honorPoints any
---@return any
function Player:SetHonorPoints(honorPoints) end

---@param factionId any
---@param reputationValue any
---@return any
function Player:SetReputation(factionId, reputationValue) end

---@param restBonus any
---@return any
function Player:SetRestBonus(restBonus) end

---@param id any
---@param step any
---@param currVal any
---@param maxVal any
---@return any
function Player:SetSkill(id, step, currVal, maxVal) end

---@param arg1 any
---@return any
function Player:SetTaxiCheat(arg1) end

---@param summoner any
---@return any
function Player:SummonPlayer(summoner) end

---@param entry any
---@param creature any
---@return any
function Player:TalkedToCreature(entry, creature) end

---@param mapId any
---@param x any
---@param y any
---@param z any
---@param orientation any
---@return any
function Player:Teleport(mapId, x, y, z, orientation) end

---@param emoteText any
---@return any
function Player:TextEmote(emoteText) end

---@return any
function Player:ToggleAFK() end

---@return any
function Player:ToggleDND() end

---@param text any
---@param lang any
---@param receiver any
---@param arg4 any
---@return any
function Player:Whisper(text, lang, receiver, arg4) end

---@param text any
---@param lang any
---@return any
function Player:Yell(text, lang) end
