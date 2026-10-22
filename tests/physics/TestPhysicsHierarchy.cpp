#include "physics/PhysicsTestSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>

#include <doctest/doctest.h>

using namespace illusion;
using namespace illusion::test;

TEST_CASE("Inactive bodies leave the simulation and come back where their Transform is")
{
    PhysicsFixture f;
    HierarchyIndex hierarchy;
    createGround(f.world);
    const Entity group = f.world.create(Transform{});
    const Entity box =
        f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), test::box(Vec3(0.5f)));
    REQUIRE(setParent(f.world, box, group));
    f.physics.update(kStep);
    CHECK(f.physics.bodyCount() == 2);
    CHECK(f.world.has<WorldSpaceAuthority>(box)); // dynamic bodies move in world space

    setEnabled(f.world, group, false);
    updateTransformHierarchy(f.world, hierarchy);
    f.physics.update(kStep);
    CHECK(f.physics.bodyCount() == 1);
    CHECK_FALSE(f.world.has<PhysicsBody>(box));
    CHECK_FALSE(f.world.has<WorldSpaceAuthority>(box));
    // Nothing moves an Inactive body, and nothing collides with it.
    const Vec3 parked = f.world.get<Transform>(box).position;
    simulate(f.physics, 0.5);
    CHECK(f.world.get<Transform>(box).position == parked);
    std::vector<Entity> found;
    CHECK(f.physics.overlap(sphere(0.6f), parked, Quat(1.0f, 0.0f, 0.0f, 0.0f), found) == 0);

    // Enabled again, the body starts from its current Transform.
    f.world.get<Transform>(group).position = Vec3(3.0f, 0.0f, 0.0f);
    setEnabled(f.world, group, true);
    updateTransformHierarchy(f.world, hierarchy);
    f.physics.update(kStep);
    CHECK(f.physics.bodyCount() == 2);
    CHECK(f.world.has<PhysicsBody>(box));
    CHECK(f.world.get<Transform>(box).position.x == doctest::Approx(3.0f).epsilon(1e-3));
}

TEST_CASE("children of a dynamic body follow it and its LocalTransform is written back")
{
    PhysicsFixture f;
    HierarchyIndex hierarchy;
    const Entity holder = f.world.create(at(Vec3(10.0f, 0.0f, 0.0f)));
    const Entity falling =
        f.world.create(at(Vec3(10.0f, 20.0f, 0.0f)), body(MotionType::Dynamic), sphere(0.5f));
    REQUIRE(setParent(f.world, falling, holder));
    const Entity lamp = f.world.create(Transform{});
    REQUIRE(setParent(f.world, lamp, falling, ParentMode::KeepLocal));
    f.world.get<LocalTransform>(lamp).position = Vec3(0.0f, 1.0f, 0.0f);

    simulate(f.physics, 0.5);
    updateTransformHierarchy(f.world, hierarchy);
    const Vec3 bodyPosition = f.world.get<Transform>(falling).position;
    CHECK(bodyPosition.y < 19.0f);
    CHECK(f.world.get<LocalTransform>(falling).position.y == doctest::Approx(bodyPosition.y));
    CHECK(f.world.get<Transform>(lamp).position.y == doctest::Approx(bodyPosition.y + 1.0f));
}

TEST_CASE("joints wait while a connected body is Inactive and return with it")
{
    PhysicsFixture f;
    HierarchyIndex hierarchy;
    const Entity anchor =
        f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Static), box(Vec3(0.1f)));
    Joint joint;
    joint.type = JointType::Point;
    joint.connected = anchor;
    f.world.create(at(Vec3(1.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.25f)), joint);
    f.physics.update(kStep);
    CHECK(f.physics.jointCount() == 1);

    setEnabled(f.world, anchor, false);
    updateTransformHierarchy(f.world, hierarchy);
    f.physics.update(kStep);
    CHECK(f.physics.jointCount() == 0);
    f.physics.update(kStep);
    CHECK(f.physics.jointCount() == 0);

    setEnabled(f.world, anchor, true);
    updateTransformHierarchy(f.world, hierarchy);
    f.physics.update(kStep);
    CHECK(f.physics.jointCount() == 1);
}
