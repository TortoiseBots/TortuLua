// Bot API: the once-per-tick snapshot and the event diff (B1) and the order outbox with its tickets and outcomes (B2).
// The world thread pumps the provider (snapshot + the run ring), diffs it against the last tick and emits BOT_ADDED ...
// RUN_LOST in the world state, then hands the queued orders over and polls the open tickets. Every other thread reads
// the last immutable snapshot and posts orders; nothing here touches the provider off the world thread.
#pragma once

#include "engine/BotProvider.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace fl
{
class Engine;

// The process-wide hub: the bot API is one extension registered once, and every state (the world state and, with
// MapStates = 1, the map states) reads the same snapshot.
class BotHub
{
public:
    // A read keeps the snapshot fresh for this long after it, even with no handler bound.
    static constexpr uint64_t kReadWindowMs = 60'000;
    // With nobody listening and nothing read recently, the snapshot is rebuilt at most this often.
    static constexpr uint64_t kIdlePeriodMs = 1'000;
    // A ticket whose outcome never arrived ends as ORDER_FAILED `lost` after this long.
    static constexpr uint64_t kLostMs = 30'000;
    // B2: the outbox ceiling: orders handed over by any state and waiting for the next world tick.
    static constexpr size_t kOutboxMax = 1024;
    // B2: open tickets (queued or running) allowed per state.
    static constexpr size_t kOpenTicketsMax = 4096;
    // B2: the order rate cap of one script file in one state: a burst of 20, then one token every kOrderRefillMs.
    static constexpr size_t kOrderBurst = 20;
    static constexpr uint64_t kOrderRefillMs = 100;
    // B2: how many handed-out order seqs the last-order diff keeps out of its ticket-0 events.
    static constexpr size_t kLuaSeqMemory = 4096;

    // What postOrder answers: the ticket the script correlates its events with, or a refusal and its reason.
    struct OrderPost
    {
        uint64_t ticket = 0;   // 0 = refused, nothing was queued
        char const* code = ""; // rate-limited | queue-full
        std::string message;
    };
    // B2: hands one order over from any state and thread. `domain` is the calling state (Engine::orderDomain: its
    // tickets, its open-ticket count and its rate buckets), `script` the calling script's data namespace (the bucket),
    // `nowMs` the host clock. The order reaches the provider at the next pump; the bot runs it at its next tick.
    OrderPost postOrder(uint64_t domain, std::string const& script, OrderRequest order, uint64_t nowMs);
    // Forgets one state's tickets, its queued orders and its rate buckets (a reload, a closed state). The queued
    // orders are marked cancelled and the pump reports each one as ORDER_FAILED `cancelled` before anything reaches
    // the provider; a ticket already handed over keeps its place and its terminal event (see pumpOrders).
    // A provider that goes unavailable is the other way an order can end early: clear() drains the whole table and
    // reports each open ticket once, as ORDER_FAILED `unavailable`.
    void dropOrders(uint64_t domain);

    // World thread, once per tick (the host's OnUpdate, after the deferred work): rebuilds the snapshot when it is due,
    // diffs it against the last tick and emits the events. Never blocks a map thread: the hub lock is held only for the
    // snapshot swap.
    void pump(Engine& world, uint64_t nowMs);
    // The last snapshot (never null). Safe from any thread; the object it points to is immutable.
    std::shared_ptr<BotSnapshot const> snapshot() const;
    // Notes one read through the bot API (any thread): the snapshot stays fresh for kReadWindowMs after it.
    void noteRead(uint64_t nowMs);
    // Forgets the snapshot, the diff state, the orders and the read clock without emitting (tests, process teardown). A
    // provider that goes away is the next pump's job, and it ends its open orders first (see clear).
    void reset();

private:
    struct Track
    {
        bool alive = false;          // as of the last tick
        uint64_t orderSeq = 0;       // the last order the hub saw
        std::string orderVerb;
        bool orderOpen = false;      // ORDER_GIVEN went out, the DONE/FAILED is still owed
        uint64_t orderSinceMs = 0;   // when that order was first seen
    };
    using GuidKey = std::pair<uint64_t, uint64_t>;   // (guid.hi, guid.lo)

    // A provider that went away: every open ticket first ends ORDER_FAILED `unavailable`, then the snapshot and the diff
    // state are forgotten (no bot and no owed outcome outlives it). Runs on the world thread, before the snapshot swap.
    void clear(Engine& world, bool emit);
    // Forgets the snapshot and the diff state alone (reset, after it drops the orders without emitting).
    void forget();
    void diff(BotSnapshot& snap, Engine& world, uint64_t nowMs, bool emit);
    void noteOrder(BotInfo const& bot, Track& track, Engine& world, uint64_t nowMs, bool emit);
    void diffWipe(BotSnapshot& snap, Engine& world, bool emit);
    void emitRunEvents(std::vector<RunEventInfo> const& events, Engine& world, bool emit);

    // ---- B2 (world thread; the outbox and the ticket table are guarded by _orderMutex) ----
    struct OrderTicket
    {
        uint64_t domain = 0;   // the state that gave the order
        HandleData bot;        // kind Bot; an empty guid = a party-level call that names no bot
        std::string verb;
        uint64_t seq = 0;      // 0 until the verdict names it
        bool running = false;  // accepted at the hand-off: its outcome is owed
        uint64_t sinceMs = 0;  // when it was accepted (the 30 s deadline)
        bool committed = false;  // the hand-off took it: a domain drop leaves it and its terminal event alone
    };
    struct RateBucket
    {
        double tokens = static_cast<double>(kOrderBurst);
        uint64_t lastMs = 0;
    };
    struct PendingOrder
    {
        uint64_t ticket = 0;
        OrderRequest order;
        bool cancelled = false;   // its domain dropped while it waited here: the pump reports it, never submits it
    };
    void pumpOrders(BotProvider& provider, Engine& world, uint64_t nowMs, bool emit);
    void deliverVerdicts(std::vector<OrderRequest> const& orders, std::vector<OrderVerdict> const& verdicts,
                         Engine& world, uint64_t nowMs, bool emit, bool providerAvailable);
    void pollOutcomes(BotProvider& provider, Engine& world, uint64_t nowMs, bool emit);
    // Ends one ticket with exactly one ORDER_DONE or ORDER_FAILED and forgets it (a ticket already gone is a no-op).
    void endTicket(uint64_t ticket, bool done, std::string const& code, std::string const& message, Engine& world,
                   bool emit);
    // The one place a ticket's event goes out: ORDER_DONE, or ORDER_FAILED with its code and message.
    void emitTicketEnd(uint64_t ticketId, OrderTicket const& ticket, bool done, std::string const& code,
                       std::string const& message, Engine& world, bool emit);
    // A seq one of our tickets owns (or owned: the last 4096 stay remembered): the last-order diff leaves it to the
    // ticket's own events, even after a reload dropped the ticket or the provider went away and clear() ended it.
    bool luaSeq(uint64_t seq) const;
    void rememberLuaSeqLocked(uint64_t seq);
    bool ordersEmpty() const;
    void dropAllOrders();   // reset only: no engine to emit into, so the tickets are simply forgotten

    mutable std::mutex _mutex;
    std::shared_ptr<BotSnapshot const> _snapshot = std::make_shared<BotSnapshot const>();
    std::atomic<uint64_t> _lastReadMs{0};
    std::atomic<bool> _readSeen{false};         // a read at clock 0 is still a read
    uint64_t _lastBuildMs = 0;                  // world thread only
    bool _builtOnce = false;                    // the clock may start at 0, which is not "never built"
    std::map<GuidKey, Track> _track;            // world thread only
    std::map<GuidKey, bool> _groupAllDead;      // world thread only

    mutable std::mutex _orderMutex;             // the B2 outbox, tickets and rate buckets (any thread posts)
    std::deque<PendingOrder> _outbox;
    std::map<uint64_t, OrderTicket> _tickets;
    std::map<std::pair<uint64_t, std::string>, RateBucket> _buckets;   // (state, script) -> tokens
    std::set<uint64_t> _luaSeqs;                // the seqs our tickets handed out (the last kLuaSeqMemory)
    std::deque<uint64_t> _luaSeqOrder;          // ... in the order they were remembered
    uint64_t _nextTicket = 1;
};

BotHub& botHub();
}  // namespace fl
