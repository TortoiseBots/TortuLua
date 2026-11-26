// ForeverLua host side: fl::Host answered by ForeverCore (clock, logger, guid strings, object lookups, cached schema
// snapshots, escaping, asynchronous SELECTs and the GM-line capture).
#pragma once

#include "CommandGate.h"
#include "engine/Host.h"

#include <atomic>
#include <functional>
#include <memory>

class ResultSet;   // core type (QueryResult); only referenced here

namespace flh
{
class LuaDb;
}

namespace rbac
{
class RBACData;
}

namespace flh
{
struct SchemaLoad;

class ForeverHost final : public fl::Host
{
public:
    // bridgeSecurity: the security level the GM bridge runs at (ForeverLua.BridgeSecurity), never console rights.
    explicit ForeverHost(int bridgeSecurity = 3, uint64_t syncDbTimeoutMs = 5000);
    ~ForeverHost();   // both in the .cpp: rbac::RBACData is only forward-declared here

    uint64_t nowMs() override;
    int64_t unixTime() override;
    int64_t gameTime() override;
    void log(fl::LogLevel level, std::string const& text) override;
    std::string guidToString(fl::Guid const& guid) override;
    bool lookup(fl::HandleData const& handle, fl::ObjectInfo* out) override;
    bool despawn(fl::HandleData const& handle) override;
    std::shared_ptr<fl::SchemaSnapshot const> schema(std::string const& db) override;
    std::string escape(std::string const& db, std::string const& text) override;
    bool selectAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                     std::string& error) override;
    bool takeDbResult(uint64_t& id, fl::DbRows& out, std::string& error) override;
    void cancelSelect(uint64_t id) override;
    void pollDb() override;
    bool runGmLine(std::string const& line, std::vector<std::string>& out) override;

    // ---- Eluna mode (E0/E1), implemented in EluWorld.cpp ----
    fl::Access checkAccess(fl::HandleData const& h) override;
    bool currentMap(fl::HandleData const& h, uint32_t& mapId, uint32_t& instanceId) override;
    bool objectInWorld(fl::HandleData const& h) override;
    bool gameCall(fl::HandleData const& self, std::string_view op, std::vector<fl::EventValue> const& args,
                  std::vector<fl::EventValue>& out) override;
    bool guidInfo(fl::Guid const& g, uint32_t* entry, int* type) override;
    bool playerByName(std::string const& name, fl::HandleData& out) override;
    bool playerByGuid(fl::Guid const& guid, fl::HandleData& out) override;
    bool playerOwner(fl::HandleData const& player, uint64_t& owner) override;
    std::vector<fl::HandleData> playersInWorld(int team, bool onlyGm) override;
    size_t playerCount() override;
    void sendWorldMessage(std::string const& msg) override;
    bool performSpawn(int spawnType, uint32_t entry, uint32_t mapId, uint32_t instanceId, float x, float y, float z,
                      float o, bool save, uint32_t respawnOrDespawn, uint32_t phase, fl::HandleData& out) override;
    std::vector<fl::HandleData> objectsInRange(fl::HandleData const& self, int kind, float range, uint32_t entry,
                                               int hostile, int dead) override;
    std::string itemLink(uint32_t entry) override;
    std::string areaName(uint32_t id) override;
    std::string coreVersion() override;
    bool mapExists(uint32_t mapId, uint32_t instanceId) override;
    bool questExists(uint32_t questId) override;
    bool spellExists(uint32_t spellId) override;
    bool rawQuery(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, fl::DbRows& out,
                  std::string& error) override;
    bool rawExecute(std::string const& db, std::string const& sql) override;
    void runCommand(std::string const& line) override;
    void requestReload() override;
    // ReloadEluna asked for a reload; the world script performs it outside the Lua call.
    bool takeReloadRequest() { return _reloadRequested.exchange(false); }

    // ---- E2 globals ----
    bool guildByName(std::string const& name, fl::HandleData& out) override;
    bool guildByLeaderGuid(fl::Guid const& leader, fl::HandleData& out) override;
    void saveAllPlayers() override;
    void kickPlayer(fl::HandleData const& player) override;
    int banPlayer(int mode, std::string const& nameOrIp, uint32_t durationSecs, std::string const& reason,
                  std::string const& who) override;
    std::vector<uint32_t> sendMail(std::string const& subject, std::string const& text, uint32_t receiverLow,
                                   uint32_t senderLow, uint32_t stationery, uint32_t delay, uint32_t money,
                                   uint32_t cod, std::vector<std::pair<uint32_t, uint32_t>> const& items) override;
    void addVendorItem(uint32_t entry, uint32_t item, int32_t maxCount, uint32_t incrTime,
                       uint32_t extendedCost) override;
    void vendorRemoveItem(uint32_t entry, uint32_t item) override;
    void vendorRemoveAllItems(uint32_t entry) override;
    std::vector<uint32_t> activeGameEvents() override;
    bool isGameEventActive(uint32_t eventId) override;
    void startGameEvent(uint32_t eventId, bool force) override;
    void stopGameEvent(uint32_t eventId, bool force) override;

    // ---- E3 ----
    bool rawQueryAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                       std::string& error) override;
    bool takeRawQueryResult(uint64_t& id, fl::DbRows& out, std::string& error) override;
    void cancelRawQuery(uint64_t id) override;
    // Only marks the core's command map as changed (this can run inside a command); applyCommandMapChange() rebuilds it.
    void commandsChanged() override;
    // The world script calls this from its update, outside every command: clears the core's command map when a change is
    // pending, so it is rebuilt from the current command list on the next command. True when it did.
    bool applyCommandMapChange();
    void endLuaCall() override;
    bool sendPacket(fl::HandleData const& target, uint32_t opcode, std::vector<uint8_t> const& data,
                    bool ignorePlayersInBg, uint64_t ignore) override;
    bool sendPacketToRanked(fl::HandleData const& target, uint32_t opcode, std::vector<uint8_t> const& data,
                            uint32_t rank) override;
    // Group and guild changes (E2 and the long tail). The core calls its script hooks while its own mutation is still
    // open; a change a hook asks for is therefore queued to the next world tick instead of running inside that mutation.
    void runSocialWrite(char const* what, std::function<void()> job);
    void enterSocialHook() override { ++_socialHookDepth; }
    void leaveSocialHook() override { --_socialHookDepth; }
    // Both flags are touched on the world thread only (the callers branch on the thread/place first).
    bool socialWriteBlocked() const { return _socialWriteActive || _socialHookDepth > 0; }
    bool _socialWriteActive = false;
    int _socialHookDepth = 0;
    bool dataLoad(std::vector<fl::DataRow>& out, std::string& error) override;
    bool dataWrite(fl::DataWrite const& w) override;
    void takeDataWrites(std::vector<fl::DataWriteResult>& out) override;
    bool worldOnlyReadOk() override;
    bool mapIsInstanced(uint32_t mapId) override;
    bool commandNameReserved(std::string const& name) override;
    fl::Guid buildGuid(fl::ObjKind kind, uint64_t low, uint32_t entry) override;
    uint32_t realmId() override;
    bool isInventoryPos(int kind, uint8_t bag, uint8_t slot) override;
    // The roots of the core's command table (read once at start, before any Lua command exists).
    void setCoreCommandRoots(std::vector<std::string> roots) { _coreRoots.assign(std::move(roots)); }
    // The LuaDb of the module's own connections, opened on first use.
    flh::LuaDb& db();

private:
    bool loadsOwner(char const* what);
    std::atomic<bool> _loadsOffThreadLogged{false};
    void startSchemaLoad(std::string const& db);
    void finishSchemaLoad(std::string const& db, std::shared_ptr<ResultSet> const& res);
    void collectSelect(uint64_t id, std::shared_ptr<ResultSet> const& res, size_t maxRows, size_t maxBytes);

    // The permissions of the bridge security level, loaded on the first bridge call.
    rbac::RBACData const* bridgePermissions();

    int _bridgeSecurity;
    uint64_t _dbTimeoutMs;
    std::unique_ptr<rbac::RBACData> _rbac;
    std::unique_ptr<flh::LuaDb> _db;
    std::atomic<bool> _reloadRequested{false};
    CommandMapGate _cmdGate;
    CommandNameSet _coreRoots;
    std::unique_ptr<SchemaLoad> _loads;   // the futures of the schema loads and of the selects (core types live in the .cpp)
};
}  // namespace flh
