#include "engine/BotHub.h"

#include "engine/Elu.h"
#include "engine/Engine.h"
#include "engine/EventQueue.h"
#include "engine/Value.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace fl
{
namespace
{
// The ten bot events: the ids of the maintainers' bot API specification section 3, and the test-mode names of its section 4.
enum class BotEvent : uint32_t
{
    Added = 1,       // BOT_ADDED(bot)
    Removed = 2,     // BOT_REMOVED(bot)
    OrderGiven = 3,  // ORDER_GIVEN(bot, ticket, seq, verb)
    OrderDone = 4,   // ORDER_DONE(bot, ticket, seq, verb)
    OrderFailed = 5, // ORDER_FAILED(bot, ticket, seq, verb, code, msg)
    Died = 6,        // BOT_DIED(bot)
    Wipe = 7,        // WIPE(group, bots)
    RunStep = 8,     // RUN_STEP(leader, plan, step, leg)
    RunStopped = 9,  // RUN_STOPPED(leader, plan, done|stopped, why)
    RunLost = 10     // RUN_LOST(count)
};

constexpr char const* kEventNames[] = {"bot.added",   "bot.removed",    "bot.order",    "bot.order_done", "bot.order_failed",
                                       "bot.death",   "bot.wipe",       "bot.run_step", "bot.run_stop",   "bot.run_lost"};

static_assert(sizeof(kEventNames) / sizeof(kEventNames[0]) == static_cast<size_t>(BotEvent::RunLost));

size_t eventIndex(BotEvent id) { return static_cast<size_t>(id) - 1; }

bool listeningAny(Engine& engine)
{
    if (engine.config().mode == EngineMode::Eluna)
    {
        for (uint32_t id = 1; id <= sizeof(kEventNames) / sizeof(kEventNames[0]); ++id)
            if (engine.listeningEluna(EluKind::Bot, 0, id))
                return true;
        return false;
    }
    for (char const* name : kEventNames)
        if (engine.listening(name))
            return true;
    return false;
}

// The event runs in the world state, on the world thread: the Eluna family in eluna mode, the namespaced toolkit
// event in test mode (bot API specification, section 4). An event nobody listens for costs a boolean check.
void emitEvent(Engine& engine, BotEvent id, std::vector<EventValue> args)
{
    if (engine.config().mode == EngineMode::Eluna)
    {
        if (!engine.listeningEluna(EluKind::Bot, 0, static_cast<uint32_t>(id)))
            return;
        (void)engine.emitEluna(EluKind::Bot, 0, static_cast<uint32_t>(id), args);
        return;
    }
    if (!engine.listening(kEventNames[eventIndex(id)]))
        return;
    QueuedEvent ev;
    ev.name = kEventNames[eventIndex(id)];
    ev.args = std::move(args);
    engine.emit(std::move(ev));
}

HandleData botHandle(Guid const& guid)
{
    HandleData h;
    h.kind = ObjKind::Bot;
    h.guid = guid;
    return h;
}

// A handle the BOT_REMOVED event hands out: it names a bot that left the snapshot and never resolves again.
HandleData removedBotHandle(Guid const& guid)
{
    HandleData h = botHandle(guid);
    h.removed = true;
    return h;
}

// A player handle for a master/commander/leader the provider named (an empty guid stays nil).
EventValue playerValue(HandleData const& player)
{
    if (player.guid == Guid())
        return EventValue();
    HandleData h = player;
    h.kind = ObjKind::Player;
    return EventValue(h);
}

bool guidLess(Guid const& a, Guid const& b)
{
    return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo;
}

void sortSnapshot(BotSnapshot& snap)
{
    std::sort(snap.bots.begin(), snap.bots.end(), [](BotInfo const& a, BotInfo const& b)
    {
        if (a.name != b.name)
            return a.name < b.name;
        return guidLess(a.player.guid, b.player.guid);
    });
    std::sort(snap.groups.begin(), snap.groups.end(),
              [](GroupInfo const& a, GroupInfo const& b) { return guidLess(a.group.guid, b.group.guid); });
}

// A ticket's bot for an ORDER event: nil for a party-level call that named no bot (BotCommanderPlace).
EventValue botValue(HandleData const& bot)
{
    if (bot.guid == Guid())
        return EventValue();
    return EventValue(botHandle(bot.guid));
}

// The B2 outcome text (BotProvider::outcomes), split into its code and message: `ok` finished, `fail:<code>` failed,
// anything else (and "") is still running. A ticket never invents a failure from text it cannot read.
bool orderOutcome(std::string const& text, bool& done, std::string& code, std::string& message)
{
    done = false;
    if (text == "ok")
    {
        done = true;
        return true;
    }
    if (text.compare(0, 5, "fail:") != 0)
        return false;
    size_t const codeEnd = text.find(' ', 5);
    code = text.substr(5, codeEnd == std::string::npos ? std::string::npos : codeEnd - 5);
    if (code.empty())
        code = "failed";
    if (codeEnd != std::string::npos)
    {
        size_t const start = text.find_first_not_of(' ', codeEnd);
        if (start != std::string::npos)
            message = text.substr(start);
    }
    return true;
}
}  // namespace

BotHub& botHub()
{
    static BotHub hub;
    return hub;
}

std::shared_ptr<BotSnapshot const> BotHub::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _snapshot;
}

void BotHub::noteRead(uint64_t nowMs)
{
    _readSeen.store(true, std::memory_order_relaxed);
    uint64_t cur = _lastReadMs.load(std::memory_order_relaxed);
    while (nowMs > cur && !_lastReadMs.compare_exchange_weak(cur, nowMs, std::memory_order_relaxed))
    {
    }
}

void BotHub::clear(Engine& world, bool emit)
{
    // B2: a provider that went away still owes every open order its terminal event. The ticket table is drained in one
    // step (queued and accepted tickets alike), each ticket reports ORDER_FAILED `unavailable` here, on the world
    // thread, and only then is the state forgotten: the report is delivered before anything it names is cleared.
    std::vector<std::pair<uint64_t, OrderTicket>> open;
    {
        std::lock_guard<std::mutex> lock(_orderMutex);
        open.reserve(_tickets.size());
        for (auto& ticket : _tickets)
            open.emplace_back(ticket.first, std::move(ticket.second));
        _tickets.clear();
        _outbox.clear();
        _buckets.clear();
    }
    for (auto const& ticket : open)
        emitTicketEnd(ticket.first, ticket.second, false, "unavailable", "the bot provider became unavailable", world,
                      emit);
    forget();
}

void BotHub::forget()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _snapshot = std::make_shared<BotSnapshot const>();
    }
    _track.clear();
    _groupAllDead.clear();
    _lastBuildMs = 0;
    _builtOnce = false;
}

void BotHub::reset()
{
    forget();
    dropAllOrders();   // no engine to emit into: a test teardown forgets the orders instead of ending them
    {
        // The seqs a ticket handed out: forgotten with the tickets (tests; a provider that comes back with a restarted
        // sequence counter is not what the diff must protect against).
        std::lock_guard<std::mutex> lock(_orderMutex);
        _luaSeqs.clear();
        _luaSeqOrder.clear();
    }
    _lastReadMs.store(0, std::memory_order_relaxed);
    _readSeen.store(false, std::memory_order_relaxed);
}

void BotHub::pump(Engine& world, uint64_t nowMs)
{
    BotProvider* const provider = botProvider();
    if (!provider || !provider->available())
    {
        // "Never unregistered; empty snapshot after the bot module's OnShutdown": a provider that went away leaves no bot behind, and
        // every order it still had open ends first, once each (ORDER_FAILED `unavailable`), so nothing is lost silently.
        if (_builtOnce || !_track.empty() || !ordersEmpty())
        {
            bool const listen = listeningAny(world);
            if (provider)
            {
                std::vector<RunEventInfo> last;
                provider->takeRunEvents(last);   // the final take: a shutdown's last run events
                emitRunEvents(last, world, listen);
            }
            clear(world, listen);
        }
        return;
    }

    bool const listen = listeningAny(world);
    uint64_t const readMs = _lastReadMs.load(std::memory_order_relaxed);
    bool const recentRead =
        _readSeen.load(std::memory_order_relaxed) && nowMs >= readMs && nowMs - readMs <= kReadWindowMs;
    bool const due = !_builtOnce || nowMs < _lastBuildMs || nowMs - _lastBuildMs >= kIdlePeriodMs;
    if (!listen && !recentRead && !due)
    {
        // Nobody reads and nobody listens: the ring is still drained so the provider's ring cannot overflow; the last
        // snapshot stays for a later read. The orders go out either way: a script asked for them.
        std::vector<RunEventInfo> dropped;
        provider->takeRunEvents(dropped);
        pumpOrders(*provider, world, nowMs, listen);
        return;
    }

    auto snap = std::make_shared<BotSnapshot>();
    provider->snapshot(*snap);
    std::vector<RunEventInfo> runs;
    provider->takeRunEvents(runs);
    sortSnapshot(*snap);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _snapshot = std::shared_ptr<BotSnapshot const>(snap);
    }
    _lastBuildMs = nowMs;
    _builtOnce = true;
    diff(*snap, world, nowMs, listen);
    emitRunEvents(runs, world, listen);
    // B2, after the diff: the outbox goes to submit() and the open tickets ask for their outcome.
    pumpOrders(*provider, world, nowMs, listen);
}

void BotHub::diff(BotSnapshot& snap, Engine& world, uint64_t nowMs, bool emit)
{
    std::map<GuidKey, bool> present;
    for (BotInfo const& bot : snap.bots)
    {
        GuidKey const key{bot.player.guid.hi, bot.player.guid.lo};
        present[key] = true;
        auto it = _track.find(key);
        if (it == _track.end())
        {
            Track track;
            track.alive = bot.alive;   // a bot already dead at its first sight is not a death (no BOT_ADDED edge behind it)
            it = _track.emplace(key, track).first;
            if (emit)
                emitEvent(world, BotEvent::Added, {EventValue(botHandle(bot.player.guid))});
            noteOrder(bot, it->second, world, nowMs, emit);
            continue;
        }
        Track& track = it->second;
        if (track.alive && !bot.alive && emit)
            emitEvent(world, BotEvent::Died, {EventValue(botHandle(bot.player.guid))});
        track.alive = bot.alive;
        noteOrder(bot, track, world, nowMs, emit);
    }
    for (auto it = _track.begin(); it != _track.end();)
    {
        if (present.count(it->first))
        {
            ++it;
            continue;
        }
        // The bot left the world: BOT_REMOVED with a handle that never resolves. A bot leaving does not erase an order
        // ticket: the ticket ends on its outcome, on the 30 s `lost` deadline, or `unavailable` if the provider goes
        // away; a reload of the giving state cancels it only while it is still queued.
        if (emit)
            emitEvent(world, BotEvent::Removed,
                      {EventValue(removedBotHandle(Guid{it->first.first, it->first.second}))});
        it = _track.erase(it);
    }
    diffWipe(snap, world, emit);
}

void BotHub::noteOrder(BotInfo const& bot, Track& track, Engine& world, uint64_t nowMs, bool emit)
{
    OrderInfo const& order = bot.lastOrder;
    std::vector<EventValue> const botArg{EventValue(botHandle(bot.player.guid))};
    if (order.seq != track.orderSeq)
    {
        // The last order changed: an older one that never told its outcome is lost (each ticket ends exactly once).
        if (track.orderOpen && emit)
        {
            std::vector<EventValue> args = botArg;
            args.emplace_back(static_cast<int64_t>(0));   // not a Lua ticket
            args.emplace_back(static_cast<int64_t>(track.orderSeq));
            args.emplace_back(track.orderVerb);
            args.emplace_back(std::string("lost"));
            args.emplace_back(std::string("a later order replaced it before the outcome arrived"));
            emitEvent(world, BotEvent::OrderFailed, std::move(args));
        }
        track.orderOpen = false;
        track.orderSeq = order.seq;
        track.orderVerb = order.verb;
        if (order.seq == 0)
            return;
        if (luaSeq(order.seq))
            return;   // B2: a script's own order: its ticket already said GIVEN, and says DONE/FAILED
        if (emit)
        {
            std::vector<EventValue> args = botArg;
            args.emplace_back(static_cast<int64_t>(0));   // ticket 0 = not given from Lua
            args.emplace_back(static_cast<int64_t>(order.seq));
            args.emplace_back(order.verb);
            emitEvent(world, BotEvent::OrderGiven, std::move(args));
        }
        if (order.state == OrderState::Pending || order.state == OrderState::None)
        {
            track.orderOpen = true;
            track.orderSinceMs = nowMs;
        }
        else if (emit)
        {
            std::vector<EventValue> args = botArg;
            args.emplace_back(static_cast<int64_t>(0));
            args.emplace_back(static_cast<int64_t>(order.seq));
            args.emplace_back(order.verb);
            if (order.state == OrderState::Done)
            {
                emitEvent(world, BotEvent::OrderDone, std::move(args));
            }
            else
            {
                args.emplace_back(order.code.empty() ? std::string("failed") : order.code);
                args.emplace_back(order.message);
                emitEvent(world, BotEvent::OrderFailed, std::move(args));
            }
        }
        return;
    }
    if (!track.orderOpen)
        return;
    if (order.state == OrderState::Done || order.state == OrderState::Failed)
    {
        track.orderOpen = false;
        if (!emit)
            return;
        std::vector<EventValue> args = botArg;
        args.emplace_back(static_cast<int64_t>(0));
        args.emplace_back(static_cast<int64_t>(order.seq));
        args.emplace_back(order.verb);
        if (order.state == OrderState::Done)
        {
            emitEvent(world, BotEvent::OrderDone, std::move(args));
        }
        else
        {
            args.emplace_back(order.code.empty() ? std::string("failed") : order.code);
            args.emplace_back(order.message);
            emitEvent(world, BotEvent::OrderFailed, std::move(args));
        }
        return;
    }
    // Still pending after 30 s: the ticket ends `lost`, once (B2 applies the same deadline to its own tickets).
    if (nowMs >= track.orderSinceMs && nowMs - track.orderSinceMs >= kLostMs)
    {
        track.orderOpen = false;
        if (!emit)
            return;
        std::vector<EventValue> args = botArg;
        args.emplace_back(static_cast<int64_t>(0));
        args.emplace_back(static_cast<int64_t>(track.orderSeq));
        args.emplace_back(track.orderVerb);
        args.emplace_back(std::string("lost"));
        args.emplace_back(std::string("no outcome within 30 s"));
        emitEvent(world, BotEvent::OrderFailed, std::move(args));
    }
}

void BotHub::diffWipe(BotSnapshot& snap, Engine& world, bool emit)
{
    std::map<GuidKey, bool> next;
    for (GroupInfo const& group : snap.groups)
    {
        GuidKey const key{group.group.guid.hi, group.group.guid.lo};
        bool holdsBot = false, inWorld = false, allDead = true;
        for (GroupMemberInfo const& member : group.members)
        {
            if (member.isBot)
                holdsBot = true;
            if (!member.inWorld)
                continue;
            inWorld = true;
            if (member.alive)
                allDead = false;
        }
        if (!holdsBot || !inWorld)
            continue;   // a group with no bot, or with nobody in the world, cannot wipe
        bool const was = _groupAllDead.count(key) != 0 && _groupAllDead[key];
        next[key] = allDead;
        if (!allDead || was || !emit)
            continue;
        std::vector<EventValue> args;
        args.push_back(EventValue(group.group));
        auto bots = std::make_shared<EventList>();
        for (GroupMemberInfo const& member : group.members)
            if (member.isBot)
                bots->items.push_back(EventValue(botHandle(member.guid)));
        args.push_back(EventValue(bots));
        emitEvent(world, BotEvent::Wipe, std::move(args));
    }
    _groupAllDead = std::move(next);
}

void BotHub::emitRunEvents(std::vector<RunEventInfo> const& events, Engine& world, bool emit)
{
    if (!emit)
        return;   // nobody listens: the ring was drained and its events are dropped
    for (RunEventInfo const& event : events)
    {
        if (event.kind == RunEventKind::Step)
        {
            emitEvent(world, BotEvent::RunStep,
                      {playerValue(event.leader), EventValue(event.plan), EventValue(event.step), EventValue(event.leg)});
            continue;
        }
        if (event.kind == RunEventKind::Lost)
        {
            emitEvent(world, BotEvent::RunLost, {EventValue(event.lost)});
            continue;
        }
        emitEvent(world, BotEvent::RunStopped,
                  {playerValue(event.leader), EventValue(event.plan),
                   EventValue(std::string(event.kind == RunEventKind::Done ? "done" : "stopped")), EventValue(event.why)});
    }
}

// ---- B2: the order outbox, its tickets and their outcomes ----

BotHub::OrderPost BotHub::postOrder(uint64_t domain, std::string const& script, OrderRequest order, uint64_t nowMs)
{
    std::lock_guard<std::mutex> lock(_orderMutex);
    RateBucket& bucket = _buckets[{domain, script}];
    if (nowMs < bucket.lastMs)   // the clock was reset (a test, a restart): a fresh burst
    {
        bucket.tokens = static_cast<double>(kOrderBurst);
        bucket.lastMs = nowMs;
    }
    else if (nowMs > bucket.lastMs)
    {
        double const refill = static_cast<double>(nowMs - bucket.lastMs) / static_cast<double>(kOrderRefillMs);
        bucket.tokens = std::min(static_cast<double>(kOrderBurst), bucket.tokens + refill);
        bucket.lastMs = nowMs;
    }
    if (bucket.tokens < 1.0)
    {
        return {0, "rate-limited",
                "the script is over its order rate (a burst of 20, then one order every 100 ms): nothing was queued"};
    }
    size_t open = 0;
    for (auto const& ticket : _tickets)
        if (ticket.second.domain == domain)
            ++open;
    if (open >= kOpenTicketsMax)
        return {0, "queue-full", "this state has " + std::to_string(kOpenTicketsMax) + " order tickets open"};
    if (_outbox.size() >= kOutboxMax)
    {
        return {0, "queue-full",
                "the order outbox is full (" + std::to_string(kOutboxMax) + " orders wait for the world tick)"};
    }
    uint64_t const ticket = _nextTicket++;
    order.ticket = ticket;
    order.seq = 0;
    order.script = script;
    _tickets.emplace(ticket, OrderTicket{domain, order.bot, order.verb, 0, false, nowMs});
    _outbox.push_back(PendingOrder{ticket, std::move(order)});
    bucket.tokens -= 1.0;
    return {ticket, "", ""};
}

void BotHub::dropOrders(uint64_t domain)
{
    if (domain == 0)
        return;
    std::lock_guard<std::mutex> lock(_orderMutex);
    // One linearisation point with pumpOrders, and this lock is it: a queued order is marked cancelled here (the pump
    // reports it and never hands it to the provider), while a ticket the hand-off already took (committed) is left
    // alone, so the given/done/failed/lost of an order the provider has accepted still reaches the scripts. An order
    // is therefore either committed before the drop or cancelled before it reaches the provider: never both, never
    // neither. Tickets stay in the table until the pump resolves them (the cancellation) or the outcome ends them; a
    // ticket left open when the provider itself goes away is ended by clear() instead.
    for (PendingOrder& pending : _outbox)
    {
        auto const ticket = _tickets.find(pending.ticket);
        if (ticket != _tickets.end() && ticket->second.domain == domain && !ticket->second.committed)
            pending.cancelled = true;
    }
    for (auto it = _buckets.begin(); it != _buckets.end();)
        it = (it->first.first == domain) ? _buckets.erase(it) : std::next(it);
}

bool BotHub::ordersEmpty() const
{
    std::lock_guard<std::mutex> lock(_orderMutex);
    return _outbox.empty() && _tickets.empty();
}

void BotHub::dropAllOrders()
{
    std::lock_guard<std::mutex> lock(_orderMutex);
    _outbox.clear();
    _tickets.clear();
    _buckets.clear();
}

bool BotHub::luaSeq(uint64_t seq) const
{
    std::lock_guard<std::mutex> lock(_orderMutex);
    if (_luaSeqs.count(seq) != 0)
        return true;
    for (auto const& ticket : _tickets)
        if (ticket.second.seq == seq)
            return true;
    return false;
}

void BotHub::rememberLuaSeqLocked(uint64_t seq)
{
    if (!_luaSeqs.insert(seq).second)
        return;
    _luaSeqOrder.push_back(seq);
    while (_luaSeqOrder.size() > kLuaSeqMemory)
    {
        _luaSeqs.erase(_luaSeqOrder.front());
        _luaSeqOrder.pop_front();
    }
}

void BotHub::pumpOrders(BotProvider& provider, Engine& world, uint64_t nowMs, bool emit)
{
    std::vector<OrderRequest> orders;
    std::vector<std::pair<uint64_t, OrderTicket>> cancelled;
    {
        std::lock_guard<std::mutex> lock(_orderMutex);
        // The other side of the drop's linearisation point: an order taken here is marked committed inside this lock
        // (dropOrders then leaves its ticket alone and its terminal event still arrives), an order the drop marked
        // first is cancelled here and never reaches the provider. The lock is released before submit(): the provider
        // may block for a long time and map threads post into this outbox from script handlers, so no provider call
        // may run under a lock the scripts need.
        while (!_outbox.empty())
        {
            PendingOrder pending = std::move(_outbox.front());
            _outbox.pop_front();
            auto const ticket = _tickets.find(pending.ticket);
            if (ticket == _tickets.end())
                continue;   // already ended: a refused ticket cannot be here, a reset clears the outbox too
            if (pending.cancelled)
            {
                cancelled.emplace_back(pending.ticket, ticket->second);
                _tickets.erase(ticket);
                continue;
            }
            ticket->second.committed = true;
            orders.push_back(std::move(pending.order));
        }
    }
    // Each cancelled order's one terminal event, before the hand-off: its domain was reloaded or closed while it
    // waited, so the provider never saw it and no ORDER_GIVEN can ever come.
    for (auto const& entry : cancelled)
        emitTicketEnd(entry.first, entry.second, false, "cancelled",
                      "the state that gave the order was reloaded or closed before the hand-off", world, emit);
    if (!orders.empty())
    {
        std::vector<OrderVerdict> verdicts;
        provider.submit(orders, verdicts);
        // The provider may go away inside submit(): an order it left unanswered is then `unavailable` (the reason every
        // open ticket gets when the provider is gone), not `failed`.
        deliverVerdicts(orders, verdicts, world, nowMs, emit, provider.available());
    }
    pollOutcomes(provider, world, nowMs, emit);
}

void BotHub::deliverVerdicts(std::vector<OrderRequest> const& orders, std::vector<OrderVerdict> const& verdicts,
                             Engine& world, uint64_t nowMs, bool emit, bool providerAvailable)
{
    std::map<uint64_t, OrderVerdict const*> byTicket;
    for (OrderVerdict const& verdict : verdicts)
        byTicket.emplace(verdict.ticket, &verdict);
    for (OrderRequest const& order : orders)
    {
        auto const found = byTicket.find(order.ticket);
        if (found == byTicket.end())
        {
            endTicket(order.ticket, false, providerAvailable ? "failed" : "unavailable",
                      providerAvailable ? "the bot provider did not answer this order"
                                        : "the bot provider became unavailable",
                      world, emit);
            continue;
        }
        OrderVerdict const& verdict = *found->second;
        if (!verdict.accepted)
        {
            endTicket(order.ticket, false, verdict.code.empty() ? "failed" : verdict.code, verdict.message, world, emit);
            continue;
        }
        if (verdict.seq < 0)
        {
            endTicket(order.ticket, false, "failed", "the bot provider answered a negative sequence", world, emit);
            continue;
        }
        // Accepted: ORDER_GIVEN now. A positive seq is a queued order whose outcome follows (verdicts arrive one tick
        // later, as the spec says); seq 0 is a synchronous verb the provider ran at the hand-off (`place`, a run verb),
        // and its ORDER_DONE follows at once (the spec: "synchronous calls answer GIVEN seq 0, then DONE/FAILED").
        bool const synchronous = verdict.seq == 0;
        HandleData bot = order.bot;
        if (verdict.bot.guid != Guid())
            bot = verdict.bot;   // a picker verb (`interrupt`) names the bot it queued for
        {
            std::lock_guard<std::mutex> lock(_orderMutex);
            auto const ticket = _tickets.find(order.ticket);
            if (ticket == _tickets.end())
                continue;   // reset (a provider that went away) ended it: nothing left to report on
            ticket->second.bot = bot;
            if (!synchronous)
            {
                ticket->second.seq = static_cast<uint64_t>(verdict.seq);
                ticket->second.running = true;
                ticket->second.sinceMs = nowMs;
                rememberLuaSeqLocked(ticket->second.seq);
            }
        }
        if (emit)
        {
            emitEvent(world, BotEvent::OrderGiven,
                      {botValue(bot), EventValue(static_cast<int64_t>(order.ticket)),
                       EventValue(static_cast<int64_t>(verdict.seq)), order.verb});
        }
        if (synchronous)
            endTicket(order.ticket, true, "", "", world, emit);
    }
}

void BotHub::pollOutcomes(BotProvider& provider, Engine& world, uint64_t nowMs, bool emit)
{
    std::vector<uint32_t> seqs;
    std::vector<uint64_t> ticketIds;
    {
        std::lock_guard<std::mutex> lock(_orderMutex);
        for (auto const& entry : _tickets)
        {
            OrderTicket const& ticket = entry.second;
            if (!ticket.running || ticket.seq == 0 || ticket.seq > 0xFFFFFFFFull)
                continue;
            seqs.push_back(static_cast<uint32_t>(ticket.seq));
            ticketIds.push_back(entry.first);
        }
    }
    std::vector<std::string> results;
    if (!seqs.empty())
        provider.outcomes(seqs, results);   // one call for every open seq, in the order asked
    for (size_t i = 0; i < ticketIds.size(); ++i)
    {
        std::string const& result = i < results.size() ? results[i] : std::string();
        bool done = false;
        std::string code, message;
        if (!orderOutcome(result, done, code, message))
            continue;   // still running: the deadline below is what ends it
        endTicket(ticketIds[i], done, code, message, world, emit);
    }
    // Still running after 30 s: `lost`, exactly once (a late outcome then finds no ticket).
    std::vector<uint64_t> lost;
    {
        std::lock_guard<std::mutex> lock(_orderMutex);
        for (auto const& entry : _tickets)
        {
            OrderTicket const& ticket = entry.second;
            if (ticket.running && nowMs >= ticket.sinceMs && nowMs - ticket.sinceMs >= kLostMs)
                lost.push_back(entry.first);
        }
    }
    for (uint64_t ticket : lost)
        endTicket(ticket, false, "lost", "no outcome within 30 s", world, emit);
}

void BotHub::endTicket(uint64_t ticketId, bool done, std::string const& code, std::string const& message,
                       Engine& world, bool emit)
{
    OrderTicket ticket;
    {
        std::lock_guard<std::mutex> lock(_orderMutex);
        auto const found = _tickets.find(ticketId);
        if (found == _tickets.end())
            return;   // already ended (or dropped by a reset): every ticket ends exactly once
        ticket = found->second;
        _tickets.erase(found);
    }
    emitTicketEnd(ticketId, ticket, done, code, message, world, emit);
}

void BotHub::emitTicketEnd(uint64_t ticketId, OrderTicket const& ticket, bool done, std::string const& code,
                           std::string const& message, Engine& world, bool emit)
{
    if (!emit)
        return;
    std::vector<EventValue> args{botValue(ticket.bot), EventValue(static_cast<int64_t>(ticketId)),
                                EventValue(static_cast<int64_t>(ticket.seq)), ticket.verb};
    if (done)
    {
        emitEvent(world, BotEvent::OrderDone, std::move(args));
        return;
    }
    // A refusal at the hand-off has no seq: ORDER_FAILED carries 0 for it (the spec's argument table).
    args.emplace_back(code.empty() ? std::string("failed") : code);
    args.emplace_back(message);
    emitEvent(world, BotEvent::OrderFailed, std::move(args));
}
}  // namespace fl
