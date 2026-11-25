---@meta

---@class Creature : Unit
local Creature = {}

---@param lootMode any
---@return any
function Creature:AddLootMode(lootMode) end

---@param victim any
---@param threat any
---@param arg3 any
---@param spell any
---@return any
function Creature:AddThreat(victim, threat, arg3, spell) end

---@param target any
---@return any
function Creature:AttackStart(target) end

---@return any
function Creature:CallAssistance() end

---@param radius any
---@return any
function Creature:CallForHelp(radius) end

---@param friend any
---@param enemy any
---@param arg3 any
---@return any
function Creature:CanAssistTo(friend, enemy, arg3) end

---@return any
function Creature:CanFly() end

---@param target any
---@param arg2 any
---@return any
function Creature:CanStartAttack(target, arg2) end

---@return any
function Creature:CanSwim() end

---@return any
function Creature:ClearFixate() end

---@param victim any
---@return any
function Creature:ClearThreat(victim) end

---@return any
function Creature:ClearThreatList() end

---@param arg1 any
---@return any
function Creature:DespawnOrUnsummon(arg1) end

---@param target any
---@return any
function Creature:FixateTarget(target) end

---@return any
function Creature:FleeToGetAssistance() end

---@param target any
---@return any
function Creature:GetAggroRange(target) end

---@return any
function Creature:GetAIName() end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@param aura any
---@return any
function Creature:GetAITarget(arg1, arg2, arg3, arg4, aura) end

---@return any
function Creature:GetAITargets() end

---@return any
function Creature:GetAITargetsCount() end

---@param target any
---@return any
function Creature:GetAttackDistance(target) end

---@return any
function Creature:GetCorpseDelay() end

---@return any
function Creature:GetCreatureFamily() end

---@param spellID any
---@return any
function Creature:GetCreatureSpellCooldownDelay(spellID) end

---@return any
function Creature:GetCurrentWaypointId() end

---@return any
function Creature:GetDBTableGUIDLow() end

---@return any
function Creature:GetDefaultMovementType() end

---@return any
function Creature:GetExtraFlags() end

---@return any
function Creature:GetHomePosition() end

---@return any
function Creature:GetLootMode() end

---@return any
function Creature:GetLootRecipient() end

---@return any
function Creature:GetLootRecipientGroup() end

---@return any
function Creature:GetNPCFlags() end

---@return any
function Creature:GetRank() end

---@return any
function Creature:GetRespawnDelay() end

---@return any
function Creature:GetScriptId() end

---@return any
function Creature:GetScriptName() end

---@return any
function Creature:GetShieldBlockValue() end

---@return any
function Creature:GetSpawnId() end

---@param victim any
---@return any
function Creature:GetThreat(victim) end

---@return any
function Creature:GetWanderRadius() end

---@return any
function Creature:GetWaypointPath() end

---@param spellId any
---@return any
function Creature:HasCategoryCooldown(spellId) end

---@param lootMode any
---@return any
function Creature:HasLootMode(lootMode) end

---@return any
function Creature:HasLootRecipient() end

---@param questId any
---@return any
function Creature:HasQuest(questId) end

---@return any
function Creature:HasSearchedAssistance() end

---@param spell any
---@return any
function Creature:HasSpell(spell) end

---@param spellId any
---@return any
function Creature:HasSpellCooldown(spellId) end

---@return any
function Creature:IsBoss() end

---@return any
function Creature:IsCivilian() end

---@return any
function Creature:IsDungeonBoss() end

---@return any
function Creature:IsElite() end

---@return any
function Creature:IsGuard() end

---@return any
function Creature:IsInEvadeMode() end

---@return any
function Creature:IsRacialLeader() end

---@return any
function Creature:IsRegeneratingHealth() end

---@return any
function Creature:IsReputationGainDisabled() end

---@param player any
---@return any
function Creature:IsTappedBy(player) end

---@return any
function Creature:IsTrigger() end

---@return any
function Creature:IsWorldBoss() end

---@return any
function Creature:MoveWaypoint() end

---@return any
function Creature:RemoveCorpse() end

---@param lootMode any
---@return any
function Creature:RemoveLootMode(lootMode) end

---@return any
function Creature:ResetAllThreat() end

---@return any
function Creature:ResetLootMode() end

---@return any
function Creature:Respawn() end

---@return any
function Creature:SaveToDB() end

---@return any
function Creature:SelectVictim() end

---@param arg1 any
---@return any
function Creature:SetDeathState(arg1) end

---@param arg1 any
---@return any
function Creature:SetDefaultMovementType(arg1) end

---@param disable any
---@return any
function Creature:SetDisableGravity(disable) end

---@param arg1 any
---@return any
function Creature:SetDisableReputationGain(arg1) end

---@param main_hand any
---@param off_hand any
---@param ranged any
---@return any
function Creature:SetEquipmentSlots(main_hand, off_hand, ranged) end

---@param x any
---@param y any
---@param z any
---@param o any
---@return any
function Creature:SetHomePosition(x, y, z, o) end

---@param arg1 any
---@return any
function Creature:SetHover(arg1) end

---@return any
function Creature:SetInCombatWithZone() end

---@param lootMode any
---@return any
function Creature:SetLootMode(lootMode) end

---@param arg1 any
---@return any
function Creature:SetNoCallAssistance(arg1) end

---@param arg1 any
---@return any
function Creature:SetNoSearchAssistance(arg1) end

---@param flags any
---@return any
function Creature:SetNPCFlags(flags) end

---@param state any
---@return any
function Creature:SetReactState(state) end

---@param arg1 any
---@return any
function Creature:SetRegeneratingHealth(arg1) end

---@param arg1 any
---@return any
function Creature:SetRespawnDelay(arg1) end

---@param arg1 any
---@return any
function Creature:SetWalk(arg1) end

---@param distance any
---@return any
function Creature:SetWanderRadius(distance) end

---@param entry any
---@param arg2 any
---@return any
function Creature:UpdateEntry(entry, arg2) end
