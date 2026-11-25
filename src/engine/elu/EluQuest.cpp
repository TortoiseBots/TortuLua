// Eluna Quest methods of E2 (docs: Quest/index.html). The handle is {kind Quest, quest id}; the quest template is looked
// up again on every call, so a handle of a quest that is not in the world database raises "Quest no longer exists".
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
// Quest:GetId()
int qGetId(lua_State* L) { return readOp(L, kQuest, "quest.getId", "GetId"); }
// Quest:GetLevel()
int qGetLevel(lua_State* L) { return readOp(L, kQuest, "quest.getLevel", "GetLevel"); }
// Quest:GetTitle()
int qGetTitle(lua_State* L) { return readOp(L, kQuest, "quest.getTitle", "GetTitle"); }
// Quest:IsDaily()
int qIsDaily(lua_State* L) { return readOp(L, kQuest, "quest.isDaily", "IsDaily"); }
}  // namespace

void registerQuestMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetId", fl::safe<qGetId>}, {"GetLevel", fl::safe<qGetLevel>},
        {"GetTitle", fl::safe<qGetTitle>}, {"IsDaily", fl::safe<qIsDaily>},
        {nullptr, nullptr}};
    addMethods(L, "Quest", regs);
}
}  // namespace flu
