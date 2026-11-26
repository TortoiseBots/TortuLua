---@meta

---@class Item : Object
local Item = {}

---@return any
function Item:CanBeTraded() end

---@param arg1 any
---@return any
function Item:ClearEnchantment(arg1) end

---@return any
function Item:GetAllowableClass() end

---@return any
function Item:GetAllowableRace() end

---@return any
function Item:GetArmor() end

---@return any
function Item:GetBagSize() end

---@return any
function Item:GetBagSlot() end

---@return any
function Item:GetBuyCount() end

---@return any
function Item:GetBuyPrice() end

---@return any
function Item:GetClass() end

---@return any
function Item:GetCount() end

---@param damageSlot any
---@return any
function Item:GetDamageInfo(damageSlot) end

---@return any
function Item:GetDisplayId() end

---@return any
function Item:GetDurability() end

---@param arg1 any
---@return any
function Item:GetEnchantmentId(arg1) end

---@return any
function Item:GetEntry() end

---@return any
function Item:GetExtraFlags() end

---@return any
function Item:GetFlags() end

---@return any
function Item:GetFlags2() end

---@return any
function Item:GetInventoryType() end

---@return any
function Item:GetItemId() end

---@return any
function Item:GetItemLevel() end

---@param arg1 any
---@return any
function Item:GetItemLink(arg1) end

---@return any
function Item:GetItemSet() end

---@return any
function Item:GetMaxDurability() end

---@return any
function Item:GetMaxStackCount() end

---@return any
function Item:GetName() end

---@return any
function Item:GetOwner() end

---@return any
function Item:GetOwnerGUID() end

---@return any
function Item:GetQuality() end

---@deprecated not available (this core); this core has no legacy random property id: items carry ItemRandomBonusListId (Item::GetItemRandomBonusListId, m_randomBonusListId)
---@return any
function Item:GetRandomProperty() end

---@deprecated not available (this core); this core has no legacy random suffix id: items carry ItemRandomBonusListId (Item::GetItemRandomBonusListId, m_randomBonusListId)
---@return any
function Item:GetRandomSuffix() end

---@return any
function Item:GetRequiredLevel() end

---@return any
function Item:GetSellPrice() end

---@return any
function Item:GetSlot() end

---@return any
function Item:GetSpeed() end

---@param spellIndex any
---@return any
function Item:GetSpellId(spellIndex) end

---@param spellIndex any
---@return any
function Item:GetSpellTrigger(spellIndex) end

---@param statSlot any
---@return any
function Item:GetStatInfo(statSlot) end

---@return any
function Item:GetStatsCount() end

---@return any
function Item:GetSubClass() end

---@param questId any
---@return any
function Item:HasQuest(questId) end

---@deprecated not available (Classic 1.60)
---@return any
function Item:IsArmorVellum() end

---@return any
function Item:IsBag() end

---@return any
function Item:IsBoundAccountWide() end

---@return any
function Item:IsBoundByEnchant() end

---@return any
function Item:IsBroken() end

---@return any
function Item:IsConjuredConsumable() end

---@return any
function Item:IsCurrencyToken() end

---@return any
function Item:IsEquipped() end

---@return any
function Item:IsInBag() end

---@return any
function Item:IsInTrade() end

---@return any
function Item:IsLocked() end

---@param player any
---@return any
function Item:IsNotBoundToPlayer(player) end

---@return any
function Item:IsNotEmptyBag() end

---@return any
function Item:IsPotion() end

---@return any
function Item:IsRefundExpired() end

---@return any
function Item:IsSoulBound() end

---@deprecated not available (Classic 1.60)
---@return any
function Item:IsWeaponVellum() end

---@deprecated not available (this server); Item::SaveToDB takes a CharacterDatabaseTransaction that the caller commits synchronously: the write would block the game thread
---@return any
function Item:SaveToDB() end

---@param setBinding any
---@return any
function Item:SetBinding(setBinding) end

---@param count any
---@return any
function Item:SetCount(count) end

---@param enchantId any
---@param arg2 any
---@return any
function Item:SetEnchantment(enchantId, arg2) end

---@deprecated not available (this server); the core keeps an item in its owner's inventory, update queue and database rows; rewriting only the owner guid would leave the item in the old owner's bag
---@return any
function Item:SetOwner() end

---@deprecated not available (this core); this core has no legacy random property id: items carry ItemRandomBonusListId (Item::SetItemRandomBonusList, m_randomBonusListId)
---@return any
function Item:SetRandomProperty() end

---@deprecated not available (this core); this core has no legacy random suffix id: items carry ItemRandomBonusListId (Item::SetItemRandomBonusList, m_randomBonusListId)
---@return any
function Item:SetRandomSuffix() end
