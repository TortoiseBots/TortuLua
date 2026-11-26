// The decisions behind the riskier Player writes of the Lua API, kept game-free so they are unit-tested without the
// core (like GuildWrite.h); the core adapter in EluWorld.cpp supplies the facts and makes the calls.
//
//  * SetQuestStatus writes a status only for a quest that already holds a slot in the quest log. The core's status
//    entry defaults to the "no slot" value, and the next character save reads the quest log array at that slot: an
//    entry made without a slot is an out-of-range read and a corrupt row.
//  * BindToInstance runs the core's lock creation, which dereferences the map's instance script and its instance lock:
//    it needs an instance map that has both.
//  * GetChatTag answers the chat flag bits (AFK 1, DND 2, GM 4) the way the core sends them.
//  * SetGuildRank refuses rank 0 (Guild Master): one call would leave two masters or none.
//  * AdvanceSkill/AdvanceAllSkills pass only the room left to the skill maximum: the core adds the step to a 16-bit
//    value before the clamp, so a larger step wraps and can lower the skill.
#pragma once

#include <cstdint>

namespace flh
{
constexpr uint32_t kChatFlagAfk = 0x1;
constexpr uint32_t kChatFlagDnd = 0x2;
constexpr uint32_t kChatFlagGm = 0x4;

inline bool questStatusWriteAllowed(bool questInLog, int64_t status, int64_t statusCount)
{
    return questInLog && status >= 0 && status < statusCount;
}

inline bool bindToInstanceAllowed(bool isInstanceMap, bool hasInstanceData, bool hasInstanceLock)
{
    return isInstanceMap && hasInstanceData && hasInstanceLock;
}

inline uint32_t chatTagBits(bool afk, bool dnd, bool gm)
{
    return (afk ? kChatFlagAfk : 0u) | (dnd ? kChatFlagDnd : 0u) | (gm ? kChatFlagGm : 0u);
}

inline bool guildRankWriteAllowed(uint32_t rank) { return rank != 0; }

// The step handed to Player::UpdateSkillPro: 0..max-value, never more. The core computes `uint16 new_value = value +
// step` and only then clamps to the maximum, so a larger step wraps before the clamp and can lower the skill.
inline uint32_t skillAdvanceStep(uint32_t value, uint32_t maxValue, uint32_t step)
{
    if (step == 0 || value >= maxValue)
        return 0;
    uint32_t const room = maxValue - value;
    return step < room ? step : room;
}
}  // namespace flh
