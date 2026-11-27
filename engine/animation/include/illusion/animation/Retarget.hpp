#pragma once

/// @file
/// Retargeting (docs/adr/0065-retargeting-and-aim-layers.md): a clip authored on one skeleton played on
/// another whose joints have other names, rest poses and proportions. Joints are paired through their rig
/// bones (Skeleton::rig), the correction is computed once per clip and pair of skeletons, and sampling
/// applies it per channel. Pure functions; only the binding allocates.
///
/// @code
/// // Once, when the clip, its source skeleton (AnimationClip::skeleton) and the target are loaded.
/// auto bound = illusion::bindRetargetedClip(clip, source, target);
/// // Every update.
/// illusion::sampleClip(clip, bound->binding, bound->retarget, target, time, local);
/// @endcode

#include <illusion/animation/Pose.hpp>
#include <illusion/assets/Animation.hpp>
#include <illusion/assets/Rig.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

/// The correction of one channel of a retargeted clip, constant for a clip and a pair of skeletons.
struct ChannelRetarget
{
    /// Rotation channels: target local = pre * source local * post (model-space rest correction).
    Quat pre{1.0f, 0.0f, 0.0f, 0.0f};
    Quat post{1.0f, 0.0f, 0.0f, 0.0f}; ///< See pre.
    /// Translation channels: how the target joint moves.
    RetargetTranslation translation = RetargetTranslation::Skeleton;
    /// Translation channels: the unit change between the parents' model scales, for Scaled times the hips
    /// height ratio.
    f32 scale = 1.0f;
    Vec3 sourceRest{0.0f}; ///< The source joint's rest translation or scale (by the channel's path).
    Vec3 targetRest{0.0f}; ///< The target joint's reference translation or scale (by the channel's path).
};

/// The retargeting of one clip onto one target skeleton; empty for a clip bound by name (bindClip()).
struct ClipRetarget
{
    /// Per AnimationClip::channels entry, or empty when the clip is not retargeted.
    std::vector<ChannelRetarget> channels;
    /// The target's reference pose (one per target joint): its rest pose with every mapped bone turned to the
    /// direction of its source bone; the base that sampling overwrites.
    std::vector<JointPose> reference;
    f32 heightRatio = 1.0f; ///< Target hips height over source hips height in model space (1 without hips).

    /// True when the clip is retargeted.
    [[nodiscard]] bool active() const noexcept { return !channels.empty(); }
};

/// A clip bound to a target skeleton through retargeting.
struct RetargetedClip
{
    ClipBinding binding;   ///< The target joint of each channel (-1 for a source joint without a pair).
    ClipRetarget retarget; ///< The corrections.
};

/// Why bindRetargetedClip() failed.
enum class RetargetError : u8
{
    NoRig,      ///< The source or the target has an empty Skeleton::rig.
    NoPairs,    ///< No joint of the clip's channels has a pair in the target.
    NoSkeleton, ///< The clip has no source skeleton (AnimationClip::skeleton is -1).
};

/// Stable name of @p error, e.g. "NoRig".
[[nodiscard]] std::string_view retargetErrorName(RetargetError error) noexcept;

/// The joint of @p name in @p skeleton: a joint of that name, else the joint whose rig bone it is, else -1.
/// Every joint name the animation Components, graphs and ragdoll profiles hold is resolved through it.
[[nodiscard]] i32 findRigJoint(const Skeleton& skeleton, std::string_view name) noexcept;

/// Per joint of @p target, the joint of @p source it is paired with or -1: the joint with the same rig bone,
/// else (for a joint without one) the joint with the same name. Allocates.
[[nodiscard]] std::vector<i32> matchJoints(const Skeleton& source, const Skeleton& target);

/// Target hips height over source hips height, in model space (Skeleton::rootTransform included); 1 when
/// either has no `hips` bone or its hips are not above the model origin.
[[nodiscard]] f32 rigHeightRatio(const Skeleton& source, const Skeleton& target) noexcept;

/// Binds @p clip, authored on @p source, to @p target: pairs joints with matchJoints(), builds the reference
/// pose (bone directions aligned to the source's rest, hierarchically, so a T-pose clip plays as a T-pose on
/// an A-pose rig) and each channel's correction. Allocates; call once per clip and target.
[[nodiscard]] Expected<RetargetedClip, RetargetError>
bindRetargetedClip(const AnimationClip& clip, const Skeleton& source, const Skeleton& target);

/// Writes the pose of @p clip at @p time (already passed through clipTime()) on @p target into @p out: with
/// an active @p retarget, its reference pose overwritten by every bound channel through its correction;
/// otherwise exactly sampleClip(clip, binding, target, time, out). Does not allocate.
void sampleClip(const AnimationClip& clip, const ClipBinding& binding, const ClipRetarget& retarget,
                const Skeleton& target, f32 time, std::span<JointPose> out) noexcept;

/// A clip and the skeleton it was authored on, as an animation graph's clip lookup returns them.
struct ClipSource
{
    const AnimationClip* clip = nullptr; ///< The clip, or null when missing.
    const Skeleton* skeleton = nullptr;  ///< Its source skeleton, or null to bind it by name.
};

} // namespace illusion
