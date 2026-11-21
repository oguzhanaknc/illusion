#include "Budget.hpp"

#include <illusion/assets/Animation.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/Ragdoll.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cmath>
#include <string_view>
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

// Budget: ns per Collider of one PhysicsAssetResolver::update() with every Collider resolved (the per-frame
// cost); the reload checks added by live reload must keep it at a key comparison per Collider.
ILL_BENCHMARK("physics/asset-resolve-steady", 20.0)
{
    constexpr u32 kColliders = 2000;
    JobSystem jobs;
    VirtualFileSystem files;
    AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    PhysicsWorld physics(world, jobs);
    PhysicsAssetResolver colliders(physics, streamer);
    RigidBody body;
    body.motionType = MotionType::Static;
    for (u32 i = 0; i < kColliders; ++i)
    {
        Transform transform;
        transform.position = Vec3(static_cast<f32>(i % 50) * 3.0f, 0.0f, static_cast<f32>(i / 50) * 3.0f);
        Collider collider;
        collider.shape = i % 2 == 0 ? ColliderShape::ConvexHull : ColliderShape::Mesh;
        collider.mesh = AssetRef(i % 3 == 0 ? "builtin:/mesh/cube" : "builtin:/mesh/sphere");
        world.create(transform, body, collider);
    }
    colliders.waitIdle();
    physics.update(1.0 / 60.0);
    bench.minEpochIterations(2000)
        .batch(kColliders)
        .unit("collider")
        .run("update() with 2000 resolved Colliders", [&] { colliders.update(); });
}

// Budget: ns per fixed step of 20 active ragdolls of 12 parts each falling onto the ground and each other,
// with their (re)starts every two seconds included, about 6x a desktop result (0.17 ms); ADR 0061 allows 1 ms
// of physics time.
ILL_BENCHMARK("physics/ragdolls-20", 1'000'000.0)
{
    constexpr u32 kRagdolls = 20;
    constexpr u32 kRestart = 120; // steps between restarts, so that the ragdolls never fall asleep
    struct Bone
    {
        std::string_view name;
        i32 parent;
        Vec3 translation;
        f32 radius;
    };
    const Bone bones[] = {
        {"hips", -1, Vec3(0.0f, 1.0f, 0.0f), 0.12f},       {"spine", 0, Vec3(0.0f, 0.15f, 0.0f), 0.11f},
        {"chest", 1, Vec3(0.0f, 0.2f, 0.0f), 0.13f},       {"head", 2, Vec3(0.0f, 0.3f, 0.0f), 0.1f},
        {"arm_l", 2, Vec3(0.2f, 0.2f, 0.0f), 0.05f},       {"forearm_l", 4, Vec3(0.28f, 0.0f, 0.0f), 0.045f},
        {"arm_r", 2, Vec3(-0.2f, 0.2f, 0.0f), 0.05f},      {"forearm_r", 6, Vec3(-0.28f, 0.0f, 0.0f), 0.045f},
        {"thigh_l", 0, Vec3(0.12f, -0.05f, 0.0f), 0.07f},  {"shin_l", 8, Vec3(0.0f, -0.42f, 0.0f), 0.055f},
        {"thigh_r", 0, Vec3(-0.12f, -0.05f, 0.0f), 0.07f}, {"shin_r", 10, Vec3(0.0f, -0.42f, 0.0f), 0.055f},
    };
    Skeleton skeleton;
    RagdollProfile profile;
    std::vector<Vec3> model;
    for (const Bone& bone : bones)
    {
        const Vec3 position =
            (bone.parent >= 0 ? model[static_cast<usize>(bone.parent)] : Vec3(0.0f)) + bone.translation;
        model.push_back(position);
        SkeletonJoint& joint = skeleton.joints.emplace_back();
        joint.name = bone.name;
        joint.parent = bone.parent;
        joint.translation = bone.translation;
        joint.inverseBind = glm::translate(Mat4(1.0f), -position);
        RagdollPart& part = profile.parts.emplace_back();
        part.joint = bone.name;
        part.radius = bone.radius;
        part.halfHeight = 0.08f;
    }
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
    Ragdoll settings;
    settings.active = true;
    std::vector<Entity> entities;
    for (u32 i = 0; i < kRagdolls; ++i)
    {
        entities.push_back(world.create(Transform{}, settings));
    }
    std::vector<Mat4> joints(skeleton.joints.size());
    const auto restart = [&] {
        for (u32 i = 0; i < kRagdolls; ++i)
        {
            // Four rows close enough to land on each other, tilted so that they topple.
            const Mat4 placement =
                glm::translate(Mat4(1.0f),
                               Vec3(static_cast<f32>(i % 5) * 0.6f, 1.0f + 0.5f * static_cast<f32>(i % 3),
                                    static_cast<f32>(i / 5) * 0.6f)) *
                glm::rotate(Mat4(1.0f), 0.3f + 0.1f * static_cast<f32>(i), Vec3(0.0f, 0.0f, 1.0f));
            for (usize j = 0; j < joints.size(); ++j)
            {
                joints[j] = placement * glm::inverse(skeleton.joints[j].inverseBind);
            }
            (void)physics.startRagdoll(entities[i], settings, profile, skeleton, joints);
        }
    };
    restart();
    u32 step = 0;
    bench.run("step 20 ragdolls of 12 parts", [&] {
        if (++step % kRestart == 0)
        {
            restart();
        }
        physics.update(1.0 / 60.0);
    });
}
