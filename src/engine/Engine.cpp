#include "engine/Engine.h"

#include "engine/Api.h"
#include "engine/BotHub.h"
#include "engine/Elu.h"
#include "engine/GuidText.h"
#include "engine/Handle.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>

// ForeverLua patch 0001 (dep/lua/patches): the step limit of string.find/match/gmatch/gsub, set before every call.
extern thread_local unsigned long fl_lua_match_step_limit;

namespace fl
{
namespace
{
constexpr int kHookStep = 1000;  // instructions between two looks at the clock
constexpr int kHookStepBig = 100;              // ... with a large heap: one memcmp/memcpy of a big string is not free
constexpr size_t kBigHeapBytes = 2u << 20;     // from this heap size on, the count hook runs every kHookStepBig
constexpr size_t kMaxSingleAlloc = 4u << 20;   // no single Lua allocation (a string, a table part) may exceed this
constexpr size_t kMaxScriptBytes = 1u << 20;
constexpr size_t kMaxLastErrors = 5;
constexpr size_t kMaxErrorRing = 50;
constexpr int kMaxEluDepth = 8;                 // nested Eluna events: up to 8, then the event is dropped
constexpr unsigned long kNoBudgetMatchSteps = 1'000'000'000ul;   // pattern steps when the call has no time budget

namespace fs = std::filesystem;

// Process-wide ids. An asynchronous query id names one query for the life of the process: a candidate state, the
// running one and a retired one never share an id, so a cancel or a result can never reach another query. The same
// holds for the tokens of ForeverData writes (the host's writer thread outlives every engine object).
std::atomic<uint64_t> g_nextAsyncId{1};

// H2 (E4/E5 review): the host's finished asynchronous queries are one process-wide queue. Only the world state takes
// them (the world thread); each result goes by its process-wide id to the inbox of the state that issued it, and every
// state drains only its own inbox on its own thread. A result whose state is gone (closed, retired) is dropped here.
std::mutex g_asyncRouteMutex;
std::unordered_map<uint64_t, std::weak_ptr<AsyncInbox>> g_asyncRoutes;

void routeAsync(uint64_t id, std::shared_ptr<AsyncInbox> const& inbox)
{
    std::lock_guard<std::mutex> lock(g_asyncRouteMutex);
    g_asyncRoutes[id] = inbox;
}

void unrouteAsync(uint64_t id)
{
    std::lock_guard<std::mutex> lock(g_asyncRouteMutex);
    g_asyncRoutes.erase(id);
}

// True when the result reached a live inbox.
bool deliverAsync(uint64_t id, DbRows&& rows, std::string&& error)
{
    std::lock_guard<std::mutex> lock(g_asyncRouteMutex);
    auto it = g_asyncRoutes.find(id);
    if (it == g_asyncRoutes.end())
        return false;
    std::shared_ptr<AsyncInbox> inbox = it->second.lock();
    g_asyncRoutes.erase(it);
    if (!inbox)
        return false;
    std::lock_guard<std::mutex> inboxLock(inbox->mutex);
    inbox->done.push_back(AsyncInbox::Done{id, std::move(rows), std::move(error)});
    return true;
}
std::atomic<uint64_t> g_nextDataToken{1};
constexpr uint64_t kDataLoadRetryMs = 30000;

// The allocator behind the memory cap. `osize` is only a size when `ptr` is not null.
void* limitedAlloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    auto* mem = static_cast<MemState*>(ud);
    if (!ptr)
        osize = 0;
    if (nsize == 0)
    {
        if (ptr)
        {
            std::free(ptr);
            mem->used -= osize;
        }
        return nullptr;
    }
    if (nsize > kMaxSingleAlloc)
    {
        // A 4 MiB string, or a table part of 4 MiB, is far beyond anything a script needs (the largest legit strings
        // are a 1 MiB chunk and a 4 MiB string.rep). Refusing the part, rather than only the total, keeps one
        // allocation from blocking the world thread while the kernel serves it (review N2/N5/N6).
        ++mem->failures;
        return nullptr;  // Lua raises LUA_ERRMEM inside the pcall
    }
    if (nsize > osize && mem->used - osize + nsize > mem->cap)
    {
        ++mem->failures;
        return nullptr;  // Lua raises LUA_ERRMEM inside the pcall
    }
    void* np = std::realloc(ptr, nsize);
    if (!np)
        return nsize <= osize ? ptr : nullptr;  // a shrink must not fail
    mem->used = mem->used - osize + nsize;
    return np;
}

constexpr int kHookMask = LUA_MASKCOUNT | LUA_MASKCALL;

// Protected calls in flight across every engine of the process. A reload loads a candidate state beside the running
// one, so the old engine's _callDepth is still 0 while the candidate runs: a Gm.run or a host hook in the candidate
// must not dispatch an event into the old state (review N7).
std::atomic<int> g_callsInFlight{0};

struct CallGuard
{
    CallGuard() { g_callsInFlight.fetch_add(1); }
    ~CallGuard() { g_callsInFlight.fetch_sub(1); }
};
constexpr unsigned long kMatchStepsPerMs = 100000;   // pattern steps allowed per ms of call budget

void hookFn(lua_State* L, lua_Debug* ar)
{
    Engine::from(L)->onHook(L, ar);
}

int msgHandler(lua_State* L);

int msgHandler(lua_State* L)
{
    char const* msg = lua_tostring(L, 1);
    if (!msg)
    {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

struct ProtectCtx
{
    std::function<void(lua_State*)> const* fn;
    std::string* result;
};

int trampoline(lua_State* L)
{
    auto* c = static_cast<ProtectCtx*>(lua_touserdata(L, 1));
    std::string err;
    bool failed = false;
    try
    {
        (*c->fn)(L);
    }
    catch (std::exception const& e)
    {
        failed = true;
        err = e.what();
    }
    if (failed)
        return luaL_error(L, "%s", err.empty() ? "internal error" : err.c_str());
    if (lua_gettop(L) >= 2)
    {
        size_t n;
        char const* s = luaL_tolstring(L, 2, &n);
        c->result->assign(s, n);
    }
    return 0;
}

bool insideRoot(fs::path const& root, fs::path const& p)
{
    auto r = root.begin();
    auto q = p.begin();
    for (; r != root.end(); ++r, ++q)
        if (q == p.end() || *q != *r)
            return false;
    return true;
}

bool readFile(fs::path const& p, std::string& out, std::string& error)
{
    std::error_code ec;
    auto size = fs::file_size(p, ec);
    if (ec)
    {
        error = "cannot read file";
        return false;
    }
    if (size > kMaxScriptBytes)
    {
        error = "file too large";
        return false;
    }
    std::ifstream in(p, std::ios::binary);
    if (!in)
    {
        error = "cannot open file";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// B2: the order domains are process-wide, so a reload, a retired candidate and a new state never share tickets.
std::atomic<uint64_t> g_nextOrderDomain{1};

}  // namespace

Engine::Engine(Host& host, Config config) : _host(host), _cfg(std::move(config)), _queue(_cfg.queueCapacity), _sql(host)
{
    _orderDomain = g_nextOrderDomain.fetch_add(1, std::memory_order_relaxed);
    for (auto const& r : _cfg.commandRoots)
        _forwarded.insert(lower(r));
    for (auto const& r : _cfg.bridgeDeny)
        _bridgeDeny.insert(lower(r));
}

Engine::~Engine()
{
    // A map state may be destroyed by a thread that is not the one that last ran it; the Lua mutex is the state's lock.
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    closeState();
}

Engine* Engine::from(lua_State* L)
{
    return *static_cast<Engine**>(lua_getextraspace(L));
}

bool Engine::onOwnerThread() const
{
    return _owner.load() == std::this_thread::get_id();
}

void Engine::claimOwnerThread()
{
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    // The owner is the thread that runs this state's map update. A nested call must not move it.
    if (_callDepth == 0)
        _owner.store(std::this_thread::get_id());
}

void Engine::closeAsMapState()
{
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L)
        return;
    if (_callDepth > 0)
    {
        ++_wrongThread;
        _host.log(LogLevel::Error, "ForeverLua: a map state cannot be closed from inside its own Lua call");
        return;
    }
    _owner.store(std::this_thread::get_id());
    emitEluna(EluKind::Server, 0, 16, {});   // LUA_STATE_CLOSE, in the state that is going away
    closeState();
}

// ---------------------------------------------------------------------------------------------------------------
// State

bool Engine::openState()
{
    _mem->used = 0;
    _mem->cap = _cfg.memoryCap;
    _mem->failures = 0;
    _L = lua_newstate(limitedAlloc, _mem.get());
    if (!_L)
        return false;
    *static_cast<Engine**>(lua_getextraspace(_L)) = this;

    CallResult r = protect("setup", Budget{1000, 1'000'000'000}, [this](lua_State* L)
    {
        openSandbox(L, _cfg);
        if (_cfg.mode == EngineMode::Eluna)
            registerEluna(L);   // classes over the handles and the Eluna globals; the toolkit tables are not installed
        else
        {
            registerApi(L);
            if (_binder)
                _binder(L);
            registerForeverData(L, "Data");   // E3: the persistent store is offered in test mode too
        }
    });
    if (!r.ok)
    {
        lua_close(_L);
        _L = nullptr;
        return false;
    }
    _running.store(true, std::memory_order_release);
    return true;
}

void Engine::closeState()
{
    // Order matters for a concurrent post() from a map thread: first stop accepting events (no listener left, not
    // running), then drop what was queued, then close the state.
    _running.store(false, std::memory_order_release);
    // B2: a state that is going away drops its bot orders: a queued one ends `cancelled` before the hand-off, a
    // committed one keeps its ticket and its terminal event.
    botHub().dropOrders(_orderDomain);
    _orderDomain = 0;
    flushDataBlocking();   // the last write-behind of ForeverData: every changed key, bounded in time
    bool const hadCommands = !_luaCommands.empty();
    _luaCommands.clear();
    if (!_asyncQueries.empty())
        _host.log(LogLevel::Warn, "ForeverLua: " + std::to_string(_asyncQueries.size()) +
                                      " pending asynchronous database queries were dropped (their callbacks will not run)");
    for (auto const& e : _asyncQueries)
    {
        unrouteAsync(e.first);
        _host.cancelRawQuery(e.first);
        if (_L)
            luaL_unref(_L, LUA_REGISTRYINDEX, e.second.ref);
    }
    _asyncQueries.clear();
    {
        std::lock_guard<std::mutex> inboxLock(_asyncInbox->mutex);
        _asyncInbox->done.clear();
    }
    {
        std::lock_guard<std::mutex> lock(_listenMutex);
        _listenNames.clear();
        _listenerTotal.store(0);
    }
    _queue.clear();
    _pending.reset();
    for (auto const& e : _db)
        _host.cancelSelect(e.first);   // a late answer must not reach a closed state
    _db.clear();
    if (_L)
    {
        lua_close(_L);
        _L = nullptr;
    }
    _timers.clear();
    _runningTimer = 0;
    _handlers.clear();
    _byEvent.clear();
    _commands.clear();
    _replyLines.clear();
    _eluHandlers.clear();
    _eluByEvent.clear();
    rebuildListenIndex();
    _eluTimers.clear();
    _eluTimerByTimerId.clear();
    _worldObjectDataOwners.clear();
    _dataSweepCursor.clear();
    _packets.clear();
    _packetOrder.clear();
    _deferredUnrefs.clear();
    _scriptChunks.clear();
    _sql.reset();
    _callDepth = 0;
    _bridgeDepth = 0;
    _call = CallState{};
    if (hadCommands && tellsHostCommands())
        _host.commandsChanged();   // the Lua command slots are gone (only marked: the world script rebuilds the map)
}

bool Engine::start()
{
    if (_L)
        return true;
    _owner.store(std::this_thread::get_id());
    // B2: a fresh state (a first start, or the start that follows a close): its own order domain.
    _orderDomain = g_nextOrderDomain.fetch_add(1, std::memory_order_relaxed);
    if (!openState())
    {
        _host.log(LogLevel::Error, "ForeverLua: could not create the Lua state");
        return false;
    }
    if (!_dataInherited && !_dataOwner)
        loadData();   // ForeverData (E3) is available to the first script that runs (a map state uses the world's)
    return true;
}

void Engine::shutdown()
{
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L)
        return;
    if (!onOwnerThread() || _callDepth > 0)
    {
        ++_wrongThread;
        return;
    }
    closeState();
}

// ---------------------------------------------------------------------------------------------------------------
// Protected calls and the budget

void Engine::onHook(lua_State* L, lua_Debug* ar)
{
    if (!_call.active)
        return;
    if (ar->event == LUA_HOOKCOUNT)
    {
        int count = lua_gethookcount(L);
        int step = count > 0 ? count : kHookStep;
        if (!_call.broke)
        {
            _call.instr += static_cast<uint64_t>(step);
            if (_call.instr < _call.maxInstr && _host.nowMs() < _call.deadlineMs)
            {
                // One VM operation between two hooks is not free: `a == b` or `a .. b` on a multi-megabyte string is a
                // multi-megabyte memcmp/memcpy. From a large heap on, look at the clock ten times as often (review N5).
                if (step > kHookStepBig && _mem->used >= kBigHeapBytes)
                {
                    lua_sethook(L, hookFn, kHookMask, kHookStepBig);
                    if (_L != L)
                        lua_sethook(_L, hookFn, kHookMask, kHookStepBig);
                }
                return;
            }
            _call.broke = true;
        }
    }
    else
    {
        // A call event: the clock is read before every function call, so a loop of expensive C functions overruns its
        // budget by at most one call, not by a thousand instructions' worth of them.
        if (!_call.broke && _host.nowMs() < _call.deadlineMs)
            return;
        // Past the deadline (or after a break) the message handler still has to run after an error: let it through.
        lua_getinfo(L, "f", ar);
        bool handler = lua_iscfunction(L, -1) && lua_tocfunction(L, -1) == msgHandler;
        lua_pop(L, 1);
        if (handler)
            return;
        _call.broke = true;
    }
    // From the first break on, every thread that reaches this hook (the main thread and any coroutine) looks at every
    // single instruction and call. A script then cannot swallow the budget error with pcall and carry on: its very
    // next instruction raises the error again, outside the pcall it was hiding in.
    if (lua_gethookcount(L) != 1)
        lua_sethook(L, hookFn, kHookMask, 1);
    if (_L != L && lua_gethookcount(_L) != 1)
        lua_sethook(_L, hookFn, kHookMask, 1);
    lua_pushliteral(L, "budget");
    lua_error(L);
}

CallResult Engine::protect(std::string const& what, Budget budget, std::function<void(lua_State*)> const& body,
                           bool logErrors)
{
    CallResult r;
    if (!_L)
    {
        r.error = "engine not running";
        return r;
    }
    // Eluna mode: every Lua call holds one recursive mutex, so a hook on a map thread runs its handlers at once
    // (owner = lock holder + depth) instead of posting them to the world thread.
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
    {
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
        if (_callDepth == 0)
            _luaThread = std::this_thread::get_id();
        if (_callDepth >= kMaxEluDepth)
        {
            r.error = "nested Eluna call at depth " + std::to_string(kMaxEluDepth) + " refused";
            _host.log(LogLevel::Error, "ForeverLua: " + what + ": " + r.error);
            return r;
        }
    }
    if (_callDepth > 0 && _cfg.mode != EngineMode::Eluna)
    {
        r.error = "re-entrant call refused";
        return r;
    }
    ++_callDepth;
    CallGuard callGuard;
    // A nested call (eluna mode: a handler whose game call fires another event) must give the outer call its budget state
    // and its hook back; the inner call also runs inside what is left of the outer one.
    CallState const outer = _call;
    lua_Hook const outerHook = lua_gethook(_L);
    int const outerMask = lua_gethookmask(_L);
    int const outerCount = lua_gethookcount(_L);
    constexpr uint64_t kUnlimited = (std::numeric_limits<uint64_t>::max)() / 2;
    _call = CallState{};
    _call.active = true;
    // Budget fields of 0 are "no limit of that kind" (the Eluna mode default is 0 ms and 0 instructions: no hook at all).
    _call.deadlineMs = budget.maxMs != 0 ? _host.nowMs() + budget.maxMs : kUnlimited;
    _call.maxInstr = budget.maxInstr != 0 ? budget.maxInstr : kUnlimited;
    if (outer.active)
    {
        _call.deadlineMs = std::min(_call.deadlineMs, outer.deadlineMs);
        uint64_t const left = outer.maxInstr > outer.instr ? outer.maxInstr - outer.instr : 0;
        _call.maxInstr = std::min(_call.maxInstr, left);
        _call.broke = outer.broke;
    }
    bool const limited = _call.deadlineMs < kUnlimited || _call.maxInstr < kUnlimited;
    // Saved, not zeroed on the way out: a state loaded inside another engine's call must give the outer call its limit
    // back (review N7).
    unsigned long const previousLimit = fl_lua_match_step_limit;
    unsigned long steps = budget.maxMs == 0
                              ? kNoBudgetMatchSteps
                              : static_cast<unsigned long>(std::min<uint64_t>(budget.maxMs, 600000) * kMatchStepsPerMs);
    if (outer.active && previousLimit != 0)
        steps = std::min(steps, previousLimit);
    fl_lua_match_step_limit = steps;
    if (limited)
        lua_sethook(_L, hookFn, kHookMask, _call.broke ? 1 : kHookStep);

    ProtectCtx ctx{&body, &r.result};
    int base = lua_gettop(_L);
    lua_pushcfunction(_L, msgHandler);   // light C functions and light userdata allocate nothing
    lua_pushcfunction(_L, trampoline);
    lua_pushlightuserdata(_L, &ctx);
    int status = lua_pcall(_L, 1, 0, base + 1);

    lua_sethook(_L, outerHook, outerMask, outerCount);
    fl_lua_match_step_limit = previousLimit;
    r.budget = _call.broke;
    CallState const inner = _call;
    _call = outer;
    if (outer.active)
        _call.instr += inner.instr;   // what the inner call used counts against the outer one
    --_callDepth;
    if (_callDepth == 0)
        _host.endLuaCall();   // transient Spell/Aura handles die with the call that produced them

    r.status = status;
    r.ok = status == LUA_OK;
    std::string full;
    if (!r.ok)
    {
        if (lua_type(_L, -1) == LUA_TSTRING)
            full = lua_tostring(_L, -1);
        else
            full = "(non-string error)";
        size_t cut = full.find("\nstack traceback:");
        r.error = cut == std::string::npos ? full : full.substr(0, cut);
        r.memory = status == LUA_ERRMEM;
        r.result.clear();
    }
    lua_settop(_L, base);
    if (!r.ok && logErrors)
    {
        recordError(what, r.error);
        recordScriptError(what, r.error);
        _host.log(LogLevel::Error, "ForeverLua: " + what + ": " + full);
    }
    return r;
}

void Engine::recordError(std::string const& what, std::string const& error)
{
    _lastErrors.push_back(what + ": " + error);
    while (_lastErrors.size() > kMaxLastErrors)
        _lastErrors.pop_front();
}

bool Engine::noteBreak(std::vector<uint64_t>& breaks)
{
    uint64_t now = _host.nowMs();
    breaks.push_back(now);
    breaks.erase(std::remove_if(breaks.begin(), breaks.end(),
                                [&](uint64_t t) { return now - t > _cfg.breakWindowMs; }),
                 breaks.end());
    return static_cast<int>(breaks.size()) >= _cfg.breaksToDisable;
}

void Engine::unrefLater(int ref)
{
    if (!_L)
        return;
    if (_cfg.mode == EngineMode::Eluna && _callDepth >= kMaxEluDepth)
    {
        _deferredUnrefs.push_back(ref);   // a protected call is refused at the depth cap: the next tick releases it
        return;
    }
    protect("unref", Budget{100, 1'000'000}, [ref](lua_State* L) { luaL_unref(L, LUA_REGISTRYINDEX, ref); }, false);
}

void Engine::drainDeferredUnrefs()
{
    if (_deferredUnrefs.empty() || !_L || _callDepth > 0)
        return;
    std::vector<int> refs;
    refs.swap(_deferredUnrefs);
    for (int ref : refs)
        unrefLater(ref);
}

std::string Engine::where(lua_State* L) const
{
    if (!L)
        return {};
    lua_Debug ar{};
    if (!lua_getstack(L, 1, &ar))
        return {};
    if (!lua_getinfo(L, "Sl", &ar))
        return {};
    std::string s = ar.short_src;   // char[60], never null
    s += ":" + std::to_string(ar.currentline);
    return s;
}

void Engine::recordScriptError(std::string const& what, std::string const& error)
{
    // "sub/file.lua:12: boom": the script and the line come from the chunk name, so the ring reads like the log.
    std::string first = error;
    size_t nl = first.find('\n');
    if (nl != std::string::npos)
        first.resize(nl);
    std::string script, message;
    int line = 0;
    size_t pos = std::string::npos;
    for (size_t i = first.find(':'); i != std::string::npos && i + 1 < first.size(); i = first.find(':', i + 1))
        if (std::isdigit(static_cast<unsigned char>(first[i + 1])))
        {
            pos = i;
            break;
        }
    if (pos != std::string::npos)
    {
        size_t end = pos + 1;
        while (end < first.size() && std::isdigit(static_cast<unsigned char>(first[end])))
            ++end;
        script = first.substr(0, pos);
        line = std::atoi(first.substr(pos + 1, end - pos - 1).c_str());
        message = end < first.size() ? first.substr(end) : std::string();
        if (message.rfind(": ", 0) == 0)
            message.erase(0, 2);
        else if (!message.empty() && message.front() == ':')
            message.erase(0, 1);
    }
    else
    {
        script = what;
        message = first;
    }
    uint64_t now = _host.nowMs();
    for (auto& e : _errorRing)
        if (e.script == script && e.line == line && e.message == message)
        {
            ++e.count;
            e.lastMs = now;
            return;
        }
    ScriptError e;
    e.script = script;
    e.line = line;
    e.message = message.size() > 300 ? message.substr(0, 300) : message;
    e.count = 1;
    e.firstMs = now;
    e.lastMs = now;
    _errorRing.push_back(std::move(e));
    while (_errorRing.size() > kMaxErrorRing)
        _errorRing.pop_front();
}

void Engine::clearErrorRing()
{
    _errorRing.clear();
}

std::string Engine::errorRingText() const
{
    if (_errorRing.empty())
        return "ForeverLua: no script errors";
    uint64_t now = _host.nowMs();
    std::ostringstream o;
    o << "ForeverLua: " << _errorRing.size() << " script error(s) (newest last)";
    for (auto const& e : _errorRing)
    {
        o << "\n  " << e.script << ":" << e.line << ": " << e.message << " (x" << e.count << ", last "
          << ((now - e.lastMs) / 1000) << "s ago)";
    }
    return o.str();
}

namespace
{
thread_local ItemUseScope* t_itemUse = nullptr;
}

ItemUseScope::ItemUseScope(Guid item, uint32_t entry) : _item(item), _entry(entry), _prev(t_itemUse)
{
    t_itemUse = this;
}

ItemUseScope::~ItemUseScope() { t_itemUse = _prev; }

bool ItemUseScope::blocks(Guid const* item, uint32_t entry)
{
    for (ItemUseScope const* s = t_itemUse; s; s = s->_prev)
        if ((item && *item == s->_item) || (entry != 0 && entry == s->_entry))
            return true;
    return false;
}

bool Engine::noteHookGap(std::string const& key)
{
    // H4: hooks call this from any map thread (outside the Lua lock): the set has its own mutex.
    std::lock_guard<std::mutex> lock(_hookGapMutex);
    return _hookGaps.insert(key).second;
}

void Engine::logLua(LogLevel level, std::string const& text)
{
    _host.log(level, "[lua] " + text);
}

CallResult Engine::runChunk(std::string const& code, std::string const& chunkName, bool commandBudget)
{
    return protect("chunk " + chunkName, commandBudget ? _cfg.commandBudget : _cfg.eventBudget,
                   [&](lua_State* L)
    {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunkName.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 1);
    });
}

// ---------------------------------------------------------------------------------------------------------------
// Script folder

namespace
{
struct ScriptFile
{
    std::string rel;      // relative path inside its root, with '/'
    std::filesystem::path abs;
};
}  // namespace

LoadReport Engine::loadFolder()
{
    LoadReport rep;
    if (!_L)
    {
        rep.errors.push_back("engine not running");
        return rep;
    }
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::path(_cfg.scriptPath), ec);
    if (ec || _cfg.scriptPath.empty() || !fs::is_directory(root, ec))
    {
        rep.folderMissing = true;
        rep.errors.push_back("script folder not found: " + _cfg.scriptPath);
        _host.log(LogLevel::Error, "ForeverLua: script folder not found: " + _cfg.scriptPath);
        return rep;
    }

    std::vector<ScriptFile> files;
    if (_cfg.mode == EngineMode::Eluna)
    {
        // Eluna mode: the folder loads recursively, sorted by path, with a folder's init.lua before its neighbours.
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code e2;
            fs::path p = it->path();
            if (p.extension() != ".lua" || !fs::is_regular_file(p, e2))   // follows symlinks; the check below still applies
                continue;
            // A symlink out of the folder is kept in the list: the load loop below refuses it and names the path.
            files.push_back(ScriptFile{p.lexically_relative(root).generic_string(), p});
        }
        std::sort(files.begin(), files.end(), [](ScriptFile const& a, ScriptFile const& b)
        {
            fs::path pa(a.rel), pb(b.rel);
            // Folders compare component by component ("sub" < "sub/deep" < "sub-x"), not as one string.
            int const folders = pa.parent_path().compare(pb.parent_path());
            if (folders != 0)
                return folders < 0;
            bool ia = pa.filename() == "init.lua", ib = pb.filename() == "init.lua";
            if (ia != ib)
                return ia;   // a folder's init.lua first
            return pa.filename() < pb.filename();
        });
    }
    else
    {
        std::vector<std::string> names;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code e2;
            fs::path p = it->path();
            if (p.extension() != ".lua" || !fs::is_regular_file(p, e2))  // follows symlinks
                continue;
            names.push_back(p.filename().string());
        }
        std::sort(names.begin(), names.end());
        auto init = std::find(names.begin(), names.end(), "init.lua");
        if (init != names.end())
            std::rotate(names.begin(), init, init + 1);
        for (auto const& name : names)
            files.push_back(ScriptFile{name, root / name});
    }

    for (auto const& file : files)
    {
        fs::path p = fs::weakly_canonical(file.abs, ec);
        if (ec || !insideRoot(root, p))
        {
            rep.errors.push_back(file.rel + ": outside the script folder");
            _host.log(LogLevel::Error, "ForeverLua: " + file.rel + " resolves outside the script folder, skipped");
            continue;
        }
        std::string src, err;
        if (!readFile(p, src, err))
        {
            rep.errors.push_back(file.rel + ": " + err);
            _host.log(LogLevel::Error, "ForeverLua: " + file.rel + ": " + err);
            continue;
        }
        std::string chunk = "@" + file.rel;
        noteScriptChunk(chunk, file.rel);
        CallResult r = protect("load " + file.rel, _cfg.loadBudget, [&](lua_State* L)
        {
            if (luaL_loadbufferx(L, src.data(), src.size(), chunk.c_str(), "t") != LUA_OK)
                lua_error(L);
            lua_call(L, 0, 0);
        });
        if (r.ok)
            rep.loaded.push_back(file.rel);
        else
            rep.errors.push_back(file.rel + ": " + r.error);
    }
    return rep;
}

LoadReport Engine::load()
{
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (_L && (!onOwnerThread() || _callDepth > 0))
    {
        ++_wrongThread;
        LoadReport rep;
        rep.errors.push_back("load refused: wrong thread or inside Lua");
        return rep;
    }
    return loadFolder();
}

void Engine::adoptState(Engine& o)
{
    std::swap(_L, o._L);
    std::swap(_mem, o._mem);   // the allocator of each state points at its MemState: the states keep theirs
    std::swap(_handlers, o._handlers);
    std::swap(_byEvent, o._byEvent);
    std::swap(_eluHandlers, o._eluHandlers);
    std::swap(_eluByEvent, o._eluByEvent);
    std::swap(_eluTimers, o._eluTimers);
    std::swap(_eluTimerByTimerId, o._eluTimerByTimerId);
    std::swap(_nextEluHandlerId, o._nextEluHandlerId);
    std::swap(_nextEluTimerId, o._nextEluTimerId);
    std::swap(_commands, o._commands);
    std::swap(_timers, o._timers);
    std::swap(_runningTimer, o._runningTimer);
    std::swap(_nextHandlerId, o._nextHandlerId);
    std::swap(_replyLines, o._replyLines);
    {
        std::scoped_lock lock(_listenMutex, o._listenMutex);
        std::swap(_listenNames, o._listenNames);
        int mine = _listenerTotal.load();
        _listenerTotal.store(o._listenerTotal.load());
        o._listenerTotal.store(mine);
    }
    rebuildListenIndex();
    o.rebuildListenIndex();
    *static_cast<Engine**>(lua_getextraspace(_L)) = this;
    if (o._L)
        *static_cast<Engine**>(lua_getextraspace(o._L)) = &o;
    std::swap(_packets, o._packets);   // E5: the adopted state keeps the packets its load created
    std::swap(_packetOrder, o._packetOrder);
    std::swap(_nextPacketId, o._nextPacketId);
    _queue.clear();     // queued events belonged to the old handlers: dropped, the drop counter stays
    _pending.reset();
    for (auto const& e : _db)
        _host.cancelSelect(e.first);   // their callbacks lived in the state that is going away
    _db.clear();
    std::swap(_db, o._db);   // a Db.select issued while the candidate loaded belongs to the state that is adopted
    std::swap(_nextDbId, o._nextDbId);   // ... and its id must not be handed out again by the next select
    // E3: the pending asynchronous queries. `_asyncQueries` still holds the OLD state's (only `_L` was swapped so far) and
    // the callbacks live in the old Lua state, which `o` now owns and closes: cancel the host side by id (ids are
    // process-wide, so the candidate's own queries cannot be named), drop the callbacks with a log line and never touch
    // the adopted registry. The candidate's queries move over with their callbacks.
    if (!_asyncQueries.empty())
        _host.log(LogLevel::Warn, "ForeverLua: reload dropped " + std::to_string(_asyncQueries.size()) +
                                      " pending asynchronous database queries of the old state (their callbacks will not run)");
    for (auto const& e : _asyncQueries)
    {
        unrouteAsync(e.first);
        _host.cancelRawQuery(e.first);
    }
    _asyncQueries.clear();
    {
        std::lock_guard<std::mutex> inboxLock(_asyncInbox->mutex);
        _asyncInbox->done.clear();   // answers to the old state's queries: their callbacks are gone
    }
    std::swap(_asyncQueries, o._asyncQueries);
    {
        // The candidate's queries now belong to this engine: their routes and any answer already in the candidate's inbox
        // move here, under the route lock, so the world's router cannot drop one in between.
        std::lock_guard<std::mutex> lock(g_asyncRouteMutex);
        for (auto const& e : _asyncQueries)
            g_asyncRoutes[e.first] = _asyncInbox;
        std::scoped_lock inboxes(_asyncInbox->mutex, o._asyncInbox->mutex);
        for (auto& d : o._asyncInbox->done)
            _asyncInbox->done.push_back(std::move(d));
        o._asyncInbox->done.clear();
    }
    // The command list: the core's map is rebuilt (later, outside any command) only when the names really changed.
    bool commandsDiffer = _luaCommands.size() != o._luaCommands.size();
    for (size_t i = 0; !commandsDiffer && i < _luaCommands.size(); ++i)
        commandsDiffer = _luaCommands[i].name != o._luaCommands[i].name;
    std::swap(_luaCommands, o._luaCommands);
    // ForeverData: the adopted values (already merged with what the old state's close handlers wrote) move over; the writes
    // in flight stay with this engine (the host reports them to it); the retired engine is not a writer.
    std::swap(_data, o._data);
    std::swap(_dataScriptBytes, o._dataScriptBytes);
    std::swap(_dataBytes, o._dataBytes);
    std::swap(_dataKeyCount, o._dataKeyCount);
    std::swap(_dataDirty, o._dataDirty);
    std::swap(_dataVersion, o._dataVersion);
    _dataVersion = (std::max)(_dataVersion, o._dataVersion);   // a later change never reuses the version of a write in flight
    std::swap(_dataLoaded, o._dataLoaded);
    std::swap(_nextDataFlushMs, o._nextDataFlushMs);
    std::swap(_scriptChunks, o._scriptChunks);
    _dataWriter = true;
    o._dataWriter = false;
    if (commandsDiffer && tellsHostCommands())
        _host.commandsChanged();   // the slots now name other commands
    // B2: the orders of the scripts that are going away drop with the old domain (a queued one ends `cancelled` before
    // the hand-off, a committed one keeps its terminal event); the adopted state's own (its load-time calls, its
    // buckets) keep the id they posted under.
    botHub().dropOrders(_orderDomain);
    _orderDomain = o._orderDomain;
    o._orderDomain = 0;
    _sql.reset();       // the schema snapshots are per state
    _callDepth = 0;
    _bridgeDepth = 0;
    _call = CallState{};
    _running.store(true, std::memory_order_release);
}

LoadReport Engine::reload(std::function<void()> const& beforeAdopt)
{
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (running() && (!onOwnerThread() || _callDepth > 0))
    {
        ++_wrongThread;
        LoadReport rep;
        rep.errors.push_back("reload refused: wrong thread or inside Lua");
        return rep;
    }
    if (!running())
    {
        // Nothing to keep: start() and load().
        closeState();
        if (!start())
        {
            LoadReport rep;
            rep.errors.push_back("could not create the Lua state");
            return rep;
        }
        LoadReport rep = loadFolder();
        rep.applied = rep.errors.empty();
        if (rep.applied)
            ++_reloads;
        return rep;
    }
    // A second engine builds the new state beside the running one. Its ids continue ours, so none is reused.
    Engine candidate(_host, _cfg);
    candidate._binder = _binder;
    candidate._nextHandlerId = _nextHandlerId;
    candidate._nextDbId = _nextDbId;   // a Db.select issued while the candidate loads keeps a unique id
    candidate._timers.setNextId(_timers.nextId());
    // ForeverData survives the reload: the scripts of the candidate see the running state's values (dirty ones
    // included), not only what the database holds, and the table is not read again. The candidate never writes (it may be
    // thrown away); the state that is adopted does.
    candidate._candidate = true;
    candidate._dataInherited = true;
    candidate._dataWriter = false;
    // E4: a map state's candidate keeps using the world state's store. The world's candidate gets a copy; the versions of
    // the copy are noted, so every key changed in the running store while the candidate loads (by a map state or by the
    // old state's close handlers) is carried over below and wins over a load-time write of the same key.
    candidate._dataOwner = _dataOwner;
    std::map<DataKey, uint64_t> dirtyBefore;
    uint64_t versionBefore = 0;
    if (!_dataOwner)
    {
        std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
        candidate._data = _data;
        candidate._dataScriptBytes = _dataScriptBytes;
        candidate._dataBytes = _dataBytes;
        candidate._dataKeyCount = _dataKeyCount;
        candidate._dataDirty = _dataDirty;
        candidate._dataVersion = _dataVersion;
        candidate._nextDataFlushMs = _nextDataFlushMs;
        candidate._dataLoaded = _dataLoaded;
        dirtyBefore = _dataDirty;
        versionBefore = _dataVersion;
    }
    if (!candidate.start())
    {
        LoadReport rep;
        rep.errors.push_back("could not create the Lua state; the running state was kept");
        return rep;
    }
    LoadReport rep = candidate.loadFolder();
    if (!rep.errors.empty())
    {
        for (auto const& e : candidate._lastErrors)
            recordError("reload", e);
        rep.errors.push_back("reload not applied: the running state was kept");
        return rep;
    }
    // What the old state's close handlers write counts too (and, E4, what a map state wrote while the candidate loaded):
    // run them, then carry every key changed since the copy over to the candidate (they ran last, so they win over a
    // load-time write of the same key). The store stays locked from the merge to the end of the adopt, so no map state's
    // write can fall between the two.
    if (beforeAdopt)
        beforeAdopt();   // the old state is still the live one: its close handlers run now, and only now
    std::unique_lock<std::recursive_mutex> dataLock(_dataMutex, std::defer_lock);
    if (!_dataOwner)
        dataLock.lock();
    if (!_dataOwner && _dataVersion != versionBefore)
        for (auto const& d : _dataDirty)
        {
            auto before = dirtyBefore.find(d.first);
            if (before != dirtyBefore.end() && before->second == d.second)
                continue;   // not touched by a close handler
            auto s = _data.find(d.first.first);
            if (s != _data.end() && s->second.count(d.first.second))
                candidate.dataPut(d.first.first, d.first.second, s->second[d.first.second]);
            else
                candidate.dataDrop(d.first.first, d.first.second);
        }
    adoptState(candidate);
    ++_reloads;
    rep.applied = true;
    return rep;
}

CallResult Engine::runScript(std::string const& name)
{
    CallResult res;
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L)
    {
        res.error = "engine not running";
        return res;
    }
    if (!onOwnerThread() || _callDepth > 0)
    {
        _wrongThread.fetch_add(1);
        res.error = "refused: wrong thread or called from Lua";
        return res;
    }
    std::string err;
    std::string path = resolveModule(name, err);
    if (path.empty())
    {
        res.error = err;
        return res;
    }
    std::string src;
    if (!readFile(fs::path(path), src, err))
    {
        res.error = name + ": " + err;
        return res;
    }
    std::string chunk = "@" + name + ".lua";
    noteScriptChunk(chunk, name + ".lua");
    return protect("run " + name, _cfg.commandBudget, [&](lua_State* L)
    {
        if (luaL_loadbufferx(L, src.data(), src.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 0);
    });
}

std::string Engine::resolveModule(std::string const& name, std::string& error) const
{
    return resolveModuleEx(name, nullptr, error);
}

std::string Engine::resolveModuleEx(std::string const& name, std::string* rel, std::string& error) const
{
    if (_cfg.mode == EngineMode::Eluna)
    {
        // "a.b" means the file a/b.lua or a/b/init.lua. Every segment is [A-Za-z0-9_]+, so no dot-dot, no slash games.
        std::string path;
        if (name.empty() || name.size() > 128 || name.front() == '.' || name.back() == '.')
        {
            error = "bad module name '" + name + "'";
            return {};
        }
        bool segmentEmpty = false;
        for (size_t i = 0; i < name.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(name[i]);
            if (c == '.')
            {
                if (i + 1 < name.size() && name[i + 1] == '.')
                    segmentEmpty = true;
                path += '/';
                continue;
            }
            if (!(std::isalnum(c) || c == '_' || c == '/'))
            {
                error = "bad module name '" + name + "'";
                return {};
            }
            if (c == '/' && (i == 0 || name[i - 1] == '.' || name[i - 1] == '/'))
                segmentEmpty = true;
            path += static_cast<char>(c);
        }
        if (segmentEmpty || path.find("//") != std::string::npos)
        {
            error = "bad module name '" + name + "'";
            return {};
        }
        std::vector<std::string> roots;
        roots.push_back(_cfg.scriptPath);
        for (auto const& r : _cfg.requirePath)
            if (!r.empty())
                roots.push_back(r);
        std::error_code ec;
        for (auto const& rootText : roots)
        {
            fs::path root = fs::weakly_canonical(fs::path(rootText), ec);
            if (ec || !fs::is_directory(root, ec))
                continue;
            for (char const* suffix : {".lua", "/init.lua"})
            {
                fs::path candidate = fs::weakly_canonical(root / (path + suffix), ec);
                if (ec || !insideRoot(root, candidate) || !fs::is_regular_file(candidate, ec))
                    continue;
                if (rel)
                    *rel = candidate.lexically_relative(root).generic_string();
                return candidate.string();
            }
        }
        error = "module '" + name + "' not found";
        return {};
    }

    // [A-Za-z0-9_/]+ : no dots (so no ".."), no backslash, no drive letter. No leading, trailing or doubled slash.
    bool ok = !name.empty() && name.size() <= 128 && name.front() != '/' && name.back() != '/' &&
              name.find("//") == std::string::npos;
    for (unsigned char c : name)
        if (!(std::isalnum(c) || c == '_' || c == '/'))
            ok = false;
    if (!ok)
    {
        error = "bad module name '" + name + "'";
        return {};
    }
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::path(_cfg.scriptPath), ec);
    fs::path p = fs::weakly_canonical(root / (name + ".lua"), ec);
    if (ec || !insideRoot(root, p))
    {
        error = "module '" + name + "' is outside the script folder";
        return {};
    }
    if (!fs::is_regular_file(p, ec))
    {
        error = "module '" + name + "' not found";
        return {};
    }
    if (rel)
        *rel = name + ".lua";
    return p.string();
}

// ---------------------------------------------------------------------------------------------------------------
// Db.select (asynchronous)

bool Engine::startSelect(std::string const& db, std::string const& sql, size_t maxRows, int ref, std::string& error)
{
    if (_db.size() >= _cfg.maxPendingSelects)
    {
        error = "busy";
        luaL_unref(_L, LUA_REGISTRYINDEX, ref);
        return false;
    }
    uint64_t id = _nextDbId++;
    // The host posts the query and answers later: no database call happens inside the Lua call (review, high: a
    // synchronous SELECT held the world thread through pool waits and a slow server).
    if (!_host.selectAsync(db, sql, maxRows, _cfg.maxResultBytes, id, error))
    {
        luaL_unref(_L, LUA_REGISTRYINDEX, ref);
        return false;
    }
    _db[id] = DbPending{ref, _host.nowMs() + _cfg.dbTimeoutMs};
    return true;
}

void Engine::deliverSelect(int ref, DbRows const& rows, std::string const& error)
{
    if (!error.empty() && error != "timeout")
        _host.log(LogLevel::Warn, "[lua] Db.select failed: " + error);
    CallResult r = protect("Db.select callback", _cfg.eventBudget, [&](lua_State* L)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!error.empty())
        {
            lua_pushnil(L);
            lua_pushlstring(L, error.data(), error.size());
            lua_call(L, 2, 0);
            return;
        }
        // The result is copied into Lua only when it fits the byte cap (the memory cap cannot see the host's copy).
        size_t bytes = 0;
        bool tooLarge = rows.tooLarge;
        for (size_t r = 0; r < rows.rows.size() && !tooLarge; ++r)
            for (DbValue const& v : rows.rows[r])
                if (auto const* s = std::get_if<std::string>(&v))
                {
                    bytes += s->size();
                    if (bytes > _cfg.maxResultBytes)
                    {
                        tooLarge = true;
                        break;
                    }
                }
        if (tooLarge)
        {
            lua_pushnil(L);
            lua_pushliteral(L, "too_large");
            lua_call(L, 2, 0);
            return;
        }
        lua_createtable(L, static_cast<int>(rows.rows.size()), 0);
        for (size_t r = 0; r < rows.rows.size(); ++r)
        {
            lua_createtable(L, 0, static_cast<int>(rows.columns.size()));
            for (size_t c = 0; c < rows.columns.size() && c < rows.rows[r].size(); ++c)
            {
                DbValue const& v = rows.rows[r][c];
                if (std::holds_alternative<std::monostate>(v))
                    continue;
                if (auto const* i = std::get_if<int64_t>(&v))
                    lua_pushinteger(L, static_cast<lua_Integer>(*i));
                else if (auto const* d = std::get_if<double>(&v))
                    lua_pushnumber(L, *d);
                else
                {
                    std::string const& s = std::get<std::string>(v);
                    lua_pushlstring(L, s.data(), s.size());
                }
                lua_setfield(L, -2, rows.columns[c].c_str());
            }
            lua_rawseti(L, -2, static_cast<lua_Integer>(r + 1));
        }
        lua_call(L, 1, 0);
    });
    if (!r.ok)
        _host.log(LogLevel::Warn, "[lua] Db.select callback: " + r.error);
    unrefLater(ref);
}

void Engine::pollDb(uint64_t deadlineMs)
{
    if (!_L || !onOwnerThread() || _callDepth > 0)
        return;
    // H1: only the world state polls the host's database structures (Db.select exists in test mode only, which has one
    // state; a map state has nothing to collect there).
    if (!routesHostResults())
        return;
    _host.pollDb();   // schema loads and the ready query callbacks; never blocks
    uint64_t id = 0;
    for (int i = 0; i < 32; ++i)
    {
        if (i > 0 && deadlineMs && _host.nowMs() >= deadlineMs)
            break;   // the rest of the results is collected in the next tick
        DbRows rows;
        std::string err;
        if (!_host.takeDbResult(id, rows, err))
            break;
        auto it = _db.find(id);
        if (it == _db.end())
            continue;   // already timed out or cancelled: the late answer is dropped
        int ref = it->second.ref;
        _db.erase(it);
        deliverSelect(ref, rows, err);
    }
    // The client-side timeout: MAX_EXECUTION_TIME only binds MySQL, and it does not cover pool waits or transfer.
    uint64_t now = _host.nowMs();
    std::vector<uint64_t> late;
    for (auto const& e : _db)
        if (now >= e.second.deadlineMs)
            late.push_back(e.first);
    for (uint64_t pid : late)
    {
        auto it = _db.find(pid);
        if (it == _db.end())
            continue;
        int ref = it->second.ref;
        _db.erase(it);
        _host.cancelSelect(pid);
        deliverSelect(ref, DbRows{}, "timeout");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Handlers, events

void Engine::addListenerName(std::string const& event)
{
    std::lock_guard<std::mutex> lock(_listenMutex);
    ++_listenNames[event];
    _listenerTotal.fetch_add(1);
}

void Engine::dropListenerName(std::string const& event)
{
    std::lock_guard<std::mutex> lock(_listenMutex);
    auto it = _listenNames.find(event);
    if (it == _listenNames.end())
        return;
    if (--it->second <= 0)
        _listenNames.erase(it);
    _listenerTotal.fetch_sub(1);
}

bool Engine::listening(std::string const& event) const
{
    if (_listenerTotal.load(std::memory_order_relaxed) <= 0)
        return false;
    std::lock_guard<std::mutex> lock(_listenMutex);
    return _listenNames.count(event) != 0;
}

uint64_t Engine::addHandler(std::string const& event, int ref, HandlerFilter const& filter)
{
    if (_handlers.size() >= _cfg.maxHandlers)
        return 0;
    Handler h;
    h.id = _nextHandlerId++;
    h.event = event;
    h.ref = ref;
    h.filter = filter;
    uint64_t id = h.id;
    _handlers.emplace(id, std::move(h));
    _byEvent[event].push_back(id);
    addListenerName(event);
    return id;
}

bool Engine::removeHandler(uint64_t id)
{
    auto it = _handlers.find(id);
    if (it == _handlers.end())
        return false;
    luaL_unref(_L, LUA_REGISTRYINDEX, it->second.ref);  // called from inside a protected call
    auto& ids = _byEvent[it->second.event];
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    std::string event = it->second.event;
    _handlers.erase(it);
    dropListenerName(event);
    return true;
}

namespace
{
std::string entryKey(EluKind kind, uint32_t entry, uint32_t event)
{
    return std::to_string(static_cast<int>(kind)) + "|e|" + std::to_string(entry) + "|" + std::to_string(event);
}

// Exact (full guid) or, for the legacy integer form, "any high half".
std::string uniqueKeyText(EluKind kind, UniqueKey const& u, uint32_t event)
{
    std::string k = std::to_string(static_cast<int>(kind));
    if (u.anyHigh)
        k += "|w|" + std::to_string(u.lo);
    else
        k += "|g|" + std::to_string(u.hi) + ":" + std::to_string(u.lo);
    return k + "|" + std::to_string(u.instance) + "|" + std::to_string(event);
}

uint64_t mix64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
uint64_t entryHash(EluKind kind, uint32_t entry, uint32_t event)
{
    return mix64((static_cast<uint64_t>(kind) << 56) ^ (static_cast<uint64_t>(event) << 32) ^ entry ^ 0x1111111111111111ULL);
}
// The unique index ignores the high half: it may say yes for a creature that only shares the counter, never no for a
// registered one (the emit then looks the exact key up).
uint64_t uniqueHash(EluKind kind, uint64_t lo, uint32_t instance, uint32_t event)
{
    return mix64(mix64((static_cast<uint64_t>(kind) << 56) ^ (static_cast<uint64_t>(event) << 32) ^ instance) ^ lo);
}

// One Lua value from the stack as an event value (a handle, boolean, number or string).
EventValue valueFromStack(lua_State* L, int idx)
{
    switch (lua_type(L, idx))
    {
        case LUA_TBOOLEAN: return EventValue(lua_toboolean(L, idx) != 0);
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx))
                return EventValue(static_cast<int64_t>(lua_tointeger(L, idx)));
            return EventValue(static_cast<double>(lua_tonumber(L, idx)));
        case LUA_TSTRING:
        {
            size_t n = 0;
            char const* s = lua_tolstring(L, idx, &n);
            return EventValue(std::string(s, n));
        }
        case LUA_TUSERDATA:
            if (HandleData const* h = toHandleAny(L, idx))
                return EventValue(*h);
            return EventValue();
        default: return EventValue();
    }
}
}  // namespace

void Engine::rebuildListenIndex()
{
    auto idx = std::make_shared<ListenIndex>();
    for (auto const& e : _eluHandlers)
    {
        EluHandler const& h = e.second;
        if (h.unique)
            idx->uniqueKeys.insert(uniqueHash(h.kind, h.unique->lo, h.unique->instance, h.event));
        else
            idx->entryKeys.insert(entryHash(h.kind, h.entry, h.event));
    }
    std::lock_guard<std::mutex> lock(_listenMutex);
    _listenIndex = std::move(idx);
}

uint64_t Engine::addEluHandler(EluKind kind, uint32_t entry, int ref, uint32_t event, uint32_t shots,
                               std::optional<UniqueKey> unique, std::string where)
{
    if (_handlers.size() + _eluHandlers.size() >= _cfg.maxHandlers)
        return 0;
    EluHandler h;
    h.id = _nextEluHandlerId++;
    h.kind = kind;
    h.entry = entry;
    h.unique = unique;
    h.event = event;
    h.shots = shots;
    h.ref = ref;
    h.where = std::move(where);
    uint64_t id = h.id;
    _eluByEvent[unique ? uniqueKeyText(kind, *unique, event) : entryKey(kind, entry, event)].push_back(id);
    _eluHandlers.emplace(id, std::move(h));
    rebuildListenIndex();
    return id;
}

bool Engine::removeEluHandler(uint64_t id)
{
    auto it = _eluHandlers.find(id);
    if (it == _eluHandlers.end())
        return false;
    EluHandler const& h = it->second;
    std::string const key = h.unique ? uniqueKeyText(h.kind, *h.unique, h.event) : entryKey(h.kind, h.entry, h.event);
    auto bucket = _eluByEvent.find(key);
    if (bucket != _eluByEvent.end())
    {
        auto& ids = bucket->second;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        if (ids.empty())
            _eluByEvent.erase(bucket);
    }
    if (_L)
        luaL_unref(_L, LUA_REGISTRYINDEX, h.ref);
    _eluHandlers.erase(it);
    rebuildListenIndex();
    return true;
}

size_t Engine::clearEluUniqueHandlers(EluKind kind, UniqueKey const& key, std::optional<uint32_t> event)
{
    std::vector<uint64_t> ids;
    for (auto const& e : _eluHandlers)
    {
        EluHandler const& h = e.second;
        if (h.kind != kind || !h.unique || (event && h.event != *event))
            continue;
        UniqueKey const& u = *h.unique;
        if (u.lo != key.lo || u.instance != key.instance)
            continue;
        if (key.anyHigh || u.anyHigh || u.hi == key.hi)
            ids.push_back(e.first);
    }
    for (uint64_t id : ids)
        removeEluHandler(id);
    return ids.size();
}

size_t Engine::clearEluHandlers(EluKind kind, std::optional<uint32_t> entry, std::optional<uint32_t> event)
{
    std::vector<uint64_t> ids;
    for (auto const& e : _eluHandlers)
        if (e.second.kind == kind && (!entry || (e.second.entry == *entry && !e.second.unique)) &&
            (!event || e.second.event == *event))
            ids.push_back(e.first);
    for (uint64_t id : ids)
        removeEluHandler(id);
    return ids.size();
}

bool Engine::listeningEluna(EluKind kind, uint32_t entry, uint32_t event) const
{
    std::shared_ptr<ListenIndex const> idx;
    {
        std::lock_guard<std::mutex> lock(_listenMutex);   // held for a shared_ptr copy only, never across Lua
        idx = _listenIndex;
    }
    return idx->entryKeys.count(entryHash(kind, entry, event)) != 0;
}

bool Engine::listeningElunaUnique(EluKind kind, Guid const& guid, uint32_t instance, uint32_t event) const
{
    std::shared_ptr<ListenIndex const> idx;
    {
        std::lock_guard<std::mutex> lock(_listenMutex);
        idx = _listenIndex;
    }
    return idx->uniqueKeys.count(uniqueHash(kind, guid.lo, instance, event)) != 0;
}

size_t Engine::runEluHandlers(uint32_t event, std::vector<EventValue> const& args, EventResult& out,
                              std::vector<uint64_t> const& ids)
{
    size_t calls = 0;
    std::vector<uint64_t> expired;
    // Numeric returns, shared by the folds below.
    auto numeric = [](EventValue const& v, int64_t* result) -> bool
    {
        if (auto const* i = std::get_if<int64_t>(&v.v))
        {
            *result = *i;
            return true;
        }
        if (auto const* d = std::get_if<double>(&v.v))
        {
            *result = static_cast<int64_t>(*d);
            return true;
        }
        return false;
    };
    // `ids` is a snapshot. A handler may cancel itself, a later one, or call Clear*: every id is looked up again before
    // it runs, and nothing of the map node is used after the call returns except through a fresh lookup (finding 3).
    for (uint64_t id : ids)
    {
        auto it = _eluHandlers.find(id);
        if (it == _eluHandlers.end())
            continue;
        EluKind const kind = it->second.kind;
        uint32_t const handlerEvent = it->second.event;
        int const ref = it->second.ref;
        EluEventSpec const* spec = eluEventSpec(kind, handlerEvent);
        std::vector<EventValue> rets;
        uint64_t const startedMs = _host.nowMs();
        CallResult r = protect("event " + std::string(eluKindName(kind)) + " #" + std::to_string(handlerEvent) +
                                   " handler #" + std::to_string(id),
                               _cfg.eventBudget, [&](lua_State* L)
        {
            int top = lua_gettop(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_pushinteger(L, static_cast<lua_Integer>(event));
            for (auto const& a : args)
                pushValue(L, a);
            lua_call(L, static_cast<int>(args.size()) + 1, LUA_MULTRET);
            int nres = lua_gettop(L) - top;
            for (int i = 1; i <= nres; ++i)
                rets.push_back(valueFromStack(L, top + i));
            lua_settop(L, top);
        });
        uint64_t const elapsedMs = _host.nowMs() - startedMs;
        ++calls;
        // Fold the returns by the docs convention of that event (eluEventSpec).
        bool const isTrue = !rets.empty() && std::holds_alternative<bool>(rets[0].v) && std::get<bool>(rets[0].v);
        bool const isFalse = !rets.empty() && std::holds_alternative<bool>(rets[0].v) && !std::get<bool>(rets[0].v);
        switch (spec ? spec->ret : EluReturn::None)
        {
            case EluReturn::StopOnTrue:
                if (isTrue)
                    out.handled = true;
                break;
            case EluReturn::CancelOnFalse:
                if (isFalse)
                    out.cancel = true;
                break;
            case EluReturn::ValueOnFirst:
                if (!rets.empty() && numeric(rets[0], &out.value))
                    out.hasValue = true;
                break;
            case EluReturn::StopAndValue:
                if (isTrue)
                    out.handled = true;
                if (rets.size() >= 2 && numeric(rets[1], &out.value))
                    out.hasValue = true;
                break;
            case EluReturn::TextOnSecond:
                if (isFalse)
                    out.cancel = true;
                if (rets.size() >= 2)
                    if (auto const* s = std::get_if<std::string>(&rets[1].v))
                    {
                        out.text = *s;
                        out.hasText = true;
                    }
                break;
            case EluReturn::None: break;
        }
        auto live = _eluHandlers.find(id);   // the handler may have removed itself (or been cleared) during the call
        if (live == _eluHandlers.end())
            continue;
        // E3 profile: always count the call, and log a handler that is slower than SlowHandlerMs once a minute.
        ++live->second.calls;
        live->second.totalMs += elapsedMs;
        if (elapsedMs > live->second.maxMs)
            live->second.maxMs = elapsedMs;
        if (_cfg.slowHandlerMs && elapsedMs >= _cfg.slowHandlerMs &&
            (live->second.slowLoggedMs == 0 || startedMs - live->second.slowLoggedMs >= 60000))
        {
            live->second.slowLoggedMs = startedMs == 0 ? 1 : startedMs;   // 0 is "never logged"
            ++_slowHandlers;
            std::string const where = live->second.where.empty() ? std::string() : live->second.where + ": ";
            _host.log(LogLevel::Warn, "ForeverLua: " + where + "handler " + eluKindName(kind) + " #" +
                                          std::to_string(handlerEvent) + " (#" + std::to_string(id) + ") took " +
                                          std::to_string(elapsedMs) + " ms");
        }
        if (!r.ok && r.budget && noteBreak(live->second.breaks))
        {
            std::string msg = "event handler #" + std::to_string(id) + " disabled after " +
                              std::to_string(_cfg.breaksToDisable) + " budget breaks";
            recordError("event", msg);
            _host.log(LogLevel::Error, "ForeverLua: " + msg);
            expired.push_back(id);
        }
        if (live->second.shots != 0 && --live->second.shots == 0)
            expired.push_back(id);
    }
    for (uint64_t id : expired)
        removeEluHandler(id);
    out.calls = static_cast<uint32_t>(calls);
    return calls;
}

EventResult Engine::emitEluna(EluKind kind, uint32_t entry, uint32_t event, std::vector<EventValue> const& args)
{
    EventResult out;
    std::unique_lock<std::recursive_mutex> lock;
    if (_cfg.mode == EngineMode::Eluna)
        lock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L)
        return out;   // a closed state (a router may still hold it pinned): nothing runs
    auto it = _eluByEvent.find(entryKey(kind, entry, event));
    if (it == _eluByEvent.end())
        return out;
    std::vector<uint64_t> ids = it->second;
    runEluHandlers(event, args, out, ids);
    return out;
}

EventResult Engine::emitElunaUnique(EluKind kind, Guid const& guid, uint32_t instance, uint32_t event,
                                    std::vector<EventValue> const& args)
{
    EventResult out;
    std::unique_lock<std::recursive_mutex> lock;
    if (_cfg.mode == EngineMode::Eluna)
        lock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L)
        return out;
    std::vector<uint64_t> ids;
    UniqueKey exact{guid.hi, guid.lo, instance, false};
    UniqueKey wild{0, guid.lo, instance, true};
    for (UniqueKey const& k : {exact, wild})
    {
        auto it = _eluByEvent.find(uniqueKeyText(kind, k, event));
        if (it != _eluByEvent.end())
            ids.insert(ids.end(), it->second.begin(), it->second.end());
    }
    if (ids.empty())
        return out;
    runEluHandlers(event, args, out, ids);
    return out;
}

bool Engine::post(QueuedEvent&& event)
{
    if (!listening(event.name))
        return false;
    return _queue.push(std::move(event));
}

void Engine::pumpBots()
{
    botHub().pump(*this, _host.nowMs());
}

void Engine::beginTick()
{
    _windowOpen = true;
    // The tick budget 0 means "off" (the eluna mode default): no deadline for the emits/timers of the tick.
    _windowEnd = _cfg.tickBudgetMs ? _host.nowMs() + _cfg.tickBudgetMs : (std::numeric_limits<uint64_t>::max)() / 2;
}

void Engine::emit(QueuedEvent&& event)
{
    if (!running())
        return;
    if (_cfg.mode == EngineMode::Eluna)
    {
        // Eluna: the hook's thread runs the handlers now, under the Lua mutex (owner = lock holder + depth). Nested
        // events run at once up to kMaxEluDepth; beyond that the event is dropped with one log line.
        if (_callDepth >= kMaxEluDepth || _pending)
        {
            _host.log(LogLevel::Error, "ForeverLua: event " + event.name + " dropped: nested Eluna calls at depth " +
                                           std::to_string(_callDepth));
            return;
        }
        dispatch(event, (std::numeric_limits<uint64_t>::max)() / 2);
        return;
    }
    // Only the owner thread looks at the state; any other thread queues (post() is thread-safe).
    if (onOwnerThread() && _callDepth == 0 && g_callsInFlight.load() == 0 && !_pending)
    {
        // One deadline for every emit of this tick, not a fresh tickBudgetMs per chat line or login: past it the event
        // waits for the tick (review N4).
        uint64_t now = _host.nowMs();
        if (!_windowOpen)
            beginTick();
        if (now < _windowEnd)
        {
            dispatch(event, _windowEnd);
            return;
        }
    }
    post(std::move(event));
}

bool Engine::runHandler(Handler& h, QueuedEvent const& ev)
{
    uint64_t id = h.id;
    int ref = h.ref;
    Budget b = ev.longBudget ? _cfg.commandBudget : _cfg.eventBudget;
    CallResult r = protect("event " + ev.name + " handler #" + std::to_string(id), b, [&](lua_State* L)
    {
        if (!lua_checkstack(L, static_cast<int>(ev.args.size()) + 4))
            luaL_error(L, "too many event arguments");
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        for (auto const& a : ev.args)
            pushValue(L, a);
        lua_call(L, static_cast<int>(ev.args.size()), 0);
    });
    auto it = _handlers.find(id);  // the handler may have removed itself
    if (it != _handlers.end())
    {
        ++it->second.calls;
        if (!r.ok)
        {
            ++it->second.errors;
            if (r.budget && noteBreak(it->second.breaks))
            {
                it->second.disabled = true;
                std::string msg = "handler #" + std::to_string(id) + " for " + ev.name + " disabled after " +
                                  std::to_string(_cfg.breaksToDisable) + " budget breaks";
                recordError("event " + ev.name, msg);
                _host.log(LogLevel::Error, "ForeverLua: " + msg);
            }
        }
    }
    return r.ok;
}

// Runs the handlers of p from p.next on, at least one, and stops when the deadline has passed (the rest stays in p).
size_t Engine::runPending(Pending& p, uint64_t deadlineMs)
{
    size_t calls = 0;
    while (p.next < p.ids.size())
    {
        if (!_L)
        {
            p.next = p.ids.size();
            break;
        }
        if (calls > 0 && _host.nowMs() >= deadlineMs)
            break;
        uint64_t id = p.ids[p.next++];
        auto it = _handlers.find(id);
        if (it == _handlers.end() || it->second.disabled)
            continue;
        HandlerFilter const& f = it->second.filter;
        if (f.hasEntry && !(p.ev.hasEntry && p.ev.entry == f.entry))
            continue;
        if (p.ev.isBot && !f.bots)
            continue;
        runHandler(it->second, p.ev);  // an error in one handler never stops the next
        ++calls;
    }
    return calls;
}

size_t Engine::dispatch(QueuedEvent const& ev, uint64_t deadlineMs)
{
    auto bucket = _byEvent.find(ev.name);
    if (bucket == _byEvent.end())
        return 0;
    Pending p;
    p.ev = ev;
    p.ids = bucket->second;  // handlers may be added or removed while we run
    size_t calls = runPending(p, deadlineMs);
    if (p.next < p.ids.size() && _L)
        _pending = std::move(p);  // the rest runs in the next ticks, in order, before any later event
    return calls;
}

// ---------------------------------------------------------------------------------------------------------------
// Timers

uint64_t Engine::addTimer(int ref, uint64_t delayMs, uint64_t intervalMs, uint64_t count)
{
    if (_timers.size() >= _cfg.maxTimers)
        return 0;
    return _timers.add(_host.nowMs(), delayMs, intervalMs, count, ref);
}

bool Engine::cancelTimer(uint64_t id)
{
    int ref = 0;
    if (!_timers.cancel(id, &ref))
        return false;
    // A timer cancelling itself from its own callback: tick() unrefs it after the call returns.
    if (id != _runningTimer)
        luaL_unref(_L, LUA_REGISTRYINDEX, ref);
    return true;
}

bool Engine::runOneTimer(uint64_t nowMs)
{
    TimerEntry e;
    if (!_timers.popDue(nowMs, e))
        return false;
    _runningTimer = e.id;
    int ref = e.ref;

    // An Eluna timed event (CreateLuaEvent / WorldObject:RegisterEvent): the callback gets (eventId, delay, repeats) and,
    // for an object-bound event, the object as the 4th argument; it is armed again for the next shot before the ref
    // could be released.
    auto byTimer = _eluTimerByTimerId.find(e.id);
    bool const eluna = byTimer != _eluTimerByTimerId.end();
    uint64_t publicId = 0;
    uint64_t lastDelay = 0;
    std::optional<HandleData> owner;
    if (eluna)
    {
        publicId = byTimer->second;
        _eluTimerByTimerId.erase(byTimer);
        auto meta = _eluTimers.find(publicId);
        if (meta != _eluTimers.end())
        {
            meta->second.timerId = 0;
            ++meta->second.fired;
            lastDelay = meta->second.lastDelay;
            if (meta->second.hasOwner)
            {
                // An event of an object that left the world is dropped, never run (a logged-out player's repeating
                // event must not keep firing and fill the timer table).
                if (ownerGone(meta->second.owner))
                {
                    _eluTimers.erase(meta);
                    _runningTimer = 0;
                    unrefLater(ref);
                    return true;
                }
                uint32_t m = 0, i = 0;
                if (_host.currentMap(meta->second.owner, m, i))   // a moved player: hand out the handle of its map now
                {
                    meta->second.owner.mapId = m;
                    meta->second.owner.instanceId = i;
                }
                owner = meta->second.owner;
            }
        }
    }

    CallResult r = protect(eluna ? "timer #" + std::to_string(publicId) : "timer #" + std::to_string(e.id),
                           _cfg.eventBudget, [&](lua_State* L)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!eluna)
        {
            lua_call(L, 0, 0);
            return;
        }
        auto meta = _eluTimers.find(publicId);
        uint32_t repeatsLeft = 0;
        if (meta != _eluTimers.end() && meta->second.repeats != 0 && meta->second.fired <= meta->second.repeats)
            repeatsLeft = meta->second.repeats - meta->second.fired + 1;
        lua_pushinteger(L, static_cast<lua_Integer>(publicId));
        lua_pushinteger(L, static_cast<lua_Integer>(lastDelay));
        lua_pushinteger(L, static_cast<lua_Integer>(repeatsLeft));
        if (owner)
        {
            pushHandle(L, *owner);
            lua_call(L, 4, 0);
        }
        else
            lua_call(L, 3, 0);
    });
    _runningTimer = 0;

    if (eluna)
    {
        auto meta = _eluTimers.find(publicId);
        bool const cancelled = meta == _eluTimers.end() || meta->second.cancelled;
        bool const again = !cancelled && (meta->second.repeats == 0 || meta->second.fired < meta->second.repeats);
        if (again)
        {
            if (armLuaTimer(publicId) == 0)
                unrefLater(ref);   // the timer table was full: the callback is gone, so is its registry reference
            return true;           // otherwise the ref now belongs to the new shot
        }
        if (meta != _eluTimers.end())
            _eluTimers.erase(meta);
        unrefLater(ref);
        return true;
    }

    TimerEntry* live = _timers.find(e.id);
    if (live && !r.ok && r.budget && noteBreak(live->breaks))
    {
        std::string msg = "timer #" + std::to_string(e.id) + " cancelled after " +
                          std::to_string(_cfg.breaksToDisable) + " budget breaks";
        recordError("timer", msg);
        _host.log(LogLevel::Error, "ForeverLua: " + msg);
        int dead = 0;
        _timers.cancel(e.id, &dead);
        live = nullptr;
    }
    if (!live)
        unrefLater(ref);  // finished one-shot, last repeat, cancelled from inside, or disabled
    return true;
}

uint64_t Engine::pickLuaDelay(LuaTimer const& t)
{
    // Eluna timed events take the delay as given (0 = the next tick); the 50 ms floor is the toolkit Timer's.
    // Eluna timed events take the delay as given (0 = the next tick); the 50 ms floor is the toolkit Timer's. A
    // repeating event gets at least 1 ms so an infinite 0-delay event cannot spin inside one tick.
    bool const repeating = t.repeats == 0 || t.fired + 1 < t.repeats;
    uint64_t lo = std::max<uint64_t>(t.delayMin, repeating ? 1 : 0);
    uint64_t hi = std::max<uint64_t>(t.delayMax, lo);
    if (hi == lo)
        return lo;
    _rng = _rng * 1103515245u + 12345u;
    uint64_t span = hi - lo + 1;
    return lo + (static_cast<uint64_t>(_rng >> 8) % span);
}

// Arms the next shot of a timed event (one-shot internally: the public id never changes).
uint64_t Engine::armLuaTimer(uint64_t publicId)
{
    auto it = _eluTimers.find(publicId);
    if (it == _eluTimers.end())
        return 0;
    LuaTimer& t = it->second;
    uint64_t delay = pickLuaDelay(t);
    t.lastDelay = delay;
    t.timerId = _timers.add(_host.nowMs(), delay, 0, 1, t.ref);
    if (t.timerId == 0)
    {
        _host.log(LogLevel::Error, "ForeverLua: timer limit reached, timed event " + std::to_string(publicId) +
                                       " dropped");
        _eluTimers.erase(it);
        return 0;
    }
    _eluTimerByTimerId[t.timerId] = publicId;
    return t.timerId;
}

uint64_t Engine::addLuaEvent(int ref, uint64_t delayMin, uint64_t delayMax, uint32_t repeats,
                             std::optional<HandleData> owner)
{
    if (_eluTimers.size() >= _cfg.maxTimers)
        return 0;
    uint64_t id = _nextEluTimerId++;
    LuaTimer t;
    t.delayMin = delayMin;
    t.delayMax = std::max(delayMin, delayMax);
    t.repeats = repeats;
    t.ref = ref;
    if (owner)
    {
        t.hasOwner = true;
        t.owner = *owner;
    }
    _eluTimers.emplace(id, t);
    if (armLuaTimer(id) == 0)
        return 0;
    return id;
}

// ---------------------------------------------------------------------------------------------------------------
// E5: owned WorldPacket values

bool Engine::chargePacketBytes(size_t n)
{
    // M3: packet bytes count against the state's MemoryLimitMB like the Lua heap (they live in C++, outside the Lua
    // allocator, so the cap would not see them otherwise).
    if (n == 0)
        return true;
    if (_mem->used + n > _mem->cap)
    {
        ++_mem->failures;
        return false;
    }
    _mem->used += n;
    return true;
}

void Engine::releasePacket(std::map<uint64_t, PacketValue>::iterator it)
{
    _mem->used -= (std::min)(_mem->used, it->second.charged);
    _packets.erase(it);
}

std::optional<HandleData> Engine::addPacket(uint32_t opcode, size_t size, std::string& error)
{
    if (size > kMaxPacketBytes)
        size = kMaxPacketBytes;
    // M4: the size is what the core's WorldPacket(opcode, res) treats it as, a reserve: the packet starts empty and the
    // Write* calls fill it from byte 0.
    if (!chargePacketBytes(size))
    {
        error = "not enough memory for the packet (the state's MemoryLimitMB)";
        return std::nullopt;
    }
    uint64_t const id = _nextPacketId++;
    PacketValue p;
    p.opcode = opcode;
    p.bytes.reserve(size);
    p.charged = size;
    _packets.emplace(id, std::move(p));
    _packetOrder.push_back(id);
    while (_packetOrder.size() > kMaxPackets)
    {
        auto old = _packets.find(_packetOrder.front());
        if (old != _packets.end())
            releasePacket(old);
        _packetOrder.pop_front();
    }
    HandleData h;
    h.kind = ObjKind::Packet;
    h.guid.lo = id;
    return h;
}

bool Engine::packetGrow(PacketValue& p, size_t n, std::string& error)
{
    if (p.bytes.size() + n > kMaxPacketBytes)
    {
        error = "WorldPacket: the packet would exceed " + std::to_string(kMaxPacketBytes) + " bytes";
        return false;
    }
    size_t const need = p.bytes.size() + n;
    if (need > p.charged)
    {
        if (!chargePacketBytes(need - p.charged))
        {
            error = "WorldPacket: not enough memory (the state's MemoryLimitMB)";
            return false;
        }
        p.charged = need;
    }
    return true;
}

EventResult Engine::emitPacketEvent(uint32_t opcode, uint32_t event, std::vector<uint8_t> const& bytes,
                                    EventValue const& player)
{
    // H4: the copy is made, used and freed inside one hold of this state's Lua lock: a packet hook runs on any map
    // thread (WorldSession::SendPacket, WorldSession::Update) and must never touch the packet table outside it.
    EventResult out;
    std::unique_lock<std::recursive_mutex> lock;
    if (_cfg.mode == EngineMode::Eluna)
        lock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L || !listeningEluna(EluKind::Packet, opcode, event))
        return out;
    size_t const size = (std::min)(bytes.size(), kMaxPacketBytes);
    EventValue packetValue;
    uint64_t id = 0;
    if (chargePacketBytes(size))
    {
        // M3: an event copy is not in the script ring (it cannot push a script's own packet out) and lives for this
        // event only; a handle kept past it raises "WorldPacket no longer exists".
        id = _nextPacketId++;
        PacketValue p;
        p.opcode = opcode;
        p.bytes.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        p.charged = size;
        _packets.emplace(id, std::move(p));
        HandleData h;
        h.kind = ObjKind::Packet;
        h.guid.lo = id;
        packetValue = EventValue(h);
    }
    else if (noteHookGap("packet-copy-memory"))
        _host.log(LogLevel::Warn, "ForeverLua: a packet event got no packet copy: the state's MemoryLimitMB is reached");
    out = emitEluna(EluKind::Packet, opcode, event, {packetValue, player});
    if (id)
    {
        auto it = _packets.find(id);
        if (it != _packets.end())
            releasePacket(it);
    }
    return out;
}

void Engine::dropPacket(uint64_t id)
{
    auto it = _packets.find(id);
    if (it != _packets.end())
        releasePacket(it);   // a stale id stays in _packetOrder until the ring pops it
}

size_t Engine::packetCount() const
{
    std::unique_lock<std::recursive_mutex> lock;
    if (_cfg.mode == EngineMode::Eluna)
        lock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    return _packets.size();
}

Engine::PacketValue* Engine::packet(uint64_t id)
{
    auto it = _packets.find(id);
    return it == _packets.end() ? nullptr : &it->second;
}

Engine::PacketValue const* Engine::packet(uint64_t id) const
{
    auto it = _packets.find(id);
    return it == _packets.end() ? nullptr : &it->second;
}

namespace
{
// An object-bound event belongs to the object, not to the map it was on when the handle was made.
bool sameObject(HandleData const& a, HandleData const& b)
{
    if (a.kind != b.kind)
        return false;
    if (a.kind == ObjKind::Map)
        return a == b;
    return a.guid == b.guid;
}
}  // namespace

bool Engine::ownerGone(HandleData const& owner)
{
    // Only a thread that may touch the object asks the game: the tick runs on the world thread with the maps idle. Any
    // other answer means "someone else's to look at", and the event stays.
    Access const access = _host.checkAccess(owner);
    if (access != Access::Ok)
        return access == Access::Gone;
    ObjectInfo info;
    return !_host.lookup(owner, &info);
}

void Engine::trackWorldObjectData(HandleData const& owner, std::string const& guidKey)
{
    _worldObjectDataOwners[guidKey] = owner;
}

void Engine::eraseWorldObjectDataKey(std::string const& key)
{
    _worldObjectDataOwners.erase(key);
    if (!_L)
        return;
    lua_getfield(_L, LUA_REGISTRYINDEX, "fl.WorldObjectData");
    if (lua_istable(_L, -1))
    {
        lua_pushnil(_L);
        lua_setfield(_L, -2, key.c_str());
    }
    lua_pop(_L, 1);
}

void Engine::removeWorldObjectData(HandleData const& owner)
{
    eraseWorldObjectDataKey(guidText(owner.guid));
}

size_t Engine::removeLuaEventsOf(HandleData const& owner)
{
    std::unique_lock<std::recursive_mutex> lock;
    if (_cfg.mode == EngineMode::Eluna)
        lock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    removeWorldObjectData(owner);
    return removeLuaEvents(owner, false);
}

void Engine::sweepOwners(uint64_t nowMs)
{
    (void)nowMs;
    // Rotating, bounded: at most 256 distinct owners per second are looked at.
    constexpr size_t kMaxOwnersPerSweep = 256;
    std::vector<HandleData> owners;
    uint64_t last = _sweepCursor;
    auto it = _eluTimers.upper_bound(_sweepCursor);
    for (; it != _eluTimers.end() && owners.size() < kMaxOwnersPerSweep; ++it)
    {
        last = it->first;
        if (!it->second.hasOwner)
            continue;
        bool seen = false;
        for (auto const& o : owners)
            seen = seen || sameObject(o, it->second.owner);
        if (!seen)
            owners.push_back(it->second.owner);
    }
    _sweepCursor = it == _eluTimers.end() ? 0 : last;
    for (auto const& o : owners)
        if (ownerGone(o))
            removeLuaEvents(o, false);

    if (!_worldObjectDataOwners.empty())
    {
        std::vector<std::string> dataGone;
        std::string lastKey = _dataSweepCursor;
        auto dit = _dataSweepCursor.empty() ? _worldObjectDataOwners.begin()
                                            : _worldObjectDataOwners.upper_bound(_dataSweepCursor);
        size_t checked = 0;
        for (; dit != _worldObjectDataOwners.end() && checked < kMaxOwnersPerSweep; ++dit, ++checked)
        {
            lastKey = dit->first;
            if (ownerGone(dit->second))
                dataGone.push_back(dit->first);
        }
        _dataSweepCursor = dit == _worldObjectDataOwners.end() ? std::string() : lastKey;
        for (std::string const& key : dataGone)
            eraseWorldObjectDataKey(key);
    }
}

bool Engine::removeLuaEvent(uint64_t id, std::optional<HandleData> owner, bool onlyGlobal)
{
    auto it = _eluTimers.find(id);
    if (it == _eluTimers.end())
        return false;
    if (onlyGlobal && it->second.hasOwner)
        return false;
    if (owner && (!it->second.hasOwner || !sameObject(it->second.owner, *owner)))
        return false;
    if (it->second.timerId != 0)
    {
        int ref = 0;
        _eluTimerByTimerId.erase(it->second.timerId);
        if (_timers.cancel(it->second.timerId, &ref) && ref != 0)
            unrefLater(ref);
        _eluTimers.erase(it);
    }
    else
    {
        it->second.cancelled = true;   // removed from inside its own callback: the ref is released after the call
    }
    return true;
}

size_t Engine::removeLuaEvents(std::optional<HandleData> owner, bool onlyGlobal)
{
    std::vector<uint64_t> ids;
    for (auto const& e : _eluTimers)
    {
        if (onlyGlobal && e.second.hasOwner)
            continue;
        if (owner && !(e.second.hasOwner && sameObject(e.second.owner, *owner)))
            continue;
        ids.push_back(e.first);
    }
    for (uint64_t id : ids)
        removeLuaEvent(id, owner, onlyGlobal);
    return ids.size();
}

bool Engine::runOneEvent(uint64_t deadlineMs)
{
    if (_pending)
    {
        Pending p = std::move(*_pending);
        _pending.reset();
        runPending(p, deadlineMs);
        if (p.next < p.ids.size() && _L)
            _pending = std::move(p);
        return true;
    }
    QueuedEvent ev;
    if (!_queue.pop(ev))
        return false;
    dispatch(ev, deadlineMs);
    return true;
}

size_t Engine::finishPending()
{
    size_t calls = 0;
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L || !onOwnerThread() || _callDepth > 0)
        return 0;
    uint64_t deadline = _host.nowMs() + _cfg.shutdownBudgetMs;
    pollDb(deadline);
    pollAsyncQueries(_host.nowMs());
    flushData(_host.nowMs(), true);
    // Both what a long emit left pending AND the queue: server.stop is queued whenever any event was still pending when
    // it was emitted, and the old drain looked only at _pending, so the stop handlers were silently dropped (N3).
    bool progress = true;
    while (progress && _L)
    {
        progress = false;
        if (_pending)
        {
            Pending p = std::move(*_pending);
            _pending.reset();
            calls += runPending(p, deadline);
            if (p.next < p.ids.size() && _L)
                _pending = std::move(p);
            progress = true;
        }
        else
        {
            QueuedEvent ev;
            if (_queue.pop(ev))
            {
                dispatch(ev, deadline);
                progress = true;
            }
        }
        if (_host.nowMs() >= deadline)
            break;   // bounded: the rest is dropped with the close (documented: Config::shutdownBudgetMs)
    }
    return calls;
}

size_t Engine::tick()
{
    if (!running())
        return 0;
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!onOwnerThread())
    {
        ++_wrongThread;
        return 0;
    }
    if (_callDepth > 0)
        return 0;
    uint64_t start = _host.nowMs();
    if (!_windowOpen || start >= _windowEnd)
        beginTick();   // the tick gets its own window when the emits of this tick used theirs up
    uint64_t deadline = _windowEnd;
    size_t items = 0;
    auto over = [&] { return _host.nowMs() >= deadline; };
    // A budget of 0 means "off", but one tick never runs an unbounded number of Lua items (a script bug must not hold
    // the world thread): the rest waits for the next tick.
    constexpr size_t kMaxTickItems = 10000;
    drainDeferredUnrefs();
    if (_cfg.mode == EngineMode::Eluna && start >= _nextOwnerSweepMs)
    {
        _nextOwnerSweepMs = start + 1000;
        sweepOwners(start);
    }
    pollDb(deadline);
    pollAsyncQueries(start);
    if (!_dataOwner && !dataAvailable() && !_dataInherited && start >= _nextDataLoadMs)
    {
        _nextDataLoadMs = start + kDataLoadRetryMs;
        loadData();   // the table could not be read at start: try again
    }
    flushData(start, false);
    bool progress = true;
    while (progress && _L)
    {
        progress = false;
        if (items && over())
            break;
        if (items >= kMaxTickItems)
            break;
        if (runOneTimer(_host.nowMs()))
        {
            progress = true;
            ++items;
        }
        if (items && over())
            break;
        if (runOneEvent(deadline))
        {
            progress = true;
            ++items;
        }
    }
    return items;
}

// ---------------------------------------------------------------------------------------------------------------
// Commands

bool Engine::registerCommand(std::string const& root, int ref, int minSecurity)
{
    std::string r = lower(root);
    if (!_forwarded.count(r))
        return false;
    auto it = _commands.find(r);
    if (it != _commands.end())
        luaL_unref(_L, LUA_REGISTRYINDEX, it->second.ref);  // a later register replaces the handler
    _commands[r] = Command{ref, minSecurity};
    return true;
}

void Engine::appendReply(std::string const& text)
{
    if (_replyLines.size() >= _cfg.maxReplyLines)
        return;
    _replyLines.push_back(text.size() > _cfg.maxReplyLineBytes ? text.substr(0, _cfg.maxReplyLineBytes) : text);
}

CommandResult Engine::runCommand(std::string const& root, std::string const& tail, int security,
                                 std::optional<HandleData> player)
{
    CommandResult res;
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L || !onOwnerThread() || _callDepth > 0 || _bridgeDepth > 0)
    {
        if (_L)
            ++_wrongThread;
        res.status = CommandStatus::Refused;
        res.error = "command refused: engine not running, wrong thread or re-entrant call";
        return res;
    }
    auto it = _commands.find(lower(root));
    if (it == _commands.end())
    {
        res.status = CommandStatus::NoHandler;
        res.error = "no handler for '" + root + "'";
        return res;
    }
    if (security < it->second.minSecurity)
    {
        res.status = CommandStatus::Denied;
        res.error = "access denied";
        res.lines.push_back("You may not use this command.");
        return res;
    }
    ParsedTail pt = parseTail(tail);
    if (!pt.error.empty())
    {
        res.status = CommandStatus::Refused;
        res.error = pt.error;
        res.lines.push_back("Bad command: " + pt.error);
        return res;
    }

    _replyLines.clear();
    int ref = it->second.ref;
    CallResult r = protect("command " + lower(root), _cfg.commandBudget, [&](lua_State* L)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_createtable(L, 0, 6);
        if (player)
        {
            pushHandle(L, *player);
            lua_setfield(L, -2, "player");
        }
        lua_pushinteger(L, security);
        lua_setfield(L, -2, "security");
        lua_pushlstring(L, tail.data(), tail.size());
        lua_setfield(L, -2, "tail");
        lua_createtable(L, static_cast<int>(pt.args.size()), 0);
        for (size_t i = 0; i < pt.args.size(); ++i)
        {
            lua_pushlstring(L, pt.args[i].data(), pt.args[i].size());
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_setfield(L, -2, "args");
        if (pt.id)
        {
            lua_pushlstring(L, pt.id->data(), pt.id->size());
            lua_setfield(L, -2, "id");
        }
        luaL_setmetatable(L, "fl.Ctx");
        lua_call(L, 1, 0);
    });
    res.lines = std::move(_replyLines);
    _replyLines.clear();
    if (!r.ok)
    {
        res.status = CommandStatus::Error;
        res.error = r.error;
        res.lines.push_back("Lua error: " + r.error);
    }
    return res;
}

// ---------------------------------------------------------------------------------------------------------------
// E3: profiling, asynchronous raw SQL, ForeverData and RegisterCommand

std::vector<HandlerProfile> Engine::eluProfile() const
{
    std::vector<HandlerProfile> out;
    out.reserve(_eluHandlers.size());
    for (auto const& e : _eluHandlers)
    {
        EluHandler const& h = e.second;
        HandlerProfile p;
        p.id = h.id;
        p.kind = h.kind;
        p.event = h.event;
        p.entry = h.entry;
        p.where = h.where;
        p.calls = h.calls;
        p.totalMs = h.totalMs;
        p.maxMs = h.maxMs;
        out.push_back(std::move(p));
    }
    return out;
}

void Engine::resetEluProfile()
{
    for (auto& e : _eluHandlers)
    {
        e.second.calls = 0;
        e.second.totalMs = 0;
        e.second.maxMs = 0;
        e.second.slowLoggedMs = 0;
    }
    _slowHandlers = 0;
}

bool Engine::startAsyncQuery(std::string const& db, std::string const& sql, int ref, std::string& error)
{
    if (!_L || !running())
    {
        error = "engine not running";
        return false;
    }
    uint64_t const id = g_nextAsyncId.fetch_add(1);
    routeAsync(id, _asyncInbox);   // before the host sees it: a result is routable from its first moment
    if (!_host.rawQueryAsync(db, sql, 1024, 1u << 20, id, error))
    {
        unrouteAsync(id);
        return false;
    }
    AsyncPending p;
    p.ref = ref;
    p.db = db;
    p.deadlineMs = _cfg.dbTimeoutMs ? _host.nowMs() + _cfg.dbTimeoutMs : (std::numeric_limits<uint64_t>::max)() / 2;
    _asyncQueries.emplace(id, std::move(p));
    return true;
}

void Engine::pollAsyncQueries(uint64_t nowMs)
{
    if (!_L)
        return;
    // 0. The world state takes the host's finished queries and hands each to the inbox of the state that issued it
    //    (H2). A map state never touches the host's queue.
    if (routesHostResults())
    {
        uint64_t rid = 0;
        DbRows rrows;
        std::string rerror;
        size_t taken = 0;
        while (taken++ < 1024 && _host.takeRawQueryResult(rid, rrows, rerror))
        {
            deliverAsync(rid, std::move(rrows), std::move(rerror));
            rrows = DbRows{};
            rerror.clear();
        }
    }
    // 1. Finished queries of this state (its own inbox). A result for a query that timed out is dropped.
    std::deque<AsyncInbox::Done> mine;
    {
        std::lock_guard<std::mutex> inboxLock(_asyncInbox->mutex);
        mine.swap(_asyncInbox->done);
    }
    for (auto& done : mine)
    {
        uint64_t const id = done.id;
        DbRows rows = std::move(done.rows);
        std::string const error = std::move(done.error);
        auto it = _asyncQueries.find(id);
        if (it == _asyncQueries.end())
            continue;
        int const ref = it->second.ref;
        std::string const db = it->second.db;
        _asyncQueries.erase(it);
        bool const answered = error.empty() && !rows.rows.empty() && !rows.columns.empty() && !rows.tooLarge;
        if (!error.empty())
            _host.log(LogLevel::Warn, "ForeverLua: " + db + " async query failed: " + error);
        protect("async query", _cfg.eventBudget, [&](lua_State* L)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            if (answered)
                eluPushQuery(L, std::move(rows));
            else
                lua_pushnil(L);
            lua_call(L, 1, 0);
        });
        luaL_unref(_L, LUA_REGISTRYINDEX, ref);
    }
    // 2. The client-side deadline (DbTimeoutMs): the callback answers nil, the late result is cancelled.
    for (auto it = _asyncQueries.begin(); it != _asyncQueries.end();)
    {
        if (nowMs < it->second.deadlineMs)
        {
            ++it;
            continue;
        }
        int const ref = it->second.ref;
        std::string const db = it->second.db;
        unrouteAsync(it->first);
        _host.cancelRawQuery(it->first);
        it = _asyncQueries.erase(it);
        _host.log(LogLevel::Warn, "ForeverLua: " + db + " async query timed out after " +
                                      std::to_string(_cfg.dbTimeoutMs) + " ms");
        protect("async query timeout", _cfg.eventBudget, [&](lua_State* L)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_pushnil(L);
            lua_call(L, 1, 0);
        });
        luaL_unref(_L, LUA_REGISTRYINDEX, ref);
    }
}

namespace
{
// The 255-byte column of the table: a longer file path keeps its first bytes and a hash of the whole path.
std::string namespaceOf(std::string const& rel)
{
    if (rel.size() <= Engine::kMaxDataKey)
        return rel;
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : rel)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
    return rel.substr(0, 230) + "#" + hex;
}
}  // namespace

void Engine::noteScriptChunk(std::string const& source, std::string const& rel)
{
    if (_scriptChunks.size() < 8192)
        _scriptChunks[source] = rel;
}

bool Engine::isScriptChunk(std::string const& source) const
{
    return _scriptChunks.count(source) != 0;
}

std::string Engine::dataNamespace(lua_State* L) const
{
    // The first Lua frame above the running C function: a pcall (or any other C function) between the script and
    // ForeverData is skipped, and the name comes from the loader's table, never from a chunk name a script chose.
    lua_Debug ar{};
    for (int level = 1; L && lua_getstack(L, level, &ar); ++level)
    {
        if (!lua_getinfo(L, "S", &ar))
            break;
        if (ar.what && ar.what[0] == 'C')
            continue;
        auto it = _scriptChunks.find(ar.source ? ar.source : "");
        return it == _scriptChunks.end() ? std::string("chunk") : namespaceOf(it->second);
    }
    return "chunk";
}

bool Engine::loadData()
{
    if (_dataOwner)
        return true;   // a map state: the world state loads the one store
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    if (_dataLoaded)
        return true;
    std::vector<DataRow> rows;
    std::string error;
    if (!_host.dataLoad(rows, error))
    {
        if (!_dataLoadLogged)
            _host.log(LogLevel::Error, "ForeverLua: ForeverData could not be loaded: " + error +
                                           ". The store stays unavailable (reads answer nil, writes are refused) and the load is "
                                           "retried every 30 seconds");
        _dataLoadLogged = true;
        _nextDataLoadMs = _host.nowMs() + kDataLoadRetryMs;
        return false;
    }
    // Bounded: a row that breaks a bound is skipped (logged below, left untouched in the table).
    size_t skipped = 0;
    for (auto& r : rows)
    {
        size_t const charge = r.key.size() + r.value.size();
        bool const bad = r.script.empty() || r.key.empty() || r.key.size() > kMaxDataKey || r.value.size() > kMaxDataValue ||
                         _dataScriptBytes[r.script] + charge > kMaxDataScript ||
                         (_data[r.script].count(r.key) == 0 && _data[r.script].size() >= kMaxDataKeysPerScript) ||
                         _dataBytes + charge > kMaxDataBytes || _dataKeyCount >= kMaxDataKeys;
        if (bad)
        {
            ++skipped;
            if (_data[r.script].empty())
                _data.erase(r.script);
            continue;
        }
        dataPut(r.script, r.key, r.value, false);
    }
    if (skipped)
        _host.log(LogLevel::Warn, "ForeverLua: " + std::to_string(skipped) +
                                      " stored ForeverData rows were not loaded (empty or oversized key, value over 64 KB, "
                                      "or a namespace over its bound); they stay in the table untouched");
    _dataLoaded = true;
    _dataLoadLogged = false;
    return true;
}

void Engine::markDirty(DataKey const& key)
{
    _dataDirty[key] = ++_dataVersion;
}

void Engine::dataPut(std::string const& script, std::string const& key, std::string const& json, bool dirty)
{
    auto& values = _data[script];
    auto it = values.find(key);
    size_t const oldCharge = it == values.end() ? 0 : key.size() + it->second.size();
    size_t const newCharge = key.size() + json.size();
    if (it == values.end())
        ++_dataKeyCount;
    _dataScriptBytes[script] += newCharge;
    _dataScriptBytes[script] -= oldCharge;
    _dataBytes += newCharge;
    _dataBytes -= oldCharge;
    values[key] = json;
    if (dirty)
        markDirty({script, key});
}

bool Engine::dataDrop(std::string const& script, std::string const& key)
{
    auto s = _data.find(script);
    if (s == _data.end())
        return false;
    auto k = s->second.find(key);
    if (k == s->second.end())
        return false;
    size_t const charge = key.size() + k->second.size();
    _dataScriptBytes[script] -= charge;
    _dataBytes -= charge;
    --_dataKeyCount;
    s->second.erase(k);
    if (s->second.empty())
    {
        _data.erase(s);
        _dataScriptBytes.erase(script);
    }
    markDirty({script, key});   // the deletion still has to reach the database
    return true;
}

bool Engine::dataAvailable() const
{
    if (_dataOwner)
        return _dataOwner->dataAvailable();
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    return _dataLoaded;
}

size_t Engine::dataPending() const
{
    if (_dataOwner)
        return _dataOwner->dataPending();
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    return _dataDirty.size();
}

bool Engine::dataGet(std::string const& script, std::string const& key, std::string& json) const
{
    if (_dataOwner)
        return _dataOwner->dataGet(script, key, json);
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    if (!_dataLoaded)
        return false;
    auto s = _data.find(script);
    if (s == _data.end())
        return false;
    auto k = s->second.find(key);
    if (k == s->second.end())
        return false;
    json = k->second;
    return true;
}

bool Engine::dataSet(std::string const& script, std::string const& key, std::string const& json, std::string& error)
{
    if (_dataOwner)
        return _dataOwner->dataSet(script, key, json, error);
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    if (!_dataLoaded)
    {
        error = "ForeverData is unavailable: the stored values could not be read (see the server log)";
        return false;
    }
    if (script.empty())
    {
        error = "no script namespace";
        return false;
    }
    if (key.empty() || key.size() > kMaxDataKey)
    {
        error = "key must be 1..255 bytes";
        return false;
    }
    if (json.size() > kMaxDataValue)
    {
        error = "value is larger than 64 KB";
        return false;
    }
    size_t oldCharge = 0;
    bool const isNew = [&]
    {
        auto s = _data.find(script);
        if (s == _data.end())
            return true;
        auto k = s->second.find(key);
        if (k == s->second.end())
            return true;
        oldCharge = key.size() + k->second.size();
        return false;
    }();
    size_t const newCharge = key.size() + json.size();
    auto sb = _dataScriptBytes.find(script);
    size_t const scriptBytes = sb == _dataScriptBytes.end() ? 0 : sb->second;
    if (scriptBytes - oldCharge + newCharge > kMaxDataScript)
    {
        error = "the script already stores 1 MB";
        return false;
    }
    if (isNew)
    {
        auto s = _data.find(script);
        if ((s != _data.end() && s->second.size() >= kMaxDataKeysPerScript) || _dataKeyCount >= kMaxDataKeys)
        {
            error = "too many keys (16384 per script, 262144 in all)";
            return false;
        }
    }
    if (_dataBytes - oldCharge + newCharge > kMaxDataBytes)
    {
        error = "ForeverData is full (64 MB in all)";
        return false;
    }
    dataPut(script, key, json);
    return true;
}

bool Engine::dataRemove(std::string const& script, std::string const& key)
{
    if (_dataOwner)
        return _dataOwner->dataRemove(script, key);
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    if (!_dataLoaded)
        return false;
    return dataDrop(script, key);
}

std::vector<std::string> Engine::dataKeys(std::string const& script) const
{
    if (_dataOwner)
        return _dataOwner->dataKeys(script);
    std::vector<std::string> out;
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    if (!_dataLoaded)
        return out;
    auto s = _data.find(script);
    if (s != _data.end())
        for (auto const& k : s->second)
            out.push_back(k.first);
    return out;
}

void Engine::pollDataWrites()
{
    std::vector<DataWriteResult> results;
    _host.takeDataWrites(results);
    size_t failed = 0;
    std::string firstError;
    for (auto const& r : results)
    {
        auto it = _dataInflight.find(r.token);
        if (it == _dataInflight.end())
            continue;   // a write of a retired engine, or one that was dropped at shutdown
        DataKey const key = it->second.key;
        uint64_t const version = it->second.version;
        _dataInflight.erase(it);
        _dataInflightKeys.erase(key);
        if (!r.ok)
        {
            ++failed;
            if (firstError.empty())
                firstError = r.error;
            continue;   // stays dirty: the next flush tries again
        }
        auto d = _dataDirty.find(key);
        if (d != _dataDirty.end() && d->second == version)
            _dataDirty.erase(d);   // confirmed, and not changed again since it was sent
    }
    if (failed)
        _host.log(LogLevel::Warn, "ForeverLua: " + std::to_string(failed) + " ForeverData writes failed (" + firstError +
                                      "); they stay dirty and are retried");
}

void Engine::flushData(uint64_t nowMs, bool force)
{
    if (!_dataWriter || _dataOwner)
        return;   // a candidate never writes; a map state's values are the world state's, which writes them
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    pollDataWrites();
    if (!_dataLoaded)
        return;
    if (!force && nowMs < _nextDataFlushMs)
        return;
    _nextDataFlushMs = nowMs + (_cfg.dataFlushMs ? _cfg.dataFlushMs : 1);
    size_t sent = 0;
    for (auto const& d : _dataDirty)
    {
        if (_dataInflightKeys.count(d.first))
            continue;   // the key has a write in flight: its completion decides whether it is still dirty
        DataWrite w;
        w.token = g_nextDataToken.fetch_add(1);
        w.script = d.first.first;
        w.key = d.first.second;
        auto s = _data.find(w.script);
        if (s != _data.end())
        {
            auto v = s->second.find(w.key);
            if (v != s->second.end())
                w.value = v->second;
            else
                w.erase = true;
        }
        else
            w.erase = true;
        if (!_host.dataWrite(w))
            break;   // the host's queue is full: the rest stays dirty for the next flush
        _dataInflight[w.token] = DataInflight{d.first, d.second};
        _dataInflightKeys.insert(d.first);
        if (++sent >= 512)
            break;
    }
}

bool Engine::flushDataBlocking()
{
    if (!_dataWriter || _dataOwner)
        return true;
    std::lock_guard<std::recursive_mutex> dataLock(_dataMutex);
    if (!_dataLoaded)
        return true;
    uint64_t const start = _host.nowMs();
    uint64_t const deadline = start + _cfg.dataShutdownMs;
    for (;;)
    {
        flushData(_host.nowMs(), true);
        if (_dataDirty.empty() && _dataInflight.empty())
            return true;
        if (_host.nowMs() >= deadline)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::string names;
    size_t shown = 0;
    for (auto const& d : _dataDirty)
    {
        if (shown++ >= 5)
            break;
        names += (names.empty() ? "" : ", ") + d.first.first + ":" + d.first.second.substr(0, 40);
    }
    _host.log(LogLevel::Error, "ForeverLua: " + std::to_string(_dataDirty.size()) +
                                   " ForeverData changes were not written to the database at shutdown (first: " + names + ")");
    _dataDirty.clear();
    _dataInflight.clear();
    _dataInflightKeys.clear();
    return false;
}

bool Engine::registerLuaCommand(std::string const& name, int minSecurity, int ref, std::string const& help,
                                std::string& error, std::string const& whereText)
{
    if (_cfg.mode != EngineMode::Eluna)
    {
        error = "RegisterCommand is only available in eluna mode";
        return false;
    }
    std::string cmd = lower(name);
    while (!cmd.empty() && cmd.front() == '.')
        cmd.erase(cmd.begin());
    if (cmd.empty() || cmd.size() > 32)
    {
        error = "the command name must be 1..32 characters";
        return false;
    }
    for (char c : cmd)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
        {
            error = "the command name may only contain a-z, 0-9, '_' and '-'";
            return false;
        }
    // A name that equals or prefixes a root of the core's command table, "lua" or a configured root is refused: the core
    // aborts the server when two commands share a name, and a prefix would hijack a core command's short form.
    // (Checked here for the module's own roots, and by the host for the core's table.)
    for (std::string const& root : _forwarded)
        if (root.compare(0, cmd.size(), cmd) == 0)
        {
            error = "the name '" + cmd + "' equals or starts a command that already exists (." + root + ")";
            return false;
        }
    if (std::string("lua").compare(0, cmd.size(), cmd) == 0 || _host.commandNameReserved(cmd))
    {
        error = "the name '" + cmd + "' equals or starts the name of a core command";
        return false;
    }
    for (auto& c : _luaCommands)
        if (c.name == cmd)
        {
            if (_L)
                luaL_unref(_L, LUA_REGISTRYINDEX, c.ref);
            c.ref = ref;
            c.minSecurity = minSecurity;
            c.help = help;
            c.where = whereText;
            return true;   // the command map names are unchanged: nothing to rebuild
        }
    if (_luaCommands.size() >= kMaxLuaCommands)
    {
        error = "32 Lua commands are already registered";
        return false;
    }
    _luaCommands.push_back(LuaCommand{cmd, help, whereText, minSecurity, ref});
    if (tellsHostCommands())
        _host.commandsChanged();   // a candidate state tells the host nothing: adoptState does, and only when the names differ
    return true;
}

CommandResult Engine::runLuaCommand(size_t index, std::string const& tail, int security,
                                    std::optional<HandleData> player, std::string const& expectedName)
{
    CommandResult res;
    std::unique_lock<std::recursive_mutex> luaLock;
    if (_cfg.mode == EngineMode::Eluna)
        luaLock = std::unique_lock<std::recursive_mutex>(_luaMutex);
    if (!_L || !onOwnerThread() || _callDepth > 0 || _bridgeDepth > 0)
    {
        if (_L)
            ++_wrongThread;
        res.status = CommandStatus::Refused;
        res.error = "command refused: engine not running, wrong thread or re-entrant call";
        return res;
    }
    if (index >= _luaCommands.size())
    {
        res.status = CommandStatus::NoHandler;
        res.error = "no such Lua command";
        return res;
    }
    LuaCommand const& cmd = _luaCommands[index];
    if (!expectedName.empty() && cmd.name != expectedName)
    {
        // The core's map still carries the name of an older command list (a reload or RegisterCommand ran and the map is
        // rebuilt on the next world tick): never run another command under this name.
        res.status = CommandStatus::NoHandler;
        res.error = "the command was replaced";
        return res;
    }
    if (security < cmd.minSecurity)
    {
        res.status = CommandStatus::Denied;
        res.error = "access denied";
        res.lines.push_back("You may not use this command.");
        return res;
    }
    // Whitespace-split arguments, bounded (32 words of at most 512 bytes).
    std::vector<std::string> words;
    std::string cur;
    for (char c : tail)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (!cur.empty())
            {
                if (words.size() < 32)
                    words.push_back(cur);
                cur.clear();
            }
        }
        else if (cur.size() < 512)
            cur += c;
    }
    if (!cur.empty() && words.size() < 32)
        words.push_back(cur);

    int const ref = cmd.ref;
    std::vector<std::string> lines;
    CallResult r = protect("command " + cmd.name, _cfg.commandBudget, [&](lua_State* L)
    {
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (player)
            pushHandle(L, *player);
        else
            lua_pushnil(L);
        lua_createtable(L, static_cast<int>(words.size()), 0);
        for (size_t i = 0; i < words.size(); ++i)
        {
            lua_pushlstring(L, words[i].data(), words[i].size());
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_call(L, 2, LUA_MULTRET);
        int nres = lua_gettop(L) - top;
        for (int i = 1; i <= nres && lines.size() < _cfg.maxReplyLines; ++i)
        {
            int const idx = top + i;
            if (lua_type(L, idx) == LUA_TSTRING)
            {
                size_t n = 0;
                char const* s = lua_tolstring(L, idx, &n);
                lines.emplace_back(s, n > _cfg.maxReplyLineBytes ? _cfg.maxReplyLineBytes : n);
            }
            else if (lua_type(L, idx) == LUA_TTABLE)
            {
                lua_Integer const count = static_cast<lua_Integer>(lua_rawlen(L, idx));
                for (lua_Integer j = 1; j <= count && lines.size() < _cfg.maxReplyLines; ++j)
                {
                    lua_rawgeti(L, idx, j);
                    if (lua_type(L, -1) == LUA_TSTRING)
                    {
                        size_t n = 0;
                        char const* s = lua_tolstring(L, -1, &n);
                        lines.emplace_back(s, n > _cfg.maxReplyLineBytes ? _cfg.maxReplyLineBytes : n);
                    }
                    lua_pop(L, 1);
                }
            }
        }
        lua_settop(L, top);
    });
    res.lines = std::move(lines);
    if (!r.ok)
    {
        res.status = CommandStatus::Error;
        res.error = r.error;
        res.lines.push_back("Lua error: " + r.error);
    }
    return res;
}

// ---------------------------------------------------------------------------------------------------------------
// Status

EngineStatus Engine::status() const
{
    EngineStatus s;
    s.running = running();
    s.memoryUsed = _mem->used;
    s.memoryCap = _mem->cap ? _mem->cap : _cfg.memoryCap;
    s.handlers = _handlers.size();
    for (auto const& h : _handlers)
        if (h.second.disabled)
            ++s.disabledHandlers;
    s.timers = _timers.size();
    s.elunaHandlers = _eluHandlers.size();
    s.errors = _errorRing.size();
    s.queued = _queue.size();
    s.queueDropped = _queue.dropped();
    s.reloads = _reloads;
    s.wrongThreadRefused = _wrongThread.load();
    s.lastErrors.assign(_lastErrors.begin(), _lastErrors.end());
    return s;
}

std::string Engine::statusText() const
{
    EngineStatus s = status();
    std::ostringstream o;
    o << "ForeverLua: " << (s.running ? "on" : "off") << ", memory " << (s.memoryUsed / 1024) << " KB of "
      << (s.memoryCap / (1024 * 1024)) << " MB, handlers " << s.handlers << " (" << s.disabledHandlers
      << " disabled), timers " << s.timers << ", queue " << s.queued << " (dropped " << s.queueDropped
      << "), reloads " << s.reloads << ", errors:";
    if (s.lastErrors.empty())
        o << " none";
    for (auto const& e : s.lastErrors)
        o << "\n  " << e;
    o << "\n  eluna events " << s.elunaHandlers << ", script errors " << s.errors;
    size_t shown = 0;
    for (auto it = _errorRing.rbegin(); it != _errorRing.rend() && shown < 5; ++it, ++shown)
        o << "\n  " << it->script << ":" << it->line << ": " << it->message << " (x" << it->count << ")";
    return o.str();
}
}  // namespace fl
