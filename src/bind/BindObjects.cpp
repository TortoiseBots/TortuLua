// The methods of Unit (player and creature handles), Player, Creature and GameObject handles.
// Every method looks the object up again (flh::find*); a gone object reads as nil and acts as `false, "gone"`.
#include "Bind.h"

#include "engine/Text.h"

#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "WorldSession.h"

namespace flb
{
namespace
{
Unit* unitOf(lua_State* L, int idx = 1) { return flh::findUnit(handleArg(L, idx, kUnit)); }
Player* playerOf(lua_State* L) { return flh::findPlayer(handleArg(L, 1, kPlayer)); }
Creature* creatureOf(lua_State* L) { return flh::findCreature(handleArg(L, 1, kCreature)); }
GameObject* goOf(lua_State* L) { return flh::findGameObject(handleArg(L, 1, kGameObject)); }

void pushHandleOrNil(lua_State* L, WorldObject const* o)
{
    if (o && o->IsInWorld())
        fl::pushHandle(L, flh::handleFor(o));
    else
        lua_pushnil(L);
}

// ---- Unit ----
int uMap(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    fl::pushHandle(L, flh::mapHandle(u->GetMapId(), u->GetInstanceId()));
    return 1;
}
int uLevel(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    lua_pushinteger(L, u->GetLevel());
    return 1;
}
int uHealth(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    lua_pushinteger(L, static_cast<lua_Integer>(u->GetHealth()));
    return 1;
}
int uMaxHealth(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    lua_pushinteger(L, static_cast<lua_Integer>(u->GetMaxHealth()));
    return 1;
}
int uHealthPct(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    lua_pushnumber(L, u->GetHealthPct());
    return 1;
}
int uInCombat(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    lua_pushboolean(L, u->IsInCombat());
    return 1;
}
int uVictim(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    pushHandleOrNil(L, u->GetVictim());
    return 1;
}
int uTarget(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    ObjectGuid g = u->GetTarget();
    pushHandleOrNil(L, g.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*u, g));
    return 1;
}
int uFaction(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    lua_pushinteger(L, u->GetFaction());
    return 1;
}
int uIsHostileTo(lua_State* L)
{
    Unit* u = unitOf(L, 1);
    Unit* other = unitOf(L, 2);
    if (!u || !other)
        return pushNil(L);
    lua_pushboolean(L, u->IsHostileTo(other));
    return 1;
}
int uDistanceTo(lua_State* L)
{
    Unit* u = unitOf(L);
    if (lua_isnumber(L, 2))
    {
        float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3)), z = float(luaL_checknumber(L, 4));
        if (!u)
            return pushNil(L);
        lua_pushnumber(L, u->GetDistance(x, y, z));
        return 1;
    }
    fl::HandleData const& oh = handleArg(L, 2, kObject);
    WorldObject* other = flh::findObject(oh);
    if (!u || !other)
        return pushNil(L);
    lua_pushnumber(L, u->GetDistance(other));
    return 1;
}
int uHasAura(lua_State* L)
{
    Unit* u = unitOf(L);
    lua_Integer id = luaL_checkinteger(L, 2);
    luaL_argcheck(L, id > 0 && id <= 0xFFFFFFFFll, 2, "bad spell id");
    if (!u)
        return pushNil(L);
    lua_pushboolean(L, u->HasAura(uint32(id)));
    return 1;
}
int uAura(lua_State* L)
{
    Unit* u = unitOf(L);
    lua_Integer id = luaL_checkinteger(L, 2);
    luaL_argcheck(L, id > 0 && id <= 0xFFFFFFFFll, 2, "bad spell id");
    if (!u)
        return pushNil(L);
    Aura* a = u->GetAura(uint32(id));
    if (!a)
        return pushNil(L);
    lua_createtable(L, 0, 3);
    pushString(L, a->GetCasterGUID().ToString());
    lua_setfield(L, -2, "caster");
    lua_pushinteger(L, a->GetStackAmount());
    lua_setfield(L, -2, "stacks");
    lua_pushinteger(L, a->GetDuration());   // -1 = permanent
    lua_setfield(L, -2, "remainingMs");
    return 1;
}
int uCasting(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    Spell* s = u->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!s || !s->GetSpellInfo())
        return pushNil(L);
    lua_pushinteger(L, s->GetSpellInfo()->Id);
    ObjectGuid t = s->m_targets.GetUnitTargetGUID();
    if (t.IsEmpty())
        lua_pushnil(L);
    else
        pushString(L, t.ToString());
    return 2;
}
int uChannel(lua_State* L)
{
    Unit* u = unitOf(L);
    if (!u)
        return pushNil(L);
    Spell* s = u->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!s || !s->GetSpellInfo())
        return pushNil(L);
    lua_pushinteger(L, s->GetSpellInfo()->Id);
    return 1;
}

// ---- Player ----
int pIsBot(lua_State* L)
{
    Player* p = playerOf(L);
    if (!p)
        return pushNil(L);
    lua_pushboolean(L, flh::isBot(p));
    return 1;
}
int pSecurity(lua_State* L)
{
    Player* p = playerOf(L);
    if (!p || !p->GetSession())
        return pushNil(L);
    lua_pushinteger(L, int(p->GetSession()->GetSecurity()));
    return 1;
}
int pGroup(lua_State* L)
{
    Player* p = playerOf(L);
    if (!p)
        return pushNil(L);
    Group* g = p->GetGroup();
    if (!g)
        return pushNil(L);
    lua_createtable(L, 5, 0);
    int i = 0;
    for (GroupReference const& ref : g->GetMembers())
    {
        Player* m = ref.GetSource();
        if (!m || !m->IsInWorld())
            continue;
        fl::pushHandle(L, flh::handleFor(m));
        lua_rawseti(L, -2, ++i);
    }
    pushString(L, g->GetLeaderGUID().ToString());
    return 2;
}
int pMessage(lua_State* L)
{
    Player* p = playerOf(L);
    size_t n;
    char const* s = luaL_checklstring(L, 2, &n);
    if (!p || !p->GetSession())
        return pushGone(L);
    ChatHandler(p->GetSession()).SendSysMessage(std::string_view(s, n > 4000 ? 4000 : n));
    lua_pushboolean(L, 1);
    return 1;
}
int pTeleport(lua_State* L)
{
    Player* p = playerOf(L);
    lua_Integer map = luaL_checkinteger(L, 2);
    float x = float(luaL_checknumber(L, 3)), y = float(luaL_checknumber(L, 4)), z = float(luaL_checknumber(L, 5));
    float o = float(luaL_optnumber(L, 6, 0.0));
    luaL_argcheck(L, map >= 0 && map <= 0xFFFFFFFFll, 2, "bad map id");
    if (!p)
        return pushGone(L);
    if (uint32(map) == p->GetMapId())
    {
        p->NearTeleportTo(x, y, z, o);
        lua_pushboolean(L, 1);
    }
    else
        lua_pushboolean(L, p->TeleportTo(uint32(map), x, y, z, o));
    return 1;
}

// A chat handler that answers into a list instead of the player's client (rights: the player's own).
class CaptureChat final : public ChatHandler
{
public:
    CaptureChat(WorldSession* s, std::vector<std::string>& out) : ChatHandler(s), _out(out) { }
    using ChatHandler::SendSysMessage;
    void SendSysMessage(std::string_view str, bool /*escape*/ = false) override
    {
        if (_out.size() < 1000)
            fl::splitLines(str, _out);
    }

private:
    std::vector<std::string>& _out;
};

int pRun(lua_State* L)
{
    Player* p = playerOf(L);
    size_t n;
    char const* s = luaL_checklstring(L, 2, &n);
    std::string line(s, n);
    if (!p || !p->GetSession())
        return pushGone(L);
    fl::Engine& e = eng(L);
    std::vector<std::string> out;
    std::string refusal = fl::bridgeRefusal(line, e.bridgeDeny(), e.forwardedRoots(), e.bridgeDepth());
    bool ok = false;
    if (!refusal.empty())
        out.push_back("refused: " + refusal);
    else
    {
        struct Scope
        {
            fl::Engine& e;
            explicit Scope(fl::Engine& en) : e(en) { e.enterBridge(); }
            ~Scope() { e.leaveBridge(); }
        } scope(e);
        CaptureChat handler(p->GetSession(), out);
        std::string_view text = line;
        if (!text.empty() && (text[0] == '.' || text[0] == '!'))
            text.remove_prefix(1);
        ok = handler._ParseCommands(text);
    }
    lua_pushboolean(L, ok);
    lua_createtable(L, int(out.size()), 0);
    for (size_t i = 0; i < out.size() && i < 200; ++i)
    {
        pushString(L, out[i]);
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    return 2;
}

// ---- Creature ----
int cHome(lua_State* L)
{
    Creature* c = creatureOf(L);
    if (!c)
        return pushNil(L);
    Position const& h = c->GetHomePosition();
    lua_pushnumber(L, h.GetPositionX());
    lua_pushnumber(L, h.GetPositionY());
    lua_pushnumber(L, h.GetPositionZ());
    lua_pushnumber(L, h.GetOrientation());
    return 4;
}
int cRank(lua_State* L)
{
    Creature* c = creatureOf(L);
    if (!c)
        return pushNil(L);
    lua_pushinteger(L, int(c->GetCreatureClassification()));
    return 1;
}
int cIsBoss(lua_State* L)
{
    Creature* c = creatureOf(L);
    if (!c)
        return pushNil(L);
    lua_pushboolean(L, c->isWorldBoss() || c->IsDungeonBoss());
    return 1;
}
int cIsPet(lua_State* L)
{
    Creature* c = creatureOf(L);
    if (!c)
        return pushNil(L);
    lua_pushboolean(L, c->IsPet());
    return 1;
}
int cOwner(lua_State* L)
{
    Creature* c = creatureOf(L);
    if (!c)
        return pushNil(L);
    ObjectGuid g = c->GetOwnerGUID();
    pushHandleOrNil(L, g.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*c, g));
    return 1;
}
int cKill(lua_State* L)
{
    Creature* c = creatureOf(L);
    if (!c)
        return pushGone(L);
    c->KillSelf();
    lua_pushboolean(L, 1);
    return 1;
}

// ---- GameObject ----
int gState(lua_State* L)
{
    GameObject* g = goOf(L);
    if (!g)
        return pushNil(L);
    lua_pushinteger(L, int(g->GetGoState()));
    return 1;
}
int gLootState(lua_State* L)
{
    GameObject* g = goOf(L);
    if (!g)
        return pushNil(L);
    lua_pushinteger(L, int(g->getLootState()));
    return 1;
}
int gSpawned(lua_State* L)
{
    GameObject* g = goOf(L);
    if (!g)
        return pushNil(L);
    lua_pushboolean(L, g->isSpawned());
    return 1;
}
// spawnId() of a creature or a game object (0 for a summon).
int oSpawnId(lua_State* L)
{
    fl::HandleData const& h = handleArg(L, 1, kCreature | kGameObject);
    if (h.kind == fl::ObjKind::Creature)
    {
        Creature* c = flh::findCreature(h);
        if (!c)
            return pushNil(L);
        lua_pushinteger(L, static_cast<lua_Integer>(c->GetSpawnId()));
    }
    else
    {
        GameObject* g = flh::findGameObject(h);
        if (!g)
            return pushNil(L);
        lua_pushinteger(L, static_cast<lua_Integer>(g->GetSpawnId()));
    }
    return 1;
}

// despawn([ms]): a creature despawns after ms (default now); a game object is deleted.
int oDespawn(lua_State* L)
{
    fl::HandleData const& h = handleArg(L, 1, kCreature | kGameObject);
    lua_Integer ms = luaL_optinteger(L, 2, 0);
    luaL_argcheck(L, ms >= 0 && ms <= 86400000, 2, "ms must be 0..86400000");
    if (h.kind == fl::ObjKind::Creature)
    {
        Creature* c = flh::findCreature(h);
        if (!c)
            return pushGone(L);
        c->DespawnOrUnsummon(Milliseconds(ms));
    }
    else
    {
        GameObject* g = flh::findGameObject(h);
        if (!g)
            return pushGone(L);
        g->Delete();
    }
    lua_pushboolean(L, 1);
    return 1;
}
}  // namespace

void bindObjects(lua_State* L)
{
    static luaL_Reg const regs[] = {
        // Unit
        {"map", safe<uMap>}, {"level", safe<uLevel>}, {"health", safe<uHealth>}, {"maxHealth", safe<uMaxHealth>},
        {"healthPct", safe<uHealthPct>}, {"inCombat", safe<uInCombat>}, {"victim", safe<uVictim>},
        {"target", safe<uTarget>}, {"faction", safe<uFaction>}, {"isHostileTo", safe<uIsHostileTo>},
        {"distanceTo", safe<uDistanceTo>}, {"hasAura", safe<uHasAura>}, {"aura", safe<uAura>},
        {"casting", safe<uCasting>}, {"channel", safe<uChannel>},
        // Player
        {"isBot", safe<pIsBot>}, {"security", safe<pSecurity>}, {"group", safe<pGroup>}, {"message", safe<pMessage>},
        {"teleport", safe<pTeleport>}, {"run", safe<pRun>},
        // Creature
        {"home", safe<cHome>}, {"rank", safe<cRank>}, {"isBoss", safe<cIsBoss>}, {"isPet", safe<cIsPet>},
        {"owner", safe<cOwner>}, {"kill", safe<cKill>},
        // Creature and GameObject
        {"spawnId", safe<oSpawnId>},
        {"state", safe<gState>}, {"lootState", safe<gLootState>}, {"spawned", safe<gSpawned>},
        {"despawn", safe<oDespawn>}, {nullptr, nullptr}};
    addHandleMethods(L, regs);
}
}  // namespace flb
