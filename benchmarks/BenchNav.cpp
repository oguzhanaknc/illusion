#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavBuild.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace illusion;

namespace
{

// A rolling terrain of @p quads x @p quads cells over @p extent metres (two triangles per cell), centred on
// the origin, as bake input for a surface of the same size.
NavGeometry terrain(u32 quads, f32 extent)
{
    NavGeometry geometry;
    const f32 step = extent / static_cast<f32>(quads);
    const f32 half = extent * 0.5f;
    for (u32 z = 0; z <= quads; ++z)
    {
        for (u32 x = 0; x <= quads; ++x)
        {
            const f32 px = static_cast<f32>(x) * step - half;
            const f32 pz = static_cast<f32>(z) * step - half;
            geometry.vertices.emplace_back(px, 0.4f * std::sin(px * 0.7f) * std::cos(pz * 0.5f), pz);
        }
    }
    const u32 row = quads + 1;
    for (u32 z = 0; z < quads; ++z)
    {
        for (u32 x = 0; x < quads; ++x)
        {
            const u32 a = z * row + x;
            geometry.indices.insert(geometry.indices.end(), {a, a + row, a + row + 1, a, a + row + 1, a + 1});
            geometry.areas.insert(geometry.areas.end(), {u8{0}, u8{0}});
        }
    }
    geometry.bounds = Aabb{.min = Vec3(-half, -2.0f, -half), .max = Vec3(half, 4.0f, half)};
    return geometry;
}

void addBox(World& world, Vec3 center, Vec3 halfExtents)
{
    Transform transform;
    transform.position = center;
    Collider collider;
    collider.shape = ColliderShape::Box;
    collider.halfExtents = halfExtents;
    world.create(transform, collider);
}

// A 256 m ground with a 10 m wall every 16 m, alternating along x and z, and its surface.
void wallMaze(World& world)
{
    Transform center;
    center.position = Vec3(0.0f, 2.0f, 0.0f);
    NavMeshSurface surface;
    surface.size = Vec3(256.0f, 8.0f, 256.0f);
    world.create(center, surface);
    addBox(world, Vec3(0.0f, -0.5f, 0.0f), Vec3(128.0f, 0.5f, 128.0f));
    for (i32 z = -7; z <= 7; ++z)
    {
        for (i32 x = -7; x <= 7; ++x)
        {
            const bool alongX = ((x + z) & 1) == 0;
            addBox(world, Vec3(static_cast<f32>(x) * 16.0f, 1.0f, static_cast<f32>(z) * 16.0f),
                   alongX ? Vec3(5.0f, 1.0f, 0.25f) : Vec3(0.25f, 1.0f, 5.0f));
        }
    }
}

} // namespace

// Budget: ns per bake of one 64 x 64-cell tile (9.6 m) of a 10,000-triangle terrain, about 7x a desktop
// result (2.8 ms); ADR 0062 aims at 20 ms.
ILL_BENCHMARK("nav/bake-tile", 20'000'000.0)
{
    const NavGeometry geometry = terrain(71, 9.6f);
    bench.unit("tile").run("bake one tile of 10,082 triangles", [&] {
        Expected<NavMeshData, std::string> data = bakeNavMesh(geometry, nullptr);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

// Budget: ns per bake of a 256 m surface (27 x 27 tiles) of a 32,768-triangle terrain with the Job System,
// about 6x a 12-thread desktop result (99 ms).
ILL_BENCHMARK("nav/bake-256m", 600'000'000.0)
{
    JobSystem jobs;
    const NavGeometry geometry = terrain(128, 256.0f);
    bench.epochs(3).epochIterations(1).warmup(1).unit("bake").run("bake 729 tiles in parallel", [&] {
        Expected<NavMeshData, std::string> data = bakeNavMesh(geometry, &jobs);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

// Budget: ns per 1,000 paths of about 100 m on a 256 m surface of 729 tiles with 225 walls, one thread, about
// 6x a desktop result (13 ms, mean length 96 m); ADR 0062 aims at 50 ms.
ILL_BENCHMARK("nav/find-path-1000", 80'000'000.0)
{
    JobSystem jobs;
    World world;
    wallMaze(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(&jobs);
    Random random(7);
    std::vector<std::array<Vec3, 2>> queries(1000);
    for (auto& [from, to] : queries)
    {
        const f32 angle = static_cast<f32>(random.uniform01()) * 2.0f * std::numbers::pi_v<f32>;
        const Vec2 start(static_cast<f32>(random.uniform01()) * 160.0f - 80.0f,
                         static_cast<f32>(random.uniform01()) * 160.0f - 80.0f);
        const Vec2 end =
            glm::clamp(start + 100.0f * Vec2(std::cos(angle), std::sin(angle)), Vec2(-125.0f), Vec2(125.0f));
        from = Vec3(start.x, 0.0f, start.y);
        to = Vec3(end.x, 0.0f, end.y);
    }
    std::array<Vec3, 256> corners{};
    bench.unit("1,000 paths").run("find 1,000 paths of about 100 m", [&] {
        f32 length = 0.0f;
        for (const auto& [from, to] : queries)
        {
            if (const Expected<NavPathResult, NavError> path = navigation.findPath(from, to, corners))
            {
                length += path->length;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(length);
    });
}
