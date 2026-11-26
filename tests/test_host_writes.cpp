// The guild membership write rule of src/host/GuildWrite.h, which the world-thread job runs. A script's explicit rank
// id is validated by the core (Guild::ChangeMemberRank refuses a rank the guild does not have): a member is added at the
// lowest rank first and moved only to a rank that exists, and a guild that disbanded or a player that logged out before
// the queued job ran leaves nothing written.
#include "GuildWrite.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using flh::GuildAddOutcome;
using flh::GuildWrite;
using flh::GuildWriteKind;
using flh::GuildWriteResult;

namespace
{
// The guild the job acts on, with the state a write changes and a record of every core call it made. AddMember mirrors
// the core adapter: add at the lowest rank, then set the requested rank only if the guild has it.
struct ModelGuild
{
    std::vector<uint32_t> ranks{0, 1, 2};
    std::vector<std::string> calls;
    bool accepts = true;
    bool hasMember = false;
    uint32_t memberRank = 0;

    bool RankExists(uint32_t rank) const
    {
        return std::find(ranks.begin(), ranks.end(), rank) != ranks.end();
    }
    GuildAddOutcome AddMember(bool hasRank, uint32_t rank)
    {
        if (!accepts)
        {
            calls.push_back("refused");
            return GuildAddOutcome::NotAdded;
        }
        calls.push_back("add lowest");
        hasMember = true;
        memberRank = ranks.front();
        if (!hasRank)
            return GuildAddOutcome::Added;
        if (!RankExists(rank))
            return GuildAddOutcome::AddedAtDefaultRank;
        calls.push_back("rank " + std::to_string(rank));
        memberRank = rank;
        return GuildAddOutcome::Added;
    }
    void DeleteMember(bool isDisbanding) { calls.push_back(isDisbanding ? "delete+disband" : "delete"); }
    void ChangeRank(uint32_t rank) { calls.push_back("rank " + std::to_string(rank)); }
    void Disband() { calls.push_back("disband"); }
};

GuildWrite addMember(bool hasRank, uint32_t rank)
{
    GuildWrite w;
    w.kind = GuildWriteKind::AddMember;
    w.hasRank = hasRank;
    w.rank = rank;
    return w;
}

GuildWrite changeRank(uint32_t rank)
{
    GuildWrite w;
    w.kind = GuildWriteKind::ChangeRank;
    w.rank = rank;
    return w;
}
}  // namespace

TEST_CASE("guild write: AddMember sets a rank the guild has and keeps the member at the default rank otherwise")
{
    ModelGuild guild;
    CHECK(flh::applyGuildWrite(&guild, true, addMember(true, 2)) == GuildWriteResult::Done);
    CHECK(guild.hasMember);
    CHECK(guild.memberRank == 2);
    REQUIRE(guild.calls.size() == 2);
    CHECK(guild.calls[0] == "add lowest");
    CHECK(guild.calls[1] == "rank 2");

    // 3 and 200 are not ranks of this guild (0, 1, 2): the member is added at the default rank and the job says so.
    ModelGuild unknown;
    CHECK(flh::applyGuildWrite(&unknown, true, addMember(true, 3)) == GuildWriteResult::AddedAtDefaultRank);
    CHECK(unknown.hasMember);
    CHECK(unknown.memberRank == 0);
    CHECK(unknown.calls == std::vector<std::string>{"add lowest"});

    ModelGuild big;
    CHECK(flh::applyGuildWrite(&big, true, addMember(true, 200)) == GuildWriteResult::AddedAtDefaultRank);
    CHECK(big.memberRank == 0);
    CHECK(big.calls == std::vector<std::string>{"add lowest"});

    // No rank from the script: the core's lowest rank, no rank call.
    ModelGuild fresh;
    CHECK(flh::applyGuildWrite(&fresh, true, addMember(false, 0)) == GuildWriteResult::Done);
    CHECK(fresh.calls == std::vector<std::string>{"add lowest"});
    CHECK(fresh.memberRank == 0);
}

TEST_CASE("guild write: a member the core refuses is not added and the job reports done")
{
    ModelGuild guild;
    guild.accepts = false;
    CHECK(flh::applyGuildWrite(&guild, true, addMember(true, 1)) == GuildWriteResult::Done);
    CHECK(!guild.hasMember);
    CHECK(guild.calls == std::vector<std::string>{"refused"});
}

TEST_CASE("guild write: a guild disbanded or a player logged out before the queued job runs writes nothing")
{
    ModelGuild guild;
    CHECK(flh::applyGuildWrite<ModelGuild>(nullptr, true, addMember(true, 1)) == GuildWriteResult::GuildGone);
    CHECK(flh::applyGuildWrite(&guild, false, addMember(true, 1)) == GuildWriteResult::PlayerGone);
    CHECK(flh::applyGuildWrite(&guild, false, changeRank(1)) == GuildWriteResult::PlayerGone);
    GuildWrite remove;
    remove.kind = GuildWriteKind::DeleteMember;
    CHECK(flh::applyGuildWrite(&guild, false, remove) == GuildWriteResult::PlayerGone);
    CHECK(guild.calls.empty());
    CHECK(!guild.hasMember);

    // Disband needs no player, and the guild is gone afterwards (the job must not touch it again).
    GuildWrite disband;
    CHECK(flh::applyGuildWrite(&guild, false, disband) == GuildWriteResult::Done);
    REQUIRE(guild.calls.size() == 1);
    CHECK(guild.calls[0] == "disband");
}
