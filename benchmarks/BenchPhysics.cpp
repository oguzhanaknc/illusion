#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

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
