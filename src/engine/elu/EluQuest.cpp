// Eluna Quest methods (docs: Quest/index.html). The handle is {kind Quest, quest id}; the quest template is looked up
// again on every call, so a handle of a quest that is not in the world database raises "Quest no longer exists".
//
// Quest:GetTitle is the one method of the spec's E2 list that the published class page does not document; the core's
// localized quest title answers it (docs/ELUNA-CLEANROOM.md).
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

namespace flu
{
namespace
{
// Quest:GetFlags()
int qGetFlags(lua_State* L) { return readOp(L, kQuest, "quest.getFlags", "GetFlags"); }
// Quest:GetId()
int qGetId(lua_State* L) { return readOp(L, kQuest, "quest.getId", "GetId"); }
// Quest:GetLevel()
int qGetLevel(lua_State* L) { return readOp(L, kQuest, "quest.getLevel", "GetLevel"); }
// Quest:GetMaxLevel()
int qGetMaxLevel(lua_State* L) { return readOp(L, kQuest, "quest.getMaxLevel", "GetMaxLevel"); }
// Quest:GetMinLevel()
int qGetMinLevel(lua_State* L) { return readOp(L, kQuest, "quest.getMinLevel", "GetMinLevel"); }
// Quest:GetNextQuestId()
int qGetNextQuestId(lua_State* L) { return readOp(L, kQuest, "quest.getNextQuestId", "GetNextQuestId"); }
// Quest:GetNextQuestInChain()
int qGetNextQuestInChain(lua_State* L) { return readOp(L, kQuest, "quest.getNextQuestInChain", "GetNextQuestInChain"); }
// Quest:GetPrevQuestId()
int qGetPrevQuestId(lua_State* L) { return readOp(L, kQuest, "quest.getPrevQuestId", "GetPrevQuestId"); }
// Quest:GetTitle()
int qGetTitle(lua_State* L) { return readOp(L, kQuest, "quest.getTitle", "GetTitle"); }
// Quest:GetType()
int qGetType(lua_State* L) { return readOp(L, kQuest, "quest.getType", "GetType"); }
// Quest:HasFlag(flag)
int qHasFlag(lua_State* L)
{
    return readOp(L, kQuest, "quest.hasFlag", "HasFlag",
                  {EventValue(static_cast<int64_t>(argUInt(L, 2, "flag")))});
}
// Quest:IsDaily()
int qIsDaily(lua_State* L) { return readOp(L, kQuest, "quest.isDaily", "IsDaily"); }
// Quest:IsRepeatable()
int qIsRepeatable(lua_State* L) { return readOp(L, kQuest, "quest.isRepeatable", "IsRepeatable"); }
}  // namespace

void registerQuestMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetFlags", fl::safe<qGetFlags>},
        {"GetId", fl::safe<qGetId>},
        {"GetLevel", fl::safe<qGetLevel>},
        {"GetMaxLevel", fl::safe<qGetMaxLevel>},
        {"GetMinLevel", fl::safe<qGetMinLevel>},
        {"GetNextQuestId", fl::safe<qGetNextQuestId>},
        {"GetNextQuestInChain", fl::safe<qGetNextQuestInChain>},
        {"GetPrevQuestId", fl::safe<qGetPrevQuestId>},
        {"GetTitle", fl::safe<qGetTitle>},
        {"GetType", fl::safe<qGetType>},
        {"HasFlag", fl::safe<qHasFlag>},
        {"IsDaily", fl::safe<qIsDaily>},
        {"IsRepeatable", fl::safe<qIsRepeatable>},
        {nullptr, nullptr}};
    addMethods(L, "Quest", regs);
}
}  // namespace flu
