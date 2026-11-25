// The Lua form of a 128-bit guid. Eluna documents the guid as a 64-bit number; ForeverCore's ObjectGuid is 128 bit and
// the type, the entry and the map live in the high half, so a number would lose them. Object:GetGUID() therefore returns
// a 32-digit hex string (high half first, then the low half) that compares, keys a table, prints and round-trips through
// GetPlayerByGUID/GetGUIDType/GetGUIDEntry/GetGUIDLow/RegisterUniqueCreatureEvent. An integer is still accepted by
// those functions and means the low 64 bits with a zero high half (the form GetGUIDLow-style scripts hold).
#pragma once

#include "engine/Types.h"

#include <string>
#include <string_view>

namespace fl
{
std::string guidText(Guid const& g);
// 32 hex digits (either case). False for anything else.
bool parseGuidText(std::string_view text, Guid& out);
}  // namespace fl
