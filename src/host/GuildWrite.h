// The guild membership writes of the Lua API, as the one job body the world thread runs. The script's call may have been
// queued from a map thread, so the job looks the guild and the player up again: either may be gone by then (the guild
// disbanded, the player logged out) and then nothing is written.
//
// An explicit rank id comes from a script as any byte; Guild::AddMember stores whatever id it is given and only picks a
// default when none was passed, while Guild::ChangeMemberRank asks Guild::GetRankInfo first. The job refuses a rank the
// guild has no record of, so it never makes a member that no rank covers.
//
// Header-only and templated like CreatureLookup.h, so the rule is unit-tested without the core; the core adapter
// supplies the calls (Guild, the player's guid and the character transaction).
#pragma once

#include <cstdint>

namespace flh
{
enum class GuildWriteKind
{
    AddMember,
    DeleteMember,
    ChangeRank,
    Disband
};

struct GuildWrite
{
    GuildWriteKind kind = GuildWriteKind::Disband;
    bool hasRank = false;        // AddMember: false leaves the choice to the core (its lowest rank)
    uint32_t rank = 0;           // AddMember (explicit) / ChangeRank
    bool isDisbanding = false;   // DeleteMember
};

enum class GuildWriteResult
{
    Done,
    GuildGone,     // the guild is not there any more: nothing to write
    PlayerGone,    // the player is not there any more: nothing to write
    UnknownRank    // the guild has no rank with that id: nothing was written
};

// GuildT is the core adapter:
//   bool RankExists(uint32_t rank);              Guild::GetRankInfo(GuildRankId(rank)) != nullptr
//   void AddMember(bool hasRank, uint32_t rank); Optional<GuildRankId> only when hasRank
//   void DeleteMember(bool isDisbanding);
//   void ChangeRank(uint32_t rank);              the core validates the rank here itself
//   void Disband();                              frees the guild: the caller must not touch it afterwards
template <class GuildT>
GuildWriteResult applyGuildWrite(GuildT* guild, bool playerPresent, GuildWrite const& write)
{
    if (!guild)
        return GuildWriteResult::GuildGone;
    if (write.kind == GuildWriteKind::Disband)
    {
        guild->Disband();
        return GuildWriteResult::Done;
    }
    if (!playerPresent)
        return GuildWriteResult::PlayerGone;
    if (write.kind == GuildWriteKind::AddMember)
    {
        if (write.hasRank && !guild->RankExists(write.rank))
            return GuildWriteResult::UnknownRank;
        guild->AddMember(write.hasRank, write.rank);
    }
    else if (write.kind == GuildWriteKind::DeleteMember)
        guild->DeleteMember(write.isDisbanding);
    else
        guild->ChangeRank(write.rank);
    return GuildWriteResult::Done;
}
}  // namespace flh
