// TortuLua engine: one Lua state with a sandbox, call budgets, a memory cap, error isolation, a script folder
// loader, timers, an event queue, handle objects and a guarded SELECT. It knows the world only through Host.
//
// Threading: the state lives on the thread that called start() (the "owner", the world thread in the server).
// post() and listening() are safe from any thread. emit() runs the handlers at once on the owner thread and queues the
// event anywhere else. Every other call must come from the owner thread and is refused otherwise.
#pragma once

#include "engine/CommandTail.h"
#include "engine/Elu.h"
#include "engine/EventQueue.h"
#include "engine/Host.h"
#include "engine/SqlGuard.h"
#include "engine/Timers.h"
#include "engine/Value.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace fl
{
struct MemState
{
    size_t used = 0;
    size_t cap = 0;
    uint64_t failures = 0;
};

// One Lua call's limits. Each field 0 means "no limit of that kind": 0 ms is no wall-clock budget, 0 instructions is no
// instruction budget, both 0 is no hook at all (the Eluna mode default).
// E4/E5 review (H2): the finished asynchronous queries of one state, filled by the world state's router and drained by
// the state itself on its own thread.
struct AsyncInbox
{
    struct Done
    {
        uint64_t id = 0;
        DbRows rows;
        std::string error;
    };
    std::mutex mutex;
    std::deque<Done> done;
};

struct Budget
{
    uint64_t maxMs = 20;
    uint64_t maxInstr = 10'000'000;
};

// The key of a RegisterUniqueCreatureEvent handler: the creature's full guid and its instance id. `anyHigh` is the
// legacy integer form (a low counter with no high half): it matches any creature with that low guid in the instance.
struct UniqueKey
{
    uint64_t hi = 0;
    uint64_t lo = 0;
    uint32_t instance = 0;
    bool anyHigh = false;
};

enum class EngineMode
{
    Test,    // the ForeverLua toolkit: Event/Timer/Cmd/World/Db/Json/Gm and the sandbox
    Eluna    // the Eluna-compatible API (E0/E1): classes over the handles, Register* events, raw SQL
};

// Core hooks of the maintainers' API specification, section 6 that this core tree has (PRs Karrz1/TrinityCore #375-#379). The
// values are the bits cmake/DetectCoreHooks.cmake writes into FL_CORE_HOOK_MASK; the host copies the mask into Config.
// The engine uses it only for its "needs core hook Cn" lines: the adapters themselves are compiled in or out.
enum class CoreHook : unsigned
{
    AllCreature = 1u << 0,      // C1: AllCreatureScript::OnCreatureAddWorld/OnCreatureRemoveWorld (notify only)
    AllGameObject = 1u << 1,    // C2: AllGameObjectScript::OnGameObjectAddWorld/OnGameObjectRemoveWorld (notify only)
    GossipSelect = 1u << 2,     // C3: PlayerScript::OnGossipSelect for player and item sources
    ItemUse = 1u << 3,          // C3: PlayerScript::OnItemUse for every item
    PlayerCommand = 1u << 4,    // C4: PlayerScript::OnCommand before the core's permission checks
    CreatureEvents = 1u << 5,   // C1: OnCreatureEngage/LeaveCombat/JustDied/Appear, for creatures without a ScriptName (notify only)
    GameObjectUse = 1u << 6     // C2: AllGameObjectScript::OnGameObjectUse (notify only)
};

struct Config
{
    EngineMode mode = EngineMode::Test;
    std::string scriptPath;
    std::vector<std::string> commandRoots;                    // forwarded roots, e.g. {"tt"}
    std::vector<std::string> bridgeDeny = {"lua", "server", "account", "rbac", "bnetaccount", "pdump", "ban"};
    std::vector<std::string> requirePath;                     // eluna mode: extra folders require() resolves in

    size_t memoryCap = 128u * 1024u * 1024u;
    Budget eventBudget{20, 10'000'000};                       // one event handler or timer call
    Budget commandBudget{250, 100'000'000};                   // one command, and server.stop
    Budget loadBudget{2000, 500'000'000};                     // one script file at load time
    uint32_t tickBudgetMs = 25;                               // most Lua time per tick()
    uint64_t dbTimeoutMs = 2000;                              // client-side timeout of one Db.select (MariaDB ignores the hint)
    size_t maxPendingSelects = 32;                            // Db.select requests in flight per state
    uint64_t shutdownBudgetMs = 5000;                         // most time finishPending() may spend (server.stop included)
    size_t queueCapacity = 10000;
    int breaksToDisable = 3;                                  // budget breaks inside the window disable a handler/timer
    uint64_t breakWindowMs = 10 * 60 * 1000;
    uint64_t minTimerMs = 50;
    size_t maxTimers = 10000;
    size_t maxHandlers = 1000;
    size_t maxReplyLines = 100;
    size_t maxReplyLineBytes = 4000;                          // one ctx:reply line (a TTJSON line is up to 3500)
    size_t maxResultBytes = 1u << 20;                         // string bytes of one Db.select result

    // Libraries that are NOT opened unless asked for (all false by default: the sandbox).
    bool allowIo = false;
    bool allowOs = false;
    bool allowDebug = false;
    bool allowPackage = false;
    // Eluna mode only: opens io/os/debug/package (and keeps dofile/loadfile/string.dump). Eluna mode trusts its
    // scripts; the memory cap still applies.
    bool openStdLibs = false;

    // ---- Eluna mode settings (E0) ----
    unsigned coreHooks = 0;             // CoreHook bits of the core this module is built into (host sets it; 0 = none)
    uint32_t mapStates = 0;             // E4: per-map states (0 = one world state)
    int32_t stateMapId = -1;            // E4: the map of this state, -1 for the world state (GetStateMapId)
    uint32_t stateInstanceId = 0;       // E4: the instance of this state (GetStateInstanceId)
    uint64_t slowHandlerMs = 5;         // E3 profiling logs a handler slower than this
    uint64_t syncDbTimeoutMs = 5000;    // MYSQL_OPT_READ_TIMEOUT of the module's own connections
    uint64_t dataFlushMs = 5000;        // E3 ForeverData write-behind
    uint64_t dataShutdownMs = 10000;    // the most time the last ForeverData flush at shutdown may take
};

inline bool hasCoreHook(Config const& cfg, CoreHook hook)
{
    return (cfg.coreHooks & static_cast<unsigned>(hook)) != 0;
}

// While an item USE handler runs (the global item-use hook or the item's ScriptName), the core still holds the Item it
// is using: it reads it again to cast the item's spell. A Lua handler therefore may not destroy that item inside the
// hook: Player:RemoveItem on it (by object, or by entry when the entry is the item's) is refused and logged. A script
// that wants the item gone does it from a timed event (CreateLuaEvent), which runs after the core is done with it.
// Per thread, because maps run on their own threads; scopes nest.
class ItemUseScope
{
public:
    ItemUseScope(Guid item, uint32_t entry);
    ~ItemUseScope();
    ItemUseScope(ItemUseScope const&) = delete;
    ItemUseScope& operator=(ItemUseScope const&) = delete;
    // True when `item` (if given) is the item being used, or `entry` (if not 0) is its entry.
    static bool blocks(Guid const* item, uint32_t entry);

private:
    Guid _item;
    uint32_t _entry;
    ItemUseScope* _prev;
};

struct CallResult
{
    bool ok = false;
    int status = 0;        // LUA_OK, LUA_ERRRUN, LUA_ERRMEM ...
    bool budget = false;   // the call was cut off by its instruction or time budget
    bool memory = false;   // the memory cap was hit (LUA_ERRMEM)
    std::string error;     // message without the traceback
    std::string result;    // tostring of the first returned value, if any
};

struct LoadReport
{
    std::vector<std::string> loaded;
    std::vector<std::string> errors;
    bool folderMissing = false;
    // reload() only: true when the new state replaced the old one. A reload with any error leaves the old state
    // (handlers, timers, commands, globals) untouched and applied = false.
    bool applied = false;
};

enum class CommandStatus
{
    Ok,
    NoHandler,   // no Lua function registered for the root
    Denied,      // the caller's security is below minSecurity (the handler did not run)
    Error,       // the handler failed (error already in `lines`)
    Refused      // wrong thread, re-entrant call, engine not running, or a malformed tail
};

struct CommandResult
{
    CommandStatus status = CommandStatus::Ok;
    std::vector<std::string> lines;
    std::string error;
};

struct EngineStatus
{
    bool running = false;
    size_t memoryUsed = 0;
    size_t memoryCap = 0;
    size_t handlers = 0;
    size_t disabledHandlers = 0;
    size_t timers = 0;
    size_t queued = 0;
    uint64_t queueDropped = 0;
    size_t elunaHandlers = 0;
    size_t errors = 0;                   // distinct errors in the ring
    uint64_t reloads = 0;
    uint64_t wrongThreadRefused = 0;
    std::vector<std::string> lastErrors;  // newest last, at most 5
};

class Engine
{
public:
    // E3 bounds: 64 KB per ForeverData value, 1 MB per script namespace (keys and values both count), at most 16384 keys
    // per namespace, 64 MB and 262144 keys in all, keys of 1..255 bytes, 32 Lua commands.
    static constexpr size_t kMaxDataValue = 64u * 1024u;
    static constexpr size_t kMaxDataScript = 1u << 20;
    static constexpr size_t kMaxDataKeysPerScript = 16384;
    static constexpr size_t kMaxDataBytes = 64u << 20;
    static constexpr size_t kMaxDataKeys = 262144;
    static constexpr size_t kMaxDataKey = 255;
    static constexpr size_t kMaxLuaCommands = 32;
    static constexpr size_t kMaxPackets = 1024;
    static constexpr size_t kMaxPacketBytes = 64u * 1024u;

    Engine(Host& host, Config config);
    ~Engine();
    Engine(Engine const&) = delete;
    Engine& operator=(Engine const&) = delete;

    // Creates the state (sandbox, APIs) and records the calling thread as the owner. Loads no script.
    bool start();
    // Closes the state; timers, handlers and queued events are dropped.
    void shutdown();
    // Loads init.lua first, then the other *.lua files of the script folder in sorted order. An error in one file is
    // reported and the next file still loads.
    LoadReport load();
    // Builds a second state on the side and loads the folder into it. Only when every file loaded without error does
    // it replace the running state (report.applied); otherwise the old state stays exactly as it was and the report
    // lists the errors. Refused (one error) on the wrong thread or from inside Lua. Without a running state it is
    // start() + load().
    // `beforeAdopt` runs with the OLD state, only when the candidate loaded cleanly and is about to replace it (the host's
    // LUA_STATE_CLOSE event): a failed reload never tells the surviving state that it closed.
    LoadReport reload(std::function<void()> const& beforeAdopt = nullptr);
    bool running() const { return _running.load(std::memory_order_acquire); }
    bool onOwnerThread() const;
    // The Lua heap this state's allocator has handed out (E4: every state has its own memory cap and its own heap).
    size_t memoryUsed() const { return _mem->used; }

    // ---- events ----
    // True when at least one handler is bound (cheap, any thread): the hooks check this first.
    bool listening() const { return _listenerTotal.load(std::memory_order_relaxed) > 0; }
    bool listening(std::string const& event) const;
    // Queues an event from any thread (counts a drop when full). Events nobody listens for are not queued.
    bool post(QueuedEvent&& event);
    // Starts a new per-tick Lua window: the host calls it at the start of a world tick. Every emit() in that tick
    // shares one tickBudgetMs deadline; past it an event is queued for the next tick instead of running (N4).
    void beginTick();
    // Owner thread and not inside Lua: runs the handlers now (inside the shared per-tick window). Otherwise: post().
    void emit(QueuedEvent&& event);
    // Runs due timers and queued events, at most tickBudgetMs of wall clock (always at least one item). Returns the
    // number of Lua calls made.
    size_t tick();
    // Drains what is still queued and pending (shutdown, server.stop included): no tick cap, each handler keeps its
    // own budget, and the whole drain is bounded by Config::shutdownBudgetMs. Owner thread only. Returns the number of
    // Lua calls made.
    size_t finishPending();
    // Our bot API (B1): the world thread takes the provider's once-per-tick snapshot, diffs it and emits the bot events
    // in this (world) state. Called from the host's OnUpdate after its deferred work; a no-op without a provider.
    // B2: it also hands this tick's queued orders over and polls the open tickets.
    void pumpBots();
    // B2: this state's own id in the bot order hub: its tickets, its open-ticket count and its per-script rate buckets.
    // A reload hands the adopted state's id on and drops the old scripts' tickets (adoptState); a closed state drops them.
    uint64_t orderDomain() const { return _orderDomain; }

    // ---- commands ----
    CommandResult runCommand(std::string const& root, std::string const& tail, int security,
                             std::optional<HandleData> player = std::nullopt);

    // Extra bindings (round 2: World, Map, Unit ...) registered into every new state, so a reload registers them again.
    // The function runs inside a protected call; it may add to the global tables the engine already made.
    void setBinder(std::function<void(lua_State*)> binder) { _binder = std::move(binder); }

    // ---- trusted C++ callers (the host, the tests): run a text chunk under a budget ----
    CallResult runChunk(std::string const& code, std::string const& chunkName = "=chunk", bool commandBudget = false);

    // `.lua run <name>`: runs <scriptPath>/<name>.lua once more (command budget). Refused off the owner thread or from Lua.
    CallResult runScript(std::string const& name);

    EngineStatus status() const;
    std::string statusText() const;

    Host& host() { return _host; }
    Config const& config() const { return _cfg; }
    SqlGuard& sql() { return _sql; }
    lua_State* state() { return _L; }
    static Engine* from(lua_State* L);

    // ---- E4: per-map states ----
    // The map this state belongs to (-1, 0 for the world state), as GetStateMapId/GetStateInstanceId answer it.
    int32_t stateMapId() const { return _cfg.stateMapId; }
    uint32_t stateInstanceId() const { return _cfg.stateInstanceId; }
    // A map state is created on one map update and ticked on the updates after it, which may run on another thread of the
    // core's pool: the host claims the current thread before load/reload/tick (the Lua mutex makes it exclusive).
    void claimOwnerThread();
    // E4 (H7): a map state's close from any thread (the world thread in DestroyMap, or the map's own): under the Lua lock,
    // claims the thread, fires LUA_STATE_CLOSE in this state and frees it. A call from inside this state's own Lua call is
    // refused (logged); the state is then freed by its destructor.
    void closeAsMapState();
    // E4: ForeverData of a map state lives in the world state (`owner`), the one store, loader and writer of the set.
    void useDataOwner(Engine* owner) { _dataOwner = owner; }

    // ---- used by the Lua bindings (src/engine/Api.cpp); call them only from inside a protected call ----
    // ---- E5: owned WorldPacket values (they never resolve through the host) ----
    struct PacketValue
    {
        uint32_t opcode = 0;
        std::vector<uint8_t> bytes;
        size_t readPos = 0;
        size_t charged = 0;   // bytes counted against the state's memory cap (M3)
    };
    // CreatePacket: a new, empty packet value with `reserve` bytes reserved (M4: the core's WorldPacket(opcode, res)) and
    // its handle (kind Packet, guid.lo = the local id). A state keeps at most kMaxPackets values; the oldest is dropped.
    // Null and `error` when the state's memory cap cannot hold the reserve.
    std::optional<HandleData> addPacket(uint32_t opcode, size_t reserve, std::string& error);
    // Before a Write*: room for `n` more bytes (64 KB per packet, and the state's memory cap). False and `error` if not.
    bool packetGrow(PacketValue& p, size_t n, std::string& error);
    // E5 packet events (H4): copies the bytes into a packet value of this state, runs the handlers of (opcode, event)
    // with (packet, player) and frees the copy, all under this state's Lua lock. Callable from any thread.
    EventResult emitPacketEvent(uint32_t opcode, uint32_t event, std::vector<uint8_t> const& bytes,
                                EventValue const& player);
    size_t packetCount() const;
    // The packet's Lua value was collected (its __gc): the bytes and their memory charge are freed.
    void dropPacket(uint64_t id);
    PacketValue* packet(uint64_t id);
    PacketValue const* packet(uint64_t id) const;
    struct HandlerFilter
    {
        bool hasEntry = false;
        uint32_t entry = 0;
        bool bots = false;
    };
    // Takes ownership of `ref` (a registry reference). Returns 0 when the handler limit is reached.
    uint64_t addHandler(std::string const& event, int ref, HandlerFilter const& filter);
    bool removeHandler(uint64_t id);
    // Timer ids: 0 when the timer limit is reached.
    uint64_t addTimer(int ref, uint64_t delayMs, uint64_t intervalMs, uint64_t count);
    bool cancelTimer(uint64_t id);
    bool registerCommand(std::string const& root, int ref, int minSecurity);
    void appendReply(std::string const& text);
    std::string resolveModule(std::string const& name, std::string& error) const;  // script-folder path of a module
    // Like resolveModule(), and reports the path inside its root (the Lua chunk name): eluna mode resolves "a.b" to
    // a/b.lua then a/b/init.lua under ScriptPath and RequirePath.
    std::string resolveModuleEx(std::string const& name, std::string* rel, std::string& error) const;
    void logLua(LogLevel level, std::string const& text);
    int bridgeDepth() const { return _bridgeDepth; }
    void enterBridge() { ++_bridgeDepth; }
    void leaveBridge() { --_bridgeDepth; }
    std::set<std::string> const& bridgeDeny() const { return _bridgeDeny; }
    std::set<std::string> const& forwardedRoots() const { return _forwarded; }
    bool inLua() const { return _callDepth > 0; }

    // Db.select (async): registers `ref` (the Lua callback, owned by the engine from now on) and starts the query.
    // False and an error text when the host refused or too many requests are in flight; the ref is released then.
    bool startSelect(std::string const& db, std::string const& sql, size_t maxRows, int ref, std::string& error);
    // Collects finished selects and fires their client-side timeouts (owner thread; called from tick()).
    void pollDb(uint64_t deadlineMs = 0);

    // Count hook (budget): public only so the C hook function can reach it.
    void onHook(lua_State* L, lua_Debug* ar);

    // ---- Eluna registry (E0/E1) ----
    // Register*Event: adds one handler for (family, entry, event). `unique` keys it by one creature (guid + instance)
    // instead of the entry (RegisterUniqueCreatureEvent). Takes ownership of `ref`; 0 when the handler limit is reached.
    uint64_t addEluHandler(EluKind kind, uint32_t entry, int ref, uint32_t event, uint32_t shots,
                           std::optional<UniqueKey> unique = std::nullopt, std::string where = {});
    // Safe from inside a running handler (itself, a later one, or all of them): a dispatch walks a snapshot of ids and
    // looks every id up again before it runs it, so a removed handler is skipped and never touched afterwards.
    bool removeEluHandler(uint64_t id);
    // Clear*Events: entry nullopt = all entries, event nullopt = all events.
    size_t clearEluHandlers(EluKind kind, std::optional<uint32_t> entry, std::optional<uint32_t> event);
    // ClearUniqueCreatureEvents: removes the handlers keyed by one creature.
    size_t clearEluUniqueHandlers(EluKind kind, UniqueKey const& key, std::optional<uint32_t> event);
    // E3 profiling: one row per registered handler, the time spent in it and the script:line that registered it.
    std::vector<HandlerProfile> eluProfile() const;
    void resetEluProfile();
    // Lock-free (any thread): an immutable snapshot of what is registered, replaced on every registration change. The
    // hooks on map threads ask this for every damage tick and AI update; the answer may say "yes" for a key that was just
    // removed (the emit then finds nothing), never "no" for a registered one.
    bool listeningEluna(EluKind kind, uint32_t entry, uint32_t event) const;
    // RegisterUniqueCreatureEvent handlers (keyed by the creature's guid and instance).
    bool listeningElunaUnique(EluKind kind, Guid const& guid, uint32_t instance, uint32_t event) const;
    // Runs the handlers of one event on the calling thread, synchronously (eluna mode; the lock makes it safe).
    // `args` are the event arguments after the numeric event id.
    EventResult emitEluna(EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> const& args);
    EventResult emitElunaUnique(EluKind kind, Guid const& guid, uint32_t instance, uint32_t event,
                                std::vector<EventValue> const& args);

    // CreateLuaEvent / WorldObject:RegisterEvent. `ref` is the Lua callback (owned). delayMin == delayMax: fixed.
    uint64_t addLuaEvent(int ref, uint64_t delayMin, uint64_t delayMax, uint32_t repeats,
                         std::optional<HandleData> owner);
    // Removes one timed event; with an owner, only when that object registered it. `onlyGlobal` (the docs'
    // all_Events = false) refuses an object-bound event.
    bool removeLuaEvent(uint64_t id, std::optional<HandleData> owner = std::nullopt, bool onlyGlobal = false);
    size_t removeLuaEvents(std::optional<HandleData> owner, bool onlyGlobal = false);
    // The host's hook for "this object left the world" (player logout, creature despawn): drops its timed events at
    // once, from any thread that holds the Lua lock (eluna mode) or the owner thread.
    size_t removeLuaEventsOf(HandleData const& owner);
    // The "event <name> needs core hook Cn" log, once per key. True the first time.
    bool noteHookGap(std::string const& key);

    // ---- E3: asynchronous raw SQL (WorldDBQueryAsync & co.) ----
    // `ref` is the Lua callback (owned by the engine from now on). The host answers later; the engine calls the callback
    // with an ElunaQuery or nil on its next tick (pollAsyncQueries). False and `error` release the reference.
    bool startAsyncQuery(std::string const& db, std::string const& sql, int ref, std::string& error);
    // Collects finished queries and fires the client-side deadline (owner thread; called from tick/finishPending).
    void pollAsyncQueries(uint64_t nowMs);

    // ---- E3: ForeverData ----
    // The namespace of the Lua code that called the running C function of `L`: the loader's file path of the first Lua
    // frame (C frames such as pcall are skipped), "chunk" for code the loader did not load. Never a name a script chose.
    std::string dataNamespace(lua_State* L) const;
    // The loader tells the engine which chunk (source name "@rel") belongs to which file; load() cannot reuse such a name.
    void noteScriptChunk(std::string const& source, std::string const& rel);
    bool isScriptChunk(std::string const& source) const;
    // False while the stored values could not be read (database down, more rows than the load cap): reads answer nil and
    // writes are refused, so a script can never overwrite what it could not see. The load is retried from tick().
    bool dataAvailable() const;
    bool dataGet(std::string const& script, std::string const& key, std::string& json) const;
    bool dataSet(std::string const& script, std::string const& key, std::string const& json, std::string& error);
    bool dataRemove(std::string const& script, std::string const& key);
    std::vector<std::string> dataKeys(std::string const& script) const;
    // Hands the dirty values to the host (a write counts as done only when the host confirms it; a refused or failed one
    // stays dirty and is retried). `force` ignores the flush interval. Never blocks.
    void flushData(uint64_t nowMs, bool force);
    // Keys changed but not confirmed written yet (dirty, queued or in flight).
    size_t dataPending() const;

    // ---- E3: RegisterCommand (our extension) ----
    struct LuaCommand
    {
        std::string name;
        std::string help;
        std::string where;
        int minSecurity = 0;
        int ref = 0;
    };
    // True when the command was registered (replacing a same-name one) or refused (more than 32, bad name, wrong mode).
    bool registerLuaCommand(std::string const& name, int minSecurity, int ref, std::string const& help,
                            std::string& error, std::string const& whereText = {});
    std::vector<LuaCommand> luaCommands() const { return _luaCommands; }
    // The host slot table calls this: runs command `index` with the caller's security, collecting its reply lines.
    // `expectedName`, when given, must be the name the core's command map was built with for this slot: a slot whose
    // command changed since (a reload before the map was rebuilt) answers NoHandler instead of running another command.
    CommandResult runLuaCommand(size_t index, std::string const& tail, int security,
                                std::optional<HandleData> player = std::nullopt, std::string const& expectedName = {});

    // "script:line" of the Lua frame that called the running C function of `L` (the running coroutine, not the main
    // thread; eluna mode logging). Empty outside Lua.
    std::string where(lua_State* L) const;
    // The error ring (E0): last 50 distinct script errors, newest last.
    std::string errorRingText() const;
    void clearErrorRing();
    void recordScriptError(std::string const& what, std::string const& error);

private:
    struct Handler
    {
        uint64_t id = 0;
        std::string event;
        int ref = 0;
        HandlerFilter filter;
        bool disabled = false;
        std::vector<uint64_t> breaks;
        uint64_t calls = 0, errors = 0;
    };
    // What the hooks may ask without a lock (see listeningEluna): hashes of the registered (kind, event, entry) and
    // (kind, event, low guid, instance) keys.
    struct ListenIndex
    {
        std::unordered_set<uint64_t> entryKeys;
        std::unordered_set<uint64_t> uniqueKeys;
    };
    struct Command
    {
        int ref = 0;
        int minSecurity = 0;
    };
    struct CallState
    {
        bool active = false;
        bool broke = false;
        uint64_t deadlineMs = 0;
        uint64_t instr = 0;
        uint64_t maxInstr = 0;
    };

    bool openState();
    void closeState();
    CallResult protect(std::string const& what, Budget budget, std::function<void(lua_State*)> const& body,
                       bool logErrors = true);
    void recordError(std::string const& what, std::string const& error);
    bool noteBreak(std::vector<uint64_t>& breaks);  // true = reached the limit
    void unrefLater(int ref);
    void drainDeferredUnrefs();
    void rebuildListenIndex();
    struct EluHandler
    {
        uint64_t id = 0;
        EluKind kind = EluKind::Player;
        uint32_t entry = 0;
        std::optional<UniqueKey> unique;
        uint32_t event = 0;
        uint32_t shots = 0;   // 0 = always
        int ref = 0;
        std::vector<uint64_t> breaks;
        // E3 profile: where the handler was registered, how often it ran and how long it took.
        std::string where;
        uint64_t calls = 0;
        uint64_t totalMs = 0;
        uint64_t maxMs = 0;
        uint64_t slowLoggedMs = 0;   // the last "slower than SlowHandlerMs" log
    };
    struct LuaTimer
    {
        uint64_t timerId = 0;   // the armed one-shot in Timers (0 while it fires)
        bool hasOwner = false;
        HandleData owner;
        uint64_t delayMin = 0;
        uint64_t delayMax = 0;
        uint64_t lastDelay = 0;
        uint32_t repeats = 0;   // 0 = infinite
        uint32_t fired = 0;
        bool cancelled = false; // removed from inside its own callback
        int ref = 0;            // owned by the LuaTimer map once the timer was re-armed
    };
    struct DbPending
    {
        int ref = 0;
        uint64_t deadlineMs = 0;
    };
    struct Pending
    {
        QueuedEvent ev;
        std::vector<uint64_t> ids;   // handler ids still to run are ids[next..]
        size_t next = 0;
    };
    size_t dispatch(QueuedEvent const& ev, uint64_t deadlineMs);
    size_t runPending(Pending& p, uint64_t deadlineMs);
    void adoptState(Engine& other);
    bool runHandler(Handler& h, QueuedEvent const& ev);
    bool runOneTimer(uint64_t nowMs);
    void sweepOwners(uint64_t nowMs);
    bool ownerGone(HandleData const& owner);
    bool runOneEvent(uint64_t deadlineMs);
    void deliverSelect(int ref, DbRows const& rows, std::string const& error);
    size_t runEluHandlers(uint32_t event, std::vector<EventValue> const& args, EventResult& out,
                          std::vector<uint64_t> const& ids);
    uint64_t pickLuaDelay(LuaTimer const& t);
    uint64_t armLuaTimer(uint64_t publicId);
    void addListenerName(std::string const& event);
    void dropListenerName(std::string const& event);
    LoadReport loadFolder();

    Host& _host;
    Config _cfg;
    lua_State* _L = nullptr;
    std::unique_ptr<MemState> _mem = std::make_unique<MemState>();
    CallState _call;
    std::atomic<bool> _running{false};
    int _callDepth = 0;
    std::optional<Pending> _pending;   // an event whose handlers did not all fit into one tick
    int _bridgeDepth = 0;
    std::atomic<std::thread::id> _owner{};
    std::atomic<int> _listenerTotal{0};
    mutable std::mutex _listenMutex;
    std::unordered_map<std::string, int> _listenNames;
    mutable std::atomic<uint64_t> _wrongThread{0};

    EventQueue _queue;
    std::map<uint64_t, DbPending> _db;   // Db.select requests in flight: id -> Lua callback
    uint64_t _nextDbId = 1;
    bool _windowOpen = false;   // a per-tick Lua window is open
    uint64_t _windowEnd = 0;    // ... and ends here (uts, monotonic milliseconds)
    Timers _timers;
    uint64_t _runningTimer = 0;
    uint64_t _nextHandlerId = 1;
    uint64_t _orderDomain = 0;    // B2: the bot order hub's id for this state (process-wide, unique)
    std::map<uint64_t, Handler> _handlers;
    std::map<std::string, std::vector<uint64_t>> _byEvent;
    std::map<std::string, Command> _commands;
    std::set<std::string> _bridgeDeny;
    std::set<std::string> _forwarded;
    std::vector<std::string> _replyLines;
    SqlGuard _sql;
    std::deque<std::string> _lastErrors;
    uint64_t _reloads = 0;
    std::function<void(lua_State*)> _binder;

    std::map<uint64_t, EluHandler> _eluHandlers;
    std::map<std::string, std::vector<uint64_t>> _eluByEvent;   // "kind|guid|entry|event" -> ids
    std::map<uint64_t, LuaTimer> _eluTimers;                    // public event id -> metadata
    std::map<uint64_t, uint64_t> _eluTimerByTimerId;            // Timers id -> public event id
    uint64_t _nextEluHandlerId = 1;
    uint64_t _nextEluTimerId = 1;
    uint32_t _rng = 0x9e3779b9u;
    std::deque<ScriptError> _errorRing;
    mutable std::recursive_mutex _luaMutex;                  // eluna mode: every Lua call holds it
    std::thread::id _luaThread;                              // eluna mode: the thread inside the lock
    uint64_t _slowHandlers = 0;
    std::shared_ptr<ListenIndex const> _listenIndex = std::make_shared<ListenIndex const>();   // atomic_load/store only
    std::vector<int> _deferredUnrefs;                        // refs whose release was refused at depth 8
    uint64_t _nextOwnerSweepMs = 0;                          // owner-liveness sweep of object-bound timed events
    uint64_t _sweepCursor = 0;
    std::set<std::string> _hookGaps;                         // "event X needs core hook Cn" logged once
    std::mutex _hookGapMutex;

    // ---- E3 ----
    struct AsyncPending
    {
        int ref = 0;
        uint64_t deadlineMs = 0;
        std::string db;
    };
    // Asynchronous query ids come from one process-wide counter (Engine.cpp), so a candidate state, a retired state and the
    // running one never hand out the same id and a cancel can only ever name its own query.
    std::map<uint64_t, AsyncPending> _asyncQueries;          // id -> the Lua callback awaiting a result
    std::shared_ptr<AsyncInbox> _asyncInbox = std::make_shared<AsyncInbox>();
    // H1/H2: the world state (not a candidate) is the one state that polls the host's database structures.
    bool routesHostResults() const { return !_candidate && _cfg.stateMapId < 0; }
    // E5: the owned WorldPacket values of this state, by local id (handle.guid.lo).
    std::map<uint64_t, PacketValue> _packets;
    bool chargePacketBytes(size_t n);
    void releasePacket(std::map<uint64_t, PacketValue>::iterator it);
    std::deque<uint64_t> _packetOrder;
    uint64_t _nextPacketId = 1;
    // ForeverData: script namespace -> key -> JSON text.
    using DataKey = std::pair<std::string, std::string>;
    struct DataInflight
    {
        DataKey key;
        uint64_t version = 0;
    };
    std::map<std::string, std::map<std::string, std::string>> _data;
    std::map<std::string, size_t> _dataScriptBytes;            // key + value bytes per namespace
    size_t _dataBytes = 0;
    size_t _dataKeyCount = 0;
    std::map<DataKey, uint64_t> _dataDirty;                    // changed keys -> the version of their latest change
    std::map<uint64_t, DataInflight> _dataInflight;            // host write token -> what was sent
    std::set<DataKey> _dataInflightKeys;
    uint64_t _dataVersion = 0;
    uint64_t _nextDataFlushMs = 0;
    uint64_t _nextDataLoadMs = 0;
    bool _dataLoaded = false;
    bool _dataLoadLogged = false;
    bool _dataInherited = false;    // a reload candidate: the data is copied from the running state, never read again
    bool _dataWriter = true;        // false for a candidate that may still be thrown away: it never writes
    bool _candidate = false;        // a reload candidate: it tells the host nothing (the adopted state does)
    std::map<std::string, std::string> _scriptChunks;          // source "@rel" -> the loader's file path
    // E4: a map state keeps no ForeverData of its own: every read and write goes to the world state (the owner), under the
    // owner's _dataMutex. Only the owner loads, flushes and polls the host's write results (the world thread), so there is
    // one writer and the per-key versions of the confirmed writes decide which write is the latest.
    Engine* _dataOwner = nullptr;
    // E4 (H3): only the world state's command slots are served, and a candidate tells the host nothing. A map state's
    // RegisterCommand is kept in that state only and never marks the core's command map changed.
    bool tellsHostCommands() const { return !_candidate && _cfg.stateMapId < 0; }
    mutable std::recursive_mutex _dataMutex;
    std::vector<LuaCommand> _luaCommands;                    // RegisterCommand, at most kMaxLuaCommands
    bool loadData();
    void dataPut(std::string const& script, std::string const& key, std::string const& json, bool dirty = true);
    bool dataDrop(std::string const& script, std::string const& key);
    void pollDataWrites();
    bool flushDataBlocking();
    void markDirty(DataKey const& key);
};
}  // namespace fl
