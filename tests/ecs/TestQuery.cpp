#include "TestComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <set>

using namespace illusion;
using namespace ecs_test;

TEST_CASE("each visits exactly the entities with all requested components")
{
    World world;
    const Entity moving = world.create(Position{}, Velocity{1, 2, 3});
    const Entity still = world.create(Position{});
    const Entity onlyVelocity = world.create(Velocity{});
    const Entity movingPlayer = world.create(Position{}, Velocity{1, 0, 0}, PlayerTag{});

    std::set<u64> visited;
    world.query<Position, const Velocity>().each([&](Entity e, Position& p, const Velocity& v) {
        visited.insert(e.toBits());
        p.x += v.x;
    });
    CHECK(visited == std::set<u64>{moving.toBits(), movingPlayer.toBits()});
    CHECK(world.get<Position>(moving).x == 1.0f);
    CHECK(world.get<Position>(still).x == 0.0f);
    (void)onlyVelocity;
}

TEST_CASE("each accepts callables without the Entity parameter")
{
    World world;
    world.create(Health{1});
    world.create(Health{2});
    int sum = 0;
    world.query<const Health>().each([&](const Health& h) { sum += h.value; });
    CHECK(sum == 3);
}

TEST_CASE("with and without filters")
{
    World world;
    world.create(Position{}, PlayerTag{});
    world.create(Position{}, EnemyTag{});
    world.create(Position{}, EnemyTag{});
    world.create(Position{});

    CHECK(world.query<Position>().count() == 4);
    CHECK(world.query<Position>().with<PlayerTag>().count() == 1);
    CHECK(world.query<Position>().with<EnemyTag>().count() == 2);
    CHECK(world.query<Position>().without<PlayerTag, EnemyTag>().count() == 1);
    CHECK(world.query<Position>().without<PlayerTag>().count() == 3);
}

TEST_CASE("Queries see archetypes created after their first use")
{
    World world;
    world.create(Position{});
    CHECK(world.query<Position>().count() == 1);
    world.create(Position{}, Health{});  // new archetype
    world.create(Position{}, Name{"x"}); // another new archetype
    CHECK(world.query<Position>().count() == 3);
    CHECK(world.query<Position, Health>().count() == 1);
}

TEST_CASE("each iterates across many chunks")
{
    World world;
    constexpr int kCount = 50000;
    for (int i = 0; i < kCount; ++i)
    {
        world.create(Health{i});
    }
    long long sum = 0;
    int visited = 0;
    world.query<const Health>().each([&](const Health& h) {
        sum += h.value;
        ++visited;
    });
    CHECK(visited == kCount);
    CHECK(sum == static_cast<long long>(kCount) * (kCount - 1) / 2);
}

TEST_CASE("eachChunk exposes contiguous component arrays")
{
    World world;
    for (int i = 0; i < 3000; ++i)
    {
        world.create(Position{static_cast<float>(i), 0, 0}, Velocity{1, 0, 0});
    }
    u32 total = 0;
    world.query<Position, const Velocity>().eachChunk([&](ChunkView<Position, const Velocity>& chunk) {
        const std::span<Position> positions = chunk.get<Position>();
        const std::span<const Velocity> velocities = chunk.get<const Velocity>();
        CHECK(positions.size() == chunk.size());
        CHECK(chunk.entities().size() == chunk.size());
        for (u32 i = 0; i < chunk.size(); ++i)
        {
            positions[i].x += velocities[i].x;
        }
        total += chunk.size();
    });
    CHECK(total == 3000);
    double sum = 0;
    world.query<const Position>().each([&](const Position& p) { sum += static_cast<double>(p.x); });
    CHECK(sum == doctest::Approx(3000.0 * 2999.0 / 2.0 + 3000.0));
}

TEST_CASE("parallelEach processes every entity once")
{
    World world;
    JobSystem jobs(JobSystemDesc{4, 1024});
    constexpr int kCount = 40000;
    for (int i = 0; i < kCount; ++i)
    {
        if (i % 2 == 0)
        {
            world.create(Position{}, Velocity{1, 0, 0});
        }
        else
        {
            world.create(Position{}, Velocity{1, 0, 0}, Health{});
        }
    }
    std::atomic<int> visited{0};
    world.query<Position, const Velocity>().parallelEach(jobs, [&](Position& p, const Velocity& v) {
        p.x += v.x;
        visited.fetch_add(1, std::memory_order_relaxed);
    });
    CHECK(visited.load() == kCount);
    bool allMoved = true;
    world.query<const Position>().each([&](const Position& p) { allMoved = allMoved && p.x == 1.0f; });
    CHECK(allMoved);
    CHECK_FALSE(world.isIterating());
}

TEST_CASE("Queries on an empty world do nothing")
{
    World world;
    int calls = 0;
    world.query<Position>().each([&](Position&) { ++calls; });
    CHECK(calls == 0);
    CHECK(world.query<Position>().count() == 0);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("Structural changes during iteration assert")
{
    World world;
    const Entity e = world.create(Position{});
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS_AS(world.query<Position>().each([&](Position&) { world.create(); }), test::AssertionFailure);
    CHECK_THROWS_AS(world.query<Position>().each([&](Position&) { world.remove<Position>(e); }),
                    test::AssertionFailure);
    CHECK_FALSE(world.isIterating()); // the scope unwound correctly
}
#endif
