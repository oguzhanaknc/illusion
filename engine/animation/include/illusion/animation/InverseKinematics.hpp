#pragma once

/// @file
/// Look-at and two-bone IK solvers (docs/adr/0061-sockets-ik-and-ragdolls.md): pure functions over the
/// model-space joint transforms of one pose, used by AnimationWorld's IK stages. None allocates.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <span>

namespace illusion
{

struct JointPose;
struct Skeleton;

/// Settings of solveLookAt().
struct LookAtSolve
{
    Vec3 target{0.0f};              ///< Point to look at, in the skeleton's model space.
    Vec3 forward{0.0f, 0.0f, 1.0f}; ///< The joint's forward axis in its own space.
    f32 maxAngle = 1.2217305f;      ///< Largest turn from the animated direction, in radians (70°).
    f32 weight = 1.0f;              ///< In [0, 1].
};

/// Turns the joints @p chain (the last is the head, the others its ancestors, at most four, parents first) so
/// that the head's forward axis points at the target: the turn, clamped to maxAngle, is shared evenly along
/// the chain and scaled by weight. Changes @p local (the pose's local transforms); returns the applied angle.
f32 solveLookAt(const Skeleton& skeleton, std::span<const u32> chain, const LookAtSolve& solve,
                std::span<JointPose> local) noexcept;

/// Settings of solveTwoBoneIk().
struct TwoBoneSolve
{
    Vec3 target{0.0f};                        ///< Where the end joint should be, in model space.
    Vec3 pole{0.0f, 0.0f, 1.0f};              ///< Point the middle joint bends towards, in model space.
    f32 weight = 1.0f;                        ///< In [0, 1].
    bool alignEnd = false;                    ///< Also rotate the end joint to endRotation.
    Quat endRotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Model-space rotation of the end joint when alignEnd.
};

/// Moves the end joint @p end (its parent is the middle joint, its grandparent the root of the chain) to the
/// target analytically, keeping bone lengths: an unreachable target stretches the chain straight towards it.
/// Changes @p local; returns the remaining distance between the end joint and the target.
f32 solveTwoBoneIk(const Skeleton& skeleton, u32 end, const TwoBoneSolve& solve,
                   std::span<JointPose> local) noexcept;

} // namespace illusion
