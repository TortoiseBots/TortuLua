// Tests for the Eluna AuraEffect and ElunaProcInfo classes (docs: AuraEffect/index.html, ElunaProcInfo/index.html).
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

struct AuraProcRig : Rig
{
    AuraProcRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

void setGlobal(AuraProcRig& rig, char const* name, fl::HandleData const& h)
{
    lua_State* L = rig.e().state();
    int top = lua_gettop(L);
    fl::eluPushHandle(L, h);
    lua_setglobal(L, name);
    lua_settop(L, top);
}

std::string call(AuraProcRig& rig, std::string const& expr)
{
    INFO("call: " << expr);
    return rig.ok("return tostring(" + expr + ")");
}

std::string errorOf(AuraProcRig& rig, std::string const& code)
{
    fl::CallResult r = rig.run(code);
    return r.ok ? std::string("no error") : r.error;
}
}  // namespace

TEST_CASE("auraeffect: every documented getter, mutator, stale-handle and bad-arg check")
{
    AuraProcRig rig;
    fl::HandleData caster = FakeHost::creature(60);
    fl::HandleData target1 = FakeHost::creature(61);
    fl::HandleData target2 = FakeHost::player(62);
    rig.host.add(caster, fl::ObjectInfo{"Caster", 60});
    rig.host.add(target1, fl::ObjectInfo{"Target1", 61});
    rig.host.add(target2, fl::ObjectInfo{"Target2", 62});
    rig.host.spellInfo(900);

    FakeHost::FakeAura baseAura;
    baseAura.id = 900;
    FakeHost::FakeAuraEffect fixture;
    fixture.id = 900;
    fixture.effIndex = 1;
    fixture.auraType = 42;
    fixture.amount = 150;
    fixture.baseAmount = 120;
    fixture.amplitude = 2000;
    fixture.periodicTimer = 750;
    fixture.tickNumber = 3;
    fixture.totalTicks = 8;
    fixture.remainingTicks = 5;
    fixture.canBeRecalculated = true;
    fixture.hasCaster = true;
    fixture.caster = caster;
    fixture.casterGuid = caster.guid;
    fixture.hasBase = true;
    fixture.baseAuraId = 77;
    fixture.targetList = {target1, target2};
    fixture.calculatedAmount = 210;

    fl::HandleData ae;
    ae.kind = fl::ObjKind::AuraEffect;
    ae.instanceId = 15;
    setGlobal(rig, "ae", ae);
    setGlobal(rig, "caster", caster);
    setGlobal(rig, "t1", target1);
    setGlobal(rig, "t2", target2);

    auto run = [&](std::string const& expr)
    {
        rig.host.auras[77] = baseAura;
        rig.host.auraEffects[15] = fixture;
        return call(rig, expr);
    };

    CHECK(run("ae:GetId()") == "900");
    CHECK(run("ae:GetEffIndex()") == "1");
    CHECK(run("ae:GetAuraType()") == "42");
    CHECK(run("ae:GetAmount()") == "150");
    CHECK(run("ae:GetBaseAmount()") == "120");
    CHECK(run("ae:GetAmplitude()") == "2000");
    CHECK(run("ae:GetPeriodicTimer()") == "750");
    CHECK(run("ae:GetTickNumber()") == "3");
    CHECK(run("ae:GetTotalTicks()") == "8");
    CHECK(run("ae:GetRemainingTicks()") == "5");
    CHECK(run("ae:CanBeRecalculated()") == "true");
    CHECK(run("ae:GetCaster() == caster") == "true");
    CHECK(run("ae:GetCasterGUID()") == rig.host.guidToString(caster.guid));
    CHECK(run("ae:GetBase():GetAuraId()") == "900");
    CHECK(run("ae:GetSpellInfo():GetId()") == "900");
    CHECK(run("ae:CalculateAmount(caster)") == "210");
    rig.host.auraEffects[15] = fixture;
    CHECK(rig.ok("local list = ae:GetTargetList() return tostring(#list) .. ',' .. tostring(list[1] == t1) .. ',' .. "
                 "tostring(list[2] == t2)") == "2,true,true");

    // Mutators and read-backs in one call.
    rig.host.auraEffects[15] = fixture;
    rig.host.auraEffectActions.clear();
    CHECK(rig.ok("ae:SetAmount(333) local c1 = ae:CanBeRecalculated() "
                 "ae:SetCanBeRecalculated(true) local c2 = ae:CanBeRecalculated() "
                 "ae:RecalculateAmount() local a1 = ae:GetAmount() "
                 "ae:ChangeAmount(444, true, false) local a2 = ae:GetAmount() "
                 "ae:SetPeriodicTimer(500) local pt = ae:GetPeriodicTimer() "
                 "ae:CalculatePeriodic(caster, true, false) "
                 "return tostring(c1) .. ',' .. tostring(c2) .. ',' .. a1 .. ',' .. a2 .. ',' .. pt") ==
          "false,true,210,444,500");
    CHECK(rig.host.auraEffectActions ==
          std::vector<std::string>{"recalculateAmount", "changeAmount", "calculatePeriodic"});

    // Missing caster returns nil.
    FakeHost::FakeAuraEffect bare = fixture;
    bare.hasCaster = false;
    rig.host.auraEffects[15] = bare;
    CHECK(call(rig, "ae:GetCaster() == nil") == "true");

    // Bad arguments raise before touching the host.
    rig.host.auraEffects[15] = fixture;
    CHECK(errorOf(rig, "return ae:CalculateAmount()").find("caster") != std::string::npos);
    rig.host.auraEffects[15] = fixture;
    CHECK(errorOf(rig, "return ae:SetAmount(1.5)").find("integer") != std::string::npos);
    rig.host.auraEffects[15] = fixture;
    CHECK(errorOf(rig, "return ae:SetPeriodicTimer('bad')").find("number") != std::string::npos);

    // Transient lifetime: a handle kept past its call fails.
    rig.host.auraEffects[15] = fixture;
    rig.ok("kept_ae = ae");
    CHECK(errorOf(rig, "return kept_ae:GetId()").find("AuraEffect no longer exists") != std::string::npos);
}

TEST_CASE("procinfo: every documented getter, mutator, stub and stale-handle check")
{
    AuraProcRig rig;
    fl::HandleData actor = FakeHost::player(70);
    fl::HandleData target = FakeHost::creature(71);
    fl::HandleData mapH;
    mapH.kind = fl::ObjKind::Map;
    mapH.mapId = 1;
    mapH.instanceId = 0;
    fl::HandleData procSpell;
    procSpell.kind = fl::ObjKind::Spell;
    procSpell.instanceId = 88;
    procSpell.mapId = 950;
    rig.host.spellInfo(950);

    FakeHost::FakeProcInfo fixture;
    fixture.hasActor = true;
    fixture.actor = actor;
    fixture.hasActionTarget = true;
    fixture.actionTarget = target;
    fixture.hasMap = true;
    fixture.map = mapH;
    fixture.hasProcSpell = true;
    fixture.procSpell = procSpell;
    fixture.spellId = 950;
    fixture.hasDamage = true;
    fixture.hasHeal = true;
    fixture.attackType = 1;
    fixture.block = 20;
    fixture.damage = 500;
    fixture.damageAbsorb = 30;
    fixture.damageType = 2;
    fixture.effectiveHeal = 250;
    fixture.heal = 300;
    fixture.healAbsorb = 40;
    fixture.hitMask = 0x12;
    fixture.resist = 50;
    fixture.schoolMask = 0x04;
    fixture.spellPhaseMask = 0x08;
    fixture.spellTypeMask = 0x10;
    fixture.typeMask = 0x20;

    fl::HandleData pi;
    pi.kind = fl::ObjKind::ElunaProcInfo;
    pi.instanceId = 21;
    setGlobal(rig, "pi", pi);
    setGlobal(rig, "actor", actor);
    setGlobal(rig, "target", target);
    setGlobal(rig, "mapH", mapH);

    auto run = [&](std::string const& expr)
    {
        rig.host.spells[88].id = 950;
        rig.host.procInfos[21] = fixture;
        return call(rig, expr);
    };

    CHECK(run("pi:GetActor() == actor") == "true");
    CHECK(run("pi:GetActionTarget() == target") == "true");
    CHECK(run("pi:GetMap() == mapH") == "true");
    CHECK(run("pi:GetProcSpell():GetEntry()") == "950");
    CHECK(run("pi:GetSpellInfo():GetId()") == "950");
    CHECK(run("pi:GetAttackType()") == "1");
    CHECK(run("pi:GetBlock()") == "20");
    CHECK(run("pi:GetDamage()") == "500");
    CHECK(run("pi:GetDamageAbsorb()") == "30");
    CHECK(run("pi:GetDamageType()") == "2");
    CHECK(run("pi:GetEffectiveHeal()") == "250");
    CHECK(run("pi:GetHeal()") == "300");
    CHECK(run("pi:GetHealAbsorb()") == "40");
    CHECK(run("pi:GetHitMask()") == "18");
    CHECK(run("pi:GetResist()") == "50");
    CHECK(run("pi:GetSchoolMask()") == "4");
    CHECK(run("pi:GetSpellPhaseMask()") == "8");
    CHECK(run("pi:GetSpellTypeMask()") == "16");
    CHECK(run("pi:GetTypeMask()") == "32");
    CHECK(run("pi:HasDamage()") == "true");
    CHECK(run("pi:HasHeal()") == "true");

    // Damage and heal mutators in one call.
    rig.host.procInfos[21] = fixture;
    CHECK(rig.ok("pi:SetDamage(400) pi:SetAbsorbDamage(50) pi:SetBlockDamage(25) pi:SetResistDamage(15) "
                 "pi:SetAbsorbHeal(60) pi:SetEffectiveHeal(120) "
                 "return pi:GetDamage() .. ',' .. pi:GetDamageAbsorb() .. ',' .. pi:GetBlock() .. ',' .. "
                 "pi:GetResist() .. ',' .. pi:GetHeal() .. ',' .. pi:GetHealAbsorb() .. ',' .. pi:GetEffectiveHeal()") ==
          "310,80,45,65,240,100,120");

    // SetHeal is a documented core stub.
    rig.host.procInfos[21] = fixture;
    CHECK(errorOf(rig, "return pi:SetHeal(100)").find("not available on this core") != std::string::npos);

    // Argument validation.
    rig.host.procInfos[21] = fixture;
    CHECK(errorOf(rig, "return pi:SetAbsorbDamage(-1)").find("0..4294967295") != std::string::npos);
    rig.host.procInfos[21] = fixture;
    CHECK(errorOf(rig, "return pi:SetDamage('nope')").find("number") != std::string::npos);

    // Optional handles nil when absent.
    FakeHost::FakeProcInfo empty;
    rig.host.procInfos[21] = empty;
    CHECK(call(rig, "pi:GetProcSpell() == nil") == "true");
    rig.host.procInfos[21] = empty;
    CHECK(call(rig, "pi:GetSpellInfo() == nil") == "true");
    rig.host.procInfos[21] = empty;
    CHECK(call(rig, "pi:HasDamage()") == "false");
    rig.host.procInfos[21] = empty;
    CHECK(call(rig, "pi:HasHeal()") == "false");

    // Transient lifetime: a handle kept past its call fails.
    rig.host.procInfos[21] = fixture;
    rig.ok("kept_pi = pi");
    CHECK(errorOf(rig, "return kept_pi:GetDamage()").find("ElunaProcInfo no longer exists") != std::string::npos);
}
