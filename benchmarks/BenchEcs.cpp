#include "Budget.hpp"

#include <illusion/ecs/CommandBuffer.hpp>
#include <illusion/ecs/World.hpp>

#include <vector>

using namespace illusion;

namespace bench_ecs
{
struct Position
{
    float x, y, z;
};
struct Velocity
{
    float x, y, z;
};
struct Health
{
    int value;
};
} // namespace bench_ecs

using namespace bench_ecs;

namespace
{
constexpr u32 kEntities = 100000;
}

// Budgets (ns per entity/operation) are roughly 5-10x a 12-thread desktop result; see Budget.hpp.

ILL_BENCHMARK("ecs/iterate", 5.0)
{
    World world;
    for (u32 i = 0; i < kEntities; ++i)
    {
        world.create(Position{0, 0, 0}, Velocity{1, 1, 1});
    }
    auto query = world.query<Position, const Velocity>();
    bench.batch(kEntities).unit("entity").run("each Position += Velocity", [&] {
        query.each([](Position& p, const Velocity& v) {
            p.x += v.x;
            p.y += v.y;
            p.z += v.z;
        });
        ankerl::nanobench::doNotOptimizeAway(world);
    });
}

ILL_BENCHMARK("ecs/parallel-iterate", 3.0)
{
    JobSystem jobs;
    World world;
    for (u32 i = 0; i < kEntities * 10; ++i)
    {
        world.create(Position{0, 0, 0}, Velocity{1, 1, 1});
    }
    auto query = world.query<Position, const Velocity>();
    bench.batch(kEntities * 10).unit("entity").run("parallelEach Position += Velocity", [&] {
        query.parallelEach(jobs, [](Position& p, const Velocity& v) {
            p.x += v.x;
            p.y += v.y;
            p.z += v.z;
        });
    });
}

ILL_BENCHMARK("ecs/create-destroy", 250.0)
{
    World world;
    std::vector<Entity> entities;
    entities.reserve(kEntities);
    bench.batch(kEntities).unit("entity").run("create + destroy 3 components", [&] {
        for (u32 i = 0; i < kEntities; ++i)
        {
            entities.push_back(world.create(Position{0, 0, 0}, Velocity{0, 0, 0}, Health{100}));
        }
        for (const Entity e : entities)
        {
            world.destroy(e);
        }
        entities.clear();
    });
}

ILL_BENCHMARK("ecs/add-remove", 200.0)
{
    World world;
    std::vector<Entity> entities;
    for (u32 i = 0; i < kEntities; ++i)
    {
        entities.push_back(world.create(Position{0, 0, 0}));
    }
    bench.batch(kEntities * 2).unit("op").run("emplace + remove Velocity", [&] {
        for (const Entity e : entities)
        {
            world.emplace<Velocity>(e, 1.0f, 1.0f, 1.0f);
        }
        for (const Entity e : entities)
        {
            world.remove<Velocity>(e);
        }
    });
}

ILL_BENCHMARK("ecs/get", 15.0)
{
    World world;
    std::vector<Entity> entities;
    for (u32 i = 0; i < kEntities; ++i)
    {
        entities.push_back(world.create(Position{0, 0, 0}, Health{static_cast<int>(i)}));
    }
    bench.batch(kEntities).unit("lookup").run("get<Health>", [&] {
        int sum = 0;
        for (const Entity e : entities)
        {
            sum += world.get<Health>(e).value;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

ILL_BENCHMARK("ecs/command-buffer", 250.0)
{
    World world;
    CommandBuffer commands;
    std::vector<Entity> entities;
    for (u32 i = 0; i < kEntities; ++i)
    {
        entities.push_back(world.create(Position{0, 0, 0}));
    }
    bench.batch(kEntities * 2).unit("command").run("set + remove via CommandBuffer", [&] {
        for (const Entity e : entities)
        {
            commands.set(e, Health{1});
        }
        commands.apply(world);
        for (const Entity e : entities)
        {
            commands.remove<Health>(e);
        }
        commands.apply(world);
    });
}
