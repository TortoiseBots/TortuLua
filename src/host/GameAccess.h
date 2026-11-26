// ForeverLua host side: turns the engine's opaque handle data into live core objects, and back.
// Every lookup goes through ObjectAccessor / MapManager again; nothing here keeps a pointer.
#pragma once

#include "AccessRules.h"
#include "DeferQueue.h"
#include "engine/Host.h"

#include "ObjectGuid.h"

#include <string>
#include <vector>

class AuraEffect;
class Corpse;
class Creature;
class GameObject;
class Item;
class Map;
class Player;
class ProcEventInfo;
class Spell;
class Unit;
class WorldObject;

namespace flh
{
// A Spell, AuraEffect or ProcEventInfo object of the running Lua call: transient handles that resolve only inside that call.
fl::HandleData handleForSpell(Spell* spell);
fl::HandleData handleForAuraEffect(AuraEffect* eff);
fl::HandleData handleForProcInfo(ProcEventInfo* proc);

inline ObjectGuid toGuid(fl::Guid const& g)
{
    ObjectGuid o;
    o.SetRawValue(g.hi, g.lo);
    return o;
}
inline fl::Guid fromGuid(ObjectGuid const& o) { return fl::Guid{o.GetRawValue(1), o.GetRawValue(0)}; }

// Null when the map (or that instance) is not loaded.
Map* findMap(uint32 mapId, uint32 instanceId);
// Each returns null when the object is gone, is not in the world, or the handle is of another kind.
Player* findPlayer(fl::HandleData const& h);
Creature* findCreature(fl::HandleData const& h);
GameObject* findGameObject(fl::HandleData const& h);
Corpse* findCorpse(fl::HandleData const& h);   // E5: Player:GetCorpse
Unit* findUnit(fl::HandleData const& h);
WorldObject* findObject(fl::HandleData const& h);

fl::HandleData handleFor(Player const* p);
fl::HandleData handleFor(WorldObject const* o);  // player, creature (incl. summons) or game object
fl::HandleData mapHandle(uint32 mapId, uint32 instanceId);
fl::HandleData handleForItem(Item const* item);
fl::HandleData handleForCorpse(Corpse const* corpse);
fl::HandleData handleForBattleground(Map* map);   // E5: the BattleGround handle of a battleground map

// Items are not globally addressable at this core: the module remembers the owner of an item it has handed to Lua
// (an item event) so a later method call can resolve it. Bounded; REMOVE/expiry forget the item.
void rememberItem(Item* item);
void forgetItem(ObjectGuid const& guid);
Item* findItem(fl::HandleData const& h);

// Cross-map safety of Eluna mode (the rules are AccessRules.h, game-free and unit-tested): a handle resolves on the world
// thread with the maps idle, or on the thread updating the object's CURRENT map (the MapScope of a map-thread hook).
AccessRules& rules();
PlayerMaps& playerMaps();
DeferQueue& deferred();
void setMapsIdle(bool idle);
// The thread that called startEngine (the world thread); set there.
void setWorldThread();
bool worldThread();
// The access answer for a handle, asked BEFORE any lookup: the game-side facts (where the object is now) go through
// AccessRules::check.
fl::Access accessFor(fl::HandleData const& h);
// The map an object is on now, from the host's own records (never by touching the object).
bool currentMapOf(fl::HandleData const& h, uint32_t& mapId, uint32_t& instanceId);
// Player map records: the hooks keep them, the world thread refreshes them every tick.
void notePlayerMap(Player const* p);
void forgetPlayerMap(Player const* p);
void refreshPlayerMaps();

// Iterates the players in the world (sessions with a Player).
std::vector<Player*> onlinePlayers();

bool isBot(Player const* p);
}  // namespace flh
