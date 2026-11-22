// Spring bones (ADR 0061): the pure chain solver (gravity, stiffness, colliders, fixed-step determinism) and
// the SpringBone stage of AnimationWorld (inertia from the Entity's motion, the editor's animated pose,
// reflection and allocation).

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/animation/Pose.hpp>
#include <illusion/animation/SpringBones.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

// "root" 2 m up and four joints of 0.25 m below it along @p direction: a strand of hair.
Skeleton strand(Vec3 direction = Vec3(0.0f, -1.0f, 0.0f))
{
    Skeleton skeleton;
    for (u32 j = 0; j < 5; ++j)
    {
        SkeletonJoint joint;
        joint.name = j == 0 ? "root" : "link" + std::to_string(j);
        joint.parent = static_cast<i32>(j) - 1;
        joint.translation = j == 0 ? Vec3(0.0f, 2.0f, 0.0f) : direction * 0.25f;
        skeleton.joints.push_back(joint);
    }
    return skeleton;
}

constexpr std::array<u32, 5> kChain{0, 1, 2, 3, 4};

// World positions of @p local's joints under @p world.
std::vector<Vec3> positions(const Skeleton& skeleton, std::span<const JointPose> local, const Mat4& world)
{
    std::vector<Mat4> model(local.size());
    localToModel(skeleton, local, model);
    std::vector<Vec3> result;
    for (const Mat4& joint : model)
    {
        result.emplace_back(world * joint[3]);
    }
    return result;
}

// Steps a strand whose root is carried along x by a sine for @p frames frames; returns the final local pose.
std::vector<JointPose> swing(int frames, f32 stiffness)
{
    const Skeleton skeleton = strand();
    std::vector<JointPose> rest(skeleton.joints.size());
    restPose(skeleton, rest);
    std::vector<SpringParticle> particles(kChain.size());
    SpringSolve solve;
    solve.stiffness = stiffness;
    resetSpringChain(skeleton, kChain, solve.world, rest, particles);
    std::vector<JointPose> local = rest;
    for (int frame = 0; frame < frames; ++frame)
    {
        local = rest;
        solve.world = glm::translate(Mat4(1.0f), Vec3(std::sin(0.1f * static_cast<f32>(frame)), 0.0f, 0.0f));
        stepSpringChain(skeleton, kChain, solve, {}, 1, particles, local);
    }
    return local;
}

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

// "hips" 0.9 m up with "spine" (0.15 m) and "head" (0.45 m) above it: no clips, the rest pose shows.
void writeTorso(VirtualFileSystem& files)
{
    REQUIRE(files.writeText("game:/torso.gltf",
                            R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
"nodes": [{"name": "hips", "translation": [0, 0.9, 0], "children": [1]}, {"name": "spine", "translation": [0, 0.15, 0], "children": [2]},
{"name": "head", "translation": [0, 0.45, 0]}], "skins": [{"joints": [0, 1, 2]}]})") == FileError::None);
}

struct Fixture
{
    Fixture()
    {
        reflectAll();
        writeTorso(project.files);
    }

    Entity create(Vec3 position)
    {
        Animator animator;
        animator.skeleton = AssetRef("game:/torso.gltf#skin/0");
        SpringBone spring;
        spring.root = "spine";
        spring.stiffness = 0.2f;
        spring.damping = 0.1f;
        Transform transform;
        transform.position = position;
        return world.create(transform, animator, spring);
    }

    [[nodiscard]] Vec3 head(Entity entity) const
    {
        const Expected<Mat4, AnimationError> matrix = service.jointTransform(entity, "head");
        REQUIRE(matrix.hasValue());
        return Vec3((*matrix)[3]);
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

} // namespace

TEST_CASE("a spring chain repeats exactly with the same fixed steps")
{
    const std::vector<JointPose> first = swing(120, 0.3f);
    const std::vector<JointPose> second = swing(120, 0.3f);
    REQUIRE(first.size() == second.size());
    for (usize j = 0; j < first.size(); ++j)
    {
        CHECK(first[j].rotation == second[j].rotation);
        CHECK(first[j].translation == second[j].translation);
    }
    const std::vector<JointPose> shorter = swing(119, 0.3f);
    CHECK(shorter[2].rotation != first[2].rotation);
}

TEST_CASE("a spring chain sags under gravity, keeps its bone lengths and holds the animated pose when stiff")
{
    const Skeleton skeleton = strand(Vec3(1.0f, 0.0f, 0.0f)); // sticking out sideways
    std::vector<JointPose> rest(skeleton.joints.size());
    restPose(skeleton, rest);
    std::vector<SpringParticle> particles(kChain.size());
    SpringSolve solve;
    solve.stiffness = 0.0f;
    solve.damping = 0.1f;
    resetSpringChain(skeleton, kChain, solve.world, rest, particles);
    std::vector<JointPose> local = rest;
    for (int frame = 0; frame < 600; ++frame)
    {
        local = rest;
        stepSpringChain(skeleton, kChain, solve, {}, 1, particles, local);
    }
    const std::vector<Vec3> hanging = positions(skeleton, local, Mat4(1.0f));
    CHECK(hanging[0] == Vec3(0.0f, 2.0f, 0.0f)); // the root stays
    CHECK(hanging[4].y < 2.0f - 0.9f);           // the tip hangs down
    for (usize j = 1; j < hanging.size(); ++j)
    {
        CHECK(glm::length(hanging[j] - hanging[j - 1]) == doctest::Approx(0.25f).epsilon(1e-4));
    }

    solve.stiffness = 1.0f;
    resetSpringChain(skeleton, kChain, solve.world, rest, particles);
    for (int frame = 0; frame < 60; ++frame)
    {
        local = rest;
        stepSpringChain(skeleton, kChain, solve, {}, 1, particles, local);
    }
    const std::vector<Vec3> stiff = positions(skeleton, local, Mat4(1.0f));
    CHECK(glm::length(stiff[4] - Vec3(1.0f, 2.0f, 0.0f)) < 0.01f);

    // Zero steps apply the particles without moving them.
    const std::vector<SpringParticle> kept = particles;
    local = rest;
    stepSpringChain(skeleton, kChain, solve, {}, 0, particles, local);
    for (usize j = 0; j < particles.size(); ++j)
    {
        CHECK(particles[j].position == kept[j].position);
    }
}

TEST_CASE("a collider sphere keeps the spring chain out")
{
    const Skeleton skeleton = strand();
    std::vector<JointPose> rest(skeleton.joints.size());
    restPose(skeleton, rest);
    std::vector<SpringParticle> particles(kChain.size());
    SpringSolve solve;
    solve.stiffness = 0.5f;
    solve.radius = 0.02f;
    // A ball right where the strand hangs, a little in front of it.
    const std::array<SpringSphere, 1> ball{SpringSphere{.center = Vec3(0.0f, 1.45f, 0.08f), .radius = 0.2f}};
    resetSpringChain(skeleton, kChain, solve.world, rest, particles);
    std::vector<JointPose> local = rest;
    for (int frame = 0; frame < 240; ++frame)
    {
        local = rest;
        stepSpringChain(skeleton, kChain, solve, ball, 1, particles, local);
    }
    for (const SpringParticle& particle : particles)
    {
        CHECK(glm::length(particle.position - ball[0].center) >= 0.22f - 1e-4f);
    }
    const std::vector<Vec3> joints = positions(skeleton, local, Mat4(1.0f));
    for (const Vec3& joint : joints)
    {
        CHECK(glm::length(joint - ball[0].center) > 0.2f);
    }
    CHECK(joints[2].z < 0.0f); // pushed behind the ball
}

TEST_CASE("SpringBone is reflected as a saved Component with its colliders and a manual section")
{
    reflectAll();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId id = registry.findByStableName("illusion.SpringBone");
    REQUIRE(id != kInvalidComponentId);
    const TypeReflection* type = registry.reflection(id);
    CHECK_FALSE(type->meta.transient);
    CHECK(type->meta.docsAnchor == "manual/animation.md#spring-bones");
    CHECK(type->meta.icon == "waves");
    CHECK(type->field("stiffness")->meta.unit == Unit::Ratio);
    CHECK(type->field("radius")->meta.unit == Unit::Meters);
    CHECK(type->field("colliders")->type == FieldType::FixedArray);
    CHECK(registry.structs().findByStableName("illusion.SpringCollider") != kInvalidStructId);
}

TEST_CASE("a SpringBone swings behind a moving Entity and settles back when it stops")
{
    Fixture f;
    const Entity entity = f.create(Vec3(0.0f));
    f.animation.waitIdle();
    CHECK(glm::length(f.head(entity) - Vec3(0.0f, 1.5f, 0.0f)) < 1e-4f); // the editor's animated pose
    f.animation.update(kStep, &f.jobs, true);
    for (int frame = 1; frame <= 30; ++frame)
    {
        f.world.get<Transform>(entity).position.x = 0.02f * static_cast<f32>(frame);
        f.animation.update(kStep, &f.jobs, true);
    }
    const Vec3 lagging = f.head(entity) - f.world.get<Transform>(entity).position;
    CHECK(lagging.x < -0.005f); // the head trails the motion
    for (int frame = 0; frame < 300; ++frame)
    {
        f.animation.update(kStep, &f.jobs, true);
    }
    CHECK(glm::length(f.head(entity) - f.world.get<Transform>(entity).position - Vec3(0.0f, 1.5f, 0.0f)) <
          0.01f);

    // Outside play the animated pose shows at once.
    f.world.get<Transform>(entity).position.x = 3.0f;
    f.animation.update(kStep, &f.jobs, false);
    CHECK(glm::length(f.head(entity) - Vec3(3.0f, 1.5f, 0.0f)) < 1e-4f);
}

TEST_CASE(
    "SpringBone stages of two worlds with the same updates match, and a steady update allocates nothing")
{
    Fixture a;
    Fixture b;
    std::vector<Entity> first;
    std::vector<Entity> second;
    for (u32 i = 0; i < 12; ++i)
    {
        first.push_back(a.create(Vec3(static_cast<f32>(i), 0.0f, 0.0f)));
        second.push_back(b.create(Vec3(static_cast<f32>(i), 0.0f, 0.0f)));
    }
    a.world.get<SpringBone>(first[3]).colliders[0] =
        SpringCollider{.joint = "hips", .offset = Vec3(0.0f, 0.9f, 0.1f), .radius = 0.3f};
    b.world.get<SpringBone>(second[3]).colliders[0] = a.world.get<SpringBone>(first[3]).colliders[0];
    a.world.get<SpringBone>(first[5]).root = "missing"; // logged once, posed without springs
    b.world.get<SpringBone>(second[5]).root = "missing";
    a.animation.waitIdle();
    b.animation.waitIdle();
    const auto run = [](Fixture& f, std::span<const Entity> entities, int frames) {
        for (int frame = 0; frame < frames; ++frame)
        {
            for (const Entity entity : entities)
            {
                f.world.get<Transform>(entity).position.z = 0.3f * std::sin(0.2f * static_cast<f32>(frame));
            }
            f.animation.update(kStep, &f.jobs, true);
        }
    };
    run(a, first, 90);
    run(b, second, 90);
    for (usize i = 0; i < first.size(); ++i)
    {
        CHECK(a.head(first[i]) == b.head(second[i]));
    }
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        run(a, first, 60);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}
