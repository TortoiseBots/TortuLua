#include "AccessRules.h"

namespace flh
{
namespace
{
thread_local std::optional<MapKey> t_scope;
}

MapScope::MapScope(uint32_t mapId, uint32_t instanceId) : _previous(t_scope)
{
    t_scope = MapKey{mapId, instanceId};
}

MapScope::~MapScope()
{
    t_scope = _previous;
}

std::optional<MapKey> MapScope::current()
{
    return t_scope;
}

fl::Access AccessRules::check(std::optional<MapKey> declared, std::optional<MapKey> current) const
{
    std::optional<MapKey> const scope = t_scope;
    if (scope)
    {
        if (!current)
            return fl::Access::Gone;
        if (declared && *declared != *current)
            return fl::Access::Gone;       // the object moved since the handle was made
        return *current == *scope ? fl::Access::Ok : fl::Access::OtherMap;
    }
    if (onWorldThread() && mapsIdle())
        return fl::Access::Ok;
    return fl::Access::OtherMap;
}

AccessRules::Place AccessRules::placeForMap(MapKey target) const
{
    std::optional<MapKey> const scope = t_scope;
    if (scope)
        return *scope == target ? Place::Direct : Place::Defer;
    return onWorldThread() && mapsIdle() ? Place::Direct : Place::Defer;
}

AccessRules::Place AccessRules::placeForWorld() const
{
    return !t_scope && onWorldThread() && mapsIdle() ? Place::Direct : Place::Defer;
}

void PlayerMaps::set(fl::Guid const& guid, MapKey key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _maps[{guid.hi, guid.lo}] = key;
}

void PlayerMaps::forget(fl::Guid const& guid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _maps.erase({guid.hi, guid.lo});
}

std::optional<MapKey> PlayerMaps::get(fl::Guid const& guid) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _maps.find({guid.hi, guid.lo});
    if (it == _maps.end())
        return std::nullopt;
    return it->second;
}

void PlayerMaps::replaceAll(std::vector<std::pair<fl::Guid, MapKey>> const& all)
{
    std::map<Key, MapKey> fresh;
    for (auto const& e : all)
        fresh[{e.first.hi, e.first.lo}] = e.second;
    std::lock_guard<std::mutex> lock(_mutex);
    _maps.swap(fresh);
}

size_t PlayerMaps::size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _maps.size();
}
}  // namespace flh

namespace flh
{
std::vector<std::pair<fl::Guid, MapKey>> PlayerMaps::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::pair<fl::Guid, MapKey>> out;
    out.reserve(_maps.size());
    for (auto const& e : _maps)
        out.emplace_back(fl::Guid{e.first.first, e.first.second}, e.second);
    return out;
}
}  // namespace flh
