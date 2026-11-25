// World.player / World.players / World.map and the Map methods.
#include "Bind.h"

#include "engine/Coords.h"

#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PhaseShift.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "Unit.h"

#include <cmath>
#include <shared_mutex>

namespace flb
{
namespace
{
constexpr int kMaxListed = 500;

bool wantsBots(lua_State* L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return false;
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_getfield(L, idx, "bots");
    bool b = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return b;
}

// ---- World ----
int worldPlayer(lua_State* L)
{
    size_t n;
    char const* s = luaL_checklstring(L, 1, &n);
    std::string key(s, n);
    if (key.empty() || key.size() > 128)
        return pushNil(L);
    Player* found = nullptr;
    ObjectGuid guid = ObjectGuid::FromString(key);   // "Player-1-0000000C-..." as ObjectGuid::ToString writes it
    if (!guid.IsEmpty() && guid.IsPlayer())
        found = ObjectAccessor::FindPlayer(guid);
    if (!found)
    {
        std::string name = key;
        if (normalizePlayerName(name))
            found = ObjectAccessor::FindPlayerByName(name);
    }
    if (!found || !found->IsInWorld())
        return pushNil(L);
    fl::pushHandle(L, flh::handleFor(found));
    return 1;
}

int worldPlayers(lua_State* L)
{
    bool bots = wantsBots(L, 1);
    lua_createtable(L, 16, 0);
    int i = 0;
    std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* p = pair.second;
        if (!p->IsInWorld() || (!bots && flh::isBot(p)))
            continue;
        if (i >= 5000)
            break;
        fl::pushHandle(L, flh::handleFor(p));
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

int worldMap(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    lua_Integer inst = luaL_optinteger(L, 2, 0);
    luaL_argcheck(L, id >= 0 && id <= 0xFFFFFFFFll, 1, "bad map id");
    luaL_argcheck(L, inst >= 0 && inst <= 0xFFFFFFFFll, 2, "bad instance id");
    if (!flh::findMap(uint32(id), uint32(inst)))
        return pushNil(L);
    fl::pushHandle(L, flh::mapHandle(uint32(id), uint32(inst)));
    return 1;
}

// ---- Map ----
Map* mapOf(lua_State* L, int idx = 1)
{
    fl::HandleData const& h = handleArg(L, idx, kMapKind);
    return flh::findMap(h.mapId, h.instanceId);
}

int mMapId(lua_State* L)
{
    return (lua_pushinteger(L, handleArg(L, 1, kMapKind).mapId), 1);
}
int mInstance(lua_State* L)
{
    return (lua_pushinteger(L, handleArg(L, 1, kMapKind).instanceId), 1);
}
int mName(lua_State* L)
{
    Map* m = mapOf(L);
    if (!m)
        return pushNil(L);
    lua_pushstring(L, m->GetMapName());
    return 1;
}

// A handle has one shared method table, so Map:name and the generic Unit/GameObject name() are the same key.
// Dispatch on the kind: a map answers its map name, every other handle the object name of Host::lookup.
int mAnyName(lua_State* L)
{
    fl::HandleData const* h = fl::toHandle(L, 1);
    if (!h)
    {
        luaL_typeerror(L, 1, "handle");
        return 0;
    }
    if (h->kind == fl::ObjKind::Map)
        return mName(L);
    fl::ObjectInfo info;
    if (!eng(L).host().lookup(*h, &info))
        return pushNil(L);
    pushString(L, info.name);
    return 1;
}
int mIsDungeon(lua_State* L)
{
    Map* m = mapOf(L);
    if (!m)
        return pushNil(L);
    lua_pushboolean(L, m->IsDungeon());
    return 1;
}

PhaseShift const& phaseOf(lua_State* L, int idx)
{
    static PhaseShift empty;
    if (lua_isnoneornil(L, idx))
        return empty;
    fl::HandleData const& h = handleArg(L, idx, kObject);
    WorldObject* o = flh::findObject(h);
    return o ? o->GetPhaseShift() : empty;
}

int mHeight(lua_State* L)
{
    // The core's terrain arrays are indexed by (int)(32 - x/533.33) without a bounds check (TerrainMgr.cpp GetGrid):
    // a NaN, an infinity or a coordinate off the map would read or write past them. Refused here, before any core
    // call (review N1).
    float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3)), z = float(luaL_checknumber(L, 4));
    if (!fl::validMapCoord(x, y, z))
        return pushNil(L);
    Map* m = mapOf(L);
    PhaseShift const& ph = phaseOf(L, 5);
    if (!m)
        return pushNil(L);
    float h = m->GetHeight(ph, x, y, z);
    if (!std::isfinite(h) || h <= INVALID_HEIGHT + 1.0f)
        return pushNil(L);
    lua_pushnumber(L, h);
    return 1;
}

int mLos(lua_State* L)
{
    // Same rule as mHeight: the LoS paths index the terrain grid and its child maps, so the six coordinates are
    // checked before any core call (review N1).
    float v[6];
    for (int i = 0; i < 6; ++i)
        v[i] = float(luaL_checknumber(L, 2 + i));
    if (!fl::validMapCoord(v[0], v[1], v[2]) || !fl::validMapCoord(v[3], v[4], v[5]))
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    Map* m = mapOf(L);
    PhaseShift const& ph = phaseOf(L, 8);
    if (!m)
        return pushNil(L);
    lua_pushboolean(L, m->isInLineOfSight(ph, v[0], v[1], v[2], v[3], v[4], v[5], LINEOFSIGHT_ALL_CHECKS,
                                          VMAP::ModelIgnoreFlags::Nothing));
    return 1;
}

int mPlayers(lua_State* L)
{
    Map* m = mapOf(L);
    if (!m)
        return pushNil(L);
    lua_createtable(L, 8, 0);
    int i = 0;
    for (MapReference const& ref : m->GetPlayers())
    {
        Player* p = ref.GetSource();
        if (!p || !p->IsInWorld())
            continue;
        if (i >= kMaxListed)
            break;
        fl::pushHandle(L, flh::handleFor(p));
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

struct AreaOpts
{
    bool hasEntry = false;
    uint32 entry = 0;
    bool alive = true;
    bool dead = false;
};

AreaOpts areaOpts(lua_State* L, int idx, bool forCreatures)
{
    AreaOpts o;
    if (lua_isnoneornil(L, idx))
        return o;
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_getfield(L, idx, "entry");
    if (!lua_isnil(L, -1))
    {
        int ok;
        lua_Integer e = lua_tointegerx(L, -1, &ok);
        luaL_argcheck(L, ok && e >= 0 && e <= 0xFFFFFFFFll, idx, "opts.entry must be a non-negative integer");
        o.hasEntry = true;
        o.entry = uint32(e);
    }
    lua_pop(L, 1);
    if (forCreatures)
    {
        lua_getfield(L, idx, "alive");
        bool hasAlive = !lua_isnil(L, -1);
        bool alive = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, idx, "dead");
        bool hasDead = !lua_isnil(L, -1);
        bool dead = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (hasAlive || hasDead)
        {
            o.alive = hasAlive && alive;
            o.dead = hasDead && dead;
        }
    }
    return o;
}

int mCreatures(lua_State* L)
{
    Map* m = mapOf(L);
    float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3)), z = float(luaL_checknumber(L, 4));
    float r = float(luaL_checknumber(L, 5));
    luaL_argcheck(L, r > 0.0f && r <= 1000.0f, 5, "radius must be in (0, 1000]");
    AreaOpts o = areaOpts(L, 6, true);
    if (!m)
        return pushNil(L);
    lua_createtable(L, 8, 0);
    int i = 0;
    float const r2 = r * r;
    auto const& store = m->GetObjectsStore().Data.template FindContainer<Creature>();
    for (auto const& pair : store)
    {
        Creature* c = pair.second;
        if (!c || !c->IsInWorld())
            continue;
        if (o.hasEntry && c->GetEntry() != o.entry)
            continue;
        bool alive = c->IsAlive();
        if ((alive && !o.alive) || (!alive && !o.dead))
            continue;
        float dx = c->GetPositionX() - x, dy = c->GetPositionY() - y, dz = c->GetPositionZ() - z;
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        if (i >= kMaxListed)
            break;
        fl::pushHandle(L, flh::handleFor(c));
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

int mGameObjects(lua_State* L)
{
    Map* m = mapOf(L);
    float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3)), z = float(luaL_checknumber(L, 4));
    float r = float(luaL_checknumber(L, 5));
    luaL_argcheck(L, r > 0.0f && r <= 1000.0f, 5, "radius must be in (0, 1000]");
    AreaOpts o = areaOpts(L, 6, false);
    if (!m)
        return pushNil(L);
    lua_createtable(L, 8, 0);
    int i = 0;
    float const r2 = r * r;
    auto const& store = m->GetObjectsStore().Data.template FindContainer<GameObject>();
    for (auto const& pair : store)
    {
        GameObject* g = pair.second;
        if (!g || !g->IsInWorld())
            continue;
        if (o.hasEntry && g->GetEntry() != o.entry)
            continue;
        float dx = g->GetPositionX() - x, dy = g->GetPositionY() - y, dz = g->GetPositionZ() - z;
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        if (i >= kMaxListed)
            break;
        fl::pushHandle(L, flh::handleFor(g));
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// Map:find(guidString): the creature, game object or player of this map with that guid (nil when none).
int mFind(lua_State* L)
{
    Map* m = mapOf(L);
    size_t n;
    char const* s = luaL_checklstring(L, 2, &n);
    if (!m)
        return pushNil(L);
    ObjectGuid g = ObjectGuid::FromString(std::string_view(s, n > 128 ? 128 : n));
    WorldObject* o = nullptr;
    if (g.IsEmpty())
        o = nullptr;
    else if (g.IsPlayer())
    {
        Player* p = ObjectAccessor::FindPlayer(g);
        o = p && p->IsInWorld() && p->GetMapId() == m->GetId() && p->GetInstanceId() == m->GetInstanceId() ? p : nullptr;
    }
    else if (g.IsAnyTypeCreature())
        o = m->GetCreature(g);
    else if (g.IsAnyTypeGameObject())
        o = m->GetGameObject(g);
    if (!o || !o->IsInWorld())
        return pushNil(L);
    fl::pushHandle(L, flh::handleFor(o));
    return 1;
}

// Map:spawnCreature(entry, x, y, z, o [, {despawnMs}]): a temporary summon (no database row). Without despawnMs it
// lives 10 minutes, so a forgotten test spawn cannot pile up; despawnMs = 0 means until despawned by hand.
int mSpawnCreature(lua_State* L)
{
    Map* m = mapOf(L);
    lua_Integer entry = luaL_checkinteger(L, 2);
    luaL_argcheck(L, entry > 0 && entry <= 0xFFFFFFFFll, 2, "bad creature entry");
    float x = float(luaL_checknumber(L, 3)), y = float(luaL_checknumber(L, 4)), z = float(luaL_checknumber(L, 5));
    float o = float(luaL_optnumber(L, 6, 0.0));
    lua_Integer despawn = 600000;
    if (!lua_isnoneornil(L, 7))
    {
        luaL_checktype(L, 7, LUA_TTABLE);
        lua_getfield(L, 7, "despawnMs");
        if (!lua_isnil(L, -1))
        {
            int ok;
            despawn = lua_tointegerx(L, -1, &ok);
            luaL_argcheck(L, ok && despawn >= 0 && despawn <= 86400000, 7, "despawnMs must be 0..86400000");
        }
        lua_pop(L, 1);
    }
    if (!m)
        return pushGone(L);
    if (!sObjectMgr->GetCreatureTemplate(uint32(entry)))
    {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "no_template");
        return 2;
    }
    Position pos(x, y, z, o);
    TempSummon* s = m->SummonCreature(uint32(entry), pos, nullptr, Milliseconds(despawn));
    if (!s)
    {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "spawn_failed");
        return 2;
    }
    fl::HandleData h = flh::handleFor(s);
    fl::QueuedEvent ev;
    ev.name = "creature.spawn";
    ev.hasEntry = true;
    ev.entry = s->GetEntry();
    ev.args.emplace_back(h);
    eng(L).emit(std::move(ev));  // inside Lua, so this is queued and runs on the next tick
    fl::pushHandle(L, h);
    return 1;
}
}  // namespace

void bindWorld(lua_State* L)
{
    lua_getglobal(L, "World");
    lua_pushcfunction(L, safe<worldPlayer>);
    lua_setfield(L, -2, "player");
    lua_pushcfunction(L, safe<worldPlayers>);
    lua_setfield(L, -2, "players");
    lua_pushcfunction(L, safe<worldMap>);
    lua_setfield(L, -2, "map");
    lua_pop(L, 1);
}

void bindMap(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"id", safe<mMapId>},         {"instance", safe<mInstance>},   {"name", safe<mAnyName>},
        {"isDungeon", safe<mIsDungeon>}, {"height", safe<mHeight>},    {"los", safe<mLos>},
        {"players", safe<mPlayers>},  {"creatures", safe<mCreatures>}, {"gameObjects", safe<mGameObjects>},
        {"spawnCreature", safe<mSpawnCreature>}, {"find", safe<mFind>}, {nullptr, nullptr}};
    addHandleMethods(L, regs);
}
}  // namespace flb
