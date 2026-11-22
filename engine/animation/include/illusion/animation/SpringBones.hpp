#pragma once

/// @file
/// Spring bones (docs/adr/0061-sockets-ik-and-ragdolls.md): a pure function that swings a joint chain of one
/// pose with Verlet integration, used by AnimationWorld's spring stage. It does not allocate.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <span>

namespace illusion
{

struct JointPose;
struct Skeleton;

/// The simulated world-space position of one joint of a spring chain, kept between steps.
struct SpringParticle
{
    Vec3 position{0.0f}; ///< Position after the last step.
    Vec3 previous{0.0f}; ///< Position before the last step (Verlet velocity).
    Mat4 matrix{1.0f};   ///< Scratch of stepSpringChain(): the joint's world matrix.
};

/// A sphere a spring chain stays out of, in world space.
struct SpringSphere
{
    Vec3 center{0.0f}; ///< Centre.
    f32 radius = 0.0f; ///< Radius.
};

/// Settings of stepSpringChain().
struct SpringSolve
{
    Mat4 world{1.0f};                 ///< Model space to world space of the pose.
    f32 stiffness = 0.6f;             ///< Pull back towards the animated pose per step, in [0, 1].
    f32 damping = 0.2f;               ///< Velocity lost per step, in [0, 1].
    Vec3 gravity{0.0f, -9.81f, 0.0f}; ///< World-space acceleration in m/s².
    f32 radius = 0.02f;               ///< Radius of each chain joint for collisions.
    f32 stepSeconds = 1.0f / 60.0f;   ///< Length of one step.
};

/// Resets @p particles (one per entry of @p chain) to the joints' world positions in @p local, at rest.
void resetSpringChain(const Skeleton& skeleton, std::span<const u32> chain, const Mat4& world,
                      std::span<const JointPose> local, std::span<SpringParticle> particles) noexcept;

/// Advances @p particles by @p steps steps of SpringSolve::stepSeconds and turns the joints of @p chain in
/// @p local towards them. @p chain is a joint and its descendants, parents first (the first entry stays at
/// its animated position, each other joint is pulled towards its animated place relative to its simulated
/// parent, keeps its bone length and is pushed out of @p spheres); zero steps only applies the particles.
void stepSpringChain(const Skeleton& skeleton, std::span<const u32> chain, const SpringSolve& solve,
                     std::span<const SpringSphere> spheres, u32 steps, std::span<SpringParticle> particles,
                     std::span<JointPose> local) noexcept;

} // namespace illusion
