// E5: RegisterBGEvent. The pinned core has BattlegroundScript (OnInit/OnStart/OnEnd, called from Battleground) and
// BattlegroundMapScript (the factory of that script), so the four documented BG events do not need the spec's C12:
// CREATE = OnInit, START = OnStart, END = OnEnd(winner), PRE_DESTROY = the map's OnDestroy. All of them run on the map's
// thread, in the map's state (E4); the BattleGround handle names the map, the instance and the type.
#include "EluRouter.h"
#include "GameAccess.h"
#include "Module.h"

#include "Battleground.h"
#include "BattlegroundScript.h"
#include "DB2Stores.h"
#include "Map.h"
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

void fireBG(Map* map, uint32 event, std::optional<int64_t> winner = std::nullopt)
{
    if (!map)
        return;
    fl::HandleData const bg = flh::handleForBattleground(map);
    std::vector<EventValue> args{EventValue(bg), EventValue(static_cast<int64_t>(bg.guid.hi)),
                                 EventValue(static_cast<int64_t>(bg.guid.lo))};
    if (winner)
        args.emplace_back(*winner);
    flh::MapKey const key{map->GetId(), map->GetInstanceId()};
    // E4/E5 final check: CREATE (OnInit) runs inside MapManager::CreateBattleground, under the core's exclusive map lock
    // and before the map is registered: it is queued and fires at the map's first update, in its state. Any later BG event
    // that arrives before that replay is appended behind it.
    uint64_t const now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch()).count());
    router().mapObjectEvent(map->GetWeakPtr().expired(), key, now, fl::EluKind::BattleGround, 0, event, std::move(args),
                            std::nullopt);
}

class forever_lua_bg_script : public BattlegroundScript
{
public:
    explicit forever_lua_bg_script(BattlegroundMap* map) : BattlegroundScript(map) { }

    void OnInit() override { fireBG(battlegroundMap, 3); }   // CREATE
    void OnStart() override { fireBG(battlegroundMap, 1); }  // START
    void OnEnd(Team winner) override { fireBG(battlegroundMap, 2, static_cast<int64_t>(winner)); }   // END
};

class forever_lua_bg_map : public BattlegroundMapScript
{
public:
    explicit forever_lua_bg_map(uint32 mapId)
        : BattlegroundMapScript(("forever_lua_bg_map_" + std::to_string(mapId)).c_str(), mapId)
    {
    }

    BattlegroundScript* GetBattlegroundScript(BattlegroundMap* map) const override
    {
        return new forever_lua_bg_script(map);
    }

    // The BG map's state is scheduled at its creation (H5: under the core's map lock, nothing is loaded there), built at
    // its first update on the map's thread, and closed with PRE_DESTROY.
    void OnCreate(BattlegroundMap* map) override
    {
        if (map)
            flh::scheduleMapState(map->GetId(), map->GetInstanceId());
    }
    void OnDestroy(BattlegroundMap* map) override
    {
        if (!map)
            return;
        flh::creationQueue().drop(flh::MapKey{map->GetId(), map->GetInstanceId()});
        fireBG(map, 4);   // PRE_DESTROY
        flh::closeMapState(map->GetId(), map->GetInstanceId());
    }
    void OnUpdate(BattlegroundMap* map, uint32 /*diff*/) override
    {
        if (!map)
            return;
        flh::beginMapUpdate(map->GetId(), map->GetInstanceId());   // the first update builds the state (H5)
        // ... then what the map's creation queued (BG CREATE, the game objects' SPAWN of the grid preload) runs here, on
        // the map's thread, outside the core's lock, in the map's state.
        router().replayCreation(flh::MapKey{map->GetId(), map->GetInstanceId()});
        flh::drainMapHooks(map->GetId(), map->GetInstanceId());   // E6: queued core-hook events of this map
        flh::tickMapState(map->GetId(), map->GetInstanceId());
    }
};
}  // namespace

void AddSC_forever_lua_battlegrounds()
{
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        MapEntry const* entry = sMapStore.LookupEntry(i);
        if (!entry || !entry->IsBattlegroundOrArena())
            continue;
        new forever_lua_bg_map(i);
    }
}
