// TortuLua engine: the interface between the game-free engine and the world it runs in.
// Round 1 fills it with a fake world (tests/FakeHost); round 2 fills it with ForeverCore.
// No core header may be included from src/engine.
#pragma once

#include "engine/Types.h"
#include "engine/Value.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace fl
{
// information_schema snapshot of one database: table -> columns. Table names are case sensitive, column names are
// case insensitive (the canonical spelling is kept).
struct SchemaSnapshot
{
    std::map<std::string, std::map<std::string, std::string>> tables;  // table -> (lower-case column -> column)

    void addTable(std::string const& table, std::vector<std::string> const& columns);
    bool hasTable(std::string const& table) const { return tables.count(table) != 0; }
    // Returns the canonical column name, or an empty string.
    std::string column(std::string const& table, std::string const& col) const;
};

class Host
{
public:
    virtual ~Host() = default;

    // Monotonic milliseconds (used for budgets, timers and the tick cap).
    virtual uint64_t nowMs() = 0;
    // Unix seconds and the game's own clock in seconds.
    virtual int64_t unixTime() = 0;
    virtual int64_t gameTime() = 0;

    virtual void log(LogLevel level, std::string const& text) = 0;
    virtual std::string guidToString(Guid const& guid) = 0;

    // Looks the object up again. Returns false when it is gone (despawned, logged out, instance unloaded).
    virtual bool lookup(HandleData const& handle, ObjectInfo* out) = 0;
    // Returns false when the object is gone.
    virtual bool despawn(HandleData const& handle) = 0;

    // The schema of "world", "characters", "auth" or "hotfixes", or null when that database is not available.
    // The engine caches the answer until a reload.
    virtual std::shared_ptr<SchemaSnapshot const> schema(std::string const& db) = 0;
    virtual std::string escape(std::string const& db, std::string const& text) = 0;
    // Starts a SELECT that the SQL guard built, on the database's asynchronous connection: it must never block the
    // calling (world) thread, and no schema query may run on it either. `id` is the engine's handle; the answer is
    // collected by takeDbResult(). Returns false and an error text for an unknown database or a full queue.
    virtual bool selectAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes,
                             uint64_t id, std::string& error) = 0;
    // Pops one finished request (rows, or a non-empty error) for the owner thread. Runs the ready work: the host may
    // read the buffered result here, but it must not call Lua. False when nothing is ready.
    virtual bool takeDbResult(uint64_t& id, DbRows& out, std::string& error) = 0;
    // The caller is gone (timeout or engine shutdown): drop the request; a late result is discarded.
    virtual void cancelSelect(uint64_t id) = 0;
    // Runs the ready schema loads and selects (owner thread, once per tick). Never blocks.
    virtual void pollDb() = 0;

    // The GM bridge: runs one command line and captures its output. The guard has already accepted the line.
    virtual bool runGmLine(std::string const& line, std::vector<std::string>& out) = 0;

    // ---- Eluna mode (E0/E1). Every default answers "this host has no game": the engine tests use them as-is. ----
    // Asked BEFORE any lookup: may the calling thread touch the object behind the handle? Ok on the world thread with the
    // maps idle, or on the thread that updates the object's CURRENT map; Gone when the object is not in the world or has
    // left the map the handle names (a stored handle then never reaches the object from the wrong thread); OtherMap when
    // another map's thread owns it.
    virtual Access checkAccess(HandleData const&) { return Access::Ok; }
    // The map the object is on now (thread-safe: from the host's own record, never by touching the object). False when
    // it is not in the world. The default answers from the handle.
    virtual bool currentMap(HandleData const& h, uint32_t& mapId, uint32_t& instanceId)
    {
        mapId = h.mapId;
        instanceId = h.instanceId;
        return true;
    }
    // Is the creature, game object or player behind the handle in the world right now (a GUID lookup in its map, never a pointer
    // kept from the hook)? Asked on the thread that may touch the object (the map's own update, or the world update with the
    // maps idle) when a queued core-hook event is drained. The default answers "yes" (the engine tests).
    virtual bool objectInWorld(HandleData const&) { return true; }
    // Runs one Eluna class method against the live game ("unit.setHealth", "player.addItem"). Arguments are the
    // validated Lua values; `out` receives the returns. False: the op is not available on this core (the binding raises).
    virtual bool gameCall(HandleData const&, std::string_view, std::vector<EventValue> const&, std::vector<EventValue>&)
    {
        return false;
    }
    // Decodes a guid without looking the object up (Object:GetEntry/GetGUIDLow/GetTypeId answer from the handle).
    virtual bool guidInfo(Guid const&, uint32_t* /*entry*/, int* /*type*/) { return false; }
    virtual bool playerByName(std::string const&, HandleData&) { return false; }
    // The full 128-bit guid; a guid with a zero high half is the legacy low-counter form of a player.
    virtual bool playerByGuid(Guid const&, HandleData&) { return false; }
    // Our bot API: the player's owner scope (the Battle.net account of the player's session; 0 = unknown). GetBotsOfOwner
    // compares it with the owner each bot's provider recorded. False/0: the module cannot tell, so no bot is "owned".
    virtual bool playerOwner(HandleData const&, uint64_t& owner)
    {
        owner = 0;
        return false;
    }
    virtual std::vector<HandleData> playersInWorld(int /*team*/, bool /*onlyGm*/) { return {}; }
    // E4 GetPlayersOnMap: the players of one map instance (docs: team 0 Alliance, 1 Horde, 2 neutral/all; onlyGM).
    virtual std::vector<HandleData> playersOnMap(uint32_t /*mapId*/, uint32_t /*instanceId*/, int /*team*/, bool /*onlyGm*/)
    {
        return {};
    }
    virtual size_t playerCount() { return 0; }
    virtual void sendWorldMessage(std::string const&) { }
    // spawnType 1 = creature, 2 = game object (PerformIngameSpawn). A host that is on the wrong thread for the target map
    // queues the spawn to the world thread and answers false (the script gets nil); the result is not available then.
    virtual bool performSpawn(int /*spawnType*/, uint32_t /*entry*/, uint32_t /*mapId*/, uint32_t /*instanceId*/,
                              float /*x*/, float /*y*/, float /*z*/, float /*o*/, bool /*save*/,
                              uint32_t /*respawnOrDespawn*/, uint32_t /*phase*/, HandleData& /*out*/)
    {
        return false;
    }
    // Objects of one kind within range: kind 0 = player, 1 = creature, 2 = game object. hostile/dead: 0 both,
    // 1/2 the first/second class (see the Eluna docs), entry 0 = any.
    virtual std::vector<HandleData> objectsInRange(HandleData const&, int /*kind*/, float /*range*/, uint32_t /*entry*/,
                                                   int /*hostile*/, int /*dead*/)
    {
        return {};
    }
    virtual std::string itemLink(uint32_t /*entry*/) { return {}; }
    virtual std::string areaName(uint32_t /*id*/) { return {}; }
    virtual std::string coreVersion() { return {}; }
    virtual bool mapExists(uint32_t /*mapId*/, uint32_t /*instanceId*/) { return false; }
    virtual bool questExists(uint32_t /*questId*/) { return false; }
    virtual bool spellExists(uint32_t /*spellId*/) { return false; }
    // Raw SQL on the module's own connections (Eluna's WorldDBQuery/WorldDBExecute): db is "world", "characters" or
    // "auth". Synchronous for the *Query forms; *Execute is queued by the host. False and `error` on failure.
    virtual bool rawQuery(std::string const& /*db*/, std::string const& /*sql*/, size_t /*maxRows*/,
                          size_t /*maxBytes*/, DbRows& /*out*/, std::string& /*error*/)
    {
        return false;
    }
    virtual bool rawExecute(std::string const& /*db*/, std::string const& /*sql*/) { return false; }
    // Eluna's RunCommand: a GM-level command line. Only the world thread may run it; from a map thread the host queues it
    // to the world thread. The default is the GM bridge (the engine tests).
    virtual void runCommand(std::string const& line)
    {
        std::vector<std::string> out;
        runGmLine(line, out);
    }
    // ReloadEluna: the host reloads the module after the current call (a reload while Lua runs is refused).
    virtual void requestReload() { }

    // ---- E2 globals (the game work behind them; every default answers "no game"). ----
    virtual bool guildByName(std::string const& /*name*/, HandleData& /*out*/) { return false; }
    virtual bool guildByLeaderGuid(Guid const& /*leader*/, HandleData& /*out*/) { return false; }
    virtual void saveAllPlayers() { }
    virtual void kickPlayer(HandleData const&) { }
    // Ban mode: 0 account, 1 character, 2 IP. Returns the core's BanReturn (0 success, 1 syntax, 2 not found, 3 a
    // longer ban exists), or -1 when the core could not decide (the script gets nil, as the docs say).
    virtual int banPlayer(int /*mode*/, std::string const& /*nameOrIp*/, uint32_t /*durationSecs*/,
                          std::string const& /*reason*/, std::string const& /*who*/)
    {
        return -1;
    }
    // SendMail: up to 12 (entry, amount) pairs; the low guids of the items that were created.
    virtual std::vector<uint32_t> sendMail(std::string const& /*subject*/, std::string const& /*text*/,
                                           uint32_t /*receiverLow*/, uint32_t /*senderLow*/, uint32_t /*stationery*/,
                                           uint32_t /*delay*/, uint32_t /*money*/, uint32_t /*cod*/,
                                           std::vector<std::pair<uint32_t, uint32_t>> const& /*items*/)
    {
        return {};
    }
    virtual void addVendorItem(uint32_t /*entry*/, uint32_t /*item*/, int32_t /*maxCount*/, uint32_t /*incrTime*/,
                               uint32_t /*extendedCost*/)
    {
    }
    virtual void vendorRemoveItem(uint32_t /*entry*/, uint32_t /*item*/) { }
    virtual void vendorRemoveAllItems(uint32_t /*entry*/) { }
    virtual std::vector<uint32_t> activeGameEvents() { return {}; }
    virtual bool isGameEventActive(uint32_t /*eventId*/) { return false; }
    virtual void startGameEvent(uint32_t /*eventId*/, bool /*force*/) { }
    virtual void stopGameEvent(uint32_t /*eventId*/, bool /*force*/) { }

    // ---- E3 ----
    // Eluna's *DBQueryAsync: the query runs on the module's worker; the owner thread collects the finished result with
    // takeRawQueryResult() and the engine runs the Lua callback on its next tick. False and `error` when the queue is
    // full or the database is unknown.
    virtual bool rawQueryAsync(std::string const& /*db*/, std::string const& /*sql*/, size_t /*maxRows*/,
                               size_t /*maxBytes*/, uint64_t /*id*/, std::string& /*error*/)
    {
        return false;
    }
    // Pops one finished asynchronous raw query. False when none is ready; runs on the owner thread, never calls Lua.
    virtual bool takeRawQueryResult(uint64_t& /*id*/, DbRows& /*out*/, std::string& /*error*/) { return false; }
    // The caller gave up (timeout or engine shutdown): the finished result is discarded.
    virtual void cancelRawQuery(uint64_t /*id*/) { }

    // The Lua command table changed (RegisterCommand at load or a reload): rebuild the core's chat command map.
    virtual void commandsChanged() { }

    // One top-level Lua call (an event handler, a timer, a command) has ended: the transient Spell/Aura handles it
    // produced are invalid from here on (or "Spell no longer exists").
    virtual void endLuaCall() { }

    // E5: Player/Group/Guild:SendPacket. The bytes are the packet value the engine owns; false when the core cannot send
    // to that target (the binding raises "<Method>: not available on this server").
    virtual bool sendPacket(fl::HandleData const& /*target*/, uint32_t /*opcode*/, std::vector<uint8_t> const& /*data*/,
                            bool /*ignorePlayersInBg*/, uint64_t /*ignore*/)
    {
        return false;
    }
    // A core-driven group or guild hook is dispatching Lua now: the core's own mutation that called the hook is still
    // open, so a change the handler asks for must not run inside it.
    virtual void enterSocialHook() { }
    virtual void leaveSocialHook() { }
    // Guild:SendPacketToRanked: the packet to the online members of one guild rank only.
    virtual bool sendPacketToRanked(fl::HandleData const& /*target*/, uint32_t /*opcode*/,
                                    std::vector<uint8_t> const& /*data*/, uint32_t /*rank*/)
    {
        return false;
    }

    // ForeverData (E3). dataLoad reads every stored value at start; false and `error` refuse the load (database down, more
    // rows than the load cap): the engine then keeps the store unavailable and retries, it never starts empty.
    virtual bool dataLoad(std::vector<DataRow>& /*out*/, std::string& error)
    {
        error = "this host has no database";
        return false;
    }
    // dataWrite queues one write; false = refused for now (queue full), the key stays dirty. The host finishes it later
    // (takeDataWrites): a write is done only when the database confirmed it. Writes of one key run in the order queued.
    virtual bool dataWrite(DataWrite const&) { return false; }
    // Moves the finished writes into `out` (owner thread).
    virtual void takeDataWrites(std::vector<DataWriteResult>& /*out*/) { }

    // ---- fixes of the E2/E3 review round ----
    // True when the calling thread may run a world-only READ (a Group or Guild method) now: the world thread with the
    // maps idle. A map-thread handler gets a Lua error instead of a racy read of world-owned social state.
    virtual bool worldOnlyReadOk() { return true; }
    // RegisterInstanceEvent / RegisterMapEvent on a dungeon or raid map need core hook C11 (the core's InstanceMapScript is
    // database-bound): true when `mapId` is a dungeon or raid map.
    virtual bool mapIsInstanced(uint32_t /*mapId*/) { return false; }
    // RegisterCommand: true when `name` (lower case) equals or is a prefix of a root of the core's command table, or of
    // one of the module's own roots. Such a name would hijack a core command (or abort the core's command map build).
    virtual bool commandNameReserved(std::string const& /*name*/) { return false; }
};
}  // namespace fl
