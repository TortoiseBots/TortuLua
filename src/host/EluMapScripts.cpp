// Map events of E2: one WorldMapScript per world (continent) map, registered at module load (before
// MapManager::Initialize creates any map). Every callback runs on the thread updating that map and fires the Map family
// (keyed by the map id) and the server's own map events (17/18/21/22/23) through flh::EluRouter, inside the map's scope.
// Dungeon and raid maps are not reachable at this core (the core's InstanceMapScript is database-bound; core hook C11).
#include "EluRouter.h"
#include "GameAccess.h"
#include "Module.h"

#include "DB2Stores.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <chrono>
#include <string>
#include <vector>

namespace
{
using fl::EventValue;
using fl::HandleData;

flh::EluRouter const& router()
{
    static flh::EluRouter const r(&flh::eventEngine);
    return r;
}

EventValue mapValue(Map* map)
{
    if (!map)
        return EventValue();
    HandleData h;
    h.kind = fl::ObjKind::Map;
    h.mapId = map->GetId();
    h.instanceId = map->GetInstanceId();
    return EventValue(h);
}
EventValue playerValue(Player* p) { return p && p->IsInWorld() ? EventValue(flh::handleFor(p)) : EventValue(); }
EventValue num(int64_t v) { return EventValue(v); }

// A map callback: the Map family, the Instance family (instance maps) and one server event, all inside the map scope.
// The server event takes the docs' (event, map, ...) arguments; the map families get instance_data = nil first.
void fire(Map* map, uint32_t event, uint32_t serverEvent, std::vector<EventValue> args,
          std::vector<EventValue> serverArgs)
{
    if (!map)
        return;
    bool const instanced = map->ToInstanceMap() != nullptr;
    // Nothing bound: the four virtual calls of a map update must not build handles (every map, every tick). M1: the map
    // families are asked in the map's own state, where a map-only registration lives.
    if (!router().listeningMap(map->GetId(), map->GetInstanceId(), instanced, event, serverEvent))
        return;
    router().mapEvent(map->GetId(), map->GetInstanceId(), instanced, event, serverEvent, std::move(args),
                      std::move(serverArgs));
}

class forever_lua_world_map : public WorldMapScript
{
public:
    explicit forever_lua_world_map(uint32 mapId)
        : WorldMapScript(("forever_lua_map_" + std::to_string(mapId)).c_str(), mapId)
    {
    }

    void OnCreate(Map* map) override
    {
        if (!map)
            return;
        // H5: the core holds its exclusive _mapsLock here (MapManager::CreateMap/prewarm): nothing may load, run Lua or
        // find a map now. The map is only scheduled; its first update builds its state and fires MAP_INITIALIZE.
        flh::scheduleMapState(map->GetId(), map->GetInstanceId());
    }
    void OnDestroy(Map* map) override
    {
        if (!map)
            return;
        router().mapEvent(map->GetId(), 0, false, 0, 18, {mapValue(map)}, {mapValue(map)});
        flh::closeMapState(map->GetId(), map->GetInstanceId());   // LUA_STATE_CLOSE, then the state is freed
    }
    void OnUpdate(Map* map, uint32 diff) override
    {
        if (!map)
            return;
        // The first update after OnCreate (or after the module started): the map's state is built here, on this thread,
        // and MAP_INITIALIZE (map event 1, server event 17) fires now, once.
        if (flh::beginMapUpdate(map->GetId(), map->GetInstanceId()))
            fire(map, 1, 17, {EventValue(), mapValue(map)}, {mapValue(map)});
        router().replayCreation(flh::MapKey{map->GetId(), map->GetInstanceId()});   // events of the creation, if any
        // E6: the creature / game object events the core hooks queued since the last update run here, outside every core
        // loop (a map with its own state; the world update drains the others).
        flh::drainMapHooks(map->GetId(), map->GetInstanceId());
        fire(map, 3, 23, {EventValue(), mapValue(map), num(diff)}, {mapValue(map), num(diff)});
        flh::tickMapState(map->GetId(), map->GetInstanceId());   // E4: the state's timers and queued work, on this thread
    }
    void OnPlayerEnter(Map* map, Player* player) override
    {
        if (!map || !player)
            return;
        // Queued, not fired here: this runs inside Map::AddPlayerToMap, before the core puts a logging-in player into its
        // player table, so a handler's GetName would find no player. The drain resolves the player by GUID.
        fl::HandleData const who = flh::handleFor(player);
        uint64_t const now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count());
        router().queuePlayerEnter(false, flh::MapKey{map->GetId(), map->GetInstanceId()}, now,
                                  map->ToInstanceMap() != nullptr, who, {EventValue(), mapValue(map), EventValue(who)},
                                  {mapValue(map), EventValue(who)}, 4, 21);
    }
    void OnPlayerLeave(Map* map, Player* player) override
    {
        if (!map)
            return;
        router().mapEvent(map->GetId(), 0, false, 0, 22, {mapValue(map), playerValue(player)},
                          {mapValue(map), playerValue(player)});
    }
};

}  // namespace

void AddSC_forever_lua_maps()
{
    // One script object per map entry: created here, before MapManager::Initialize, so every map creation and update
    // reaches it. Battleground maps and arenas stay with E5 (core hook C12).
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        MapEntry const* entry = sMapStore.LookupEntry(i);
        if (!entry || entry->IsBattlegroundOrArena())
            continue;
        if (entry->IsWorldMap())
            new forever_lua_world_map(i);
        // Dungeons and raids get no script: the core's InstanceMapScript is bound to instance_template.script in the
        // database, so a module script object would never be called (and would only add an unreferenced script name per
        // dungeon). Their map events need core hook C11; RegisterInstanceEvent and RegisterMapEvent on such a map raise
        // that error at registration.
    }
}
