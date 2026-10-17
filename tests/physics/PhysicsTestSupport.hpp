#pragma once

// Shared helpers for the physics tests.

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <cmath>

namespace illusion::test
{

inline constexpr f64 kStep = 1.0 / 60.0;

// Advances by whole steps.
inline void simulate(PhysicsWorld& physics, f64 seconds)
{
    const auto steps = static_cast<u32>(std::lround(seconds / kStep));
    for (u32 i = 0; i < steps; ++i)
    {
        physics.update(kStep);
    }
}

inline Transform at(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

inline RigidBody body(MotionType type)
{
    RigidBody rigidBody;
    rigidBody.motionType = type;
    return rigidBody;
}

inline Collider box(const Vec3& halfExtents)
{
    Collider collider;
    collider.shape = ColliderShape::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

inline Collider sphere(f32 radius)
{
    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.radius = radius;
    return collider;
}

// A 100 x 1 x 100 static floor whose top is at y = 0.
inline Entity createGround(World& world)
{
    return world.create(at(Vec3(0.0f, -0.5f, 0.0f)), body(MotionType::Static), box(Vec3(50.0f, 0.5f, 50.0f)));
}

struct PhysicsFixture
{
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorld physics{world, jobs};
};

} // namespace illusion::test
