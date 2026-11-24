#include "PoseDetail.hpp"

#include <illusion/animation/Aim.hpp>
#include <illusion/core/Assert.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace illusion
{
namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;
constexpr f32 kEpsilon = 1e-6f;
constexpr f32 kMinAimDistance = 1e-3f; // metres

[[nodiscard]] bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] f32 wrapAngle(f32 angle) noexcept
{
    angle = std::remainder(angle, 2.0f * kPi);
    return std::isfinite(angle) ? angle : 0.0f;
}

// The yaw and pitch of the unit vector @p direction.
[[nodiscard]] AimAngles directionAngles(Vec3 direction) noexcept
{
    const f32 horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    AimAngles angles;
    angles.yaw = horizontal > kEpsilon ? std::atan2(direction.x, direction.z) : 0.0f;
    angles.pitch = std::atan2(direction.y, horizontal);
    return angles;
}

// The horizontal axis that pitches a direction of yaw @p yaw upwards when turned about by a positive angle.
[[nodiscard]] Vec3 pitchAxis(f32 yaw) noexcept
{
    return {-std::cos(yaw), 0.0f, std::sin(yaw)};
}

void rotateInModel(const Skeleton& skeleton, std::span<JointPose> local, u32 joint, Quat turn) noexcept
{
    const Quat parent = detail::jointModelRotation(skeleton, local, skeleton.joints[joint].parent);
    local[joint].rotation = glm::normalize(glm::inverse(parent) * turn * parent * local[joint].rotation);
}

// The model-space position and unit aim direction of @p joint; false for a degenerate axis.
[[nodiscard]] bool aimRay(const Skeleton& skeleton, std::span<const JointPose> local, u32 joint, Vec3 axis,
                          Vec3& origin, Vec3& direction) noexcept
{
    const Mat4 model = detail::jointModelMatrix(skeleton, local, joint);
    origin = Vec3(model[3]);
    direction = Mat3(model) * axis;
    const f32 length = glm::length(direction);
    if (!(length > kEpsilon) || !std::isfinite(length) || !finite(origin))
    {
        return false;
    }
    direction /= length;
    return true;
}

} // namespace

void addPoseInModelSpace(const Skeleton& skeleton, std::span<JointPose> base,
                         std::span<const JointPose> additive, std::span<const JointPose> reference,
                         f32 weight, std::span<const f32> mask, std::span<Quat> scratch) noexcept
{
    const usize joints = skeleton.joints.size();
    ILL_ASSERT(base.size() >= joints && additive.size() >= joints && reference.size() >= joints,
               "addPoseInModelSpace needs one JointPose per joint");
    ILL_ASSERT(scratch.size() >= joints * 3, "addPoseInModelSpace needs three rotations per joint");
    ILL_ASSERT(mask.empty() || mask.size() >= joints, "addPoseInModelSpace needs one mask weight per joint");
    if (!(weight > 0.0f) || scratch.size() < joints * 3 || base.size() < joints || additive.size() < joints ||
        reference.size() < joints)
    {
        return;
    }
    const bool masked = !mask.empty() && mask.size() >= joints;
    const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    // The root transform turns all three poses alike, so it cancels out of the deltas and is left out.
    const std::span<Quat> added = scratch.first(joints);
    const std::span<Quat> referenced = scratch.subspan(joints, joints);
    const std::span<Quat> original = scratch.subspan(joints * 2, joints);
    for (usize j = 0; j < joints; ++j)
    {
        const i32 parent = skeleton.joints[j].parent;
        const auto p = static_cast<usize>(parent);
        added[j] = parent >= 0 ? added[p] * additive[j].rotation : additive[j].rotation;
        referenced[j] = parent >= 0 ? referenced[p] * reference[j].rotation : reference[j].rotation;
    }
    // Parents first: original keeps the base's model rotations, added becomes the result's.
    for (usize j = 0; j < joints; ++j)
    {
        const i32 parent = skeleton.joints[j].parent;
        const auto p = static_cast<usize>(parent);
        JointPose& pose = base[j];
        original[j] = parent >= 0 ? original[p] * pose.rotation : pose.rotation;
        const f32 w = masked ? weight * mask[j] : weight;
        Quat result = original[j];
        if (w != 0.0f)
        {
            pose.translation += (additive[j].translation - reference[j].translation) * w;
            pose.scale += (additive[j].scale - reference[j].scale) * w;
            const Quat delta = glm::normalize(added[j] * glm::inverse(referenced[j]));
            result = glm::normalize((w == 1.0f ? delta : detail::slerpShortest(identity, delta, w)) * result);
        }
        added[j] = result;
        pose.rotation = glm::normalize(parent >= 0 ? glm::inverse(added[p]) * result : result);
    }
}

AimAngles aimAngles(const Vec3& origin, const Vec3& target) noexcept
{
    const Vec3 offset = target - origin;
    const f32 distance = glm::length(offset);
    if (!(distance >= kMinAimDistance) || !std::isfinite(distance))
    {
        return {};
    }
    return directionAngles(offset / distance);
}

AimResult solveAim(const Skeleton& skeleton, std::span<const u32> chain, std::span<const f32> weights,
                   u32 joint, const AimSolve& solve, std::span<JointPose> local) noexcept
{
    const usize joints = skeleton.joints.size();
    ILL_ASSERT(joint < joints && local.size() >= joints, "solveAim needs a joint of the skeleton's pose");
    ILL_ASSERT(chain.size() <= kMaxAimChain, "solveAim takes at most kMaxAimChain chain joints");
    ILL_ASSERT(weights.size() >= chain.size(), "solveAim needs one weight per chain joint");
    AimResult result;
    const usize count = std::min({chain.size(), weights.size(), usize{kMaxAimChain}});
    if (joint >= joints || local.size() < joints || count == 0 || !finite(solve.target) ||
        !finite(solve.forward))
    {
        return result;
    }
    for (usize i = 0; i < count; ++i)
    {
        ILL_ASSERT(chain[i] < joints, "solveAim: a chain joint is not in the skeleton");
        if (chain[i] >= joints)
        {
            return result;
        }
    }
    const f32 axisLength = glm::length(solve.forward);
    const f32 weight = std::isfinite(solve.weight) ? std::clamp(solve.weight, 0.0f, 1.0f) : 0.0f;
    if (!(axisLength > kEpsilon) || !std::isfinite(axisLength) || !(weight > 0.0f))
    {
        return result;
    }
    const Vec3 axis = solve.forward / axisLength;
    std::array<f32, kMaxAimChain> shares{};
    f32 total = 0.0f;
    for (usize i = 0; i < count; ++i)
    {
        const f32 share = std::isfinite(weights[i]) ? std::max(weights[i], 0.0f) : 0.0f;
        shares.at(i) = share;
        total += share;
    }
    for (usize i = 0; i < count; ++i)
    {
        shares.at(i) = total > 0.0f ? shares.at(i) / total : 1.0f / static_cast<f32>(count);
    }
    const f32 maxYaw = std::isfinite(solve.maxYaw) ? std::clamp(solve.maxYaw, 0.0f, kPi) : 0.0f;
    const f32 maxPitch = std::isfinite(solve.maxPitch) ? std::clamp(solve.maxPitch, 0.0f, 0.5f * kPi) : 0.0f;
    std::array<Quat, kMaxAimChain> original{};
    for (usize i = 0; i < count; ++i)
    {
        original.at(i) = local[chain[i]].rotation;
    }
    AimAngles turned;
    for (u32 pass = 0; pass < kAimPasses; ++pass)
    {
        Vec3 origin(0.0f);
        Vec3 direction(0.0f);
        if (!aimRay(skeleton, local, joint, axis, origin, direction) ||
            !(glm::length(solve.target - origin) >= kMinAimDistance))
        {
            break;
        }
        AimAngles wanted = aimAngles(origin, solve.target);
        if (std::abs(wanted.yaw) > maxYaw || std::abs(wanted.pitch) > maxPitch)
        {
            result.clamped = true;
            wanted.yaw = std::clamp(wanted.yaw, -maxYaw, maxYaw);
            wanted.pitch = std::clamp(wanted.pitch, -maxPitch, maxPitch);
        }
        const AimAngles current = directionAngles(direction);
        const f32 yaw = wrapAngle(wanted.yaw - current.yaw);
        const f32 pitch = wanted.pitch - current.pitch;
        turned.yaw += yaw;
        turned.pitch += pitch;
        // Each joint pitches about the axis its share of the yaw leaves, so the turns compose to the whole
        // yaw about +y followed by the whole pitch about the final right axis: the aim axis never rolls.
        f32 cumulative = 0.0f;
        for (usize i = 0; i < count; ++i)
        {
            const f32 share = shares.at(i);
            cumulative += share * yaw;
            const Quat turn = glm::angleAxis(share * pitch, pitchAxis(current.yaw + cumulative)) *
                              glm::angleAxis(share * yaw, Vec3(0.0f, 1.0f, 0.0f));
            rotateInModel(skeleton, local, chain[i], turn);
        }
    }
    if (weight < 1.0f)
    {
        for (usize i = 0; i < count; ++i)
        {
            JointPose& pose = local[chain[i]];
            pose.rotation = glm::normalize(detail::slerpShortest(original.at(i), pose.rotation, weight));
        }
    }
    result.applied = {turned.yaw * weight, turned.pitch * weight};
    Vec3 origin(0.0f);
    Vec3 direction(0.0f);
    if (aimRay(skeleton, local, joint, axis, origin, direction))
    {
        const Vec3 offset = solve.target - origin;
        const f32 distance = glm::length(offset);
        if (distance >= kMinAimDistance && std::isfinite(distance))
        {
            result.error = std::acos(std::clamp(glm::dot(direction, offset / distance), -1.0f, 1.0f));
        }
    }
    return result;
}

} // namespace illusion
