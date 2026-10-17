#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <cmath>
#include <vector>

using namespace illusion;

// Budget: ns per body per fixed step, roughly 5-10x a 12-thread desktop result; see Budget.hpp.
ILL_BENCHMARK("physics/step", 3000.0)
{
    constexpr u32 kSide = 10;
    constexpr u32 kBodies = kSide * kSide * kSide;
    JobSystem jobs;
    World world;
    PhysicsWorld physics(world, jobs);

    Transform ground;
    ground.position = Vec3(0.0f, -0.5f, 0.0f);
    RigidBody groundBody;
    groundBody.motionType = MotionType::Static;
    Collider groundCollider;
    groundCollider.halfExtents = Vec3(50.0f, 0.5f, 50.0f);
    world.create(ground, groundBody, groundCollider);

    // A pile of boxes that never sleeps, so every step does the same amount of work.
    RigidBody box;
    box.allowSleeping = false;
    for (u32 i = 0; i < kBodies; ++i)
    {
        Transform transform;
        transform.position =
            Vec3(static_cast<f32>(i % kSide) * 1.05f, 0.5f + static_cast<f32>(i / (kSide * kSide)) * 1.05f,
                 static_cast<f32>((i / kSide) % kSide) * 1.05f);
        world.create(transform, box, Collider{});
    }
    for (int i = 0; i < 120; ++i)
    {
        physics.update(1.0 / 60.0); // let the pile settle into contact
    }
    bench.batch(kBodies).unit("body").run("step 1000 resting boxes", [&] { physics.update(1.0 / 60.0); });
}

// Budget: ns per character per fixed step.
ILL_BENCHMARK("physics/characters", 60000.0)
{
    constexpr u32 kCharacters = 64;
    JobSystem jobs;
    World world;
    PhysicsWorld physics(world, jobs);

    Transform ground;
    ground.position = Vec3(0.0f, -0.5f, 0.0f);
    RigidBody groundBody;
    groundBody.motionType = MotionType::Static;
    Collider groundCollider;
    groundCollider.halfExtents = Vec3(100.0f, 0.5f, 100.0f);
    world.create(ground, groundBody, groundCollider);

    std::vector<Entity> characters;
    for (u32 i = 0; i < kCharacters; ++i)
    {
        Transform transform;
        transform.position = Vec3(static_cast<f32>(i % 8) * 3.0f, 0.0f, static_cast<f32>(i / 8) * 3.0f);
        characters.push_back(world.create(transform, CharacterController{}));
    }
    for (int i = 0; i < 30; ++i)
    {
        physics.update(1.0 / 60.0); // land
    }
    f32 angle = 0.0f;
    bench.batch(kCharacters).unit("character").run("step 64 walking characters", [&] {
        angle += 0.01f;
        const Vec3 velocity(std::cos(angle) * 2.0f, 0.0f, std::sin(angle) * 2.0f);
        for (const Entity character : characters)
        {
            physics.setCharacterVelocity(character, velocity);
        }
        physics.update(1.0 / 60.0);
    });
}

// Budget: ns per shape cast.
ILL_BENCHMARK("physics/shape-cast", 2000.0)
{
    constexpr u32 kSide = 10;
    JobSystem jobs;
    World world;
    PhysicsWorld physics(world, jobs);
    RigidBody fixed;
    fixed.motionType = MotionType::Static;
    for (u32 i = 0; i < kSide * kSide * kSide; ++i)
    {
        Transform transform;
        transform.position =
            Vec3(static_cast<f32>(i % kSide) * 2.0f, static_cast<f32>(i / (kSide * kSide)) * 2.0f,
                 static_cast<f32>((i / kSide) % kSide) * 2.0f);
        world.create(transform, fixed, Collider{});
    }
    physics.update(0.0);
    Collider probe;
    probe.shape = ColliderShape::Sphere;
    probe.radius = 0.3f;
    u32 i = 0;
    bench.run("sphere cast into 1000 boxes", [&] {
        const f32 x = static_cast<f32>(i++ % 19);
        ankerl::nanobench::doNotOptimizeAway(physics.shapeCast(
            probe, Vec3(x, 30.0f, 3.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 50.0f));
    });
}
