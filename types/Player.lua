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

---@return any
function Player:CanBlock() end

---@param entry any
---@return any
function Player:CanCompleteQuest(entry) end

---@param questId any
---@return any
function Player:CanCompleteRepeatableQuest(questId) end

---@param arg1 any
---@param slot any
---@return any
function Player:CanEquipItem(arg1, slot) end

---@return any
function Player:CanFly() end

---@return any
function Player:CanParry() end

---@param entry any
---@return any
function Player:CanRewardQuest(entry) end

---@param questId any
---@return any
function Player:CanShareQuest(questId) end

---@return any
function Player:CanSpeak() end

---@param arg1 any
---@return any
function Player:CanUninviteFromGroup(arg1) end

---@param arg1 any
---@return any
function Player:CanUseItem(arg1) end

---@param entry any
---@return any
function Player:CompleteQuest(entry) end

---@param item any
---@param arg2 any
---@return any
function Player:DurabilityLoss(item, arg2) end

---@param arg1 any
---@param arg2 any
---@return any
function Player:DurabilityLossAll(arg1, arg2) end

---@param slot any
---@return any
function Player:DurabilityPointLossForEquipSlot(slot) end

---@param item any
---@param points any
---@return any
function Player:DurabilityPointsLoss(item, points) end

---@param points any
---@param arg2 any
---@return any
function Player:DurabilityPointsLossAll(points, arg2) end

---@param pos any
---@param arg2 any
---@param discount any
---@return any
function Player:DurabilityRepair(pos, arg2, discount) end

---@param arg1 any
---@param discount any
---@param arg3 any
---@return any
function Player:DurabilityRepairAll(arg1, discount, arg3) end

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
function Player:GetChampioningFaction() end

---@deprecated not available (this core)
---@return any
function Player:GetChatTag() end

---@return any
function Player:GetCoinage() end

---@return any
function Player:GetCorpse() end

---@return any
function Player:GetDbcLocale() end

---@return any
function Player:GetDbLocaleIndex() end

---@return any
function Player:GetDifficulty() end

---@return any
function Player:GetDrunkValue() end

---@param slot any
---@return any
function Player:GetEquippedItemBySlot(slot) end

---@deprecated not available (this core)
---@return any
function Player:GetFreeTalentPoints() end

---@return any
function Player:GetGMRank() end

---@return any
function Player:GetGroup() end

---@return any
function Player:GetGroupInvite() end

---@return any
function Player:GetGuild() end

---@return any
function Player:GetGuildId() end

---@return any
function Player:GetGuildName() end

---@return any
function Player:GetGuildRank() end

---@return any
function Player:GetHealthBonusFromStamina() end

---@return any
function Player:GetHonorPoints() end

---@return any
function Player:GetInGameTime() end

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

---@deprecated not available (this core)
---@return any
function Player:GetLifetimeKills() end

---@return any
function Player:GetMailCount() end

---@deprecated not available (this core)
---@return any
function Player:GetMailItem() end

---@return any
function Player:GetManaBonusFromIntellect() end

---@param skill any
---@return any
function Player:GetMaxSkillValue(skill) end

---@return any
function Player:GetNearbyGameObject() end

---@param radius any
---@return any
function Player:GetNextRandomRaidMember(radius) end

---@return any
function Player:GetOriginalGroup() end

---@return any
function Player:GetOriginalSubGroup() end

---@deprecated not available (this core)
---@return any
function Player:GetPhaseMaskForSpawn() end

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
function Player:GetQuestLevel(questId) end

---@param questId any
---@return any
function Player:GetQuestRewardStatus(questId) end

---@param questId any
---@return any
function Player:GetQuestStatus(questId) end

---@return any
function Player:GetRecruiterId() end

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
function Player:GetSelectedPlayer() end

---@return any
function Player:GetSelectedUnit() end

---@return any
function Player:GetSelection() end

---@return any
function Player:GetShieldBlockValue() end

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
function Player:GetSubGroup() end

---@return any
function Player:GetTeam() end

---@return any
function Player:GetTotalPlayedTime() end

---@return any
function Player:GetXP() end

---@return any
function Player:GetXPForNextLevel() end

---@param xp any
---@return any
function Player:GetXPRestBonus(xp) end

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

---@param flag any
---@return any
function Player:HasAtLoginFlag(flag) end

---@param itemId any
---@param arg2 any
---@param arg3 any
---@return any
function Player:HasItem(itemId, arg2, arg3) end

---@return any
function Player:HasPendingBind() end

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

---@param spellId any
---@param spec any
---@return any
function Player:HasTalent(spellId, spec) end

---@param bitIndex any
---@return any
function Player:HasTitle(bitIndex) end

---@return any
function Player:InArena() end

---@return any
function Player:InBattleground() end

---@param arg1 any
---@return any
function Player:InBattlegroundQueue(arg1) end

---@param entry any
---@return any
function Player:IncompleteQuest(entry) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:InRandomLfgDungeon() end

---@return any
function Player:IsAcceptingWhispers() end

---@return any
function Player:IsAFK() end

---@return any
function Player:IsAlliance() end

---@return any
function Player:IsARecruiter() end

---@return any
function Player:IsDND() end

---@return any
function Player:IsFalling() end

---@return any
function Player:IsFlying() end

---@return any
function Player:IsGM() end

---@return any
function Player:IsGMChat() end

---@return any
function Player:IsGMVisible() end

---@param player any
---@return any
function Player:IsGroupVisibleFor(player) end

---@deprecated not available (this core)
---@return any
function Player:IsHonorOrXPTarget() end

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

---@param player any
---@return any
function Player:IsInSameGroupWith(player) end

---@param player any
---@return any
function Player:IsInSameRaidWith(player) end

---@return any
function Player:IsInWater() end

---@return any
function Player:IsMoving() end

---@return any
function Player:IsNeverVisible() end

---@return any
function Player:IsOutdoorPvPActive() end

---@return any
function Player:IsRested() end

---@return any
function Player:IsTaxiCheater() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:IsUsingLfg() end

---@deprecated not available (this core)
---@return any
function Player:IsVisibleForPlayer() end

---@return any
function Player:KickPlayer() end

---@param entry any
---@return any
function Player:KilledMonsterCredit(entry) end

---@return any
function Player:KillPlayer() end

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

---@param arg1 any
---@return any
function Player:ResetTalents(arg1) end

---@return any
function Player:ResetTalentsCost() end

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

---@param cinematicId any
---@return any
function Player:SendCinematicStart(cinematicId) end

---@param sender any
---@return any
function Player:SendListInventory(sender) end

---@param movieId any
---@return any
function Player:SendMovieStart(movieId) end

---@param message any
---@return any
function Player:SendNotification(message) end

---@return any
function Player:SendPacket() end

---@param sender any
---@return any
function Player:SendShowBank(sender) end

---@return any
function Player:SendSpiritResurrect() end

---@param sender any
---@return any
function Player:SendTaxiMenu(sender) end

---@param sender any
---@return any
function Player:SendTrainerList(sender) end

---@param variable any
---@param value any
---@param arg3 any
---@return any
function Player:SendUpdateWorldState(variable, value, arg3) end

---@param on any
---@return any
function Player:SetAcceptWhispers(on) end

---@deprecated not available (Classic 1.60)
---@return any
function Player:SetAchievement() end

---@deprecated not available (Classic 1.60)
---@return any
function Player:SetArenaPoints() end

---@param flag any
---@return any
function Player:SetAtLoginFlag(flag) end

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

---@param value any
---@return any
function Player:SetDrunkValue(value) end

---refuses a race id the core's race store does not know with an error and leaves team and faction unchanged
---@param race any
---@return any
function Player:SetFactionForRace(race) end

---@param on any
---@return any
function Player:SetGameMaster(on) end

---@param arg1 any
---@return any
function Player:SetGender(arg1) end

---@param on any
---@return any
function Player:SetGMChat(on) end

---@param on any
---@return any
function Player:SetGMVisible(on) end

---@param honorPoints any
---@return any
function Player:SetHonorPoints(honorPoints) end

---@param on any
---@return any
function Player:SetPvPDeath(on) end

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

---@return any
function Player:SpawnBones() end

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
