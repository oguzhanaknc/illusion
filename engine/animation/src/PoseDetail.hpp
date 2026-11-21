#pragma once

#include <illusion/animation/Pose.hpp>

#include <string_view>

namespace illusion::detail
{

// The local pose of @p joint in @p clip at @p time (passed through clipTime()): its rest pose overwritten by
// the bound channels of that joint. Cheaper than sampleClip() when only a few joints are needed.
[[nodiscard]] JointPose sampleJoint(const AnimationClip& clip, const ClipBinding& binding,
                                    const Skeleton& skeleton, u32 joint, f32 time) noexcept;

// The index of the joint named @p name in @p skeleton, or -1.
[[nodiscard]] i32 findJoint(const Skeleton& skeleton, std::string_view name) noexcept;

} // namespace illusion::detail
