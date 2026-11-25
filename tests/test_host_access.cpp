// The host's access rules and deferred work (src/host/AccessRules.cpp, DeferQueue.cpp): the thread/map model that decides
// whether a handle may be resolved, game-free so it is the same code the module runs.
#include "AccessRules.h"
#include "DeferQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

using flh::AccessRules;
using flh::MapKey;
using fl::Access;

namespace
{
std::optional<MapKey> key(uint32_t m, uint32_t i = 0) { return MapKey{m, i}; }
}  // namespace

TEST_CASE("access: inside a map scope only the object's current map is reachable")
{
    AccessRules rules;
    rules.setWorldThread();
    flh::MapScope scope(1, 0);
    CHECK(rules.check(key(1), key(1)) == Access::Ok);
    CHECK(rules.check(key(2), key(2)) == Access::OtherMap);             // another map's object
    CHECK(rules.check(key(1, 4), key(1, 4)) == Access::OtherMap);       // another instance of the same map
    CHECK(rules.check(key(1), key(1, 33)) == Access::Gone);             // it moved to instance 33: a stale handle
    CHECK(rules.check(key(1), key(2)) == Access::Gone);                 // ... or to another map entirely
    CHECK(rules.check(key(1), std::nullopt) == Access::Gone);           // not in the world
    CHECK(rules.check(std::nullopt, key(1)) == Access::Ok);             // an item: it carries no map, the owner's counts
    CHECK(rules.check(std::nullopt, key(2)) == Access::OtherMap);
}

TEST_CASE("access: the world thread reaches everything only while the maps are idle")
{
    AccessRules rules;
    rules.setWorldThread();
    CHECK(rules.check(key(3), key(3)) == Access::Ok);
    CHECK(rules.check(key(3), key(9)) == Access::Ok);                   // a moved player: still resolvable by guid here
    rules.setMapsIdle(false);
    CHECK(rules.check(key(3), key(3)) == Access::OtherMap);             // the maps are updating on their threads
    rules.setMapsIdle(true);
    CHECK(rules.check(key(3), key(3)) == Access::Ok);

    // Any other thread without a scope: nothing.
    Access fromWorker = Access::Ok;
    std::thread t([&] { fromWorker = rules.check(key(3), key(3)); });
    t.join();
    CHECK(fromWorker == Access::OtherMap);
}

TEST_CASE("access: map scopes nest and the inner one gives the outer one back")
{
    CHECK_FALSE(flh::MapScope::current().has_value());
    {
        flh::MapScope outer(1, 2);
        REQUIRE(flh::MapScope::current().has_value());
        CHECK(*flh::MapScope::current() == MapKey{1, 2});
        {
            flh::MapScope inner(3, 4);          // a nested hook (unit:Kill() firing OnCreatureKill) on another thread's map
            CHECK(*flh::MapScope::current() == MapKey{3, 4});
        }
        // The old destructor reset to "no scope" here, so every later call of the outer handler was refused.
        REQUIRE(flh::MapScope::current().has_value());
        CHECK(*flh::MapScope::current() == MapKey{1, 2});
        {
            flh::MapScope same(1, 2);
        }
        CHECK(*flh::MapScope::current() == MapKey{1, 2});
    }
    CHECK_FALSE(flh::MapScope::current().has_value());
}

TEST_CASE("access: scopes are per thread")
{
    flh::MapScope here(5, 0);
    std::optional<MapKey> there = MapKey{9, 9};
    std::thread t([&] { there = flh::MapScope::current(); });
    t.join();
    CHECK_FALSE(there.has_value());
    CHECK(*flh::MapScope::current() == MapKey{5, 0});
}

TEST_CASE("access: where a world change may run")
{
    AccessRules rules;
    rules.setWorldThread();
    using Place = AccessRules::Place;
    CHECK(rules.placeForMap(MapKey{1, 0}) == Place::Direct);            // world thread, maps idle
    CHECK(rules.placeForWorld() == Place::Direct);
    rules.setMapsIdle(false);
    CHECK(rules.placeForMap(MapKey{1, 0}) == Place::Defer);
    CHECK(rules.placeForWorld() == Place::Defer);
    rules.setMapsIdle(true);
    {
        flh::MapScope scope(1, 0);
        CHECK(rules.placeForMap(MapKey{1, 0}) == Place::Direct);        // this map's own thread
        CHECK(rules.placeForMap(MapKey{2, 0}) == Place::Defer);         // another map's: queue
        CHECK(rules.placeForMap(MapKey{1, 1}) == Place::Defer);
        CHECK(rules.placeForWorld() == Place::Defer);                   // a command is the world thread's alone
    }
    Place worker = Place::Direct;
    std::thread t([&] { worker = rules.placeForMap(MapKey{1, 0}); });
    t.join();
    CHECK(worker == Place::Defer);
}

TEST_CASE("access: the player map record answers from the hooks, not from the player")
{
    flh::PlayerMaps maps;
    fl::Guid a{4ull << 58, 1}, b{4ull << 58, 2};
    CHECK_FALSE(maps.get(a).has_value());
    maps.set(a, MapKey{0, 0});
    maps.set(b, MapKey{1, 0});
    CHECK(*maps.get(a) == MapKey{0, 0});
    maps.set(a, MapKey{0, 33});                                          // OnMapChanged
    CHECK(*maps.get(a) == MapKey{0, 33});
    // The same low counter with another high half is another player (or no player at all).
    CHECK_FALSE(maps.get(fl::Guid{9ull << 58, 1}).has_value());
    maps.forget(b);                                                      // OnLogout
    CHECK_FALSE(maps.get(b).has_value());
    // The world thread's per-tick refresh replaces the whole table with the truth.
    maps.replaceAll({{b, MapKey{2, 0}}});
    CHECK_FALSE(maps.get(a).has_value());
    CHECK(*maps.get(b) == MapKey{2, 0});
    CHECK(maps.size() == 1);
    CHECK(maps.snapshot().size() == 1);

    // Many threads read while one writes: no crash, no torn value (ASan/TSan builds see it).
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (uint32_t i = 0; !stop && i < 20000; ++i)
            maps.set(a, MapKey{i % 5, i % 7});
    });
    std::thread reader([&] {
        for (int i = 0; i < 20000; ++i)
        {
            auto v = maps.get(a);
            if (v)
                CHECK(v->map < 5);
        }
    });
    reader.join();
    stop = true;
    writer.join();
}

TEST_CASE("deferred work: bounded, ordered, and a job queued by a job waits for the next drain")
{
    flh::DeferQueue q(3);
    std::vector<int> ran;
    CHECK(q.push([&] { ran.push_back(1); }));
    CHECK(q.push([&] { ran.push_back(2); q.push([&] { ran.push_back(99); }); }));
    CHECK(q.push([&] { ran.push_back(3); }));
    CHECK_FALSE(q.push([&] { ran.push_back(4); }));                      // full: dropped
    CHECK(q.dropped() == 1);
    CHECK(q.size() == 3);
    CHECK(q.drain() == 3);
    CHECK(ran == std::vector<int>{1, 2, 3});
    CHECK(q.size() == 1);                                                // the job the second job queued
    CHECK(q.drain() == 1);
    CHECK(ran.back() == 99);
    CHECK(q.drain() == 0);

    // Producers on several threads, one consumer: every accepted job runs exactly once.
    flh::DeferQueue big(100000);
    std::atomic<int> count{0};
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t)
        producers.emplace_back([&] {
            for (int i = 0; i < 2000; ++i)
                big.push([&] { ++count; });
        });
    for (auto& t : producers)
        t.join();
    CHECK(big.drain() == 8000);
    CHECK(count == 8000);
}
