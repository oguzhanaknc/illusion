#include "TestComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/ecs/CommandBuffer.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

using namespace illusion;
using namespace ecs_test;
using test::LifetimeCounter;

TEST_CASE("CommandBuffer defers structural changes recorded during iteration")
{
    World world;
    const Entity a = world.create(Health{0});
    const Entity b = world.create(Health{10});
    const Entity c = world.create(Health{20});

    CommandBuffer commands;
    world.query<const Health>().each([&](Entity e, const Health& h) {
        if (h.value == 0)
        {
            commands.destroy(e);
        }
        else if (h.value == 10)
        {
            commands.set(e, Position{1, 2, 3});
            commands.set(e, PlayerTag{});
        }
        else
        {
            commands.remove<Health>(e);
        }
    });
    CHECK(commands.size() == 4);
    CHECK(world.isAlive(a)); // nothing applied yet

    commands.apply(world);
    CHECK(commands.empty());
    CHECK_FALSE(world.isAlive(a));
    CHECK(world.get<Position>(b).z == 3.0f);
    CHECK(world.has<PlayerTag>(b));
    CHECK_FALSE(world.has<Health>(c));
}

TEST_CASE("CommandBuffer::set replaces existing components")
{
    World world;
    const Entity e = world.create(Name{"old"});
    CommandBuffer commands;
    commands.set(e, Name{"new"});
    commands.apply(world);
    CHECK(world.get<Name>(e).value == "new");
}

TEST_CASE("CommandBuffer::create spawns entities with components")
{
    World world;
    CommandBuffer commands;
    commands.create(Position{5, 0, 0}, Name{"spawned"}, EnemyTag{});
    commands.create(Health{3});
    commands.apply(world);
    CHECK(world.aliveCount() == 2);
    CHECK(world.query<const Position, const Name>().with<EnemyTag>().count() == 1);
    world.query<const Name>().each([](const Name& n) { CHECK(n.value == "spawned"); });
    CHECK(world.query<const Health>().count() == 1);
}

TEST_CASE("CommandBuffer ignores commands for dead entities without leaking")
{
    LifetimeCounter::reset();
    {
        World world;
        const Entity e = world.create();
        CommandBuffer commands;
        commands.set(e, LifetimeCounter(1));
        commands.remove<Position>(e);
        commands.destroy(e);
        world.destroy(e);
        commands.apply(world);
        CHECK(LifetimeCounter::alive == 0);
    }
    CHECK(LifetimeCounter::alive == 0);
}

TEST_CASE("CommandBuffer::clear and destruction release recorded payloads")
{
    LifetimeCounter::reset();
    World world;
    const Entity e = world.create();
    {
        CommandBuffer commands;
        commands.set(e, LifetimeCounter(1));
        CHECK(LifetimeCounter::alive == 1);
        commands.clear();
        CHECK(LifetimeCounter::alive == 0);
        CHECK(commands.empty());
        commands.set(e, LifetimeCounter(2));
    }
    CHECK(LifetimeCounter::alive == 0);
}

TEST_CASE("CommandBuffer spans multiple blocks and handles oversized payloads")
{
    World world;
    CommandBuffer commands(1024);
    for (int i = 0; i < 2000; ++i)
    {
        commands.create(Health{i});
    }
    commands.create(Big{});
    commands.create(Aligned64{});
    commands.apply(world);
    CHECK(world.query<const Health>().count() == 2000);
    CHECK(world.query<const Big>().count() == 1);
    CHECK(world.query<const Aligned64>().count() == 1);

    // Reuse after apply.
    for (int i = 0; i < 2000; ++i)
    {
        commands.create(Health{i});
    }
    commands.apply(world);
    CHECK(world.query<const Health>().count() == 4000);
}
