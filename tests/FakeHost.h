// A fake world for the engine tests: objects in a map, a schema, canned query results, a controllable clock and a log.
#pragma once

#include "AccessRules.h"
#include "DeferQueue.h"
#include "engine/Engine.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <vector>

namespace fltest
{
class FakeHost : public fl::Host
{
public:
    FakeHost();

    // ---- review round E4/E5: who may touch which host structure ----
    // The real host's database containers (schema loads, query callbacks, the finished results, the ForeverData write
    // results) have one owner, the world thread (H1). Every poll/take below counts a call from any other thread here.
    std::thread::id worldThreadId;
    std::atomic<int> offWorldPolls{0};
    std::vector<std::string> offWorldOps;   // under _mutex
    void notePoll(char const* op);
    // H5: the core calls OnCreate under its exclusive _mapsLock, and every host call that finds a map (lookup, the access
    // check, a game call, the player list of a map) takes a shared lock on the same mutex: a re-lock from that thread
    // is a self-deadlock on the real server. A test sets this flag around the modelled OnCreate.
    static thread_local bool mapsLockHeld;
    std::atomic<int> mapsRelocks{0};
    void noteMapLock()
    {
        if (mapsLockHeld)
            ++mapsRelocks;
    }

    // ---- clock: the real steady clock plus an offset the test can advance ----
    uint64_t nowMs() override;
    void advance(uint64_t ms) { offset += ms; }
    int64_t unixTime() override { return 1700000000 + static_cast<int64_t>(nowMs() / 1000); }
    int64_t gameTime() override { return 4242; }

    void log(fl::LogLevel level, std::string const& text) override;
    bool logContains(std::string const& needle) const;
    size_t logCount(fl::LogLevel level) const;

    std::string guidToString(fl::Guid const& g) override;

    // ---- objects ----
    // The fake mirrors what the core does that the engine depends on: 128-bit guids whose high half carries the type (and
    // the entry of a creature/game object) so two kinds can share a low counter; creatures and game objects live in the
    // store of ONE map and are found only through it; players are found by guid in a global table and may change map;
    // access follows the same flh::AccessRules the module uses.
    static constexpr uint64_t kTypePlayer = 4, kTypeItem = 3, kTypeCreature = 9, kTypeGameObject = 11, kTypePet = 10,
                              kTypeCorpse = 14;
    static fl::Guid makeGuid(uint64_t type, uint64_t low, uint32_t entry = 0, uint16_t mapId = 0);
    static fl::HandleData creature(uint64_t id, uint32_t map = 0, uint32_t instance = 0, uint32_t entry = 0);
    // A hunter/warlock pet: a creature handle whose guid is a Pet guid. The core keeps it in the map's Pet store, so only
    // the lookup rule of src/host/CreatureLookup.h (GetCreature, then GetPet for a Pet guid) finds it.
    static fl::HandleData pet(uint64_t id, uint32_t map = 0, uint32_t instance = 0, uint32_t entry = 0);
    bool petFallback = true;   // false: the pre-fix lookup (Map::GetCreature only) that never found a pet
    static fl::HandleData player(uint64_t id, uint32_t map = 0, uint32_t instance = 0);
    static fl::HandleData gameObject(uint64_t id, uint32_t map = 0, uint32_t instance = 0, uint32_t entry = 0);
    static fl::HandleData corpse(uint64_t id, uint32_t map = 0, uint32_t instance = 0);
    static fl::HandleData item(uint64_t id);
    void add(fl::HandleData const& h, fl::ObjectInfo const& info);
    void remove(fl::HandleData const& h);
    // A player teleports: its map record changes, old handles keep the map they were made on.
    void moveTo(fl::HandleData const& h, uint32_t map, uint32_t instance = 0);
    fl::ObjectInfo* find(fl::HandleData const& h);
    bool lookup(fl::HandleData const& h, fl::ObjectInfo* out) override;
    bool despawn(fl::HandleData const& h) override;
    // Which thread is the world thread and whether the maps are updating (the rules every access check goes through).
    flh::AccessRules rules;
    flh::DeferQueue deferred;                     // spawns and commands queued from a map thread
    std::vector<std::string> commands;            // RunCommand lines that actually ran

    // ---- database ----
    std::map<std::string, std::shared_ptr<fl::SchemaSnapshot>> schemas;   // db -> snapshot
    std::map<std::string, fl::DbRows> results;                             // exact SQL -> rows
    fl::DbRows defaultRows;
    std::vector<std::string> queries;                                      // every SELECT that was sent
    int schemaCalls = 0;
    bool failSelect = false;
    bool throwOnSelect = false;
    bool badEscape = false;                                                // escape() returns the text unchanged
    bool stalled = false;                                                  // selectAsync() answers never (a wedged pool)
    bool manualSelect = false;   // selectAsync() only records the request; answerSelect() completes it later
    std::function<void()> onSelect;                                        // runs when a select starts
    std::shared_ptr<fl::SchemaSnapshot const> schema(std::string const& db) override;
    std::string escape(std::string const& db, std::string const& text) override;
    bool selectAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                     std::string& error) override;
    bool takeDbResult(uint64_t& id, fl::DbRows& out, std::string& error) override;
    void cancelSelect(uint64_t id) override;
    void pollDb() override { notePoll("pollDb"); }
    // Requests the engine started and that have not been answered yet (visible to the tests).
    struct DbRequest
    {
        uint64_t id = 0;
        std::string sql;
        size_t maxRows = 0;
        size_t maxBytes = 0;
    };
    std::vector<DbRequest> dbRequests;
    void answerSelect(uint64_t id, fl::DbRows const& rows) { _dbResults[id] = rows; }   // manualSelect only
    size_t cancelled = 0;

    // ---- Eluna mode (E0/E1) ----
    bool denyAccess = false;                    // mayAccessObject answers false ("object is on another map")
    std::map<std::string, std::vector<fl::EventValue>> gameResults;   // op -> returns
    std::vector<std::string> gameCalls;         // every op the bindings dispatched
    std::function<bool(fl::HandleData const&, std::string const&, std::vector<fl::EventValue> const&,
                       std::vector<fl::EventValue>&)> onGameCall;   // runs first; true = answered
    std::map<std::string, fl::HandleData> playersByName;
    std::vector<fl::HandleData> players;

    // ---- E5 Unit long tail, round 1 ----
    // The unit state the documented Unit methods of that round read and write, so a test proves a value flows in both
    // directions. Only the round's own ops are answered here; anything else is still gameResults.
    struct FakeUnit
    {
        int64_t maxHealth = 1000;
        int64_t health = 1000;
        std::map<int32_t, int32_t> powerTypeMax;   // per power type
        std::map<int32_t, int32_t> powerTypeValue;
        int32_t powerType = 0;
        std::map<uint32_t, double> stats;
        std::map<uint32_t, int64_t> baseSpellPower;
        uint32_t standState = 0;
        bool mounted = false;
        uint32_t mountId = 0;
        bool stopped = false, casting = false, rooted = false, inWater = false, underWater = false, pvp = false;
        bool attackingPlayer = false, charmed = false;
        uint32_t unitStates = 0;
        bool vendor = false, questGiver = false, trainer = false, banker = false, innkeeper = false;
        bool gossip = false, spiritHealer = false, serviceProvider = false;
        std::map<uint32_t, double> speed;          // type -> GetSpeed answer
        std::map<uint32_t, double> speedRate;      // type -> SetSpeed rate
        std::vector<uint32_t> forcedSpeed;         // SetSpeed types sent with forced = true
        std::map<std::string, bool> controlled;    // setRooted/setConfused/setFeared/setStunned
        uint32_t movementType = 0;
        bool teleported = false;
        double teleportX = 0, teleportY = 0, teleportZ = 0, teleportO = 0;
        double facing = 0;          // radians
        bool facingObject = false;
        uint64_t facingObjectLow = 0;
        bool inCombat = false;
        uint64_t combatWithLow = 0;
        struct Heal
        {
            uint64_t targetLow = 0;
            uint32_t spell = 0, amount = 0;
            bool critical = false;
        };
        std::vector<Heal> heals;
        uint32_t stoppedSpell = 0;
        bool hasStoppedSpell = false;
        bool hasCurrentSpell = false;
        uint32_t currentSpellId = 0;
        int32_t interruptedType = -1;
        bool interruptDelayed = true;
        uint32_t auraSpell = 0;
        uint32_t ownedAuraSpell = 0;
        int removeAllAuras = 0, removeArenaAuras = 0;
        std::string charmerGuid, ownerGuid, controllerGuid, creatorGuid, petGuid;
        std::vector<fl::HandleData> friendlyUnits, unfriendlyUnits;
        double listRange = 0;
        uint32_t displayId = 0, nativeDisplayId = 0, creatureType = 0;
        std::string classAsString, raceAsString;
        int32_t classLocale = 0, raceLocale = 0;
        uint32_t classMask = 0;
        uint64_t raceMask = 0;
        struct Chat
        {
            uint32_t type = 0, lang = 0;
            std::string msg;
            uint64_t targetLow = 0;
        };
        std::vector<Chat> chats;
        uint32_t emote = 0, emoteState = 0;
        bool moving = false, flying = false, visible = true;
        uint32_t sheath = 0;
        bool waterWalk = false, sanctuary = false;
        int deMorphs = 0, restoreDisplayId = 0, restoreFaction = 0;
        bool canModifyStats = true, dying = false, ffa = false, canFly = false, meleeDisabled = false;
        bool armorer = false, auctioneer = false, battleMaster = false, guildMaster = false;
        bool spiritGuide = false, spiritService = false, tabardDesigner = false, taxi = false;
        bool accessiblePlace = true;
        uint64_t accessibleObjLow = 0;
        double accessibleRadius = 0;
        std::string name;
        std::string charmGuid, controllerGuids, critterGuid;
        int removeBindSightAuras = 0, removeCharmAuras = 0;
        std::set<uint32_t> immuneMechanics;
        struct StatMod
        {
            uint32_t mod = 0;
            uint32_t type = 0;
            double value = 0;
            bool apply = true;
            bool isPct = false;
        };
        std::vector<StatMod> statMods;
        struct AoFCast
        {
            double x = 0, y = 0, z = 0;
            uint32_t spell = 0;
            bool triggered = true;
        };
        std::vector<AoFCast> aofCasts;
        struct EmoteMsg
        {
            std::string msg;
            uint64_t receiverLow = 0;
            bool bossEmote = false;
        };
        std::vector<EmoteMsg> unitEmotes;
        // The action ops with nothing to read back: the op and its arguments, in order.
        std::vector<std::pair<std::string, std::vector<fl::EventValue>>> actions;
    };
    std::map<std::pair<uint64_t, uint64_t>, FakeUnit> unitStates;
    FakeUnit& unitState(fl::HandleData const& h) { return unitStates[{h.guid.hi, h.guid.lo}]; }
    bool unitStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                       std::vector<fl::EventValue>& out);

    // ---- E5 Creature/Item long tail, round 1 ----
    // The creature state the documented Creature methods of that round read and write. A setter writes the field and
    // the getter of the same value reads it back; a refusal (no AI, unknown entry, unknown path) is answered `false`.
    struct FakeCreature
    {
        bool hasAi = true;
        bool isPet = false;                 // a pet / player-controlled creature: the creature-AI-only calls refuse
        bool regenForbidden = false;        // the template forbids health regeneration (SetRegeneratingHealth(true) refused)
        bool healthZero = false;            // a dead creature: SetDeathState(ALIVE) would leave it alive at 0 health
        std::set<uint32_t> noModelEntries;  // entries whose template has no usable model (UpdateEntry refused)
        uint64_t victimLow = 0;             // SelectVictim's answer (a player low guid), 0 = none
        // AI and combat
        int attackStarts = 0;
        uint64_t attackStartTargetLow = 0;
        int selectVictims = 0, callAssistances = 0, callForHelps = 0, flees = 0, zoneCombats = 0;
        double callForHelpRadius = 0;
        bool noCallAssistance = false, noSearchAssistance = false, searchedAssistance = false, evadeMode = false;
        double aggroRange = 0, attackDistance = 0;
        uint64_t rangeTargetLow = 0;
        bool canAssist = false;
        bool assistCheckFaction = true;
        uint64_t assistFriendLow = 0, assistEnemyLow = 0;
        bool canStartAttack = false;
        bool canStartAttackForce = true;
        uint64_t canStartAttackTargetLow = 0;
        bool tapped = false;
        uint64_t lastTappedLow = 0;
        std::vector<uint64_t> tapList;      // player low guids (HasLootRecipient/GetLootRecipient*)
        std::vector<uint64_t> tapGroups;    // the group low guid of the recipient at the same index
        // threat
        std::map<uint64_t, double> threat;  // victim low -> threat
        int resetAllThreats = 0, clearThreatLists = 0, clearFixates = 0;
        uint64_t fixateLow = 0;
        std::vector<uint64_t> threatOrder;  // GetAITargets answer, in order
        // state
        bool civilian = false, guard = false, racialLeader = false, trigger = false, dungeonBoss = false;
        bool regenerating = true, hover = false, disableGravity = false, walk = false;
        bool reputationDisabled = false, canFly = false, canSwim = false;
        int64_t shieldBlockValue = 0;
        // spawn and movement
        uint32_t waypointPath = 0, currentWaypoint = 0;
        std::set<uint32_t> knownPaths;      // paths the fake waypoint manager holds
        int moveWaypoints = 0;
        uint32_t defaultMovementType = 0;
        double wanderRadius = 0;
        double homeX = 0, homeY = 0, homeZ = 0, homeO = 0;
        uint32_t respawnDelay = 0, corpseDelay = 0;
        int corpseRemovals = 0;
        uint32_t deathState = 0;
        uint32_t equipMain = 0, equipOff = 0, equipRanged = 0;
        // identity
        uint32_t family = 0, extraFlags = 0, rank = 0, scriptId = 0, dbGuidLow = 0;
        uint64_t npcFlags = 0;
        std::string aiName;
        bool hasTemplate = true;
        std::set<uint32_t> knownEntries;    // creature entries UpdateEntry accepts
        std::set<uint32_t> knownDataLow;    // CreatureData guids UpdateEntry accepts
        std::set<uint32_t> knownItems;      // item entries SetEquipmentSlots accepts
        uint32_t entry = 0;
        // spells
        std::set<uint32_t> spells;          // the creature's own spells (HasSpell)
        std::set<uint32_t> serverSpells;    // spell ids the fake's spell manager knows
        std::set<uint32_t> spellCooldowns, categoryCooldowns;
        std::map<uint32_t, int64_t> spellCooldownDelay;   // milliseconds
        uint32_t lastSpellId = 0;
        // loot mode and quests
        uint32_t lootMode = 1;
        std::set<uint32_t> quests;
        // The action ops with nothing to read back: the op and its arguments, in order.
        std::vector<std::pair<std::string, std::vector<fl::EventValue>>> actions;
    };
    std::map<std::pair<uint64_t, uint64_t>, FakeCreature> creatureStates;
    FakeCreature& creatureState(fl::HandleData const& h) { return creatureStates[{h.guid.hi, h.guid.lo}]; }
    bool creatureStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                           std::vector<fl::EventValue>& out);

    // The item state the documented Item methods of that round read and write.
    struct FakeItemState
    {
        bool hasTemplate = true;
        std::string name = "Wool Cloth";
        uint32_t entry = 0, itemLevel = 0, requiredLevel = 0, itemClass = 0, subClass = 0, inventoryType = 0;
        uint32_t quality = 0, displayId = 0, buyCount = 1, maxStack = 1, statsCount = 0, itemSet = 0;
        uint64_t buyPrice = 0, sellPrice = 0, allowableRace = 0;
        int64_t allowableClass = 0;
        uint32_t armor = 0, durability = 0, maxDurability = 0, flags = 0, flags2 = 0;
        bool bag = false, soulBound = false, accountWide = false, boundByEnchant = false, locked = false;
        bool broken = false, currencyToken = false, notEmptyBag = false, inBag = false, inTrade = false;
        bool potion = false, conjured = false, refundExpired = false, tradable = false, notBoundToPlayer = false;
        uint32_t spellIds[5] = {0, 0, 0, 0, 0}, spellTriggers[5] = {0, 0, 0, 0, 0};
        uint32_t spellCount = 0;
        bool damageRowMissing = false;      // the item level has no row in the damage tables (GetDamageInfo refused)
        uint32_t damageType = 0;
        double minDamage = 0, maxDamage = 0;
        double statValue = 0;
        int32_t statType = 0;
        bool hasOwner = false;
        bool ownerMissing = false;          // the owner is not online: the enchant setters refuse
        uint64_t ownerLow = 0;
        std::string ownerGuid;
        bool binding = false;
        std::map<uint32_t, uint32_t> enchantments;   // slot -> enchant id
        std::set<uint32_t> knownEnchants;
        uint32_t bagSize = 0;
        uint32_t extraFlags = 0, speed = 0;
        std::set<uint32_t> quests;
        bool boundToPlayerAnswer = false;
        std::vector<std::pair<std::string, std::vector<fl::EventValue>>> actions;
    };
    std::map<std::pair<uint64_t, uint64_t>, FakeItemState> itemStates;
    FakeItemState& itemState(fl::HandleData const& h) { return itemStates[{h.guid.hi, h.guid.lo}]; }
    bool itemStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                       std::vector<fl::EventValue>& out);

    // ---- Object / WorldObject / GameObject coverage ----
    struct FakeWorldObject
    {
        double combatReach = 0.0;
        bool losOk = true;
        bool hostileToAll = false;
        bool friendlyToAll = false;
        struct SoundCall
        {
            std::string op;
            uint32_t id = 0;
            uint64_t targetPlayerLow = 0;
        };
        std::vector<SoundCall> sounds;
        struct WoChat
        {
            std::string op, msg;
            uint32_t lang = 0;
            uint64_t targetLow = 0;
            bool boss = false;
        };
        std::vector<WoChat> chats;
    };
    std::map<std::pair<uint64_t, uint64_t>, FakeWorldObject> woStates;
    FakeWorldObject& woState(fl::HandleData const& h) { return woStates[{h.guid.hi, h.guid.lo}]; }
    bool worldObjectStateCall(fl::HandleData const& self, std::string const& op,
                              std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out);

    struct FakeGameObject
    {
        uint32_t goState = 1, lootState = 1, displayId = 0;
        bool spawned = true, active = false, destructible = false, transport = false, spawnedByDefault = true;
        int32_t respawnTime = 0;
        uint32_t dbLow = 0;
        std::set<uint32_t> quests;
        std::optional<fl::HandleData> lootRecipient, lootRecipientGroup;
        std::vector<std::pair<std::string, std::vector<fl::EventValue>>> actions;
    };
    std::map<std::pair<uint64_t, uint64_t>, FakeGameObject> goStates;
    FakeGameObject& goState(fl::HandleData const& h) { return goStates[{h.guid.hi, h.guid.lo}]; }
    bool gameObjectStateCall(fl::HandleData const& self, std::string const& op,
                             std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out);

    struct FakeQuest
    {
        uint32_t id = 0;
        int32_t level = 0;
        int32_t minLevel = 0;
        uint32_t maxLevel = 0;
        uint32_t type = 0;
        uint32_t flags = 0;
        int32_t prevQuestId = 0;
        uint32_t nextQuestId = 0;
        uint32_t nextQuestInChain = 0;
        std::string title;
        bool daily = false;
        bool repeatable = false;
    };
    std::map<uint32_t, FakeQuest> questStates;
    bool questStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                        std::vector<fl::EventValue>& out);
    std::vector<fl::HandleData> objectsInRange(fl::HandleData const& self, int kind, float range, uint32_t entry,
                                               int hostile, int dead) override;
    std::map<std::string, fl::DbRows> rawResults;   // sql -> rows (rawQuery)
    bool rawFail = false;
    std::vector<std::string> rawQueries;
    std::vector<std::string> rawExecuted;
    struct SpawnRequest
    {
        int type = 0;
        uint32_t entry = 0;
        uint32_t mapId = 0;
        uint32_t instanceId = 0;
        bool save = false;
        uint32_t respawn = 0;
        uint32_t phase = 0;
    };
    std::vector<SpawnRequest> spawns;
    std::optional<fl::HandleData> spawnResult;
    std::vector<std::string> worldMessages;
    int reloadsRequested = 0;
    uint32_t fakeRealmId = 1;
    fl::Guid buildGuid(fl::ObjKind kind, uint64_t low, uint32_t entry, uint16_t mapId = 0) override;
    uint32_t realmId() override { return fakeRealmId; }
    bool isInventoryPos(int kind, uint8_t bag, uint8_t slot) override;
    fl::Access checkAccess(fl::HandleData const& h) override;
    bool currentMap(fl::HandleData const& h, uint32_t& mapId, uint32_t& instanceId) override;
    bool objectInWorld(fl::HandleData const& h) override;
    bool gameCall(fl::HandleData const& self, std::string_view op, std::vector<fl::EventValue> const& args,
                  std::vector<fl::EventValue>& out) override;
    bool guidInfo(fl::Guid const& g, uint32_t* entry, int* type) override;
    bool playerByName(std::string const& name, fl::HandleData& out) override;
    bool playerByGuid(fl::Guid const& guid, fl::HandleData& out) override;
    // The bot API's owner scope: (guid.hi, guid.lo) -> the Battle.net account of that player.
    std::map<std::pair<uint64_t, uint64_t>, uint64_t> playerOwners;
    bool playerOwner(fl::HandleData const& h, uint64_t& owner) override;
    std::vector<fl::HandleData> playersInWorld(int team, bool onlyGm) override;
    std::vector<fl::HandleData> playersOnMap(uint32_t mapId, uint32_t instanceId, int team, bool onlyGm) override;
    size_t playerCount() override { return players.size(); }
    void sendWorldMessage(std::string const& msg) override { worldMessages.push_back(msg); }
    bool performSpawn(int type, uint32_t entry, uint32_t mapId, uint32_t instanceId, float, float, float, float,
                      bool save, uint32_t respawnOrDespawn, uint32_t phase, fl::HandleData& out) override;
    std::string itemLink(uint32_t entry) override { return "|cffffffff|Hitem:" + std::to_string(entry) + "|h[x]|h|r"; }
    std::string areaName(uint32_t id) override { return id == 12 ? "Elwynn Forest" : std::string(); }
    std::string coreVersion() override { return "test-1.0"; }
    bool mapExists(uint32_t mapId, uint32_t) override { return mapId == 0 || mapId == 1; }
    bool questExists(uint32_t id) override { return id == 100 || questStates.count(id) != 0; }
    bool rawQuery(std::string const&, std::string const& sql, size_t, size_t, fl::DbRows& out,
                  std::string& error) override;
    bool rawExecute(std::string const&, std::string const& sql) override;
    void runCommand(std::string const& line) override;
    void requestReload() override { ++reloadsRequested; }

    // ---- E2 globals ----
    std::map<std::string, fl::HandleData> guildsByName;
    std::map<uint64_t, fl::HandleData> guildsByLeaderLow;
    int saveAllCalls = 0;
    std::vector<fl::HandleData> kicked;
    struct BanRequest
    {
        int mode = 0;
        std::string target;
        uint32_t duration = 0;
        std::string reason;
        std::string who;
    };
    std::vector<BanRequest> bans;
    int banResult = 0;
    struct MailRequest
    {
        std::string subject, text;
        uint32_t receiver = 0, sender = 0, stationery = 0, delay = 0, money = 0, cod = 0;
        std::vector<std::pair<uint32_t, uint32_t>> items;
    };
    std::vector<MailRequest> mails;
    std::vector<uint32_t> mailItemGuids;
    struct VendorCall
    {
        int op = 0;   // 0 add, 1 remove, 2 remove all
        uint32_t entry = 0, item = 0;
        int32_t maxCount = 0;
        uint32_t incrTime = 0, extendedCost = 0;
    };
    std::vector<VendorCall> vendorCalls;
    std::vector<uint32_t> activeEvents;
    std::vector<std::pair<uint32_t, bool>> startedEvents, stoppedEvents;

    bool guildByName(std::string const& name, fl::HandleData& out) override;
    bool guildByLeaderGuid(fl::Guid const& leader, fl::HandleData& out) override;
    // ---- E5 Player long tail, round 1 ----
    // A player state the documented Player methods of that round read and write, so a test proves a value flows in both
    // directions. Only the round's own ops are answered here; every other op is still `gameResults`.
    struct FakeSkill
    {
        uint32_t step = 0, cur = 0, max = 0;
        int32_t perm = 0, temp = 0;
        bool known = false;
    };
    struct FakeItem
    {
        uint64_t low = 0;
        uint32_t entry = 0;
        uint32_t bag = 0, slot = 0;
    };
    // One durability call of E7 round 2, with the arguments the binding sent.
    struct FakeDurability
    {
        std::string op;
        uint64_t itemLow = 0;
        double percent = 0;
        int64_t points = 0;
        int64_t slot = 0;
        bool inventory = true;
        int64_t pos = 0;
        bool takeCost = true;
        double discount = 1.0;
    };
    struct FakePlayer
    {
        int64_t money = 0;
        uint32_t xp = 0, xpNext = 0;
        double restBonus = 0;
        bool rested = false;
        uint32_t honor = 0;
        std::map<uint32_t, int32_t> reputation;
        std::map<uint32_t, FakeSkill> skills;
        std::set<uint32_t> spellCooldowns;               // HasSpellCooldown
        std::map<uint32_t, uint32_t> spellCooldownDelay; // GetSpellCooldownDelay, milliseconds
        std::vector<std::pair<uint32_t, bool>> resetSpells;   // ResetSpellCooldown(spellId, update)
        std::vector<std::pair<uint32_t, bool>> resetTypes;    // ResetTypeCooldowns(category, update)
        int resetAll = 0;
        std::vector<FakeItem> items;
        uint64_t nextItemGuid = 9000;
        std::set<uint32_t> equipped;                     // equipment slots with an item
        std::set<uint32_t> rewarded;                     // HasReceivedQuestReward / GetQuestRewardStatus
        std::set<uint32_t> activeQuests;
        std::set<uint32_t> completed;
        bool canReward = false, canEquip = false, canUse = false;
        bool hasQuestForGO = false, hasQuestForItem = false;
        uint16_t reqKillCount = 0;
        uint32_t repRank = 0;
        bool inGroup = false, inGuild = false, flying = false, moving = false, falling = false, inWater = false;
        bool taxiCheat = false, inBg = false, afk = false, dnd = false, immune = false, envImmune = false, ffa = false;
        uint32_t bgId = 0, bgTypeId = 0;
        std::string guildName, ip = "ip-default";
        int dbLocale = 0;
        uint32_t latency = 0, totalPlayed = 0, levelPlayed = 0;
        bool loggedOut = false, logoutSave = true;
        bool bindSet = false;
        double bindX = 0, bindY = 0, bindZ = 0;
        uint32_t bindMap = 0, bindArea = 0;
        bool repaired = false, repairTakeCost = false, repairGuildBank = false;
        double repairDiscount = 0;
        bool pvp = false;
        // E7 long tail, round 2: the documented reads.
        bool canBlock = false, canCompleteRepeatable = false, canFly = false, canParry = false, canShare = false;
        bool canSpeak = true;
        uint8_t partyResult = 0;
        uint32_t championing = 0;
        int dbcLocale = 0, difficulty = 0;
        uint8_t drunk = 0, guildRank = 0, subGroup = 0, originalSubGroup = 0;
        double healthBonus = 0, manaBonus = 0;
        uint32_t inGameTime = 0, mailCount = 0, recruiterId = 0, atLoginFlags = 0, shieldBlockValue = 0;
        uint32_t xpRestBonus = 0;                        // GetXPRestBonus(answer for any xp)
        int32_t questLevel = 0;
        bool pendingBind = false, inArena = false, inBgQueue = false, acceptWhispers = false, isRecruiter = false;
        bool gmChat = false, gmVisible = true, neverVisible = false, outdoorPvp = false;
        uint32_t resetTalentsCost = 0;
        uint64_t groupInvite = 0, originalGroup = 0;     // a group handle's low guid, 0 = none
        uint64_t nearbyGameObject = 0, nextRaidMember = 0, selectedPlayer = 0, selectedUnitCreature = 0;
        std::set<std::pair<uint32_t, uint8_t>> talents;  // HasTalent(spellId, spec)
        std::set<uint32_t> titles;                       // HasTitle(bitIndex)
        std::set<uint64_t> groupMates, raidMates, visibleFor;  // IsInSameGroupWith / IsInSameRaidWith / IsGroupVisibleFor
        // E7 long tail, round 2: the writes.
        bool gameMaster = false, pvpDeath = false, bonesSpawned = false, killed = false;
        uint8_t gender = 0, factionRace = 0;
        bool resetTalentsNoCost = false, resetTalentsResult = true;
        int resetTalentsCalls = 0;
        std::vector<FakeDurability> durabilities;
        // The action ops with nothing to read back: the op and its arguments, in order.
        std::vector<std::pair<std::string, std::vector<fl::EventValue>>> actions;
    };
    std::map<std::pair<uint64_t, uint64_t>, FakePlayer> playerStates;
    FakePlayer& playerState(fl::HandleData const& h) { return playerStates[{h.guid.hi, h.guid.lo}]; }
    bool playerStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                         std::vector<fl::EventValue>& out);
    void saveAllPlayers() override { ++saveAllCalls; }
    void kickPlayer(fl::HandleData const& p) override { kicked.push_back(p); }
    int banPlayer(int mode, std::string const& target, uint32_t duration, std::string const& reason,
                  std::string const& who) override;
    std::vector<uint32_t> sendMail(std::string const& subject, std::string const& text, uint32_t receiver,
                                   uint32_t sender, uint32_t stationery, uint32_t delay, uint32_t money, uint32_t cod,
                                   std::vector<std::pair<uint32_t, uint32_t>> const& items) override;
    void addVendorItem(uint32_t entry, uint32_t item, int32_t maxCount, uint32_t incrTime,
                       uint32_t extendedCost) override;
    void vendorRemoveItem(uint32_t entry, uint32_t item) override;
    void vendorRemoveAllItems(uint32_t entry) override;
    std::vector<uint32_t> activeGameEvents() override { return activeEvents; }
    bool isGameEventActive(uint32_t id) override;
    void startGameEvent(uint32_t id, bool force) override { startedEvents.emplace_back(id, force); }
    void stopGameEvent(uint32_t id, bool force) override { stoppedEvents.emplace_back(id, force); }

    // ---- Long tail: Spell, Aura and SpellInfo (round 1) ----
    // The transient spell and aura a running Lua call owns. The real host clears its store in endLuaCall, so the fake
    // does too: a handle kept from an earlier call finds nothing and the binding raises the family's stale-handle error.
    struct FakeSpell
    {
        uint32_t id = 0;                                  // the SpellInfo id the handle names
        bool hasCaster = false;
        fl::HandleData caster;
        bool hasOriginalCaster = false;
        fl::HandleData originalCaster;
        int32_t castTime = 0;
        uint64_t delayStart = 0, delayMoment = 0;
        int32_t duration = 0;
        std::vector<std::pair<int32_t, int32_t>> powerCost;   // (power type, amount)
        uint32_t state = 0;
        bool hasTarget = false;
        fl::HandleData target;
        bool hasDest = false;
        double destX = 0, destY = 0, destZ = 0;
        bool autoRepeat = false, positive = false, triggered = false, channelActive = false;
        bool ignoringCooldowns = false, interruptable = false, needSendToClient = false, procDisabled = false;
        bool deletable = false, autoActionReset = false, executedCurrently = false, referencedFromCurrent = false;
        uint32_t triggeredBySpellId = 0;                  // IsTriggeredByAura(spellInfo)
        uint32_t runeState = 0;
        uint32_t effectCount = 0;                         // the core's own effect list length for this spell
        int32_t damage[32] = {};
        int64_t unitTargets[32] = {}, goTargets[32] = {}, itemTargets[32] = {};
        // The action ops (Cast, Finish, ...) with their arguments, in order.
        std::vector<std::pair<std::string, std::vector<fl::EventValue>>> actions;
    };
    std::map<uint32_t, FakeSpell> spells;                 // by the handle's instanceId
    std::vector<std::string> spellActions;                // every Spell action op, in order (survives the call's clear)
    FakeSpell& spellState(fl::HandleData const& h) { return spells[h.instanceId]; }

    struct FakeAura
    {
        uint32_t id = 0;
        bool hasCaster = false;
        fl::HandleData caster;
        fl::Guid casterGuid;
        uint32_t casterLevel = 0;
        int32_t duration = 0, maxDuration = 0;
        bool hasOwner = false;
        fl::HandleData owner;
        uint32_t stackAmount = 1;
        uint32_t maxStackAmount = 0;                      // Aura::CalcMaxStackAmount (the spell's own limit)
        std::vector<std::string> actions;                 // "remove", "refresh"
    };
    std::map<uint32_t, FakeAura> auras;                   // by the handle's instanceId
    std::vector<std::string> auraActions;                 // every Aura action op, in order
    FakeAura& auraState(fl::HandleData const& h) { return auras[h.instanceId]; }

    // One documented spell entry. Every member is a value a test can set, so a getter's answer proves the binding and
    // the op read the field the docs name (a distinct value per field catches a swapped mapping).
    struct FakeSpellInfo
    {
        uint32_t id = 0;
        int64_t category = 0, dispel = 0, mechanic = 0, schoolMask = 0, attributes = 0, attributesCu = 0;
        int64_t attributesEx = 0, attributesEx2 = 0, attributesEx3 = 0, attributesEx4 = 0, attributesEx5 = 0;
        int64_t attributesEx6 = 0, attributesEx7 = 0;
        int64_t duration = 0, maxDuration = 0, recoveryTime = 0, categoryRecoveryTime = 0;
        int64_t stackAmount = 0, maxAffectedTargets = 0, spellFamilyName = 0, explicitTargetMask = 0;
        int64_t auraState = 0, spellSpecific = 0;
        int64_t baseLevel = 0, spellLevel = 0, maxLevel = 0, maxTargetLevel = 0, dmgClass = 0;
        int64_t powerType = 0, manaCost = 0, manaCostPerLevel = 0, manaPerSecond = 0;
        int64_t procChance = 0, procCharges = 0, procFlags = 0, rank = 0, targets = 0, targetCreatureType = 0;
        int64_t interruptFlags = 0, auraInterruptFlags = 0, channelInterruptFlags = 0;
        int64_t startRecoveryCategory = 0, startRecoveryTime = 0, preventionType = 0, attackType = 0;
        int64_t activeIconID = 0, spellIconID = 0, requiresSpellFocus = 0, areaGroupId = 0;
        int64_t casterAuraSpell = 0, casterAuraState = 0, casterAuraStateNot = 0;
        int64_t targetAuraSpell = 0, targetAuraState = 0, targetAuraStateNot = 0;
        int64_t facingCasterFlags = 0, equippedItemClass = 0;
        int64_t equippedItemSubClassMask = 0, equippedItemInventoryTypeMask = 0;
        int64_t stances = 0, stancesNot = 0, calcCastTime = 0;
        double maxRange = 0, minRange = 0, speed = 0;
        int64_t spellFamilyFlags[3] = {};
        int64_t totem[2] = {}, totemCategory[2] = {};
        int64_t reagent[8] = {}, reagentCount[8] = {};
        std::set<int64_t> effects;                         // HasEffect(effect)
        std::set<int64_t> auraTypes;                       // HasAura(aura)
        std::set<uint32_t> negativeEffects;                // IsPositiveEffect(effIndex) is the complement
        std::set<uint32_t> triggeredBy, sameRankOf, differentRankOf, highRankOf, dispels, pierces, providesImmunity;
        bool hasAreaAuraEffect = false, hasOnlyDamageEffects = false, hasInitialAggro = false;
        bool passive = false, autocastable = false, stackableWithRanks = false, channeled = false, positive = false;
        bool affectingArea = false, targetingArea = false, needsExplicitUnitTarget = false;
        bool deathPersistent = false, requiringDeadTarget = false, allowingDeadTarget = false;
        bool canBeUsedInCombat = false, cooldownStartedOnEvent = false, ranked = false, singleTarget = false;
        bool multiSlotAura = false, groupBuff = false, moveAllowedChannel = false, nextMeleeSwingSpell = false;
        bool rangedWeaponSpell = false, autoRepeatRangedSpell = false, affectedBySpellMods = false;

        // Extended SpellInfo fields and per-effect table.
        int64_t allEffectsMechanicMask = 0, allowedMechanicMask = 0;
        int64_t excludeCasterAuraSpell = 0, excludeTargetAuraSpell = 0;
        int64_t manaCostPercentage = 0, maxTicks = 0;
        int64_t spellVisual[2] = {};
        int64_t diminishingGroup = 0, diminishingGroupType = 0, diminishingLimitDuration = 0, diminishingMaxLevel = 0;
        bool abilityLearnedWithProfession = false, breakingStealth = false, explicitDiscovery = false;
        bool lootCrafting = false, passiveStackableWithRanks = false, primaryProfession = false;
        bool primaryProfessionFirstRank = false, profession = false, professionOrRiding = false;
        bool stackableOnOneSlotWithDifferentCasters = false, needsComboPoints = false;
        std::set<uint32_t> skillTypes;
        uint32_t affectedFamilyName = 0;
        uint32_t affectedFlags[3] = {};
        std::set<uint32_t> exclusiveWith, exclusivePerCasterWith;
        std::set<uint64_t> fitItems, checkTargetCreatureTypes, cancelsAuraEffects;
        int64_t checkTargetResult = 0;
        struct FakeTargetInfo
        {
            int64_t checkType = 0, directionType = 0, objectType = 0, referenceType = 0, selectionCategory = 0, target = 0;
            double directionAngle = 0.0;
            bool isArea = false;
        };
        struct FakeEffect
        {
            bool isEffect = false, isAura = false, isAreaAuraEffect = false, isTargetingArea = false;
            bool isUnitOwnedAuraEffect = false, hasRadius = false;
            int64_t auraType = 0, effectType = 0, amplitude = 0, basePoints = 0, calcValue = 0, chainTarget = 0;
            int64_t dieSides = 0;
            int64_t implicitTargetType = 0, effectIndex = 0, itemType = 0, mechanic = 0, mechanicMask = 0;
            int64_t miscValue = 0, miscValueB = 0, providedTargetMask = 0, triggerSpell = 0, usedTargetObjectType = 0;
            int64_t spellClassMask[3] = {};
            double bonusMultiplier = 0.0, calcRadius = 0.0, damageMultiplier = 0.0, pointsPerComboPoint = 0.0;
            double realPointsPerLevel = 0.0, valueMultiplier = 0.0;
            FakeTargetInfo targetA, targetB;
        };
        std::vector<FakeEffect> effectSlots;
    };
    std::map<uint32_t, FakeSpellInfo> spellInfos;         // by spell id
    FakeSpellInfo& spellInfo(uint32_t id) { FakeSpellInfo& s = spellInfos[id]; s.id = id; return s; }
    bool spellExists(uint32_t id) override { return spellInfos.count(id) != 0; }

    struct FakeAuraEffect
    {
        uint32_t id = 0;
        uint32_t effIndex = 0;
        int64_t auraType = 0;
        int32_t amount = 0, baseAmount = 0, periodicTimer = 0;
        uint32_t amplitude = 0, tickNumber = 0, totalTicks = 0, remainingTicks = 0;
        bool canBeRecalculated = true;
        bool hasCaster = false;
        fl::HandleData caster;
        fl::Guid casterGuid;
        bool hasBase = false;
        uint32_t baseAuraId = 0;
        std::vector<fl::HandleData> targetList;
        int32_t calculatedAmount = 0;
        std::vector<std::string> actions;
    };
    std::map<uint32_t, FakeAuraEffect> auraEffects;
    std::vector<std::string> auraEffectActions;

    struct FakeProcInfo
    {
        bool hasActor = false;
        fl::HandleData actor;
        bool hasActionTarget = false;
        fl::HandleData actionTarget;
        bool hasMap = false;
        fl::HandleData map;
        bool hasProcSpell = false;
        fl::HandleData procSpell;
        uint32_t spellId = 0;
        bool hasDamage = false, hasHeal = false;
        uint32_t attackType = 0, block = 0, damage = 0, damageAbsorb = 0, damageType = 0;
        uint32_t effectiveHeal = 0, heal = 0, healAbsorb = 0, hitMask = 0, resist = 0;
        uint32_t schoolMask = 0, spellPhaseMask = 0, spellTypeMask = 0, typeMask = 0;
        std::vector<std::string> actions;
    };
    std::map<uint32_t, FakeProcInfo> procInfos;
    std::vector<std::string> procInfoActions;

    bool spellStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                        std::vector<fl::EventValue>& out);

    // ---- E3: asynchronous SQL ----
    struct AsyncRequest
    {
        uint64_t id = 0;
        std::string db, sql;
        size_t maxRows = 0, maxBytes = 0;
    };
    std::vector<AsyncRequest> asyncRequests;
    std::deque<std::pair<uint64_t, fl::DbRows>> asyncReady;
    std::map<uint64_t, std::string> asyncReadyErrors;
    std::vector<uint64_t> asyncCancelled;
    bool failAsync = false;
    bool rawQueryAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                       std::string& error) override;
    bool takeRawQueryResult(uint64_t& id, fl::DbRows& out, std::string& error) override;
    // Like LuaDb::cancelAsync: a finished result of that id is dropped, and a result that arrives later is ignored.
    void cancelRawQuery(uint64_t id) override;
    void completeAsync(uint64_t id, fl::DbRows const& rows)
    {
        if (std::find(asyncCancelled.begin(), asyncCancelled.end(), id) == asyncCancelled.end())
            asyncReady.emplace_back(id, rows);
    }
    void completeAsyncError(uint64_t id, std::string const& error) { asyncReadyErrors[id] = error; }

    // ---- E3: ForeverData ----
    std::vector<fl::DataRow> dataRows;   // what dataLoad() answers
    bool dataLoadOk = true;
    std::string dataLoadError = "database down";
    int dataLoadCalls = 0;
    std::map<std::pair<std::string, std::string>, std::string> storedData;   // the "database": confirmed writes
    std::set<std::pair<std::string, std::string>> erasedData;
    int dataStoreCalls = 0;
    int dataEraseCalls = 0;
    int dataStoreFailures = 0;   // the next N writes are finished with a failure (the database refused them)
    bool dataHold = false;       // accepted writes stay pending until finishDataWrites(): the database has not answered
    bool dataQueueFull = false;  // dataWrite() refuses (the host's queue is full)
    bool dataCompleteOnPoll = false;   // takeDataWrites() lets the database answer every pending write first
    size_t dataQueueLimit = 0;   // when non-zero: at most this many writes pending at once
    std::vector<fl::DataWrite> dataPending;
    std::vector<fl::DataWriteResult> dataDone;
    std::vector<std::string> dataWriteOrder;   // "script/key" of every accepted write, in order
    bool dataLoad(std::vector<fl::DataRow>& out, std::string& error) override;
    bool dataWrite(fl::DataWrite const& w) override;
    void takeDataWrites(std::vector<fl::DataWriteResult>& out) override;
    // The database answers every pending write (the failures first, as configured).
    void finishDataWrites();

    // ---- review round: world-only reads, instance maps and command names ----
    bool worldReadOk = true;
    std::set<uint32_t> instancedMaps;
    std::set<std::string> coreCommandRoots;   // lower-case roots of the "core" command table
    bool worldOnlyReadOk() override { return worldReadOk; }
    bool mapIsInstanced(uint32_t mapId) override { return instancedMaps.count(mapId) != 0; }
    bool commandNameReserved(std::string const& name) override
    {
        for (auto const& r : coreCommandRoots)
            if (r.compare(0, name.size(), name) == 0)
                return true;
        return false;
    }

    // ---- E3: commands and the end of a Lua call ----
    std::atomic<int> commandsChangedCalls{0};
    std::atomic<int> endLuaCalls{0};
    std::atomic<bool> transientActive{false};   // a Spell/Aura answer of onGameCall is live while this is true
    struct SentPacket
    {
        fl::HandleData target;
        uint32_t opcode = 0;
        std::vector<uint8_t> data;
        bool ignorePlayersInBg = false;
        uint64_t ignore = 0;
        uint32_t rank = 0;   // SendPacketToRanked only
        uint32_t toRank = 0; // 1 = the ranked form was used
    };
    std::vector<SentPacket> packets;
    bool sendPacket(fl::HandleData const& target, uint32_t opcode, std::vector<uint8_t> const& data,
                    bool ignorePlayersInBg, uint64_t ignore) override
    {
        packets.push_back(SentPacket{target, opcode, data, ignorePlayersInBg, ignore, 0, 0});
        return true;
    }
    bool sendPacketToRanked(fl::HandleData const& target, uint32_t opcode, std::vector<uint8_t> const& data,
                            uint32_t rank) override
    {
        packets.push_back(SentPacket{target, opcode, data, false, 0, rank, 1});
        return true;
    }
    void commandsChanged() override { ++commandsChangedCalls; }
    void endLuaCall() override
    {
        ++endLuaCalls;
        transientActive = false;
        // The real host's store of the running call (src/host/EluWorld.cpp): a handle kept past its call finds nothing.
        spells.clear();
        auras.clear();
        auraEffects.clear();
        procInfos.clear();
    }

    // ---- GM bridge ----
    std::vector<std::string> gmLines;                                      // every line the bridge ran
    std::function<void()> onGm;
    bool runGmLine(std::string const& line, std::vector<std::string>& out) override;

    uint64_t offset = 0;
    bool frozen = false;   // the clock moves only with advance()
    uint64_t tickMs = 0;   // frozen clock only: every read of the clock moves it on by this many ms (a deterministic busy host)
    std::atomic<size_t> lookupCalls{0};                                                // every lookup(): the tests prove the access check came first
    bool throwOnLookup = false;                                            // lookup() throws std::runtime_error("lookup exploded")
    size_t bigCellBytes = 0;                                               // select() answers one row with a cell this big
    std::vector<std::pair<fl::LogLevel, std::string>> logs;

private:
    // The database side (selects, async queries, ForeverData writes) is called from map threads too: submissions are
    // thread-safe on the real host (LuaDb's queue locks, the select relay), so the fake locks them as well.
    std::recursive_mutex _dbMutex;
    mutable std::mutex _mutex;   // the test threads (E4: two map states at once) may log and call the game together
    std::map<uint64_t, fl::DbRows> _dbResults;
    std::map<uint64_t, std::string> _dbErrors;
    struct Obj
    {
        fl::ObjectInfo info;
        flh::MapKey current;
    };
    using ObjKey = std::tuple<int, uint64_t, uint64_t>;
    static ObjKey keyOf(fl::HandleData const& h) { return {static_cast<int>(h.kind), h.guid.hi, h.guid.lo}; }
    std::map<ObjKey, Obj> _objects;
    std::chrono::steady_clock::time_point _t0;
};

// A temporary script folder, removed at the end.
class TempDir
{
public:
    TempDir();
    ~TempDir();
    std::string path() const { return _path.string(); }
    void write(std::string const& relName, std::string const& content);
    void remove(std::string const& relName);
    std::filesystem::path _path;
};

fl::Config testConfig(std::string const& scriptPath);
std::shared_ptr<fl::SchemaSnapshot> standardSchema();
}  // namespace fltest
