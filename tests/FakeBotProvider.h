// The fake bot provider of the engine tests (the maintainers' bot API specification section 6): canned snapshots, a canned run
// ring, the orders it was handed and their verdicts, and an outcome text per order seq. Only the world thread calls it,
// like the real provider.
#pragma once

#include "FakeHost.h"
#include "engine/BotHub.h"
#include "engine/BotProvider.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fltest
{
class FakeBotProvider final : public fl::BotProvider
{
public:
    bool isAvailable = true;
    fl::BotSnapshot next;                   // what snapshot() answers
    std::vector<fl::RunEventInfo> runRing;  // what takeRunEvents() answers, then empties

    int snapshots = 0;
    int runDrains = 0;

    // ---- B2: orders ----
    int submits = 0;
    int outcomeCalls = 0;
    std::vector<fl::OrderRequest> submitted;             // every request handed over, in order
    std::vector<fl::OrderVerdict> nextVerdicts;          // answered in order, then cleared; empty = accept every order
    uint32_t seqCounter = 100;                           // the seq the fake hands to an accepted order
    std::map<uint32_t, std::string> outcomeBySeq;        // absent (or "") = the order is still running
    std::vector<uint32_t> askedSeqs;                     // every seq outcomes() was asked for, in order
    std::vector<std::string> answered;                   // what outcomes() answered last, in the order asked
    // Called first thing inside submit(), before anything is recorded: a test blocks here to drive the other threads
    // (a reload) while the provider is inside the hand-off.
    std::function<void()> onSubmit;
    // A provider that went away mid-call: submit() records the orders but answers no verdict for them.
    bool answerNothing = false;

    bool available() override { return isAvailable; }

    void snapshot(fl::BotSnapshot& out) override
    {
        ++snapshots;
        out = next;
    }

    void takeRunEvents(std::vector<fl::RunEventInfo>& out) override
    {
        ++runDrains;
        out.insert(out.end(), runRing.begin(), runRing.end());
        runRing.clear();
    }

    void submit(std::vector<fl::OrderRequest> const& orders, std::vector<fl::OrderVerdict>& verdicts) override
    {
        if (onSubmit)
            onSubmit();
        ++submits;
        submitted.insert(submitted.end(), orders.begin(), orders.end());
        if (answerNothing)
            return;
        if (!nextVerdicts.empty())
        {
            verdicts.insert(verdicts.end(), nextVerdicts.begin(), nextVerdicts.end());
            nextVerdicts.clear();
            return;
        }
        for (fl::OrderRequest const& order : orders)
        {
            fl::OrderVerdict verdict;
            verdict.ticket = order.ticket;
            verdict.accepted = true;
            verdict.seq = static_cast<int64_t>(++seqCounter);
            verdict.bot = order.bot;
            verdicts.push_back(verdict);
        }
    }

    void outcomes(std::vector<uint32_t> const& seqs, std::vector<std::string>& results) override
    {
        ++outcomeCalls;
        askedSeqs.insert(askedSeqs.end(), seqs.begin(), seqs.end());
        results.clear();
        for (uint32_t seq : seqs)
        {
            auto const it = outcomeBySeq.find(seq);
            results.push_back(it == outcomeBySeq.end() ? std::string() : it->second);
        }
        answered = results;
    }

    // A bot whose character is a player of the fake world; name and level as the tests spell them.
    static fl::BotInfo bot(uint64_t low, std::string name, uint64_t owner = 0)
    {
        fl::BotInfo b;
        b.player = FakeHost::player(low);
        b.name = std::move(name);
        b.level = 60;
        b.cls = 1;
        b.role = "dps";
        b.engineState = "non-combat";
        b.hasEngine = true;
        b.owner = owner;
        return b;
    }
};

// Registers a fake provider (or none) and empties the hub for the life of the guard: the bot API is process-wide.
class BotProviderGuard
{
public:
    explicit BotProviderGuard(fl::BotProvider* provider = nullptr)
    {
        fl::botHub().reset();
        fl::registerBotProvider(provider);
    }
    ~BotProviderGuard()
    {
        fl::registerBotProvider(nullptr);
        fl::botHub().reset();
    }
    BotProviderGuard(BotProviderGuard const&) = delete;
    BotProviderGuard& operator=(BotProviderGuard const&) = delete;
};
}  // namespace fltest
