#include "GameAccess.h"

#include "CreatureLookup.h"

#include "Battleground.h"
#include "Corpse.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "Map.h"
#include "Pet.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace flh
{
Map* findMap(uint32 mapId, uint32 instanceId)
{
    return sMapMgr->FindMap(mapId, instanceId);
}

Player* findPlayer(fl::HandleData const& h)
{
    if (h.kind != fl::ObjKind::Player)
        return nullptr;
    Player* p = ObjectAccessor::FindPlayer(toGuid(h.guid));
    return p && p->IsInWorld() ? p : nullptr;
}

Creature* findCreature(fl::HandleData const& h)
{
    if (h.kind != fl::ObjKind::Creature)
        return nullptr;
    Map* m = findMap(h.mapId, h.instanceId);
    if (!m)
        return nullptr;
    Creature* c = flh::findCreatureOrPet<Creature>(*m, toGuid(h.guid));   // a pet lives in the Pet store
    return c && c->IsInWorld() ? c : nullptr;
}

GameObject* findGameObject(fl::HandleData const& h)
{
    if (h.kind != fl::ObjKind::GameObject)
        return nullptr;
    Map* m = findMap(h.mapId, h.instanceId);
    if (!m)
        return nullptr;
    GameObject* g = m->GetGameObject(toGuid(h.guid));
    return g && g->IsInWorld() ? g : nullptr;
}

Corpse* findCorpse(fl::HandleData const& h)
{
    if (h.kind != fl::ObjKind::Corpse)
        return nullptr;
    Map* m = findMap(h.mapId, h.instanceId);
    if (!m)
        return nullptr;
    Corpse* c = m->GetCorpse(toGuid(h.guid));
    return c && c->IsInWorld() ? c : nullptr;
}

Unit* findUnit(fl::HandleData const& h)
{
    if (h.kind == fl::ObjKind::Player)
        return findPlayer(h);
    if (h.kind == fl::ObjKind::Creature)
        return findCreature(h);
    return nullptr;
}

WorldObject* findObject(fl::HandleData const& h)
{
    switch (h.kind)
    {
        case fl::ObjKind::Player: return findPlayer(h);
        case fl::ObjKind::Creature: return findCreature(h);
        case fl::ObjKind::GameObject: return findGameObject(h);
        case fl::ObjKind::Corpse: return findCorpse(h);
        case fl::ObjKind::Map: return nullptr;
    }
    return nullptr;
}

fl::HandleData handleFor(Player const* p)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Player;
    h.guid = fromGuid(p->GetGUID());
    h.mapId = p->GetMapId();
    h.instanceId = p->GetInstanceId();
    return h;
}

fl::HandleData handleFor(WorldObject const* o)
{
    fl::HandleData h;
    h.guid = fromGuid(o->GetGUID());
    h.mapId = o->GetMapId();
    h.instanceId = o->GetInstanceId();
    if (o->IsPlayer())
        h.kind = fl::ObjKind::Player;
    else if (o->IsCreature())
        h.kind = fl::ObjKind::Creature;
    else
        h.kind = fl::ObjKind::GameObject;
    return h;
}

fl::HandleData mapHandle(uint32 mapId, uint32 instanceId)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Map;
    h.mapId = mapId;
    h.instanceId = instanceId;
    return h;
}

fl::HandleData handleForCorpse(Corpse const* corpse)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Corpse;
    h.guid = fromGuid(corpse->GetGUID());
    h.mapId = corpse->GetMapId();
    h.instanceId = corpse->GetInstanceId();
    return h;
}

fl::HandleData handleForBattleground(Map* map)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::BattleGround;
    h.mapId = map->GetId();
    BattlegroundMap* bm = map->ToBattlegroundMap();
    Battleground* bg = bm ? bm->GetBG() : nullptr;
    h.guid.lo = bg ? bg->GetInstanceID() : map->GetInstanceId();
    h.guid.hi = bg ? static_cast<uint64_t>(bg->GetTypeID()) : 0;
    h.instanceId = static_cast<uint32_t>(h.guid.lo);
    return h;
}

fl::HandleData handleForItem(Item const* item)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Item;
    h.guid = fromGuid(item->GetGUID());
    return h;
}

namespace
{
struct ItemOwner
{
    ObjectGuid owner;
    uint32 entry = 0;
};
std::optional<ObjectGuid> itemOwnerOf(ObjectGuid const& guid);
std::mutex g_itemMutex;
std::map<std::pair<uint64_t, uint64_t>, ItemOwner> g_items;
std::deque<std::pair<uint64_t, uint64_t>> g_itemOrder;
constexpr size_t kMaxRememberedItems = 4096;
}  // namespace

void rememberItem(Item* item)
{
    if (!item)
        return;
    std::lock_guard<std::mutex> lock(g_itemMutex);
    auto key = std::make_pair(item->GetGUID().GetRawValue(1), item->GetGUID().GetRawValue(0));
    if (g_items.emplace(key, ItemOwner{item->GetOwnerGUID(), item->GetEntry()}).second)
        g_itemOrder.push_back(key);
    while (g_itemOrder.size() > kMaxRememberedItems)
    {
        g_items.erase(g_itemOrder.front());
        g_itemOrder.pop_front();
    }
}

void forgetItem(ObjectGuid const& guid)
{
    std::lock_guard<std::mutex> lock(g_itemMutex);
    auto key = std::make_pair(guid.GetRawValue(1), guid.GetRawValue(0));
    g_items.erase(key);
}

Item* findItem(fl::HandleData const& h)
{
    if (h.kind != fl::ObjKind::Item)
        return nullptr;
    ObjectGuid guid = toGuid(h.guid);
    ObjectGuid owner;
    {
        std::lock_guard<std::mutex> lock(g_itemMutex);
        auto it = g_items.find(std::make_pair(guid.GetRawValue(1), guid.GetRawValue(0)));
        if (it == g_items.end())
            return nullptr;
        owner = it->second.owner;
    }
    Player* p = ObjectAccessor::FindPlayer(owner);
    if (!p)
        return nullptr;
    Item* item = p->GetItemByGuid(guid);
    return item && item->IsInWorld() ? item : nullptr;
}

namespace
{
AccessRules g_rules;
PlayerMaps g_playerMaps;
DeferQueue g_deferred;
std::optional<ObjectGuid> itemOwnerOf(ObjectGuid const& guid)
{
    std::lock_guard<std::mutex> lock(g_itemMutex);
    auto it = g_items.find(std::make_pair(guid.GetRawValue(1), guid.GetRawValue(0)));
    if (it == g_items.end())
        return std::nullopt;
    return it->second.owner;
}
}  // namespace

AccessRules& rules() { return g_rules; }
PlayerMaps& playerMaps() { return g_playerMaps; }
DeferQueue& deferred() { return g_deferred; }
void setMapsIdle(bool idle) { g_rules.setMapsIdle(idle); }
void setWorldThread() { g_rules.setWorldThread(); }
bool worldThread() { return g_rules.onWorldThread(); }

bool currentMapOf(fl::HandleData const& h, uint32_t& mapId, uint32_t& instanceId)
{
    std::optional<MapKey> key;
    switch (h.kind)
    {
        case fl::ObjKind::Player: key = g_playerMaps.get(h.guid); break;
        case fl::ObjKind::Item:
            if (auto owner = itemOwnerOf(toGuid(h.guid)))
                key = g_playerMaps.get(fromGuid(*owner));
            break;
        default: key = MapKey{h.mapId, h.instanceId}; break;   // creatures, game objects and maps never change map
    }
    if (!key)
        return false;
    mapId = key->map;
    instanceId = key->instance;
    return true;
}

fl::Access accessFor(fl::HandleData const& h)
{
    switch (h.kind)
    {
        case fl::ObjKind::Player:
            return g_rules.check(MapKey{h.mapId, h.instanceId}, g_playerMaps.get(h.guid));
        case fl::ObjKind::Item:
        {
            std::optional<MapKey> current;
            if (auto owner = itemOwnerOf(toGuid(h.guid)))
                current = g_playerMaps.get(fromGuid(*owner));
            return g_rules.check(current, current);   // an item handle carries no map: the owner's map is the one
        }
        case fl::ObjKind::Creature:
        case fl::ObjKind::GameObject:
        case fl::ObjKind::Corpse:
        case fl::ObjKind::Map:
        case fl::ObjKind::BattleGround:
        {
            MapKey const key{h.mapId, h.instanceId};
            return g_rules.check(key, key);
        }
        default: return fl::Access::Ok;   // group, guild, quest ... carry no map object
    }
}

void notePlayerMap(Player const* p)
{
    if (p)
        g_playerMaps.set(fromGuid(p->GetGUID()), MapKey{p->GetMapId(), p->GetInstanceId()});
}

void forgetPlayerMap(Player const* p)
{
    if (p)
        g_playerMaps.forget(fromGuid(p->GetGUID()));
}

void refreshPlayerMaps()
{
    std::vector<std::pair<fl::Guid, MapKey>> all;
    for (Player* p : onlinePlayers())
        all.emplace_back(fromGuid(p->GetGUID()), MapKey{p->GetMapId(), p->GetInstanceId()});
    g_playerMaps.replaceAll(all);
}

std::vector<Player*> onlinePlayers()
{
    std::vector<Player*> out;
    for (auto const& pair : sWorld->GetAllSessions())
        if (Player* p = pair.second->GetPlayer())
            if (p->IsInWorld())
                out.push_back(p);
    return out;
}

bool isBot(Player const* p)
{
    return p && p->GetSession() && p->GetSession()->IsBot();
}
}  // namespace flh
