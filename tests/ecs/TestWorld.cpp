#include "TestComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

using namespace illusion;
using namespace ecs_test;
using test::LifetimeCounter;

TEST_CASE("Creating and destroying entities")
{
    World world;
    CHECK(world.aliveCount() == 0);

    const Entity a = world.create();
    const Entity b = world.create(Position{1, 2, 3});
    CHECK(world.isAlive(a));
    CHECK(world.isAlive(b));
    CHECK(a != b);
    CHECK(world.aliveCount() == 2);

    world.destroy(a);
    CHECK_FALSE(world.isAlive(a));
    CHECK(world.isAlive(b));
    CHECK(world.aliveCount() == 1);
    CHECK_FALSE(world.isAlive(kNullEntity));
}

TEST_CASE("Recycled entity slots get a new generation")
{
    World world;
    const Entity first = world.create();
    world.destroy(first);
    const Entity second = world.create();
    CHECK(second.index == first.index);
    CHECK(second.generation == first.generation + 1);
    CHECK_FALSE(world.isAlive(first));
    CHECK(world.isAlive(second));
}

TEST_CASE("create() with components stores their values")
{
    World world;
    const Entity e = world.create(Position{1, 2, 3}, Velocity{4, 5, 6}, PlayerTag{});
    CHECK(world.has<Position>(e));
    CHECK(world.has<Velocity>(e));
    CHECK(world.has<PlayerTag>(e));
    CHECK_FALSE(world.has<Health>(e));
    CHECK(world.get<Position>(e).y == 2.0f);
    CHECK(world.get<Velocity>(e).z == 6.0f);
}

TEST_CASE("emplace, set, get, tryGet and remove")
{
    World world;
    const Entity e = world.create();

    Position& position = world.emplace<Position>(e, 1.0f, 2.0f, 3.0f);
    CHECK(position.x == 1.0f);
    world.emplace<PlayerTag>(e);
    CHECK(world.has<PlayerTag>(e));

    world.set(e, Health{50});
    CHECK(world.get<Health>(e).value == 50);
    world.set(e, Health{75}); // assigns in place
    CHECK(world.get<Health>(e).value == 75);

    CHECK(world.tryGet<Velocity>(e) == nullptr);
    REQUIRE(world.tryGet<Position>(e) != nullptr);

    const World& constWorld = world;
    CHECK(constWorld.get<Position>(e).z == 3.0f);

    CHECK(world.remove<Position>(e));
    CHECK_FALSE(world.has<Position>(e));
    CHECK_FALSE(world.remove<Position>(e));
    CHECK(world.get<Health>(e).value == 75); // other components survive the move
    CHECK(world.remove<PlayerTag>(e));
    CHECK_FALSE(world.has<PlayerTag>(e));
}

TEST_CASE("emplace may reference the entity's own components")
{
    World world;
    const Entity e = world.create(Position{1, 2, 3});
    // The Position reference would dangle if the World moved the entity before constructing Velocity.
    const Position& position = world.get<Position>(e);
    world.emplace<Velocity>(e, position.x, position.y, position.z);
    CHECK(world.get<Velocity>(e).y == 2.0f);
}

TEST_CASE("Components keep their values when other entities are destroyed (swap-remove)")
{
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 5000; ++i)
    {
        entities.push_back(world.create(Health{i}));
    }
    // Destroy every third entity; survivors must keep their own values.
    for (usize i = 0; i < entities.size(); i += 3)
    {
        world.destroy(entities[i]);
    }
    for (usize i = 0; i < entities.size(); ++i)
    {
        if (i % 3 == 0)
        {
            CHECK_FALSE(world.isAlive(entities[i]));
        }
        else
        {
            REQUIRE(world.isAlive(entities[i]));
            CHECK(world.get<Health>(entities[i]).value == static_cast<int>(i));
        }
    }
}

TEST_CASE("Non-trivial components are constructed and destroyed exactly once")
{
    LifetimeCounter::reset();
    {
        World world;
        std::vector<Entity> entities;
        for (int i = 0; i < 1000; ++i)
        {
            entities.push_back(world.create(LifetimeCounter(i), Name{"entity " + std::to_string(i)}));
        }
        CHECK(LifetimeCounter::alive == 1000);

        // Archetype moves must relocate, not copy or leak.
        for (usize i = 0; i < entities.size(); i += 2)
        {
            world.emplace<Position>(entities[i]);
        }
        CHECK(LifetimeCounter::alive == 1000);

        for (usize i = 0; i < entities.size(); i += 4)
        {
            world.remove<LifetimeCounter>(entities[i]);
        }
        CHECK(LifetimeCounter::alive == 750);

        for (usize i = 1; i < entities.size(); i += 4)
        {
            world.destroy(entities[i]);
        }
        CHECK(LifetimeCounter::alive == 500);

        for (usize i = 2; i < entities.size(); i += 4)
        {
            CHECK(world.get<LifetimeCounter>(entities[i]).value == static_cast<int>(i));
            CHECK(world.get<Name>(entities[i]).value == "entity " + std::to_string(i));
        }
    }
    CHECK(LifetimeCounter::alive == 0);
}

TEST_CASE("Over-aligned and large components")
{
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 100; ++i)
    {
        const Entity e = world.create(Aligned64{}, Big{});
        world.get<Big>(e).bytes[999] = static_cast<u8>(i);
        entities.push_back(e);
    }
    for (usize i = 0; i < entities.size(); ++i)
    {
        const auto address = reinterpret_cast<std::uintptr_t>(&world.get<Aligned64>(entities[i]));
        CHECK(address % 64 == 0);
        CHECK(world.get<Big>(entities[i]).bytes[999] == static_cast<u8>(i));
    }
}

TEST_CASE("clear() destroys everything and invalidates handles")
{
    LifetimeCounter::reset();
    World world;
    const Entity a = world.create(LifetimeCounter(1));
    const Entity b = world.create(Position{});
    world.clear();
    CHECK(LifetimeCounter::alive == 0);
    CHECK(world.aliveCount() == 0);
    CHECK_FALSE(world.isAlive(a));
    CHECK_FALSE(world.isAlive(b));

    const Entity c = world.create(Position{7, 0, 0});
    CHECK(world.isAlive(c));
    CHECK(world.get<Position>(c).x == 7.0f);
    CHECK_FALSE(world.isAlive(a));
}

TEST_CASE("Archetypes are shared between entities with the same components")
{
    World world;
    CHECK(world.archetypeCount() == 1); // the empty archetype
    const Entity a = world.create(Position{}, Velocity{});
    CHECK(world.archetypeCount() == 2);   // no intermediate {Position} archetype
    world.create(Velocity{}, Position{}); // order does not matter
    CHECK(world.archetypeCount() == 2);
    world.remove<Velocity>(a); // creates {Position}
    CHECK(world.archetypeCount() == 3);
    world.emplace<Velocity>(a); // back to {Position, Velocity}
    CHECK(world.archetypeCount() == 3);
}

TEST_CASE("Randomized operations match a reference model")
{
    World world;
    std::mt19937 rng(12345);
    struct Expected
    {
        bool hasPosition = false;
        bool hasHealth = false;
        float x = 0;
        int health = 0;
    };
    std::unordered_map<u64, Expected> model;
    std::vector<Entity> alive;

    for (int step = 0; step < 20000; ++step)
    {
        const auto op = static_cast<u32>(rng() % 6);
        if (op == 0 || alive.empty())
        {
            const Entity e = world.create();
            alive.push_back(e);
            model[e.toBits()] = Expected{};
            continue;
        }
        const auto pick = static_cast<usize>(rng() % alive.size());
        const Entity e = alive[pick];
        Expected& expected = model[e.toBits()];
        switch (op)
        {
        case 1:
            world.destroy(e);
            model.erase(e.toBits());
            alive[pick] = alive.back();
            alive.pop_back();
            break;
        case 2:
            expected.x = static_cast<float>(step);
            expected.hasPosition = true;
            world.set(e, Position{expected.x, 0, 0});
            break;
        case 3:
            expected.health = step;
            expected.hasHealth = true;
            world.set(e, Health{step});
            break;
        case 4:
            CHECK(world.remove<Position>(e) == expected.hasPosition);
            expected.hasPosition = false;
            break;
        default:
            CHECK(world.remove<Health>(e) == expected.hasHealth);
            expected.hasHealth = false;
            break;
        }
    }

    CHECK(world.aliveCount() == alive.size());
    for (const Entity e : alive)
    {
        const Expected& expected = model[e.toBits()];
        REQUIRE(world.isAlive(e));
        CHECK(world.has<Position>(e) == expected.hasPosition);
        CHECK(world.has<Health>(e) == expected.hasHealth);
        if (expected.hasPosition)
        {
            CHECK(world.get<Position>(e).x == expected.x);
        }
        if (expected.hasHealth)
        {
            CHECK(world.get<Health>(e).value == expected.health);
        }
    }
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("Destroying a dead entity asserts")
{
    World world;
    const Entity e = world.create();
    world.destroy(e);
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS_AS(world.destroy(e), test::AssertionFailure);
}
#endif
