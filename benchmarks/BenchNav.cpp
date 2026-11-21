#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/nav/NavBuild.hpp>

#include <cmath>

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
