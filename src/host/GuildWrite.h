// The guild membership writes of the Lua API, as the one job body the world thread runs. The script's call may have been
// queued from a map thread, so the job looks the guild and the player up again: either may be gone by then (the guild
// disbanded, the player logged out) and then nothing is written.
//
// An explicit rank id comes from a script as any byte. The job never hands the core an id the core has not validated:
// the member is added at the core's own default (lowest) rank, and only then is the requested rank set through
// Guild::ChangeMemberRank, which refuses a rank the guild has no record of. A refused rank leaves the member at the
// default rank and the job reports AddedAtDefaultRank, so no member is ever stored with a rank no record covers.
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

// What the core adapter's AddMember reports.
enum class GuildAddOutcome
{
    Added,
    AddedAtDefaultRank,   // the member is in, at the lowest rank, because the requested rank does not exist
    NotAdded              // the core refused the member (nothing was written)
};

enum class GuildWriteResult
{
    Done,
    GuildGone,            // the guild is not there any more: nothing to write
    PlayerGone,           // the player is not there any more: nothing to write
    AddedAtDefaultRank    // the member was added, but at the default rank: the requested rank does not exist
};

// GuildT is the core adapter:
//   GuildAddOutcome AddMember(bool hasRank, uint32_t rank); the core adds at its lowest rank, then, when hasRank, sets
//                                                           rank through Guild::ChangeMemberRank (which validates it)
//   void DeleteMember(bool isDisbanding);
//   void ChangeRank(uint32_t rank);                         the core validates the rank here itself
//   void Disband();                                         frees the guild: the caller must not touch it afterwards
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
        if (guild->AddMember(write.hasRank, write.rank) == GuildAddOutcome::AddedAtDefaultRank)
            return GuildWriteResult::AddedAtDefaultRank;
    }
    else if (write.kind == GuildWriteKind::DeleteMember)
        guild->DeleteMember(write.isDisbanding);
    else
        guild->ChangeRank(write.rank);
    return GuildWriteResult::Done;
}
}  // namespace flh
