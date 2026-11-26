// E5 Item long tail, round 1: the most used documented Item methods (docs Item/index.html). The fake world carries a
// small item state (FakeHost::FakeItemState) so a value can be read back through the method that reads it; every
// method is also called on a gone item, which must raise the family's stale-handle error, and with a bad argument,
// which must raise before anything reaches the host.
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

struct ItemRig : Rig
{
    ItemRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

// Stores a player and an item in the Lua state through their own events (the item event's documented arguments are
// event, player, item).
void storeHandles(ItemRig& rig, fl::HandleData const& p, fl::HandleData const& item)
{
    rig.ok("RegisterPlayerEvent(3, function(ev, player) stored = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(p)});
    rig.ok("RegisterItemEvent(900, 5, function(ev, player, it) bag = it end)");
    rig.e().emitEluna(fl::EluKind::Item, 900, 5, {fl::EventValue(p), fl::EventValue(item)});
}

// The fake item a test starts from: a template, an owner (the stat value scales with it), one spell and one enchant.
void prepare(FakeHost& host, fl::HandleData const& item, fl::HandleData const& p)
{
    FakeHost::FakeItemState& it = host.itemState(item);
    it.hasTemplate = true;
    it.hasOwner = true;
    it.ownerLow = p.guid.lo;
    it.spellCount = 1;
    it.knownEnchants = {1};
    it.bag = true;
    it.bagSize = 16;
}

// Every method of the round, in the call form the docs give (the receiver is the stored item).
std::vector<std::string> const& roundCalls()
{
    static std::vector<std::string> const calls = {
        // identity and template
        "GetName()",
        "GetItemId()",
        "GetItemLevel()",
        "GetRequiredLevel()",
        "GetClass()",
        "GetSubClass()",
        "GetInventoryType()",
        "GetQuality()",
        "GetDisplayId()",
        "GetBuyPrice()",
        "GetSellPrice()",
        "GetBuyCount()",
        "GetMaxStackCount()",
        "GetStatsCount()",
        "GetItemSet()",
        "GetAllowableClass()",
        "GetAllowableRace()",
        "GetArmor()",
        "GetDurability()",
        "GetMaxDurability()",
        "GetFlags()",
        "GetFlags2()",
        "GetSpellId(0)",
        "GetSpellTrigger(0)",
        "GetStatInfo(0)",
        "GetDamageInfo(1)",
        "GetDamageInfo(2)",
        // state
        "IsBag()",
        "IsSoulBound()",
        "IsBoundAccountWide()",
        "IsBoundByEnchant()",
        "IsNotBoundToPlayer(stored)",
        "IsLocked()",
        "IsBroken()",
        "IsCurrencyToken()",
        "IsNotEmptyBag()",
        "IsInBag()",
        "IsInTrade()",
        "IsPotion()",
        "IsConjuredConsumable()",
        "IsRefundExpired()",
        "CanBeTraded()",
        "HasQuest(100)",
        // owner and enchants
        "GetOwnerGUID()",
        "SetBinding(true)",
        "GetEnchantmentId(0)",
        "SetEnchantment(1, 1)",
        "ClearEnchantment(1)",
        "GetBagSize()",
        "GetExtraFlags()",
        "GetSpeed()",
    };
    return calls;
}
}  // namespace

TEST_CASE("eluna item long tail: every method of the round reaches the host and a gone item raises")
{
    ItemRig rig;
    fl::HandleData p = FakeHost::player(71);
    fl::HandleData item = FakeHost::item(900);
    fl::ObjectInfo info;
    info.name = "Bag";
    rig.host.add(p, info);
    info.entry = 900;
    rig.host.add(item, info);
    prepare(rig.host, item, p);
    storeHandles(rig, p, item);

    for (std::string const& call : roundCalls())
    {
        INFO("call: bag:" << call);
        CHECK(rig.run("bag:" + call).ok);
    }

    // SetOwner is a stub (the item would be left in the old owner's bag).
    {
        fl::CallResult r = rig.run("bag:SetOwner(stored)");
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("SetOwner: not available on this server") != std::string::npos);
    }
    // The two Classic-era stubs and the blocking save raise the documented errors.
    for (char const* call : {"bag:IsWeaponVellum()", "bag:IsArmorVellum()"})
    {
        INFO("call: " << call);
        fl::CallResult r = rig.run(call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("not available on Classic 1.60") != std::string::npos);
    }
    fl::CallResult save = rig.run("bag:SaveToDB()");
    REQUIRE_FALSE(save.ok);
    CHECK(save.error.find("not available on this server") != std::string::npos);
    // Random property/suffix methods have no equivalent on ForeverCore (it uses bonus lists).
    for (char const* call : {"bag:GetRandomProperty()", "bag:GetRandomSuffix()",
                             "bag:SetRandomProperty(1)", "bag:SetRandomSuffix(2)"})
    {
        INFO("call: " << call);
        fl::CallResult r = rig.run(call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("not available on this core") != std::string::npos);
    }

    // The family's stale-handle rule: the same calls on a gone item raise before anything reaches the host.
    rig.host.remove(item);
    for (std::string const& call : roundCalls())
    {
        INFO("call: bag:" << call);
        fl::CallResult r = rig.run("bag:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Item no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna item long tail: the values flow out of the fake item state")
{
    ItemRig rig;
    fl::HandleData p = FakeHost::player(81);
    fl::HandleData item = FakeHost::item(901);
    fl::ObjectInfo info;
    info.name = "Values";
    rig.host.add(p, info);
    info.entry = 901;
    rig.host.add(item, info);
    prepare(rig.host, item, p);
    storeHandles(rig, p, item);
    FakeHost::FakeItemState& it = rig.host.itemState(item);

    it.name = "Linen Bag";
    it.entry = 901;
    it.itemLevel = 15;
    it.requiredLevel = 5;
    it.itemClass = 0;
    it.subClass = 1;
    it.inventoryType = 18;
    it.quality = 2;
    it.displayId = 12345;
    it.buyPrice = 400;
    it.sellPrice = 100;
    it.buyCount = 5;
    it.maxStack = 20;
    it.statsCount = 2;
    it.itemSet = 3;
    it.allowableClass = 1;
    it.allowableRace = 0x1FF;
    it.armor = 30;
    it.durability = 40;
    it.maxDurability = 50;
    it.flags = 0x8;
    it.flags2 = 0x10;
    it.extraFlags = 0x20;
    it.speed = 2400;
    it.spellIds[0] = 4242;
    it.spellTriggers[0] = 0;
    it.statValue = 7.5;
    it.statType = 4;
    it.damageType = 3;
    it.minDamage = 2.5;
    it.maxDamage = 4.5;

    CHECK(rig.ok("return bag:GetName()") == "Linen Bag");
    CHECK(rig.ok("return bag:GetItemId()") == "901");
    CHECK(rig.ok("return bag:GetItemLevel() .. ',' .. bag:GetRequiredLevel()") == "15,5");
    CHECK(rig.ok("return bag:GetClass() .. ',' .. bag:GetSubClass()") == "0,1");
    CHECK(rig.ok("return bag:GetInventoryType() .. ',' .. bag:GetQuality()") == "18,2");
    CHECK(rig.ok("return bag:GetDisplayId()") == "12345");
    CHECK(rig.ok("return bag:GetBuyPrice() .. ',' .. bag:GetSellPrice()") == "400,100");
    CHECK(rig.ok("return bag:GetBuyCount() .. ',' .. bag:GetMaxStackCount()") == "5,20");
    CHECK(rig.ok("return bag:GetStatsCount() .. ',' .. bag:GetItemSet()") == "2,3");
    CHECK(rig.ok("return bag:GetAllowableClass()") == "1");
    CHECK(rig.ok("return bag:GetAllowableRace()") == "511");
    CHECK(rig.ok("return bag:GetArmor()") == "30");
    CHECK(rig.ok("return bag:GetDurability() .. ',' .. bag:GetMaxDurability()") == "40,50");
    CHECK(rig.ok("return bag:GetFlags() .. ',' .. bag:GetFlags2()") == "8,16");
    CHECK(rig.ok("return bag:GetExtraFlags() .. ',' .. bag:GetSpeed()") == "32,2400");
    CHECK(rig.ok("return bag:GetSpellId(0)") == "4242");
    CHECK(rig.ok("return bag:GetSpellTrigger(0)") == "0");
    CHECK(rig.ok("local v, t = bag:GetStatInfo(0) return v .. ',' .. t") == "7.5,4");
    CHECK(rig.ok("local d, lo, hi = bag:GetDamageInfo(1) return d .. ',' .. lo .. ',' .. hi") == "3,2.5,4.5");
    // the core's item data has one damage pair: the second slot is zeros, not a repeat of the first
    CHECK(rig.ok("local d, lo, hi = bag:GetDamageInfo(2) return d .. ',' .. lo .. ',' .. hi") == "0,0.0,0.0");
    CHECK(rig.ok("return bag:GetBagSize()") == "16");

    // state flags
    it.soulBound = it.accountWide = it.boundByEnchant = it.locked = it.broken = true;
    it.currencyToken = it.notEmptyBag = it.inBag = it.inTrade = it.potion = it.conjured = true;
    it.refundExpired = it.tradable = it.notBoundToPlayer = true;
    it.quests = {100};
    CHECK(rig.ok("return tostring(bag:IsBag()) .. tostring(bag:IsSoulBound()) .. "
                 "tostring(bag:IsBoundAccountWide()) .. tostring(bag:IsBoundByEnchant())") == "truetruetruetrue");
    CHECK(rig.ok("return tostring(bag:IsNotBoundToPlayer(stored))") == "true");
    CHECK(rig.ok("return tostring(bag:IsLocked()) .. tostring(bag:IsBroken()) .. "
                 "tostring(bag:IsCurrencyToken()) .. tostring(bag:IsNotEmptyBag())") == "truetruetruetrue");
    CHECK(rig.ok("return tostring(bag:IsInBag()) .. tostring(bag:IsInTrade()) .. tostring(bag:IsPotion()) .. "
                 "tostring(bag:IsConjuredConsumable())") == "truetruetruetrue");
    CHECK(rig.ok("return tostring(bag:IsRefundExpired()) .. tostring(bag:CanBeTraded())") == "truetrue");
    CHECK(rig.ok("return tostring(bag:HasQuest(100)) .. tostring(bag:HasQuest(101))") == "truefalse");

    // owner and enchants
    it.ownerGuid = "00000000000000040000000000000051";
    CHECK(rig.ok("return bag:GetOwnerGUID()") == it.ownerGuid);
    rig.ok("bag:SetBinding(true)");
    CHECK(it.binding);
    rig.ok("bag:SetBinding(false)");
    CHECK_FALSE(it.binding);
    it.enchantments[0] = 0;
    CHECK(rig.ok("return bag:GetEnchantmentId(0)") == "0");
    rig.ok("bag:SetEnchantment(1, 0)");
    CHECK(it.enchantments[0] == 1);
    CHECK(rig.ok("return bag:GetEnchantmentId(0)") == "1");
    CHECK(rig.ok("return tostring(bag:ClearEnchantment(0))") == "true");
    CHECK(it.enchantments[0] == 0);
    CHECK(rig.ok("return tostring(bag:ClearEnchantment(0))") == "false");   // nothing left in that slot
}

TEST_CASE("eluna item long tail: a bad argument raises before the host is called")
{
    ItemRig rig;
    fl::HandleData p = FakeHost::player(91);
    fl::HandleData item = FakeHost::item(902);
    fl::ObjectInfo info;
    info.name = "Args";
    rig.host.add(p, info);
    info.entry = 902;
    rig.host.add(item, info);
    prepare(rig.host, item, p);
    storeHandles(rig, p, item);

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };
    size_t const callsBefore = rig.host.gameCalls.size();

    CHECK(errorOf("bag:GetSpellId(-1)").find("spellIndex must be an integer 0..4294967295") != std::string::npos);
    CHECK(errorOf("bag:GetStatInfo(10)").find("statSlot must be an integer 0..9") != std::string::npos);
    CHECK(errorOf("bag:GetDamageInfo(0)").find("damageSlot must be 1 or 2") != std::string::npos);
    CHECK(errorOf("bag:GetDamageInfo(3)").find("damageSlot must be 1 or 2") != std::string::npos);
    CHECK(errorOf("bag:GetEnchantmentId(13)").find("enchantSlot must be an integer 0..12") != std::string::npos);
    CHECK(errorOf("bag:SetEnchantment(1, 99)").find("enchantSlot must be an integer 0..12") != std::string::npos);
    CHECK(errorOf("bag:ClearEnchantment(-1)").find("enchantSlot must be an integer 0..4294967295") !=
          std::string::npos);
    CHECK(errorOf("bag:SetBinding(1)").find("bad argument") != std::string::npos);
    CHECK(errorOf("bag:IsNotBoundToPlayer(5)").find("bad argument") != std::string::npos);
    CHECK(rig.host.gameCalls.size() == callsBefore);   // nothing reached the host
}

TEST_CASE("eluna item long tail: the host's refusals raise with the method's own message")
{
    ItemRig rig;
    fl::HandleData p = FakeHost::player(101);
    fl::HandleData item = FakeHost::item(903);
    fl::ObjectInfo info;
    info.name = "Refusals";
    rig.host.add(p, info);
    info.entry = 903;
    rig.host.add(item, info);
    prepare(rig.host, item, p);
    storeHandles(rig, p, item);
    FakeHost::FakeItemState& it = rig.host.itemState(item);

    auto errorOf = [&rig](std::string const& code)
    {
        fl::CallResult r = rig.run(code);
        REQUIRE_FALSE(r.ok);
        return r.error;
    };

    // a spell index past the item's spells
    CHECK(errorOf("bag:GetSpellId(1)").find("spellIndex is beyond the item's spells") != std::string::npos);
    CHECK(errorOf("bag:GetSpellTrigger(1)").find("spellIndex is beyond the item's spells") != std::string::npos);
    // the stat value scales with the owner
    it.hasOwner = false;
    CHECK(errorOf("bag:GetStatInfo(0)").find("has no owner to scale the stat value") != std::string::npos);
    it.hasOwner = true;
    // an enchant the server does not know
    CHECK(errorOf("bag:SetEnchantment(999, 0)").find("not an enchant the server knows") != std::string::npos);
    // the enchant setters need the item's owner online (the core's SetEnchantment dereferences it)
    it.ownerMissing = true;
    CHECK(errorOf("bag:SetEnchantment(1, 0)").find("the item's owner is not online") != std::string::npos);
    it.enchantments[0] = 1;
    CHECK(rig.ok("return tostring(bag:ClearEnchantment(0))") == "false");   // nothing removed without an owner
    CHECK(it.enchantments[0] == 1);
    it.ownerMissing = false;
    // an item level with no row in the core's damage tables (its damage-per-second lookup asserts)
    it.damageRowMissing = true;
    CHECK(errorOf("bag:GetDamageInfo(1)").find("its item level has no damage row") != std::string::npos);
    it.damageRowMissing = false;
    // a missing template
    it.hasTemplate = false;
    CHECK(errorOf("bag:CanBeTraded()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:IsNotEmptyBag()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:GetName()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:GetClass()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:GetBagSize()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:GetExtraFlags()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:GetSpeed()").find("the item has no template on the server") != std::string::npos);
    CHECK(errorOf("bag:GetMaxStackCount()").find("the item has no template on the server") != std::string::npos);
}

TEST_CASE("eluna item events: missing add/equip/unequip register as honest C13 stubs and log once")
{
    ItemRig rig;
    rig.ok("local c1 = RegisterItemEvent(900, 6, function() end)\n"
           "local c2 = RegisterItemEvent(900, 7, function() end)\n"
           "local c3 = RegisterItemEvent(900, 8, function() end)\n"
           "assert(type(c1) == 'function' and type(c2) == 'function' and type(c3) == 'function')");
    CHECK(rig.e().status().elunaHandlers == 3);
    CHECK(rig.host.logContains("event item ADD needs core hook C13"));
    CHECK(rig.host.logContains("event item EQUIP needs core hook C13"));
    CHECK(rig.host.logContains("event item UNEQUIP needs core hook C13"));

    // Second registration of the same event ids does not repeat the gap warning.
    size_t const logsBefore = rig.host.logs.size();
    rig.ok("local c4 = RegisterItemEvent(901, 6, function() end)\n"
           "RegisterItemEvent(901, 7, function() end)\n"
           "RegisterItemEvent(901, 8, function() end)\n"
           "c4()");
    CHECK(rig.host.logs.size() == logsBefore);
    CHECK(rig.e().status().elunaHandlers == 5);

    rig.ok("ClearItemEvents(900) ClearItemEvents(901)");
    CHECK(rig.e().status().elunaHandlers == 0);
}
