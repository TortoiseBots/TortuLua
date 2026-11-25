// Finding a creature handle's object in its map. A hunter/warlock pet is a Creature in the C++ sense, but the core keeps
// it in the map's Pet store, not the Creature store: Map::GetCreature(guid) never finds it (Map::GetPet does). A handle of
// kind Creature may carry a Pet GUID (the AddWorld hook of a pet hands one over), so the lookup tries both. Header-only and
// templated so the rule is unit-tested without the core.
#pragma once

namespace flh
{
// MapT: GetCreature(guid) / GetPet(guid); GuidT: IsPet(). Returns the creature, the pet (as a Creature*) or null.
template <class CreatureT, class MapT, class GuidT>
CreatureT* findCreatureOrPet(MapT& map, GuidT const& guid)
{
    if (CreatureT* c = map.GetCreature(guid))
        return c;
    if (guid.IsPet())
        return map.GetPet(guid);
    return nullptr;
}
}  // namespace flh
