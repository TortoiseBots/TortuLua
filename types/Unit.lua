---@meta

---@class Unit : WorldObject
local Unit = {}

---@param spell any
---@param target any
---@return any
function Unit:AddAura(spell, target) end

---@param statType any
---@param modType any
---@param value any
---@param arg4 any
---@return any
function Unit:AddFlatStatModifier(statType, modType, value, arg4) end

---@param statType any
---@param modType any
---@param value any
---@return any
function Unit:AddPctStatModifier(statType, modType, value) end

---@param arg1 any
---@return any
function Unit:AddUnitState(arg1) end

---@param who any
---@param arg2 any
---@return any
function Unit:Attack(who, arg2) end

---@return any
function Unit:AttackStop() end

---@return any
function Unit:CanModifyStats() end

---@param target any
---@param spell any
---@param arg3 any
---@param arg4 any
---@param arg5 any
---@param arg6 any
---@param castItem any
---@param originalCaster any
---@return any
function Unit:CastCustomSpell(target, spell, arg3, arg4, arg5, arg6, castItem, originalCaster) end

---@param target any
---@param spell any
---@param arg3 any
---@return any
function Unit:CastSpell(target, spell, arg3) end

---@param x any
---@param y any
---@param z any
---@param spell any
---@param arg5 any
---@return any
function Unit:CastSpellAoF(x, y, z, spell, arg5) end

---@return any
function Unit:ClearInCombat() end

---@param arg1 any
---@return any
function Unit:ClearUnitState(arg1) end

---@param pct any
---@return any
function Unit:CountPctFromCurHealth(pct) end

---@param pct any
---@return any
function Unit:CountPctFromMaxHealth(pct) end

---@param target any
---@param arg2 any
---@param arg3 any
---@param school any
---@param arg5 any
---@return any
function Unit:DealDamage(target, arg2, arg3, school, arg5) end

---@param target any
---@param spell any
---@param amount any
---@param arg4 any
---@return any
function Unit:DealHeal(target, spell, amount, arg4) end

---@return any
function Unit:DeMorph() end

---@param arg1 any
---@return any
function Unit:DisableMelee(arg1) end

---@return any
function Unit:Dismount() end

---@param emote any
---@return any
function Unit:Emote(emote) end

---@param emoteId any
---@return any
function Unit:EmoteState(emoteId) end

---@param spellId any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@return any
function Unit:GetAura(spellId, arg2, arg3, arg4) end

---@param spellSchool any
---@return any
function Unit:GetBaseSpellPower(spellSchool) end

---@return any
function Unit:GetCharmerGUID() end

---@return any
function Unit:GetCharmGUID() end

---@return any
function Unit:GetClass() end

---@param arg1 any
---@return any
function Unit:GetClassAsString(arg1) end

---@return any
function Unit:GetClassMask() end

---@return any
function Unit:GetControllerGUID() end

---@return any
function Unit:GetControllerGUIDS() end

---@return any
function Unit:GetCreatorGUID() end

---@return any
function Unit:GetCreatureType() end

---@return any
function Unit:GetCritterGUID() end

---@param spellType any
---@return any
function Unit:GetCurrentSpell(spellType) end

---@return any
function Unit:GetDisplayId() end

---@return any
function Unit:GetFaction() end

---@param arg1 any
---@return any
function Unit:GetFriendlyUnitsInRange(arg1) end

---@return any
function Unit:GetGender() end

---@return any
function Unit:GetHealth() end

---@return any
function Unit:GetHealthPct() end

---@return any
function Unit:GetLevel() end

---@return any
function Unit:GetMaxHealth() end

---@param type any
---@return any
function Unit:GetMaxPower(type) end

---@return any
function Unit:GetMountId() end

---@return any
function Unit:GetMovementType() end

---@return any
function Unit:GetNativeDisplayId() end

---@param spellId any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@param exceptAura any
---@return any
function Unit:GetOwnedAura(spellId, arg2, arg3, arg4, exceptAura) end

---@return any
function Unit:GetOwner() end

---@return any
function Unit:GetOwnerGUID() end

---@return any
function Unit:GetPetGUID() end

---@param type any
---@return any
function Unit:GetPower(type) end

---@param type any
---@return any
function Unit:GetPowerPct(type) end

---@return any
function Unit:GetPowerType() end

---@return any
function Unit:GetRace() end

---@param arg1 any
---@return any
function Unit:GetRaceAsString(arg1) end

---@return any
function Unit:GetRaceMask() end

---@param arg1 any
---@return any
function Unit:GetSpeed(arg1) end

---@return any
function Unit:GetStandState() end

---@param statType any
---@return any
function Unit:GetStat(statType) end

---@param arg1 any
---@return any
function Unit:GetUnfriendlyUnitsInRange(arg1) end

---@deprecated not available (Classic 1.60)
---@return any
function Unit:GetVehicle() end

---@deprecated not available (Classic 1.60)
---@return any
function Unit:GetVehicleKit() end

---@return any
function Unit:GetVictim() end

---@param spell any
---@return any
function Unit:HasAura(spell) end

---@param state any
---@return any
function Unit:HasUnitState(state) end

---@param healthpct any
---@return any
function Unit:HealthAbovePct(healthpct) end

---@param healthpct any
---@return any
function Unit:HealthBelowPct(healthpct) end

---@param spellType any
---@param arg2 any
---@return any
function Unit:InterruptSpell(spellType, arg2) end

---@return any
function Unit:IsAlive() end

---@return any
function Unit:IsArmorer() end

---@return any
function Unit:IsAttackingPlayer() end

---@return any
function Unit:IsAuctioneer() end

---@return any
function Unit:IsBanker() end

---@return any
function Unit:IsBattleMaster() end

---@return any
function Unit:IsCasting() end

---@return any
function Unit:IsCharmed() end

---@return any
function Unit:IsDead() end

---@return any
function Unit:IsDying() end

---@return any
function Unit:IsFlying() end

---@param other any
---@return any
function Unit:IsFriendlyTo(other) end

---@return any
function Unit:IsFullHealth() end

---@return any
function Unit:IsGossip() end

---@return any
function Unit:IsGuildMaster() end

---@param other any
---@return any
function Unit:IsHostileTo(other) end

---@param obj any
---@param radius any
---@return any
function Unit:IsInAccessiblePlaceFor(obj, radius) end

---@return any
function Unit:IsInCombat() end

---@return any
function Unit:IsInnkeeper() end

---@return any
function Unit:IsInWater() end

---@return any
function Unit:IsMounted() end

---@return any
function Unit:IsMoving() end

---@deprecated not available (Classic 1.60)
---@return any
function Unit:IsOnVehicle() end

---@return any
function Unit:IsPvPFlagged() end

---@return any
function Unit:IsQuestGiver() end

---@return any
function Unit:IsRooted() end

---@return any
function Unit:IsServiceProvider() end

---@return any
function Unit:IsSpiritGuide() end

---@return any
function Unit:IsSpiritHealer() end

---@return any
function Unit:IsSpiritService() end

---@return any
function Unit:IsStandState() end

---@return any
function Unit:IsStopped() end

---@return any
function Unit:IsTabardDesigner() end

---@return any
function Unit:IsTaxi() end

---@return any
function Unit:IsTrainer() end

---@return any
function Unit:IsUnderWater() end

---@return any
function Unit:IsVendor() end

---@return any
function Unit:IsVisible() end

---@param target any
---@param arg2 any
---@return any
function Unit:Kill(target, arg2) end

---@param amount any
---@param type any
---@return any
function Unit:ModifyPower(amount, type) end

---@param displayId any
---@return any
function Unit:Mount(displayId) end

---@param target any
---@param dist any
---@param angle any
---@return any
function Unit:MoveChase(target, dist, angle) end

---@param arg1 any
---@return any
function Unit:MoveClear(arg1) end

---@return any
function Unit:MoveConfused() end

---@param arg1 any
---@return any
function Unit:MoveExpire(arg1) end

---@param target any
---@param arg2 any
---@return any
function Unit:MoveFleeing(target, arg2) end

---@param target any
---@param arg2 any
---@param arg3 any
---@return any
function Unit:MoveFollow(target, arg2, arg3) end

---@return any
function Unit:MoveHome() end

---@return any
function Unit:MoveIdle() end

---@param x any
---@param y any
---@param z any
---@param zSpeed any
---@param maxHeight any
---@param arg6 any
---@return any
function Unit:MoveJump(x, y, z, zSpeed, maxHeight, arg6) end

---@param radius any
---@return any
function Unit:MoveRandom(radius) end

---@return any
function Unit:MoveStop() end

---@param id any
---@param x any
---@param y any
---@param z any
---@param arg5 any
---@return any
function Unit:MoveTo(id, x, y, z, arg5) end

---@param x any
---@param y any
---@param z any
---@param o any
---@return any
function Unit:NearTeleport(x, y, z, o) end

---@param emoteId any
---@return any
function Unit:PerformEmote(emoteId) end

---@return any
function Unit:RemoveAllAuras() end

---@return any
function Unit:RemoveArenaAuras() end

---@param spell any
---@return any
function Unit:RemoveAura(spell) end

---@return any
function Unit:RemoveBindSightAuras() end

---@return any
function Unit:RemoveCharmAuras() end

---@return any
function Unit:RestoreDisplayId() end

---@return any
function Unit:RestoreFaction() end

---@param type any
---@param lang any
---@param msg any
---@param target any
---@return any
function Unit:SendChatMessageToPlayer(type, lang, msg, target) end

---@param msg any
---@param receiver any
---@param arg3 any
---@return any
function Unit:SendUnitEmote(msg, receiver, arg3) end

---@param msg any
---@param language any
---@return any
function Unit:SendUnitSay(msg, language) end

---@param msg any
---@param lang any
---@param receiver any
---@param arg4 any
---@return any
function Unit:SendUnitWhisper(msg, lang, receiver, arg4) end

---@param msg any
---@param language any
---@return any
function Unit:SendUnitYell(msg, language) end

---@param arg1 any
---@return any
function Unit:SetCanFly(arg1) end

---@param arg1 any
---@return any
function Unit:SetConfused(arg1) end

---@param arg1 any
---@return any
function Unit:SetCreatorGUID(arg1) end

---@param arg1 any
---@return any
function Unit:SetCritterGUID(arg1) end

---@param displayId any
---@return any
function Unit:SetDisplayId(displayId) end

---@param orientation any
---@return any
function Unit:SetFacing(orientation) end

---@param target any
---@return any
function Unit:SetFacingToObject(target) end

---@param faction any
---@return any
function Unit:SetFaction(faction) end

---@param arg1 any
---@return any
function Unit:SetFeared(arg1) end

---@param arg1 any
---@return any
function Unit:SetFFA(arg1) end

---@param health any
---@return any
function Unit:SetHealth(health) end

---@param immunity any
---@param arg2 any
---@return any
function Unit:SetImmuneTo(immunity, arg2) end

---@param enemy any
---@return any
function Unit:SetInCombatWith(enemy) end

---@param level any
---@return any
function Unit:SetLevel(level) end

---@param maxHealth any
---@return any
function Unit:SetMaxHealth(maxHealth) end

---@param type any
---@param maxPower any
---@return any
function Unit:SetMaxPower(type, maxPower) end

---@param name any
---@return any
function Unit:SetName(name) end

---@param displayId any
---@return any
function Unit:SetNativeDisplayId(displayId) end

---@param arg1 any
---@return any
function Unit:SetOwnerGUID(arg1) end

---@param arg1 any
---@return any
function Unit:SetPetGUID(arg1) end

---@param amount any
---@param type any
---@return any
function Unit:SetPower(amount, type) end

---@param type any
---@return any
function Unit:SetPowerType(type) end

---@param arg1 any
---@return any
function Unit:SetPvP(arg1) end

---@param arg1 any
---@return any
function Unit:SetRooted(arg1) end

---@param arg1 any
---@return any
function Unit:SetSanctuary(arg1) end

---@param sheathState any
---@return any
function Unit:SetSheath(sheathState) end

---@param arg1 any
---@param rate any
---@param arg3 any
---@return any
function Unit:SetSpeed(arg1, rate, arg3) end

---@param state any
---@return any
function Unit:SetStandState(state) end

---@param arg1 any
---@return any
function Unit:SetStunned(arg1) end

---@param arg1 any
---@return any
function Unit:SetVisible(arg1) end

---@param arg1 any
---@return any
function Unit:SetWaterWalk(arg1) end

---@param arg1 any
---@return any
function Unit:StopSpellCast(arg1) end
