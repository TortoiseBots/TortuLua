// ForeverLua bot API: the provider interface between the game-free engine and the bot engine (our own extension, not
// Eluna; nothing here is copied from Eluna's source). The bot engine implements BotProvider and registers it; the
// engine calls snapshot() and takeRunEvents() once per world tick, on the world thread, and never from a map thread.
// No Lua, no core header: the plain data below is the whole contract.
#pragma once

#include "engine/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fl
{
// The version of this interface. The provider PR checks it once at register time (the module and the provider are
// built from the same tree, so a mismatch is a build mistake, not a runtime one).
constexpr uint32_t kBotApiVersion = 2;

// Where a bot's last order stands (B1 reads it; B2 gives the orders).
enum class OrderState : uint8_t
{
    None,     // no order yet
    Pending,  // accepted, still running
    Done,     // finished
    Failed    // refused at hand-off, or failed while running
};

// A bot's last order (`h3::LastOrder` on the bot module side): Bot:GetLastOrder() answers (seq, verb, result).
struct OrderInfo
{
    uint64_t seq = 0;   // 0 = no order yet
    std::string verb;   // the bot-module verb ("follow", "attack", ...)
    std::string result; // the text of the last outcome; "" while pending, and when there is no order
    OrderState state = OrderState::None;
    std::string code;    // ORDER_FAILED's code when state is Failed ("failed", "expired", ...)
    std::string message; // ORDER_FAILED's msg when state is Failed
};

// The run a bot leads or is in (`run::All` on the bot module side): Bot:GetRun() answers this as a table, or nil.
struct RunInfo
{
    HandleData leader;    // kind Player (an empty guid = no run)
    std::string plan;     // the plan's name
    std::string state;    // running | paused | done | stopped
    uint32_t step = 0;    // the current step index
    uint32_t steps = 0;   // the plan's step count
    std::string why;      // the pause/stop reason ("" when there is none)
};

// One bot, as the module reads it every tick (B1: read-only state).
struct BotInfo
{
    HandleData player;          // kind Player: guid, map and instance of the bot's character
    std::string name;           // GetName(), as the world spells it (lists are in name order)
    uint32_t level = 0;         // GetLevel()
    uint32_t cls = 0;           // GetClass(): the core's class id (0-11)
    std::string role;           // GetRole(): tank | healer | dps | "" (h3::RoleText / RoleOf)
    bool roleForced = false;    // IsRoleForced(): a forced role, not the engine's guess
    std::string spec;           // GetSpec(): the spec's name ("" when unknown)
    bool hasEngine = false;     // HasEngine(): the bot runs a PlayerbotAI
    std::string engineState;    // GetEngineState(): combat | non-combat | dead
    bool alive = true;          // IsAlive()
    bool inCombat = false;      // IsInCombat()
    HandleData master;          // GetMaster(): kind Player, empty guid = none (BotMgr::MasterOf)
    HandleData commander;       // GetCommander(): kind Player, empty guid = none (h3::CommanderGuid)
    HandleData group;           // GetGroup(): kind Group, empty guid = none
    uint64_t owner = 0;         // the bot's owner scope: the Battle.net account memoised at login (0 = unknown)
    OrderInfo lastOrder;        // GetLastOrder()
    bool hasRun = false;        // false = GetRun() answers nil
    RunInfo run;
};

// One member of a group the snapshot names: what the WIPE edge of B1 needs ("every in-world member of a group that
// holds a bot is dead").
struct GroupMemberInfo
{
    Guid guid;
    bool isBot = false;
    bool alive = true;
    bool inWorld = true;
};

// A group the snapshot knows (only groups that hold a bot are needed; the provider may send more).
struct GroupInfo
{
    HandleData group;   // kind Group
    std::vector<GroupMemberInfo> members;
};

// One tick's whole view. The provider fills it on the world thread; the engine copies it into an immutable snapshot
// that every state may read (any thread).
struct BotSnapshot
{
    std::vector<BotInfo> bots;
    std::vector<GroupInfo> groups;
};

// One entry of the run ring that the engine turns into an event (B1 events 8-10): the bot module's run events of kind
// `step`, `done` and `stop`, plus a `Lost` overflow marker.
enum class RunEventKind : uint8_t
{
    Step,  // RUN_STEP
    Done,  // RUN_STOPPED with the state `done`
    Stop,  // RUN_STOPPED with the state `stopped`
    Lost   // RUN_LOST: events were dropped before the next one (count in lost)
};

struct RunEventInfo
{
    RunEventKind kind = RunEventKind::Step;
    HandleData leader;      // kind Player (the run's leader)
    std::string plan;       // the plan's name
    int32_t step = -1;      // -1 = the event has no step
    int32_t leg = -1;       // -1 = the event has no leg
    std::string why;        // the stop reason ("" when there is none)
    uint32_t lost = 0;      // Lost only: how many events were dropped; the other fields are empty/-1
};

// ---- B2 (orders): declared here because the interface carries them; B1 never calls submit() or outcomes() ----
// One order the engine hands to the bot engine (world thread; the bot runs it at its next BotMgr::Update).
struct OrderRequest
{
    uint64_t ticket = 0;     // the engine's public ticket (ORDER_GIVEN carries it)
    uint64_t seq = 0;        // 0 = a synchronous call: the bot engine assigns the seq
    HandleData bot;          // kind Bot
    std::string verb;        // the bot-module verb
    std::string arg;         // the resolved argument (a guid, a member name, x,y,z, a word), "" when the verb has none
    std::string script;      // the calling script's data namespace (the rate bucket)
    std::vector<std::string> words;   // the verb's words for a verb that takes several (B2)
};

// What the bot engine answered at the hand-off: accepted (with the seq) or refused with one of the B2 codes
// (`not-supported`, `disabled`, `not-a-bot`, `no-engine`, `queue-full`, `bad-argument`, `target-not-found`, ...: the
// provider's own pre-flight codes travel unchanged to the script).
struct OrderVerdict
{
    uint64_t ticket = 0;
    bool accepted = false;
    int64_t seq = 0;
    std::string code;
    std::string message;
    // The bot the order was queued for. Empty = the request's own bot. A picker verb names another bot than the request
    // did: `interrupt` hands over the candidates and the provider queues the order for the one it picked.
    HandleData bot;
};

class BotProvider
{
public:
    virtual ~BotProvider() = default;

    // False = the bot API is not built into this server: every bot global raises "bots not built" and
    // IsBotApiAvailable() answers false. The default provider answers false.
    virtual bool available() = 0;
    // Fills `out` with the bots (and the groups that hold one) as the world is now. World thread, once per tick.
    // Every value is copied: the module keeps no pointer into the bot engine.
    virtual void snapshot(BotSnapshot& out) = 0;
    // Moves the run ring's `step`/`done`/`stop` events into `out` (empties them from the ring). World thread. Also
    // called ONCE after `available()` turned false (the final take of a shutdown or a disable): the provider hands
    // over what it still holds; it may return nothing.
    virtual void takeRunEvents(std::vector<RunEventInfo>& out) = 0;
    // B2: hands orders over (world thread); each request gets exactly one verdict. Default: not-supported, one verdict
    // per request (a B1-only provider answers a script's order with a clear code instead of silence).
    virtual void submit(std::vector<OrderRequest> const& orders, std::vector<OrderVerdict>& verdicts)
    {
        verdicts.reserve(verdicts.size() + orders.size());
        for (OrderRequest const& order : orders)
        {
            OrderVerdict& verdict = verdicts.emplace_back();
            verdict.ticket = order.ticket;
            verdict.code = "not-supported";
            verdict.message = "this bot provider does not support orders";
        }
    }
    // B2: the outcome text of the given order seqs, one entry per seq, in the order asked. "" (or any text that is not
    // `ok` and does not start with `fail:`) = the order has not finished; `ok` = it finished; `fail:<code>` = it failed,
    // an optional message after one space (`fail:expired the mob despawned`). The module ends a ticket still running
    // after 30 s as ORDER_FAILED `lost`. Default: not-supported per seq.
    virtual void outcomes(std::vector<uint32_t> const& seqs, std::vector<std::string>& results)
    {
        results.insert(results.end(), seqs.size(), std::string("not-supported"));
    }
};

// Registers the provider (nullptr restores the null provider, which answers "bots not built"). Called once at module
// start, before any script runs; safe from any thread.
void registerBotProvider(BotProvider* provider);
// The registered provider; never null (the null provider when none is registered).
BotProvider* botProvider();
}  // namespace fl
