// The bounds of the core's terrain grid, as seen by the engine: the same numbers as Trinity::IsValidMapCoord and
// TerrainInfo::GetGrid (MAP_HALFSIZE = 32 grids of 533.33 yards). A call with a NaN, an infinity or a coordinate
// outside them indexes the core's terrain arrays out of bounds (TerrainMgr.cpp GetGrid: no check), so it must be
// refused before any core call. Kept here, not in src/bind, so the rule is unit-tested without the core.
#pragma once

#include <cmath>

namespace fl
{
constexpr float kMapHalfSize = 17066.666f;             // MAP_HALFSIZE
constexpr float kMapMaxCoord = kMapHalfSize - 0.5f;    // IsValidMapCoord: |c| <= MAP_HALFSIZE - 0.5

// True for a coordinate the core's grid can index: finite and within +/-kMapMaxCoord.
inline bool validMapCoord(float c)
{
    return std::isfinite(c) && std::fabs(c) <= kMapMaxCoord;
}

inline bool validMapCoord(float x, float y)
{
    return validMapCoord(x) && validMapCoord(y);
}

inline bool validMapCoord(float x, float y, float z)
{
    return validMapCoord(x) && validMapCoord(y) && validMapCoord(z);
}
}  // namespace fl
