// The plain data the engine and the host exchange: object handles, a fresh look at an object, database rows.
// No Lua, no game headers. Host.h adds the virtual interface; Value.h adds the event snapshot values.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace fl
{
enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

// Object kinds a handle can name. Player, Creature, GameObject and Map are world objects; Item, Group, Guild and Quest
// are keyed by their own ids; Query and Packet are owned values (they never resolve through the host).
enum class ObjKind : uint8_t
{
    Player,
    Creature,
    GameObject,
    Map,
    Item,
    Group,
    Guild,
    Quest,
    Spell,
    Aura,
    Query,
    Packet,
    Corpse,        // E5: a player's remains (WorldObject)
    BattleGround,  // E5: one battleground instance
    Vehicle,       // E5: Classic 1.60 has no vehicles: the class exists for the section 7 stubs
    Bot,           // our bot API: a bot of the bot engine, read from the bot snapshot (never through the host)
    SpellInfo      // long tail: a documented spell entry (an id, never stale: the core data lives for the whole run)
};

// The answer of Host::checkAccess: may the calling thread touch the object behind a handle right now?
enum class Access : uint8_t
{
    Ok,
    Gone,      // not in the world, or it has left the map the handle names (a stale handle): "<Class> no longer exists"
    OtherMap   // the object lives on a map this thread does not update: "object is on another map"
};

// An opaque 128-bit object guid (ForeverCore's ObjectGuid is 128 bit). The engine never interprets it.
struct Guid
{
    uint64_t hi = 0;
    uint64_t lo = 0;
    bool operator==(Guid const& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(Guid const& o) const { return !(*this == o); }
};

// What a Lua handle holds: never a pointer, only the data needed to find the object again.
struct HandleData
{
    ObjKind kind = ObjKind::Creature;
    Guid guid;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    // A handle delivered by a REMOVE event: it names an object that has left the world. It carries only the GUID (and so the
    // entry and type) and never resolves, even if the same GUID is in the world again later: every method that needs the
    // object answers "no longer exists" / false.
    bool removed = false;
    bool operator==(HandleData const& o) const
    {
        return kind == o.kind && guid == o.guid && mapId == o.mapId && instanceId == o.instanceId && removed == o.removed;
    }
};

// A fresh look at an object, returned by Host::lookup.
struct ObjectInfo
{
    std::string name;
    uint32_t entry = 0;
    float x = 0, y = 0, z = 0, o = 0;
    bool alive = true;
};

using DbValue = std::variant<std::monostate, int64_t, double, std::string>;

struct DbRows
{
    std::vector<std::string> columns;
    std::vector<std::vector<DbValue>> rows;
    // Set by the engine before the call: the most string/blob bytes the host may materialise (0 = no limit). A host
    // that passes it sets tooLarge and stops reading; the engine also checks the finished result itself.
    size_t maxBytes = 0;
    bool tooLarge = false;
};

// One stored ForeverData value (E3): the script namespace, its key and the JSON text. All three are byte strings: a key
// may hold any byte, and two keys that differ in case or in trailing spaces are two keys.
struct DataRow
{
    std::string script;
    std::string key;
    std::string value;
    bool removed = false;
};

// One ForeverData write handed to the host (an upsert, or a delete when `erase`). The host reports it finished with a
// DataWriteResult carrying the same token, and only then does the engine count the write as done.
struct DataWrite
{
    uint64_t token = 0;
    std::string script;
    std::string key;
    std::string value;
    bool erase = false;
};

struct DataWriteResult
{
    uint64_t token = 0;
    bool ok = false;
    std::string error;
};
}  // namespace fl
