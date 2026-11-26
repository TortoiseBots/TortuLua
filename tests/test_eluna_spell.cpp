// Long tail round 1: the documented Spell, Aura and SpellInfo methods (docs Spell/index.html, Aura/index.html,
// SpellInfo/index.html and Global/GetSpellInfo.html). The fake world carries a transient spell/aura and a fake spell
// table, so a value a test sets is the answer a getter gives (a distinct value per field catches a swapped mapping) and a
// setter is read back through the matching getter. Every method is also called on a handle from an earlier call (the
// family's stale-handle error) and with a bad argument (the argument raises before the host answers).
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

struct SpellRig : Rig
{
    SpellRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

// Makes a handle visible to the scripts of the state as a global.
void setGlobal(SpellRig& rig, char const* name, fl::HandleData const& h)
{
    lua_State* L = rig.e().state();
    int top = lua_gettop(L);
    fl::eluPushHandle(L, h);
    lua_setglobal(L, name);
    lua_settop(L, top);
}

fl::HandleData creature(uint64_t id, uint32_t entry = 0)
{
    return FakeHost::creature(id, 0, 0, entry);
}

std::string call(SpellRig& rig, std::string const& expr)
{
    INFO("call: " << expr);
    return rig.ok("return tostring(" + expr + ")");
}

std::string errorOf(SpellRig& rig, std::string const& code)
{
    fl::CallResult r = rig.run(code);
    return r.ok ? std::string("no error") : r.error;
}

// One documented getter and the value the fake holds for it.
struct NumCase
{
    char const* call;
    int64_t want;
};
}  // namespace

// The fake spell of the round: distinct values per field, so a getter that reads the wrong field fails.
FakeHost::FakeSpell spellFixture(fl::HandleData const& caster, fl::HandleData const& target)
{
    FakeHost::FakeSpell s;
    s.id = 500;
    s.hasCaster = true;
    s.caster = caster;
    s.hasOriginalCaster = true;
    s.originalCaster = target;
    s.castTime = -25;
    s.delayStart = 1234567890123;
    s.delayMoment = 7;
    s.duration = 4444;
    s.powerCost = {{0, 55}};
    s.state = 2;
    s.hasTarget = true;
    s.target = target;
    s.hasDest = true;
    s.destX = 1.5;
    s.destY = 2.25;
    s.destZ = -3.75;
    s.autoRepeat = true;
    s.positive = true;
    s.channelActive = true;
    s.interruptable = true;
    s.procDisabled = true;
    s.autoActionReset = true;
    s.runeState = 3;
    s.triggeredBySpellId = 501;
    s.effectCount = 2;
    s.damage[1] = 888;
    s.unitTargets[1] = 2;
    s.goTargets[0] = 3;
    s.itemTargets[1] = 6;
    return s;
}

TEST_CASE("spell long tail: every documented Spell getter reads the fake spell")
{
    SpellRig rig;
    fl::HandleData caster = creature(30);
    fl::HandleData target = creature(31);
    fl::HandleData sp;
    sp.kind = fl::ObjKind::Spell;
    sp.instanceId = 7;
    sp.mapId = 500;
    FakeHost::FakeSpell const s = spellFixture(caster, target);
    rig.host.spellInfo(500);
    rig.host.spellInfo(501);
    setGlobal(rig, "spell", sp);
    setGlobal(rig, "caster", caster);
    setGlobal(rig, "target", target);

    // The store holds the spell of the call that is running (a handle only lives for its own call), so every call is
    // seeded again: the handle and its object are one unit of the transient rule.
    auto run = [&](std::string const& expr)
    {
        rig.host.spells[7] = s;
        return call(rig, expr);
    };
    std::vector<NumCase> const nums = {
        {"spell:GetEntry()", 500},
        {"spell:GetCastTime()", -25},
        {"spell:GetDelayStart()", 1234567890123},
        {"spell:GetDelayMoment()", 7},
        {"spell:GetDuration()", 4444},
        {"spell:GetPowerCost()", 55},
        {"spell:GetState()", 2},
        {"spell:GetRuneState()", 3},
        {"spell:GetUnitTargetCountForEffect(1)", 2},
        {"spell:GetGameObjectTargetCountForEffect(0)", 3},
        {"spell:GetItemTargetCountForEffect(1)", 6},
        {"spell:CalculateDamage(1)", 888},
    };
    for (NumCase const& c : nums)
        CHECK(run(c.call) == std::to_string(c.want));

    CHECK(run("spell:GetCaster() == caster") == "true");
    CHECK(run("spell:GetOriginalCaster() == target") == "true");
    CHECK(run("spell:GetTarget() == target") == "true");
    CHECK(run("spell:GetSpellInfo():GetId()") == "500");
    CHECK(run("spell:IsAutoRepeat()") == "true");
    CHECK(run("spell:IsPositive()") == "true");
    CHECK(run("spell:IsChannelActive()") == "true");
    CHECK(run("spell:IsInterruptable()") == "true");
    CHECK(run("spell:IsProcDisabled()") == "true");
    CHECK(run("spell:IsAutoActionResetSpell()") == "true");
    CHECK(run("spell:IsTriggered()") == "false");
    CHECK(run("spell:IsIgnoringCooldowns()") == "false");
    CHECK(run("spell:IsNeedSendToClient()") == "false");
    CHECK(run("spell:IsDeletable()") == "false");
    CHECK(run("spell:IsTriggeredByAura(GetSpellInfo(501))") == "true");
    CHECK(run("spell:IsTriggeredByAura(GetSpellInfo(500))") == "false");
    CHECK(run("spell:UpdatePointers()") == "true");
    rig.host.spells[7] = s;
    CHECK(rig.ok("local x, y, z = spell:GetTargetDest() return string.format('%.2f/%.2f/%.2f', x, y, z)") ==
          "1.50/2.25/-3.75");

    // A spell with no destination answers three zeroes (the docs promise three numbers).
    FakeHost::FakeSpell noDest = s;
    noDest.hasDest = false;
    rig.host.spells[7] = noDest;
    CHECK(rig.ok("local x, y, z = spell:GetTargetDest() return string.format('%.1f/%.1f/%.1f', x, y, z)") ==
          "0.0/0.0/0.0");
    // A caster that left the world answers nil, not a handle with a zero guid.
    FakeHost::FakeSpell noCaster = s;
    noCaster.hasCaster = false;
    rig.host.spells[7] = noCaster;
    CHECK(call(rig, "spell:GetCaster() == nil") == "true");
    // A spell without power costs answers 0.
    FakeHost::FakeSpell noCost = s;
    noCost.powerCost.clear();
    rig.host.spells[7] = noCost;
    CHECK(call(rig, "spell:GetPowerCost()") == "0");
}

TEST_CASE("spell long tail: the documented Spell setters and actions reach the spell")
{
    SpellRig rig;
    fl::HandleData sp;
    sp.kind = fl::ObjKind::Spell;
    sp.instanceId = 8;
    sp.mapId = 600;
    rig.host.spells[8].id = 600;
    setGlobal(rig, "spell", sp);
    setGlobal(rig, "player", FakeHost::player(5));
    rig.host.spellActions.clear();

    // The setters and their read-backs run in ONE call (a handle never outlives it). Cast needs the state the core
    // casts from (PREPARING), so the test sets it there and moves it after the cast.
    CHECK(rig.ok("spell:SetAutoRepeat(true) spell:SetDelayStart(42) spell:SetState(1) spell:SetRuneState(9) "
                 "spell:SetExecutedCurrently(true) spell:SetReferencedFromCurrent(true) spell:SetSpellValue(1, 5) "
                 "spell:Cast(true) spell:SetState(3) spell:Finish() spell:Cancel() spell:CleanupTargetList() "
                 "return tostring(spell:IsAutoRepeat()) .. ',' .. spell:GetDelayStart() .. ',' .. "
                 "spell:GetState() .. ',' .. spell:GetRuneState()") == "true,42,3,9");
    std::vector<std::string> const want = {"spell.setAutoRepeat",  "spell.setDelayStart",   "spell.setState",
                                           "spell.setRuneState",   "spell.setExecutedCurrently",
                                           "spell.setReferencedFromCurrent", "spell.setSpellValue", "spell.cast",
                                           "spell.setState",       "spell.finish",           "spell.cancel",
                                           "spell.cleanupTargetList"};
    CHECK(rig.host.spellActions == want);
}

TEST_CASE("spell long tail: Cast refuses a spell the core is not preparing")
{
    SpellRig rig;
    fl::HandleData sp;
    sp.kind = fl::ObjKind::Spell;
    sp.instanceId = 10;
    sp.mapId = 650;
    setGlobal(rig, "spell", sp);
    FakeHost::FakeSpell fixture;
    fixture.id = 650;
    // LAUNCHED (the state a cast leaves the spell in) and FINISHED: both already ran their cast.
    for (uint32_t state : {2u, 4u, 5u, 0u})
    {
        fixture.state = state;
        rig.host.spells[10] = fixture;
        rig.host.spellActions.clear();
        std::string const error = errorOf(rig, "return spell:Cast(true)");
        INFO("state " << state << ": " << error);
        CHECK(error.find("PREPARING") != std::string::npos);
        CHECK(rig.host.spellActions.empty());
    }
    // PREPARING is the state the core's own cast sites run in: the cast reaches the spell.
    fixture.state = 1;
    rig.host.spells[10] = fixture;
    rig.host.spellActions.clear();
    CHECK(rig.ok("spell:Cast(true)") == "nil");
    CHECK(rig.host.spellActions == std::vector<std::string>{"spell.cast"});
}

TEST_CASE("spell long tail: a Spell handle dies with the call that made it")
{
    SpellRig rig;
    fl::HandleData sp;
    sp.kind = fl::ObjKind::Spell;
    sp.instanceId = 9;
    rig.host.spells[9].id = 700;
    setGlobal(rig, "spell", sp);
    rig.ok("kept_spell = spell");
    // endLuaCall cleared the store of the call that produced it.
    CHECK(errorOf(rig, "return kept_spell:GetEntry()").find("Spell no longer exists") != std::string::npos);
}

// The fake aura of the round.
FakeHost::FakeAura auraFixture(fl::HandleData const& caster, fl::HandleData const& owner)
{
    FakeHost::FakeAura a;
    a.id = 500;
    a.hasCaster = true;
    a.caster = caster;
    a.casterGuid = caster.guid;
    a.casterLevel = 42;
    a.duration = 1000;
    a.maxDuration = 5000;
    a.hasOwner = true;
    a.owner = owner;
    a.stackAmount = 2;
    a.maxStackAmount = 5;
    return a;
}

TEST_CASE("aura long tail: every documented Aura method reads and writes the fake aura")
{
    SpellRig rig;
    fl::HandleData caster = creature(40);
    fl::HandleData owner = FakeHost::player(41);
    fl::HandleData au;
    au.kind = fl::ObjKind::Aura;
    au.instanceId = 3;
    au.mapId = 500;
    FakeHost::FakeAura const a = auraFixture(caster, owner);
    rig.host.spellInfo(500);
    setGlobal(rig, "aura", au);
    setGlobal(rig, "caster", caster);
    setGlobal(rig, "owner", owner);

    auto run = [&](std::string const& expr)
    {
        rig.host.auras[3] = a;
        return call(rig, expr);
    };
    CHECK(run("aura:GetAuraId()") == "500");
    CHECK(run("aura:GetEntry()") == "500");
    CHECK(run("aura:GetCaster() == caster") == "true");
    CHECK(run("aura:GetCasterGUID()") == rig.host.guidToString(caster.guid));
    CHECK(run("aura:GetCasterLevel()") == "42");
    CHECK(run("aura:GetDuration()") == "1000");
    CHECK(run("aura:GetMaxDuration()") == "5000");
    CHECK(run("aura:GetStackAmount()") == "2");
    CHECK(run("aura:GetOwner() == owner") == "true");
    CHECK(run("aura:GetSpellInfo():GetId()") == "500");

    // The setters and their read-backs run in one call; the action journal survives it.
    rig.host.auras[3] = a;
    rig.host.auraActions.clear();
    CHECK(rig.ok("aura:SetDuration(250) aura:SetMaxDuration(9000) aura:SetStackAmount(3) aura:SetStackAmount(1) "
                 "aura:Remove() return aura:GetDuration() .. ',' .. aura:GetMaxDuration() .. ',' .. "
                 "aura:GetStackAmount()") == "250,9000,1");
    CHECK(rig.host.auraActions ==
          std::vector<std::string>{"setDuration", "setMaxDuration", "setStackAmount", "refresh", "setStackAmount",
                                   "remove"});

    // Aura:GetOwner is nil for an aura with no unit owner; the caster can be gone.
    FakeHost::FakeAura bare = a;
    bare.hasOwner = false;
    bare.hasCaster = false;
    rig.host.auras[3] = bare;
    CHECK(call(rig, "aura:GetOwner() == nil") == "true");
    rig.host.auras[3] = bare;   // the store holds the aura of the call that is running
    CHECK(call(rig, "aura:GetCaster() == nil") == "true");
}

TEST_CASE("aura long tail: SetStackAmount clamps to the max and zero removes the aura")
{
    SpellRig rig;
    fl::HandleData caster = creature(42);
    fl::HandleData owner = FakeHost::player(43);
    fl::HandleData au;
    au.kind = fl::ObjKind::Aura;
    au.instanceId = 5;
    FakeHost::FakeAura const base = auraFixture(caster, owner);   // 2 of 5 stacks
    setGlobal(rig, "aura", au);

    // Above the aura's max stack (the core's CalcMaxStackAmount) the count is clamped, as ModStackAmount does.
    rig.host.auras[5] = base;
    rig.host.auraActions.clear();
    CHECK(rig.ok("aura:SetStackAmount(50) return aura:GetStackAmount()") == "5");
    CHECK(rig.host.auraActions == std::vector<std::string>{"setStackAmount", "refresh"});
    // Exactly the max, and a decrease, both stay as given.
    rig.host.auras[5] = base;
    rig.host.auraActions.clear();
    CHECK(rig.ok("aura:SetStackAmount(5) return aura:GetStackAmount()") == "5");
    rig.host.auras[5] = base;
    rig.host.auraActions.clear();
    CHECK(rig.ok("aura:SetStackAmount(1) return aura:GetStackAmount()") == "1");
    CHECK(rig.host.auraActions == std::vector<std::string>{"setStackAmount"});

    // Zero does not leave a live 0-stack aura: the core removes it (ModStackAmount's <= 0 branch).
    rig.host.auras[5] = base;
    rig.host.auraActions.clear();
    CHECK(rig.ok("aura:SetStackAmount(0) return aura:GetStackAmount()") == "2");
    CHECK(rig.host.auraActions == std::vector<std::string>{"remove"});

    // A non-stackable aura (CalcMaxStackAmount 0) becomes one stack, never 0 and never more than one.
    FakeHost::FakeAura single = base;
    single.maxStackAmount = 0;
    rig.host.auras[5] = single;
    rig.host.auraActions.clear();
    CHECK(rig.ok("aura:SetStackAmount(4) return aura:GetStackAmount()") == "1");
    CHECK(rig.host.auraActions == std::vector<std::string>{"setStackAmount"});

    // A non-integer or negative count is not a stack count: the binding raises before the host is called.
    rig.host.auras[5] = base;
    CHECK(errorOf(rig, "return aura:SetStackAmount(2.5)").find("integer") != std::string::npos);
    CHECK(errorOf(rig, "return aura:SetStackAmount(-1)").find("0..255") != std::string::npos);
}

TEST_CASE("aura long tail: a Aura handle dies with the call that made it")
{
    SpellRig rig;
    fl::HandleData au;
    au.kind = fl::ObjKind::Aura;
    au.instanceId = 4;
    rig.host.auras[4].id = 500;
    setGlobal(rig, "aura", au);
    rig.ok("kept_aura = aura");
    CHECK(errorOf(rig, "return kept_aura:Remove()").find("Aura no longer exists") != std::string::npos);
}

TEST_CASE("spellinfo: GetSpellInfo and the class handle")
{
    SpellRig rig;
    rig.host.spellInfo(500);
    CHECK(call(rig, "GetSpellInfo(500):GetId()") == "500");
    CHECK(call(rig, "GetSpellInfo(499)") == "nil");
    // The handle names core data: it does not die with the call that made it.
    rig.ok("kept_info = GetSpellInfo(500)");
    CHECK(call(rig, "kept_info:GetId()") == "500");
    // Both documented producers answer the same entry.
    fl::HandleData sp;
    sp.kind = fl::ObjKind::Spell;
    sp.instanceId = 11;
    rig.host.spells[11].id = 500;
    fl::HandleData au;
    au.kind = fl::ObjKind::Aura;
    au.instanceId = 12;
    rig.host.auras[12].id = 500;
    setGlobal(rig, "spell", sp);
    setGlobal(rig, "aura", au);
    rig.host.spells[11].id = 500;   // each handle lives only for the call that owns it
    CHECK(call(rig, "spell:GetSpellInfo():GetId()") == "500");
    rig.host.auras[12].id = 500;
    CHECK(call(rig, "aura:GetSpellInfo():GetId()") == "500");
}

TEST_CASE("spellinfo: every documented getter reads the fake spell entry")
{
    SpellRig rig;
    FakeHost::FakeSpellInfo& i = rig.host.spellInfo(700);
    i.category = 101;
    i.dispel = 102;
    i.mechanic = 103;
    i.schoolMask = 104;
    i.attributes = 105;
    i.attributesCu = 106;
    i.attributesEx = 107;
    i.attributesEx2 = 108;
    i.attributesEx3 = 109;
    i.attributesEx4 = 110;
    i.attributesEx5 = 111;
    i.attributesEx6 = 112;
    i.attributesEx7 = 113;
    i.duration = 114;
    i.maxDuration = 115;
    i.recoveryTime = 116;
    i.categoryRecoveryTime = 117;
    i.stackAmount = 118;
    i.maxAffectedTargets = 119;
    i.spellFamilyName = 120;
    i.explicitTargetMask = 121;
    i.auraState = 122;
    i.spellSpecific = 123;
    i.baseLevel = 124;
    i.spellLevel = 125;
    i.maxLevel = 126;
    i.maxTargetLevel = 127;
    i.dmgClass = 128;
    i.powerType = 129;
    i.manaCost = 130;
    i.manaCostPerLevel = 131;
    i.manaPerSecond = 132;
    i.procChance = 133;
    i.procCharges = 134;
    i.procFlags = 135;
    i.rank = 136;
    i.targets = 137;
    i.targetCreatureType = 138;
    i.interruptFlags = 139;
    i.auraInterruptFlags = 140;
    i.channelInterruptFlags = 141;
    i.startRecoveryCategory = 142;
    i.startRecoveryTime = 143;
    i.preventionType = 144;
    i.attackType = 145;
    i.activeIconID = 146;
    i.spellIconID = 147;
    i.requiresSpellFocus = 148;
    i.areaGroupId = 149;
    i.casterAuraSpell = 150;
    i.casterAuraState = 151;
    i.casterAuraStateNot = 152;
    i.targetAuraSpell = 153;
    i.targetAuraState = 154;
    i.targetAuraStateNot = 155;
    i.facingCasterFlags = 156;
    i.equippedItemClass = 157;
    i.equippedItemSubClassMask = 158;
    i.equippedItemInventoryTypeMask = 159;
    i.stances = 160;
    i.stancesNot = 161;
    i.calcCastTime = 162;
    i.speed = 1.5;
    i.maxRange = 2.5;
    i.minRange = 3.5;
    fl::HandleData info;
    info.kind = fl::ObjKind::SpellInfo;
    info.guid.lo = 700;
    setGlobal(rig, "info", info);

    std::vector<NumCase> const nums = {
        {"info:GetId()", 700},
        {"info:GetCategory()", 101},
        {"info:GetDispel()", 102},
        {"info:GetMechanic()", 103},
        {"info:GetSchoolMask()", 104},
        {"info:GetAttributes()", 105},
        {"info:GetAttributesCu()", 106},
        {"info:GetAttributesEx()", 107},
        {"info:GetAttributesEx2()", 108},
        {"info:GetAttributesEx3()", 109},
        {"info:GetAttributesEx4()", 110},
        {"info:GetAttributesEx5()", 111},
        {"info:GetAttributesEx6()", 112},
        {"info:GetAttributesEx7()", 113},
        {"info:GetDuration()", 114},
        {"info:GetMaxDuration()", 115},
        {"info:GetRecoveryTime()", 116},
        {"info:GetCategoryRecoveryTime()", 117},
        {"info:GetStackAmount()", 118},
        {"info:GetMaxAffectedTargets()", 119},
        {"info:GetSpellFamilyName()", 120},
        {"info:GetExplicitTargetMask()", 121},
        {"info:GetAuraState()", 122},
        {"info:GetSpellSpecific()", 123},
        {"info:GetBaseLevel()", 124},
        {"info:GetSpellLevel()", 125},
        {"info:GetMaxLevel()", 126},
        {"info:GetMaxTargetLevel()", 127},
        {"info:GetDmgClass()", 128},
        {"info:GetPowerType()", 129},
        {"info:GetManaCost()", 130},
        {"info:GetManaCostPerlevel()", 131},
        {"info:GetManaPerSecond()", 132},
        {"info:GetProcChance()", 133},
        {"info:GetProcCharges()", 134},
        {"info:GetProcFlags()", 135},
        {"info:GetRank()", 136},
        {"info:GetTargets()", 137},
        {"info:GetTargetCreatureType()", 138},
        {"info:GetInterruptFlags()", 139},
        {"info:GetAuraInterruptFlags()", 140},
        {"info:GetChannelInterruptFlags()", 141},
        {"info:GetStartRecoveryCategory()", 142},
        {"info:GetStartRecoveryTime()", 143},
        {"info:GetPreventionType()", 144},
        {"info:GetAttackType()", 145},
        {"info:GetActiveIconID()", 146},
        {"info:GetSpellIconID()", 147},
        {"info:GetRequiresSpellFocus()", 148},
        {"info:GetAreaGroupId()", 149},
        {"info:GetCasterAuraSpell()", 150},
        {"info:GetCasterAuraState()", 151},
        {"info:GetCasterAuraStateNot()", 152},
        {"info:GetTargetAuraSpell()", 153},
        {"info:GetTargetAuraState()", 154},
        {"info:GetTargetAuraStateNot()", 155},
        {"info:GetFacingCasterFlags()", 156},
        {"info:GetEquippedItemClass()", 157},
        {"info:GetEquippedItemSubClassMask()", 158},
        {"info:GetEquippedItemInventoryTypeMask()", 159},
        {"info:GetStances()", 160},
        {"info:GetStancesNot()", 161},
        {"info:CalcCastTime()", 162},
    };
    for (NumCase const& c : nums)
        CHECK(call(rig, c.call) == std::to_string(c.want));

    CHECK(rig.ok("return string.format('%.2f/%.2f/%.2f', info:GetSpeed(), info:GetMaxRange(), info:GetMinRange())") ==
          "1.50/2.50/3.50");
}

TEST_CASE("spellinfo: the indexed getters" )
{
    SpellRig rig;
    FakeHost::FakeSpellInfo& i = rig.host.spellInfo(710);
    i.spellFamilyFlags[0] = 11;
    i.spellFamilyFlags[1] = 22;
    i.spellFamilyFlags[2] = 33;
    i.totem[0] = 44;
    i.totem[1] = 55;
    i.totemCategory[0] = 66;
    i.totemCategory[1] = 77;
    i.reagent[7] = 88;
    i.reagentCount[7] = 99;
    fl::HandleData info;
    info.kind = fl::ObjKind::SpellInfo;
    info.guid.lo = 710;
    setGlobal(rig, "info", info);
    CHECK(call(rig, "info:GetSpellFamilyFlags(0)") == "11");
    CHECK(call(rig, "info:GetSpellFamilyFlags(2)") == "33");
    CHECK(call(rig, "info:GetTotem(1)") == "55");
    CHECK(call(rig, "info:GetTotemCategory(0)") == "66");
    CHECK(call(rig, "info:GetReagent(7)") == "88");
    CHECK(call(rig, "info:GetReagentCount(7)") == "99");
    // Every index the core cannot hold raises instead of reading past its array.
    CHECK(errorOf(rig, "return info:GetSpellFamilyFlags(3)").find("is not 0..2") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetTotem(2)").find("is not 0..1") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetReagent(8)").find("is not 0..7") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetReagent(-1)").find("index") != std::string::npos);
}

TEST_CASE("spellinfo: predicates, effects and the SpellInfo arguments")
{
    SpellRig rig;
    FakeHost::FakeSpellInfo& i = rig.host.spellInfo(720);
    i.effects = {6};
    i.auraTypes = {8};
    i.negativeEffects = {1};
    i.passive = true;
    i.channeled = true;
    i.affectingArea = true;
    i.canBeUsedInCombat = true;
    i.ranked = true;
    i.affectedBySpellMods = true;
    i.triggeredBy = {721};
    i.sameRankOf = {721};
    i.differentRankOf = {722};
    i.highRankOf = {723};
    i.dispels = {721};
    i.pierces = {722};
    i.providesImmunity = {723};
    rig.host.spellInfo(721);
    rig.host.spellInfo(722);
    rig.host.spellInfo(723);
    fl::HandleData info;
    info.kind = fl::ObjKind::SpellInfo;
    info.guid.lo = 720;
    setGlobal(rig, "info", info);
    setGlobal(rig, "player", FakeHost::player(7));

    CHECK(call(rig, "info:HasEffect(6)") == "true");
    CHECK(call(rig, "info:HasEffect(7)") == "false");
    CHECK(call(rig, "info:HasAura(8)") == "true");
    CHECK(call(rig, "info:HasAura(9)") == "false");
    CHECK(call(rig, "info:IsPositiveEffect(0)") == "true");
    CHECK(call(rig, "info:IsPositiveEffect(1)") == "false");
    CHECK(call(rig, "info:IsPassive()") == "true");
    CHECK(call(rig, "info:IsChanneled()") == "true");
    CHECK(call(rig, "info:IsAffectingArea()") == "true");
    CHECK(call(rig, "info:IsTargetingArea()") == "false");
    CHECK(call(rig, "info:CanBeUsedInCombat()") == "true");
    CHECK(call(rig, "info:IsRanked()") == "true");
    CHECK(call(rig, "info:IsAffectedBySpellMods()") == "true");
    CHECK(call(rig, "info:NeedsToBeTriggeredByCaster(GetSpellInfo(721))") == "true");
    CHECK(call(rig, "info:NeedsToBeTriggeredByCaster(GetSpellInfo(722))") == "false");
    CHECK(call(rig, "info:IsRankOf(GetSpellInfo(721))") == "true");
    CHECK(call(rig, "info:IsDifferentRankOf(GetSpellInfo(722))") == "true");
    CHECK(call(rig, "info:IsHighRankOf(GetSpellInfo(723))") == "true");
    CHECK(call(rig, "info:CanDispelAura(GetSpellInfo(721))") == "true");
    CHECK(call(rig, "info:CanPierceImmuneAura(GetSpellInfo(722))") == "true");
    CHECK(call(rig, "info:CanSpellProvideImmunityAgainstAura(GetSpellInfo(723))") == "true");

    // An effect slot the core's own effect list does not hold is refused before the core indexes it.
    CHECK(errorOf(rig, "return info:IsPositiveEffect(32)").find("MAX_SPELL_EFFECTS") != std::string::npos);
    CHECK(errorOf(rig, "return info:NeedsToBeTriggeredByCaster(player)").find("expected kind") != std::string::npos);
}

TEST_CASE("spell and aura: bad arguments raise before the host is called")
{
    SpellRig rig;
    fl::HandleData sp;
    sp.kind = fl::ObjKind::Spell;
    sp.instanceId = 12;
    FakeHost::FakeSpell fixture;
    fixture.id = 800;
    fixture.effectCount = 1;
    fl::HandleData au;
    au.kind = fl::ObjKind::Aura;
    au.instanceId = 13;
    rig.host.auras[13].id = 800;
    setGlobal(rig, "spell", sp);
    setGlobal(rig, "aura", au);
    setGlobal(rig, "player", FakeHost::player(6));
    rig.host.spellInfo(800);

    // Both handles live only for the call that owns them, so every call seeds the store first.
    auto spellError = [&](std::string const& code)
    {
        rig.host.spells[12] = fixture;
        return errorOf(rig, code);
    };
    // Argument ranges the docs and the core give.
    CHECK(spellError("return spell:SetState(6)").find("not a spell state") != std::string::npos);
    CHECK(spellError("return spell:SetState(-1)").find("not a spell state") != std::string::npos);
    CHECK(errorOf(rig, "return aura:SetStackAmount(256)").find("0..255") != std::string::npos);
    CHECK(errorOf(rig, "return aura:SetStackAmount(-1)").find("0..255") != std::string::npos);
    CHECK(spellError("return spell:SetDelayStart(-1)").find("not negative") != std::string::npos);
    CHECK(spellError("return spell:SetSpellValue(40, 1)").find("is not 0..39") != std::string::npos);
    CHECK(spellError("return spell:SetSpellValue(-1, 1)").find("is not 0..39") != std::string::npos);
    CHECK(spellError("return spell:CalculateDamage(1)").find("is not an effect of spell 800") != std::string::npos);
    CHECK(spellError("return spell:GetUnitTargetCountForEffect(32)").find("MAX_SPELL_EFFECTS") != std::string::npos);
    CHECK(spellError("return spell:IsTriggeredByAura(player)").find("expected kind") != std::string::npos);
    CHECK(spellError("return spell:IsTriggeredByAura()").find("spellInfo") != std::string::npos);
    // A receiver of another class is refused by the class check.
    std::string const wrong = spellError("return spell:GetEntry(1) player:GetGUID()");
    CHECK(wrong.find("no error") == std::string::npos);
}

TEST_CASE("spellinfo: extended scalar, predicate, effect, target and stub methods")
{
    SpellRig rig;
    FakeHost::FakeSpellInfo& i = rig.host.spellInfo(730);
    i.allEffectsMechanicMask = 201;
    i.allowedMechanicMask = 202;
    i.excludeCasterAuraSpell = 203;
    i.excludeTargetAuraSpell = 204;
    i.manaCostPercentage = 15;
    i.maxTicks = 6;
    i.spellVisual[0] = 205;
    i.spellVisual[1] = 206;
    i.diminishingGroup = 7;
    i.diminishingGroupType = 2;
    i.diminishingLimitDuration = 10000;
    i.diminishingMaxLevel = 3;
    i.abilityLearnedWithProfession = true;
    i.breakingStealth = true;
    i.explicitDiscovery = true;
    i.lootCrafting = true;
    i.passiveStackableWithRanks = true;
    i.primaryProfession = true;
    i.primaryProfessionFirstRank = true;
    i.profession = true;
    i.professionOrRiding = true;
    i.stackableOnOneSlotWithDifferentCasters = true;
    i.needsComboPoints = true;
    i.skillTypes = {164};
    i.affectedFamilyName = 10;
    i.affectedFlags[0] = 0x4;
    i.exclusiveWith = {731};
    i.exclusivePerCasterWith = {732};
    i.mechanic = 3;
    i.checkTargetResult = 0;

    FakeHost::FakeSpellInfo::FakeEffect eff0;
    eff0.isEffect = true;
    eff0.isAura = true;
    eff0.isAreaAuraEffect = true;
    eff0.isTargetingArea = true;
    eff0.isUnitOwnedAuraEffect = true;
    eff0.hasRadius = true;
    eff0.auraType = 12;
    eff0.effectType = 6;
    eff0.amplitude = 3000;
    eff0.basePoints = -45;
    eff0.calcValue = -44;
    eff0.chainTarget = 4;
    eff0.dieSides = 13;
    eff0.implicitTargetType = 2;
    eff0.effectIndex = 0;
    eff0.itemType = 999;
    eff0.mechanic = 5;
    eff0.mechanicMask = 32;
    eff0.miscValue = 17;
    eff0.miscValueB = 18;
    eff0.providedTargetMask = 0x10;
    eff0.triggerSpell = 731;
    eff0.usedTargetObjectType = 3;
    eff0.spellClassMask[0] = 0x11;
    eff0.spellClassMask[1] = 0x22;
    eff0.spellClassMask[2] = 0x33;
    eff0.bonusMultiplier = 0.75;
    eff0.calcRadius = 12.5;
    eff0.damageMultiplier = 1.25;
    eff0.pointsPerComboPoint = 2.5;
    eff0.realPointsPerLevel = 1.5;
    eff0.valueMultiplier = 2.25;
    eff0.targetA = {1, 2, 3, 4, 5, 6, 0.5, true};
    eff0.targetB = {7, 8, 9, 10, 11, 12, 1.5, false};
    i.effectSlots = {eff0};

    rig.host.spellInfo(731);
    rig.host.spellInfo(732);

    fl::HandleData caster = creature(50);
    fl::HandleData target = creature(51);
    fl::HandleData item = FakeHost::item(801);
    rig.host.add(caster, fl::ObjectInfo{"Caster", 50});
    rig.host.add(target, fl::ObjectInfo{"Target", 51});
    rig.host.add(item, fl::ObjectInfo{"Test Item", 1234});
    i.fitItems = {801};
    i.checkTargetCreatureTypes = {51};

    fl::HandleData info;
    info.kind = fl::ObjKind::SpellInfo;
    info.guid.lo = 730;
    setGlobal(rig, "info", info);
    setGlobal(rig, "caster", caster);
    setGlobal(rig, "target", target);
    setGlobal(rig, "item", item);

    fl::HandleData ae;
    ae.kind = fl::ObjKind::AuraEffect;
    ae.instanceId = 99;
    rig.host.auraEffects[99].id = 731;
    i.cancelsAuraEffects = {99};
    setGlobal(rig, "ae", ae);

    CHECK(call(rig, "info:GetAllEffectsMechanicMask()") == "201");
    CHECK(call(rig, "info:GetAllowedMechanicMask()") == "202");
    CHECK(call(rig, "info:GetExcludeCasterAuraSpell()") == "203");
    CHECK(call(rig, "info:GetExcludeTargetAuraSpell()") == "204");
    CHECK(call(rig, "info:GetManaCostPercentage()") == "15");
    CHECK(call(rig, "info:GetMaxTicks()") == "6");
    CHECK(call(rig, "info:GetSpellVisual(0)") == "205");
    CHECK(call(rig, "info:GetSpellVisual(1)") == "206");
    CHECK(call(rig, "info:GetSpellMechanicMaskByEffectMask(1)") == "40");
    CHECK(call(rig, "info:GetDiminishingReturnsGroupForSpell(false)") == "7");
    CHECK(call(rig, "info:GetDiminishingReturnsGroupType(true)") == "2");
    CHECK(call(rig, "info:GetDiminishingReturnsLimitDuration(false)") == "10000");
    CHECK(call(rig, "info:GetDiminishingReturnsMaxLevel(false)") == "3");

    CHECK(call(rig, "info:CheckTarget(caster, target)") == "0");
    CHECK(call(rig, "info:CheckTargetCreatureType(target)") == "true");
    CHECK(call(rig, "info:CheckTargetCreatureType(caster)") == "false");
    CHECK(call(rig, "info:IsAbilityLearnedWithProfession()") == "true");
    CHECK(call(rig, "info:IsAbilityOfSkillType(164)") == "true");
    CHECK(call(rig, "info:IsAbilityOfSkillType(999)") == "false");
    CHECK(call(rig, "info:IsAffected(10, 4, 0, 0)") == "true");
    CHECK(call(rig, "info:IsAffected(10, 8, 0, 0)") == "false");
    CHECK(call(rig, "info:IsAuraExclusiveBySpecificWith(GetSpellInfo(731))") == "true");
    CHECK(call(rig, "info:IsAuraExclusiveBySpecificPerCasterWith(GetSpellInfo(732))") == "true");
    CHECK(call(rig, "info:IsBreakingStealth()") == "true");
    CHECK(call(rig, "info:IsExplicitDiscovery()") == "true");
    CHECK(call(rig, "info:IsItemFitToSpellRequirements(item)") == "true");
    CHECK(call(rig, "info:IsLootCrafting()") == "true");
    CHECK(call(rig, "info:IsPassiveStackableWithRanks()") == "true");
    CHECK(call(rig, "info:IsPrimaryProfession()") == "true");
    CHECK(call(rig, "info:IsPrimaryProfessionFirstRank()") == "true");
    CHECK(call(rig, "info:IsProfession()") == "true");
    CHECK(call(rig, "info:IsProfessionOrRiding()") == "true");
    CHECK(call(rig, "info:IsStackableOnOneSlotWithDifferentCasters()") == "true");
    CHECK(call(rig, "info:NeedsComboPoints()") == "true");
    rig.host.auraEffects[99].id = 731;
    CHECK(call(rig, "info:SpellCancelsAuraEffect(ae)") == "true");

    // Per-effect getters and predicates.
    CHECK(call(rig, "info:EffectIsAreaAuraEffect(0)") == "true");
    CHECK(call(rig, "info:EffectIsAura(0)") == "true");
    CHECK(call(rig, "info:EffectIsAuraType(0, 12)") == "true");
    CHECK(call(rig, "info:EffectIsAuraType(0, 99)") == "false");
    CHECK(call(rig, "info:EffectIsEffect(0)") == "true");
    CHECK(call(rig, "info:EffectIsEffectType(0, 6)") == "true");
    CHECK(call(rig, "info:EffectIsEffectType(0, 7)") == "false");
    CHECK(call(rig, "info:EffectIsTargetingArea(0)") == "true");
    CHECK(call(rig, "info:EffectIsUnitOwnedAuraEffect(0)") == "true");
    CHECK(call(rig, "info:GetEffectAmplitude(0)") == "3000");
    CHECK(call(rig, "info:GetEffectApplyAuraName(0)") == "12");
    CHECK(call(rig, "info:GetEffectBasePoints(0)") == "-45");
    CHECK(call(rig, "info:GetEffectCalcValue(0)") == "-44");
    CHECK(call(rig, "info:GetEffectChainTarget(0)") == "4");
    CHECK(call(rig, "info:GetEffectDieSides(0)") == "13");
    CHECK(call(rig, "info:GetEffectHasRadius(0)") == "true");
    CHECK(call(rig, "info:GetEffectImplicitTargetType(0)") == "2");
    CHECK(call(rig, "info:GetEffectIndex(0)") == "0");
    CHECK(call(rig, "info:GetEffectItemType(0)") == "999");
    CHECK(call(rig, "info:GetEffectMechanic(0)") == "5");
    CHECK(call(rig, "info:GetEffectMechanicMask(0)") == "32");
    CHECK(call(rig, "info:GetEffectMiscValue(0)") == "17");
    CHECK(call(rig, "info:GetEffectMiscValueB(0)") == "18");
    CHECK(call(rig, "info:GetEffectProvidedTargetMask(0)") == "16");
    CHECK(call(rig, "info:GetEffectSpellClassMask(0, 0)") == "17");
    CHECK(call(rig, "info:GetEffectSpellClassMask(0, 2)") == "51");
    CHECK(call(rig, "info:GetEffectTriggerSpell(0)") == "731");
    CHECK(call(rig, "info:GetEffectType(0)") == "6");
    CHECK(call(rig, "info:GetEffectUsedTargetObjectType(0)") == "3");
    CHECK(rig.ok("return string.format('%.2f/%.2f/%.2f/%.2f/%.2f/%.2f', info:GetEffectBonusMultiplier(0), "
                 "info:GetEffectCalcRadius(0), info:GetEffectDamageMultiplier(0), info:GetEffectPointsPerComboPoint(0), "
                 "info:GetEffectRealPointsPerLevel(0), info:GetEffectValueMultiplier(0))") ==
          "0.75/12.50/1.25/2.50/1.50/2.25");

    CHECK(call(rig, "info:GetEffectTargetACheckType(0)") == "1");
    CHECK(call(rig, "info:GetEffectTargetADirectionType(0)") == "2");
    CHECK(call(rig, "info:GetEffectTargetAObjectType(0)") == "3");
    CHECK(call(rig, "info:GetEffectTargetAReferenceType(0)") == "4");
    CHECK(call(rig, "info:GetEffectTargetASelectionCategory(0)") == "5");
    CHECK(call(rig, "info:GetEffectTargetATarget(0)") == "6");
    CHECK(call(rig, "info:GetEffectTargetAIsArea(0)") == "true");
    CHECK(rig.ok("return string.format('%.1f/%.1f', info:GetEffectTargetADirectionAngle(0), "
                 "info:GetEffectTargetBDirectionAngle(0))") == "0.5/1.5");
    CHECK(call(rig, "info:GetEffectTargetBCheckType(0)") == "7");
    CHECK(call(rig, "info:GetEffectTargetBDirectionType(0)") == "8");
    CHECK(call(rig, "info:GetEffectTargetBObjectType(0)") == "9");
    CHECK(call(rig, "info:GetEffectTargetBReferenceType(0)") == "10");
    CHECK(call(rig, "info:GetEffectTargetBSelectionCategory(0)") == "11");
    CHECK(call(rig, "info:GetEffectTargetBTarget(0)") == "12");
    CHECK(call(rig, "info:GetEffectTargetBIsArea(0)") == "false");

    // Self-cast detection from effect targets (TARGET_UNIT_CASTER == 1).
    CHECK(call(rig, "info:IsSelfCast()") == "false");
    {
        FakeHost::FakeSpellInfo& selfInfo = rig.host.spellInfo(733);
        FakeHost::FakeSpellInfo::FakeEffect se0;
        se0.isEffect = true;
        se0.targetA.target = 1;
        se0.targetB.target = 0;
        FakeHost::FakeSpellInfo::FakeEffect se1;
        se1.isEffect = false;
        se1.targetA.target = 6;
        selfInfo.effectSlots = {se0, se1};
        CHECK(call(rig, "GetSpellInfo(733):IsSelfCast()") == "true");

        FakeHost::FakeSpellInfo& areaInfo = rig.host.spellInfo(734);
        FakeHost::FakeSpellInfo::FakeEffect ae0;
        ae0.isEffect = true;
        ae0.targetA.target = 1;
        ae0.targetB.target = 20;
        areaInfo.effectSlots = {ae0};
        CHECK(call(rig, "GetSpellInfo(734):IsSelfCast()") == "false");

        rig.host.spellInfo(735);
        CHECK(call(rig, "GetSpellInfo(735):IsSelfCast()") == "false");
    }

    // Bounds checks and core stubs.
    CHECK(errorOf(rig, "return info:GetEffectType(1)").find("is not an effect") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetEffectDieSides(1)").find("is not an effect") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetEffectDieSides(-1)").find("0..4294967295") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetEffectSpellClassMask(0, 3)").find("0..2") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetSpellVisual(2)").find("0..1") != std::string::npos);
    CHECK(errorOf(rig, "return info:GetDiminishingReturnsGroupForSpell()").find("boolean") != std::string::npos);
    for (char const* stubCall : {"info:GetManaPerSecondPerLevel()", "info:GetPriority()",
                                 "info:GetRuneCostID()", "info:IsAffectedBySpellMod()"})
    {
        std::string const err = errorOf(rig, std::string("return ") + stubCall);
        INFO("stub " << stubCall << ": " << err);
        CHECK(err.find("not available on this core") != std::string::npos);
    }
}
