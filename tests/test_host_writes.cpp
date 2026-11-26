// The guild membership write rule of src/host/GuildWrite.h, which the world-thread job runs. A script's explicit rank
// id must be a rank the guild has (the core's AddMember stores any id it is given), and a guild that disbanded or a
// player that logged out before the queued job ran leaves nothing written.
#include "GuildWrite.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using flh::GuildWrite;
using flh::GuildWriteKind;
using flh::GuildWriteResult;

namespace
{
// The guild the job acts on, with the state a write changes and a record of every core call it made.
struct ModelGuild
{
    std::vector<uint32_t> ranks{0, 1, 2};
    std::vector<std::string> calls;
    bool hasMember = false;
    uint32_t memberRank = 0;

    bool RankExists(uint32_t rank) const
    {
        return std::find(ranks.begin(), ranks.end(), rank) != ranks.end();
    }
    void AddMember(bool hasRank, uint32_t rank)
    {
        calls.push_back(hasRank ? "add " + std::to_string(rank) : "add lowest");
        hasMember = true;
        memberRank = hasRank ? rank : ranks.front();
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

TEST_CASE("guild write: AddMember refuses a rank id the guild has no record of and writes a known one")
{
    ModelGuild guild;
    // 3 is not one of the guild's ranks (0, 1, 2): the core would have stored it and left the member without a record.
    CHECK(flh::applyGuildWrite(&guild, true, addMember(true, 3)) == GuildWriteResult::UnknownRank);
    CHECK(guild.calls.empty());
    CHECK(!guild.hasMember);

    CHECK(flh::applyGuildWrite(&guild, true, addMember(true, 200)) == GuildWriteResult::UnknownRank);
    CHECK(guild.calls.empty());

    CHECK(flh::applyGuildWrite(&guild, true, addMember(true, 2)) == GuildWriteResult::Done);
    REQUIRE(guild.calls.size() == 1);
    CHECK(guild.calls[0] == "add 2");
    CHECK(guild.hasMember);
    CHECK(guild.memberRank == 2);

    // No rank from the script: the core picks the lowest one, as the docs say.
    ModelGuild fresh;
    CHECK(flh::applyGuildWrite(&fresh, true, addMember(false, 0)) == GuildWriteResult::Done);
    REQUIRE(fresh.calls.size() == 1);
    CHECK(fresh.calls[0] == "add lowest");
    CHECK(fresh.memberRank == 0);
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
