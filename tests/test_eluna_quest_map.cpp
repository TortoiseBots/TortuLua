// Eluna Quest and Map coverage: every Quest method (the 4 existing + 9 added), argument checks, stale-handle
// refusal, and the Map instance-data core stubs.
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

struct QuestMapRig : Rig
{
    QuestMapRig() : Rig([](fl::Config& c) { c = elunaConfig(c); }) { }
};

fl::HandleData addQuest(QuestMapRig& rig, FakeHost::FakeQuest const& q)
{
    rig.host.questStates[q.id] = q;
    fl::HandleData h;
    h.kind = fl::ObjKind::Quest;
    h.guid.lo = q.id;
    rig.host.add(h, fl::ObjectInfo{q.title, q.id, 0, 0, 0, 0, true});
    return h;
}
}  // namespace

TEST_CASE("eluna quest: all 13 documented methods, negative prev quest, high-bit flags, and stale refusal")
{
    QuestMapRig rig;
    FakeHost::FakeQuest q;
    q.id = 420;
    q.level = 60;
    q.minLevel = 55;
    q.maxLevel = 60;
    q.type = 2;
    q.flags = 0x80001008u;   // SHARABLE (0x8) | DAILY (0x1000) | FAIL_ON_LOGOUT (0x80000000)
    q.prevQuestId = -419;    // negative prerequisite quest id
    q.nextQuestId = 421;
    q.nextQuestInChain = 422;
    q.title = "The High-Bit Quest";
    q.daily = true;
    q.repeatable = true;
    fl::HandleData const h = addQuest(rig, q);

    REQUIRE(rig.ok("q = GetQuest(420) return tostring(q ~= nil)") == "true");
    CHECK(rig.ok("return q:GetId()") == "420");
    CHECK(rig.ok("return q:GetLevel()") == "60");
    CHECK(rig.ok("return q:GetMinLevel()") == "55");
    CHECK(rig.ok("return q:GetMaxLevel()") == "60");
    CHECK(rig.ok("return q:GetType()") == "2");
    CHECK(rig.ok("return q:GetFlags()") == "2147487752");
    CHECK(rig.ok("return q:GetPrevQuestId()") == "-419");
    CHECK(rig.ok("return q:GetNextQuestId()") == "421");
    CHECK(rig.ok("return q:GetNextQuestInChain()") == "422");
    CHECK(rig.ok("return q:GetTitle()") == "The High-Bit Quest");
    CHECK(rig.ok("return tostring(q:IsDaily())") == "true");
    CHECK(rig.ok("return tostring(q:IsRepeatable())") == "true");

    CHECK(rig.ok("return tostring(q:HasFlag(0x8))") == "true");
    CHECK(rig.ok("return tostring(q:HasFlag(0x1000))") == "true");
    CHECK(rig.ok("return tostring(q:HasFlag(0x80000000))") == "true");
    CHECK(rig.ok("return tostring(q:HasFlag(0x4))") == "false");
    CHECK(rig.ok("return tostring(q:HasFlag(0))") == "false");

    // Non-daily, non-repeatable quest with zero chain ids and -1 level.
    FakeHost::FakeQuest q2;
    q2.id = 425;
    q2.level = -1;
    q2.minLevel = 0;
    q2.maxLevel = 0;
    q2.type = 0;
    q2.flags = 0;
    q2.prevQuestId = 420;
    q2.nextQuestId = 0;
    q2.nextQuestInChain = 0;
    q2.title = "Scaling Quest";
    q2.daily = false;
    q2.repeatable = false;
    addQuest(rig, q2);

    REQUIRE(rig.ok("q2 = GetQuest(425) return tostring(q2 ~= nil)") == "true");
    CHECK(rig.ok("return q2:GetLevel()") == "-1");
    CHECK(rig.ok("return q2:GetMinLevel()") == "0");
    CHECK(rig.ok("return q2:GetMaxLevel()") == "0");
    CHECK(rig.ok("return q2:GetPrevQuestId()") == "420");
    CHECK(rig.ok("return tostring(q2:IsDaily())") == "false");
    CHECK(rig.ok("return tostring(q2:IsRepeatable())") == "false");
    CHECK(rig.ok("return tostring(q2:HasFlag(0x8))") == "false");

    // Unknown quest returns nil from GetQuest.
    CHECK(rig.ok("return tostring(GetQuest(999999))") == "nil");

    // Bad arguments to HasFlag fail before touching the host.
    rig.host.gameCalls.clear();
    for (char const* bad : {"q:HasFlag()", "q:HasFlag('x')", "q:HasFlag(-1)", "q:HasFlag(4294967296)"})
    {
        fl::CallResult r = rig.run(bad);
        REQUIRE_FALSE(r.ok);
    }
    CHECK(rig.host.gameCalls.empty());

    // Removing the quest from the store makes every method raise "Quest no longer exists".
    rig.host.remove(h);
    std::vector<std::string> const calls = {
        "GetFlags()", "GetId()", "GetLevel()", "GetMaxLevel()", "GetMinLevel()", "GetNextQuestId()",
        "GetNextQuestInChain()", "GetPrevQuestId()", "GetTitle()", "GetType()", "HasFlag(8)", "IsDaily()",
        "IsRepeatable()",
    };
    for (std::string const& call : calls)
    {
        INFO("stale quest call: q:" << call);
        fl::CallResult r = rig.run("return q:" + call);
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find("Quest no longer exists") != std::string::npos);
    }
}

TEST_CASE("eluna map: GetInstanceData and SaveInstanceData are documented core stubs")
{
    QuestMapRig rig;
    fl::HandleData mapHandle;
    mapHandle.kind = fl::ObjKind::Map;
    mapHandle.mapId = 1;
    mapHandle.instanceId = 0;
    rig.host.add(mapHandle, fl::ObjectInfo{"Kalimdor", 1, 0, 0, 0, 0, true});

    REQUIRE(rig.ok("m = GetMapById(1, 0) return tostring(m ~= nil)") == "true");
    rig.host.gameCalls.clear();
    for (char const* method : {"GetInstanceData", "SaveInstanceData"})
    {
        fl::CallResult r = rig.run(std::string("return m:") + method + "()");
        REQUIRE_FALSE(r.ok);
        CHECK(r.error.find(std::string(method) + ": not available on this core") != std::string::npos);
    }
    CHECK(rig.host.gameCalls.empty());
}
