#pragma once

/// @file
/// Pose math of skeletal animation (docs/adr/0059-skeletal-animation.md): binding a clip to a skeleton,
/// sampling it, composing model-space joints, skin matrices and skinned bounds. Pure functions over spans;
/// none allocates.
///
/// @code
/// const illusion::ClipBinding binding = illusion::bindClip(clip, skeleton);
/// std::vector<illusion::JointPose> local(skeleton.joints.size());
/// std::vector<illusion::Mat4> model(local.size());
/// std::vector<illusion::SkinMatrix> skin(local.size());
/// illusion::sampleClip(clip, binding, skeleton, time, local);
/// illusion::localToModel(skeleton, local, model);
/// illusion::skinMatrices(skeleton, model, skin);
/// @endcode

#include <illusion/assets/Animation.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <span>
#include <vector>

namespace illusion
{

/// A joint's transform relative to its parent.
struct JointPose
{
    Vec3 translation{0.0f};                ///< Translation.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Unit rotation.
    Vec3 scale{1.0f};                      ///< Scale.
};

/// A skinning matrix (model space times inverse bind) as three rows of a 3 x 4 matrix, the layout the
/// skinning shader reads.
struct SkinMatrix
{
    std::array<Vec4, 3> rows{Vec4(1, 0, 0, 0), Vec4(0, 1, 0, 0), Vec4(0, 0, 1, 0)}; ///< Rows.
};
static_assert(sizeof(SkinMatrix) == 48, "SkinMatrix layout must match the skinning shader");

/// The joint each channel of a clip moves on one skeleton.
struct ClipBinding
{
    /// Joint index per AnimationClip::channels entry, or -1 when the skeleton has no joint of that name.
    std::vector<i32> joints;
    u32 boundChannels = 0; ///< Channels that found their joint.
};

/// Finds the joints of @p clip's channels in @p skeleton by name. Allocates; call once per skeleton and clip.
[[nodiscard]] ClipBinding bindClip(const AnimationClip& clip, const Skeleton& skeleton);

/// Wraps @p time into [0, duration] (@p loop) or clamps it; 0 for a clip of zero duration.
[[nodiscard]] f32 clipTime(const AnimationClip& clip, f32 time, bool loop) noexcept;

/// Writes the rest pose of @p skeleton into @p out (one entry per joint).
void restPose(const Skeleton& skeleton, std::span<JointPose> out) noexcept;

/// Writes the pose of @p clip at @p time (already passed through clipTime()) into @p out: the rest pose,
/// overwritten by every bound channel. Rotations are normalized. @p out has one entry per joint.
void sampleClip(const AnimationClip& clip, const ClipBinding& binding, const Skeleton& skeleton, f32 time,
                std::span<JointPose> out) noexcept;

/// Composes @p local into model-space joint matrices @p out (parents before children, starting from
/// Skeleton::rootTransform).
void localToModel(const Skeleton& skeleton, std::span<const JointPose> local, std::span<Mat4> out) noexcept;

/// Writes model-space matrix times inverse bind for every joint into @p out.
void skinMatrices(const Skeleton& skeleton, std::span<const Mat4> model, std::span<SkinMatrix> out) noexcept;

/// Blends @p from towards @p to by @p weight (0: from, 1: to) into @p out, per joint scaled by @p mask (one
/// weight per joint in [0, 1]; empty means 1 everywhere): linear for translation and scale, shortest-path
/// nlerp for rotation. @p out may alias @p from.
void blendPoses(std::span<const JointPose> from, std::span<const JointPose> to, f32 weight,
                std::span<const f32> mask, std::span<JointPose> out) noexcept;

/// Adds the difference of @p additive from @p reference (both local poses of the same skeleton) to @p base,
/// scaled by @p weight and @p mask (as in blendPoses()): translation and scale offsets, rotation
/// base * slerp(identity, inverse(reference) * additive, weight).
void addPose(std::span<JointPose> base, std::span<const JointPose> additive,
             std::span<const JointPose> reference, f32 weight, std::span<const f32> mask) noexcept;

/// The model-space bounds of a skinned mesh: each joint's bind-space bounds (MeshData::jointBounds; an
/// empty box for a joint that moves no vertex) transformed by its skin matrix, merged.
[[nodiscard]] Aabb skinnedBounds(std::span<const Aabb> jointBounds,
                                 std::span<const SkinMatrix> skin) noexcept;

} // namespace illusion
