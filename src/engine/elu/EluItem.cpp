// Eluna Item methods of E2 (docs: Item/index.html) and the most used ones of the long tail round 1. The handle is an
// item the module handed to Lua through an item event; it resolves through its owner's inventory (docs/E0-E1-NOTES.md).
// Everything goes through Host::gameCall; the binding only checks the object and the documented argument ranges.
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
using fl::EventValue;

// The core's EnchantmentSlot enum: PERM 0 .. PROP_ENCHANTMENT_SLOT_4 12, MAX_ENCHANTMENT_SLOT is 13.
constexpr uint32_t kMaxEnchantSlots = 13;

uint32_t enchantSlotArg(lua_State* L, int idx)
{
    uint32_t const slot = argUInt(L, idx, "enchantSlot");
    if (slot >= kMaxEnchantSlots)
        luaL_error(L, "enchantSlot must be an integer 0..12");
    return slot;
}

// A read the host can refuse (a missing template, an owner-less stat read, a spell index past the item's spells): the
// host answers `false` and the call raises with `why`.
int itemRead(lua_State* L, char const* op, char const* display, char const* why, std::vector<EventValue> args = {})
{
    fl::HandleData const& h = self(L, kItem);
    std::vector<EventValue> out;
    gameCall(L, h, op, args, out, display);
    requireHostOk(L, out, display, why);
    return pushResults(L, out);
}

// Item:GetItemLink([locale])
int iGetItemLink(lua_State* L)
{
    return readOp(L, kItem, "item.getItemLink", "GetItemLink",
                  {EventValue(static_cast<int64_t>(optUInt(L, 2, 0)))});
}
// Item:GetEntry(): the item template entry (an item guid carries no entry, unlike a creature's).
int iGetEntry(lua_State* L) { return readOp(L, kItem, "item.getEntry", "GetEntry"); }
// Item:GetCount()
int iGetCount(lua_State* L) { return readOp(L, kItem, "item.getCount", "GetCount"); }
// Item:SetCount(count)
int iSetCount(lua_State* L)
{
    return actionOp(L, kItem, "item.setCount", "SetCount",
                    {EventValue(static_cast<int64_t>(argUInt(L, 2, "count")))});
}
// Item:GetOwner()
int iGetOwner(lua_State* L) { return readHandleOp(L, kItem, "item.getOwner", "GetOwner"); }
// Item:IsEquipped()
int iIsEquipped(lua_State* L) { return readOp(L, kItem, "item.isEquipped", "IsEquipped"); }
// Item:GetBagSlot() / Item:GetSlot()
int iGetBagSlot(lua_State* L) { return readOp(L, kItem, "item.getBagSlot", "GetBagSlot"); }
int iGetSlot(lua_State* L) { return readOp(L, kItem, "item.getSlot", "GetSlot"); }

// ---- long tail round 1: identity and template ----
int iGetName(lua_State* L)
{
    return itemRead(L, "item.getName", "GetName", "the item has no template on the server");
}
int iGetItemId(lua_State* L)
{
    return itemRead(L, "item.getItemId", "GetItemId", "the item has no template on the server");
}
int iGetItemLevel(lua_State* L)
{
    return itemRead(L, "item.getItemLevel", "GetItemLevel", "the item has no template on the server");
}
int iGetRequiredLevel(lua_State* L)
{
    return itemRead(L, "item.getRequiredLevel", "GetRequiredLevel", "the item has no template on the server");
}
int iGetClass(lua_State* L)
{
    return itemRead(L, "item.getClass", "GetClass", "the item has no template on the server");
}
int iGetSubClass(lua_State* L)
{
    return itemRead(L, "item.getSubClass", "GetSubClass", "the item has no template on the server");
}
int iGetInventoryType(lua_State* L)
{
    return itemRead(L, "item.getInventoryType", "GetInventoryType", "the item has no template on the server");
}
int iGetQuality(lua_State* L)
{
    return itemRead(L, "item.getQuality", "GetQuality", "the item has no template on the server");
}
int iGetDisplayId(lua_State* L)
{
    return itemRead(L, "item.getDisplayId", "GetDisplayId", "the item has no template on the server");
}
int iGetBuyPrice(lua_State* L)
{
    return itemRead(L, "item.getBuyPrice", "GetBuyPrice", "the item has no template on the server");
}
int iGetSellPrice(lua_State* L)
{
    return itemRead(L, "item.getSellPrice", "GetSellPrice", "the item has no template on the server");
}
int iGetBuyCount(lua_State* L)
{
    return itemRead(L, "item.getBuyCount", "GetBuyCount", "the item has no template on the server");
}
int iGetMaxStackCount(lua_State* L)
{
    return itemRead(L, "item.getMaxStackCount", "GetMaxStackCount", "the item has no template on the server");
}
int iGetStatsCount(lua_State* L)
{
    return itemRead(L, "item.getStatsCount", "GetStatsCount", "the item has no template on the server");
}
int iGetItemSet(lua_State* L)
{
    return itemRead(L, "item.getItemSet", "GetItemSet", "the item has no template on the server");
}
int iGetAllowableClass(lua_State* L)
{
    return itemRead(L, "item.getAllowableClass", "GetAllowableClass", "the item has no template on the server");
}
int iGetAllowableRace(lua_State* L)
{
    return itemRead(L, "item.getAllowableRace", "GetAllowableRace", "the item has no template on the server");
}
int iGetArmor(lua_State* L)
{
    return itemRead(L, "item.getArmor", "GetArmor", "the item has no template on the server");
}
int iGetFlags(lua_State* L)
{
    return itemRead(L, "item.getFlags", "GetFlags", "the item has no template on the server");
}
int iGetFlags2(lua_State* L)
{
    return itemRead(L, "item.getFlags2", "GetFlags2", "the item has no template on the server");
}
int iGetExtraFlags(lua_State* L)
{
    return itemRead(L, "item.getExtraFlags", "GetExtraFlags", "the item has no template on the server");
}
int iGetSpeed(lua_State* L)
{
    return itemRead(L, "item.getSpeed", "GetSpeed", "the item has no template on the server");
}
int iGetDurability(lua_State* L) { return readOp(L, kItem, "item.getDurability", "GetDurability"); }
int iGetMaxDurability(lua_State* L) { return readOp(L, kItem, "item.getMaxDurability", "GetMaxDurability"); }
int iGetSpellId(lua_State* L)
{
    uint32_t const index = argUInt(L, 2, "spellIndex");
    return itemRead(L, "item.getSpellId", "GetSpellId", "spellIndex is beyond the item's spells",
                    {EventValue(static_cast<int64_t>(index))});
}
int iGetSpellTrigger(lua_State* L)
{
    uint32_t const index = argUInt(L, 2, "spellIndex");
    return itemRead(L, "item.getSpellTrigger", "GetSpellTrigger", "spellIndex is beyond the item's spells",
                    {EventValue(static_cast<int64_t>(index))});
}
// Item:GetStatInfo(statSlot): value, type (the value scales with the owner's level, so the item must have an owner).
int iGetStatInfo(lua_State* L)
{
    // the core's item stat array: MAX_ITEM_PROTO_STATS is 10
    uint32_t const slot = argU8(L, 2, "statSlot");
    if (slot >= 10)
        luaL_error(L, "statSlot must be an integer 0..9");
    return itemRead(L, "item.getStatInfo", "GetStatInfo", "the item has no owner to scale the stat value for",
                    {EventValue(static_cast<int64_t>(slot))});
}
// Item:GetDamageInfo(damageSlot): damageType, minDamage, maxDamage (slot 1 or 2).
int iGetDamageInfo(lua_State* L)
{
    uint32_t const slot = argUInt(L, 2, "damageSlot");
    if (slot < 1 || slot > 2)
        luaL_error(L, "damageSlot must be 1 or 2");
    return itemRead(L, "item.getDamageInfo", "GetDamageInfo",
                    "the item has no template on the server, or its item level has no damage row",
                    {EventValue(static_cast<int64_t>(slot))});
}

// ---- state ----
int iIsBag(lua_State* L)
{
    return triStateBool(L, kItem, "item.isBag", "IsBag", "the item has no template on the server");
}
int iIsSoulBound(lua_State* L) { return readOp(L, kItem, "item.isSoulBound", "IsSoulBound"); }
int iIsBoundAccountWide(lua_State* L)
{
    return triStateBool(L, kItem, "item.isBoundAccountWide", "IsBoundAccountWide",
                        "the item has no template on the server");
}
int iIsBoundByEnchant(lua_State* L) { return readOp(L, kItem, "item.isBoundByEnchant", "IsBoundByEnchant"); }
int iIsNotBoundToPlayer(lua_State* L)
{
    fl::HandleData const& player = objArg(L, 2, kPlayer, "player");
    return readOp(L, kItem, "item.isNotBoundToPlayer", "IsNotBoundToPlayer", {EventValue(player)});
}
int iIsLocked(lua_State* L) { return readOp(L, kItem, "item.isLocked", "IsLocked"); }
int iIsBroken(lua_State* L) { return readOp(L, kItem, "item.isBroken", "IsBroken"); }
int iIsCurrencyToken(lua_State* L)
{
    return triStateBool(L, kItem, "item.isCurrencyToken", "IsCurrencyToken",
                        "the item has no template on the server");
}
int iIsNotEmptyBag(lua_State* L)
{
    return triStateBool(L, kItem, "item.isNotEmptyBag", "IsNotEmptyBag", "the item has no template on the server");
}
int iIsInBag(lua_State* L) { return readOp(L, kItem, "item.isInBag", "IsInBag"); }
int iIsInTrade(lua_State* L) { return readOp(L, kItem, "item.isInTrade", "IsInTrade"); }
int iIsPotion(lua_State* L)
{
    return triStateBool(L, kItem, "item.isPotion", "IsPotion", "the item has no template on the server");
}
int iIsConjuredConsumable(lua_State* L)
{
    return triStateBool(L, kItem, "item.isConjuredConsumable", "IsConjuredConsumable",
                        "the item has no template on the server");
}
int iIsRefundExpired(lua_State* L) { return readOp(L, kItem, "item.isRefundExpired", "IsRefundExpired"); }
int iCanBeTraded(lua_State* L)
{
    return triStateBool(L, kItem, "item.canBeTraded", "CanBeTraded", "the item has no template on the server");
}
int iHasQuest(lua_State* L)
{
    return triStateBool(L, kItem, "item.hasQuest", "HasQuest", "the item has no template on the server",
                        {EventValue(static_cast<int64_t>(argUInt(L, 2, "questId")))});
}

// ---- owner and enchants ----
int iGetOwnerGUID(lua_State* L) { return readOp(L, kItem, "item.getOwnerGuid", "GetOwnerGUID"); }
// The core keeps an item in its owner's inventory, update queue and database rows: rewriting only the owner guid
// would leave the item in the old player's bag with a new owner, so moving an item to another player stays a stub
// until it is designed against the core's own item-transfer path.
int iSetOwner(lua_State* L)
{
    return luaL_error(L, "SetOwner: not available on this server (the item would be left in the old owner's bag)");
}
int iSetBinding(lua_State* L)
{
    return actionOp(L, kItem, "item.setBinding", "SetBinding", {EventValue(argBool(L, 2, "setBinding"))});
}
int iGetEnchantmentId(lua_State* L)
{
    uint32_t const slot = enchantSlotArg(L, 2);
    return readOp(L, kItem, "item.getEnchantmentId", "GetEnchantmentId", {EventValue(static_cast<int64_t>(slot))});
}
int iSetEnchantment(lua_State* L)
{
    uint32_t const enchantId = argUInt(L, 2, "enchantId");
    uint32_t const slot = enchantSlotArg(L, 3);
    return itemRead(L, "item.setEnchantment", "SetEnchantment", "enchantId is not an enchant the server knows, or the item's owner is not online",
                    {EventValue(static_cast<int64_t>(enchantId)), EventValue(static_cast<int64_t>(slot))});
}
int iClearEnchantment(lua_State* L)
{
    uint32_t const slot = enchantSlotArg(L, 2);
    return readOp(L, kItem, "item.clearEnchantment", "ClearEnchantment", {EventValue(static_cast<int64_t>(slot))});
}
int iGetBagSize(lua_State* L)
{
    return itemRead(L, "item.getBagSize", "GetBagSize", "the item has no template on the server");
}

// The core writes the item through a synchronous CharacterDatabase transaction, which must not run on a game thread.
int iSaveToDB(lua_State* L)
{
    return luaL_error(L, "SaveToDB: not available on this server (the write blocks the game thread)");
}
// Classic 1.60 has no vellum items (they arrive with the inscription system).
int iIsWeaponVellum(lua_State* L) { return classicStubMethodFor(L, "IsWeaponVellum"); }
int iIsArmorVellum(lua_State* L) { return classicStubMethodFor(L, "IsArmorVellum"); }
}  // namespace

void registerItemMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetItemLink", fl::safe<iGetItemLink>}, {"GetEntry", fl::safe<iGetEntry>},
        {"GetCount", fl::safe<iGetCount>},
        {"SetCount", fl::safe<iSetCount>},       {"GetOwner", fl::safe<iGetOwner>},
        {"IsEquipped", fl::safe<iIsEquipped>},   {"GetBagSlot", fl::safe<iGetBagSlot>},
        {"GetSlot", fl::safe<iGetSlot>},
        // long tail round 1
        {"GetName", fl::safe<iGetName>},         {"GetItemId", fl::safe<iGetItemId>},
        {"GetItemLevel", fl::safe<iGetItemLevel>},
        {"GetRequiredLevel", fl::safe<iGetRequiredLevel>},
        {"GetClass", fl::safe<iGetClass>},       {"GetSubClass", fl::safe<iGetSubClass>},
        {"GetInventoryType", fl::safe<iGetInventoryType>},
        {"GetQuality", fl::safe<iGetQuality>},   {"GetDisplayId", fl::safe<iGetDisplayId>},
        {"GetBuyPrice", fl::safe<iGetBuyPrice>}, {"GetSellPrice", fl::safe<iGetSellPrice>},
        {"GetBuyCount", fl::safe<iGetBuyCount>}, {"GetMaxStackCount", fl::safe<iGetMaxStackCount>},
        {"GetStatsCount", fl::safe<iGetStatsCount>}, {"GetItemSet", fl::safe<iGetItemSet>},
        {"GetAllowableClass", fl::safe<iGetAllowableClass>},
        {"GetAllowableRace", fl::safe<iGetAllowableRace>},
        {"GetArmor", fl::safe<iGetArmor>},       {"GetFlags", fl::safe<iGetFlags>},
        {"GetFlags2", fl::safe<iGetFlags2>},     {"GetExtraFlags", fl::safe<iGetExtraFlags>},
        {"GetSpeed", fl::safe<iGetSpeed>},       {"GetDurability", fl::safe<iGetDurability>},
        {"GetMaxDurability", fl::safe<iGetMaxDurability>},
        {"GetSpellId", fl::safe<iGetSpellId>},   {"GetSpellTrigger", fl::safe<iGetSpellTrigger>},
        {"GetStatInfo", fl::safe<iGetStatInfo>}, {"GetDamageInfo", fl::safe<iGetDamageInfo>},
        {"IsBag", fl::safe<iIsBag>},             {"IsSoulBound", fl::safe<iIsSoulBound>},
        {"IsBoundAccountWide", fl::safe<iIsBoundAccountWide>},
        {"IsBoundByEnchant", fl::safe<iIsBoundByEnchant>},
        {"IsNotBoundToPlayer", fl::safe<iIsNotBoundToPlayer>},
        {"IsLocked", fl::safe<iIsLocked>},       {"IsBroken", fl::safe<iIsBroken>},
        {"IsCurrencyToken", fl::safe<iIsCurrencyToken>},
        {"IsNotEmptyBag", fl::safe<iIsNotEmptyBag>}, {"IsInBag", fl::safe<iIsInBag>},
        {"IsInTrade", fl::safe<iIsInTrade>},     {"IsPotion", fl::safe<iIsPotion>},
        {"IsConjuredConsumable", fl::safe<iIsConjuredConsumable>},
        {"IsRefundExpired", fl::safe<iIsRefundExpired>},
        {"CanBeTraded", fl::safe<iCanBeTraded>}, {"HasQuest", fl::safe<iHasQuest>},
        {"GetOwnerGUID", fl::safe<iGetOwnerGUID>}, {"SetOwner", fl::safe<iSetOwner>},
        {"SetBinding", fl::safe<iSetBinding>},   {"GetEnchantmentId", fl::safe<iGetEnchantmentId>},
        {"SetEnchantment", fl::safe<iSetEnchantment>},
        {"ClearEnchantment", fl::safe<iClearEnchantment>}, {"GetBagSize", fl::safe<iGetBagSize>},
        {"SaveToDB", fl::safe<iSaveToDB>},       {"IsWeaponVellum", fl::safe<iIsWeaponVellum>},
        {"IsArmorVellum", fl::safe<iIsArmorVellum>},
        {nullptr, nullptr}};
    addMethods(L, "Item", regs);
}
}  // namespace flu
