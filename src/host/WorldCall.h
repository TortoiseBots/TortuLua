// ForeverLua host side: world-only work. Ban, Kick, SaveAllPlayers, SendMail, game events, vendor changes and group
// member changes touch state that only the world thread (with the maps idle) may change. A handler that runs on a map
// thread must not call them directly: the call is queued and runs on the world thread's next tick. Game-free and
// unit-tested (tests/test_host_commands.cpp); the core adapter supplies the job.
#pragma once

#include "AccessRules.h"
#include "DeferQueue.h"

#include <functional>
#include <optional>
#include <utility>

namespace flh
{
enum class WorldCallResult
{
    Ran,      // the caller is the world thread with the maps idle: the job ran now
    Queued,   // the job runs on the world thread's next tick (the script gets no result)
    Dropped   // the queue is full: the job did not run
};

inline WorldCallResult runOnWorld(AccessRules const& rules, DeferQueue& queue, std::function<void()> job)
{
    if (rules.placeForWorld() == AccessRules::Place::Direct)
    {
        job();
        return WorldCallResult::Ran;
    }
    return queue.push(std::move(job)) ? WorldCallResult::Queued : WorldCallResult::Dropped;
}

// The same for a job with a result. On the world thread the job runs now and the result is returned; from anywhere else a
// COPY of the job is queued (it must capture by value) and the caller gets no result. `how` tells which of the three it was.
template <typename R>
std::optional<R> callOnWorld(AccessRules const& rules, DeferQueue& queue, std::function<R()> job,
                             WorldCallResult* how = nullptr)
{
    if (rules.placeForWorld() == AccessRules::Place::Direct)
    {
        if (how)
            *how = WorldCallResult::Ran;
        return job();
    }
    bool const queued = queue.push([job = std::move(job)] { (void)job(); });
    if (how)
        *how = queued ? WorldCallResult::Queued : WorldCallResult::Dropped;
    return std::nullopt;
}
}  // namespace flh
