// Near-object search helpers for WorldObject:GetNearObject / GetNearObjects (and range clamping for grid searches).
// Header-only and game-free so the range clamp and All-container visit choice are unit-tested without ForeverCore.
#pragma once

#include <algorithm>
#include <cmath>

namespace flh
{
// Core Map/Cell grid walks convert radius to cell bounds (Cell::CalculateCellArea); clamp to one grid (533.333f) and
// replace non-finite inputs with the default grid radius so huge or infinite Lua numbers cannot overflow cell math.
inline float clampSearchRange(double raw)
{
    float const range = static_cast<float>(raw);
    if (!std::isfinite(range))
        return 533.333f;
    return std::clamp(range, 0.0f, 533.333f);
}

// Core GridDefines.h:91-92 splits WorldTypeMapContainer (Player, pets, Corpse, DynamicObject) from
// GridTypeMapContainer (GameObject, non-pet Creature). Near-object searches must visit both containers via
// VisitAllObjects rather than VisitWorldObjects, or GameObject and ordinary Creature handles are never visited.
template <class CellT, class WorldObjectT, class WorkerT>
void visitAllNearObjects(WorldObjectT* obj, WorkerT& worker, float range)
{
    CellT::VisitAllObjects(obj, worker, range);
}
}  // namespace flh
