#pragma once

#include <illusion/animation/Pose.hpp>
#include <illusion/animation/Retarget.hpp>

#include <span>
#include <string_view>

namespace illusion::detail
{

// The local pose of @p joint in @p clip at @p time (passed through clipTime()): its rest pose overwritten by
// the bound channels of that joint. Cheaper than sampleClip() when only a few joints are needed.
[[nodiscard]] JointPose sampleJoint(const AnimationClip& clip, const ClipBinding& binding,
                                    const Skeleton& skeleton, u32 joint, f32 time) noexcept;

// The retargeted local pose of @p joint (the reference pose overwritten by its bound channels through their
// corrections); sampleJoint() for a @p retarget that is not active.
[[nodiscard]] JointPose sampleJoint(const AnimationClip& clip, const ClipBinding& binding,
                                    const ClipRetarget& retarget, const Skeleton& skeleton, u32 joint,
                                    f32 time) noexcept;

// Spherical linear interpolation along the shorter of the two arcs between @p a and @p b.
[[nodiscard]] Quat slerpShortest(Quat a, Quat b, f32 t) noexcept;

// The index of the joint named @p name in @p skeleton, or -1.
[[nodiscard]] i32 findJoint(const Skeleton& skeleton, std::string_view name) noexcept;

// The model-space matrix of @p joint composed from @p local along its ancestors only.
[[nodiscard]] Mat4 jointModelMatrix(const Skeleton& skeleton, std::span<const JointPose> local,
                                    u32 joint) noexcept;

// The model-space rotation of @p joint (scales ignored); identity for -1, the skeleton's root parent.
[[nodiscard]] Quat jointModelRotation(const Skeleton& skeleton, std::span<const JointPose> local,
                                      i32 joint) noexcept;

// The rotation that turns the unit vector @p from onto the unit vector @p to by the shorter arc; opposite
// vectors turn half a circle about @p fallback (made perpendicular to @p from).
[[nodiscard]] Quat shortestArc(Vec3 from, Vec3 to, Vec3 fallback) noexcept;

// The unit vector @p from turned towards the unit vector @p to by at most @p maxAngle radians (about
// @p fallback when they are opposite).
[[nodiscard]] Vec3 turnTowards(Vec3 from, Vec3 to, f32 maxAngle, Vec3 fallback) noexcept;

} // namespace illusion::detail
