#include "Budget.hpp"

#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>

#include <format>

using namespace illusion;

namespace
{

// A level of 1000 unsaved entities and 1000 saved ones: 100 tagged roots under the level root, with nine
// children each.
void buildLevel(World& world)
{
    reflectCoreComponents();
    registerSaveComponents();
    const Entity level = world.create(Transform{}, Name{"Level"});
    for (int i = 0; i < 999; ++i)
    {
        world.create(Transform{Vec3(static_cast<f32>(i), 0.0f, 0.0f)}, Name{std::format("Rock {}", i)});
    }
    for (int i = 0; i < 100; ++i)
    {
        const Entity root = world.create(Transform{}, Name{std::format("Unit {}", i)}, Saved{});
        static_cast<void>(setParent(world, root, level));
        for (int k = 0; k < 9; ++k)
        {
            const Entity part = world.create(Transform{}, Name{std::format("Part {}", k)});
            static_cast<void>(setParent(world, part, root));
        }
    }
    GuidGenerator guids(7);
    assignMissingGuids(world, guids);
}

} // namespace

// Measured about 1.9 ms on a desktop.
ILL_BENCHMARK("save/capture-1000", 15'000'000.0)
{
    World world;
    buildLevel(world);
    bench.unit("save").run("captureSave() of 1000 saved entities in a World of 2000", [&] {
        const auto captured = captureSave(world);
        ankerl::nanobench::doNotOptimizeAway(captured);
    });
}

// Measured about 1.8 ms on a desktop.
ILL_BENCHMARK("save/apply-1000", 15'000'000.0)
{
    World world;
    buildLevel(world);
    const auto captured = captureSave(world);
    if (!captured)
    {
        return;
    }
    bench.unit("save").run("applySave() of 1000 saved entities in a World of 2000", [&] {
        const auto applied = applySave(world, captured->world);
        ankerl::nanobench::doNotOptimizeAway(applied);
    });
}
