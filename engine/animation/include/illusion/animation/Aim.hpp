#pragma once

/// @file
/// Aim offsets (docs/adr/0065-retargeting-and-aim-layers.md): additive layers applied in model space, so an
/// authored pitch/yaw blend space aims the same way whatever the body below it does, and the procedural aim
/// stage that turns a spine chain until an aim axis points at a target, with yaw and pitch kept apart so the
/// horizon never rolls. Pure functions over one pose; none allocates.
///
/// @code
/// const illusion::AimAngles angles = illusion::aimAngles(origin, target); // model space, radians
/// illusion::AimSolve solve{.target = target, .maxYaw = 1.5707964f};
/// const std::array<illusion::u32, 3> spine{spineJoint, chestJoint, upperChestJoint};
/// const std::array<illusion::f32, 3> weights{0.3f, 0.3f, 0.4f};
/// const illusion::AimResult result = illusion::solveAim(skeleton, spine, weights, handJoint, solve, local);
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <span>

namespace illusion
{

struct JointPose;
struct Skeleton;

/// Most spine joints that share an aim.
inline constexpr u32 kMaxAimChain = 4;

/// Passes of solveAim(); the second corrects the offset of the aim joint from the spine (a hand beside it).
inline constexpr u32 kAimPasses = 2;

/// Adds the difference of @p additive from @p reference to @p base in model space: per joint of @p mask
/// (empty: every joint), the model-space rotation delta additive * inverse(reference), scaled by @p weight
/// times the mask, is put in front of the base joint's model-space rotation, then turned back into local
/// rotations; translation and scale offsets are added locally as in addPose(). @p scratch holds three
/// rotations per joint. Does not allocate.
void addPoseInModelSpace(const Skeleton& skeleton, std::span<JointPose> base,
                         std::span<const JointPose> additive, std::span<const JointPose> reference,
                         f32 weight, std::span<const f32> mask, std::span<Quat> scratch) noexcept;

/// Yaw and pitch of a direction in a skeleton's model space.
struct AimAngles
{
    f32 yaw = 0.0f;   ///< About +y, from +z towards +x, in radians.
    f32 pitch = 0.0f; ///< Above the horizontal plane, in radians.
};

/// The yaw and pitch from @p origin to @p target (both in model space); zero for points closer than 1 mm.
[[nodiscard]] AimAngles aimAngles(const Vec3& origin, const Vec3& target) noexcept;

/// Settings of solveAim().
struct AimSolve
{
    Vec3 target{0.0f};              ///< Point to aim at, in the skeleton's model space.
    Vec3 forward{0.0f, 0.0f, 1.0f}; ///< The aim joint's aim axis in its own space.
    f32 maxYaw = 1.5707964f;        ///< Largest yaw of the aim from the model's +z, in radians (90°).
    f32 maxPitch = 1.0471976f;      ///< Largest pitch of the aim, up or down, in radians (60°).
    f32 weight = 1.0f;              ///< In [0, 1].
};

/// What solveAim() did.
struct AimResult
{
    AimAngles applied;    ///< Yaw and pitch turned in total (shared along the chain).
    f32 error = 0.0f;     ///< Angle between the aim axis and the target afterwards, in radians.
    bool clamped = false; ///< The target lay beyond maxYaw or maxPitch.
};

/// Turns the joints @p chain (spine joints, parents first, at most kMaxAimChain, all ancestors of @p joint)
/// so that @p joint's aim axis points at the target: the remaining yaw about model +y and pitch about the
/// yawed right axis, clamped so the aim stays within maxYaw and maxPitch, are shared by @p weights (one per
/// chain joint, normalized) and scaled by weight, in kAimPasses passes. Changes @p local; no NaN for
/// degenerate input (a target at the joint, a zero axis).
AimResult solveAim(const Skeleton& skeleton, std::span<const u32> chain, std::span<const f32> weights,
                   u32 joint, const AimSolve& solve, std::span<JointPose> local) noexcept;

} // namespace illusion
