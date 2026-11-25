#include "engine/BotProvider.h"

#include <atomic>

namespace fl
{
namespace
{
// The provider of a server where the bot API is not built in: available() is false, so every bot global raises
// "bots not built" and IsBotApiAvailable() answers false.
class NullBotProvider final : public BotProvider
{
public:
    bool available() override { return false; }
    void snapshot(BotSnapshot&) override { }
    void takeRunEvents(std::vector<RunEventInfo>&) override { }
};

NullBotProvider g_null;
std::atomic<BotProvider*> g_provider{&g_null};
}  // namespace

void registerBotProvider(BotProvider* provider)
{
    g_provider.store(provider ? provider : &g_null, std::memory_order_release);
}

BotProvider* botProvider()
{
    return g_provider.load(std::memory_order_acquire);
}
}  // namespace fl
