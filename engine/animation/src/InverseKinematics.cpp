#include "PoseDetail.hpp"

#include <illusion/animation/InverseKinematics.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace illusion
{
namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;
constexpr f32 kEpsilon = 1e-6f;
constexpr u32 kMaxLookAtChain = 4;

[[nodiscard]] bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite(Quat value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::isfinite(value.w);
}

[[nodiscard]] Mat4 poseMatrix(const JointPose& pose) noexcept
{
    Mat4 matrix = glm::mat4_cast(pose.rotation);
    matrix[0] *= pose.scale.x;
    matrix[1] *= pose.scale.y;
    matrix[2] *= pose.scale.z;
    matrix[3] = Vec4(pose.translation, 1.0f);
    return matrix;
}

[[nodiscard]] Quat rootRotation(const Skeleton& skeleton) noexcept
{
    Mat3 basis(skeleton.rootTransform);
    for (glm::length_t c = 0; c < 3; ++c)
    {
        const f32 length = glm::length(basis[c]);
        if (!(length > kEpsilon))
        {
            return Quat{1.0f, 0.0f, 0.0f, 0.0f};
        }
        basis[c] /= length;
    }
    return glm::normalize(glm::quat_cast(basis));
}

// A unit vector perpendicular to the unit vector @p v.
[[nodiscard]] Vec3 perpendicular(Vec3 v) noexcept
{
    const Vec3 axis = std::abs(v.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(v, axis));
}

// @p fallback without its part along the unit vector @p v, normalized; any perpendicular when nothing is
// left.
[[nodiscard]] Vec3 perpendicularTo(Vec3 v, Vec3 fallback) noexcept
{
    const Vec3 rest = fallback - v * glm::dot(fallback, v);
    const f32 length = glm::length(rest);
    return length > kEpsilon && std::isfinite(length) ? rest / length : perpendicular(v);
}

[[nodiscard]] Quat nlerpShortest(Quat a, Quat b, f32 t) noexcept
{
    if (glm::dot(a, b) < 0.0f)
    {
        b = -b;
    }
    return glm::normalize(a * (1.0f - t) + b * t);
}

// Turns @p joint by the model-space rotation @p turn (its local rotation is changed).
void rotateInModel(const Skeleton& skeleton, std::span<JointPose> local, u32 joint, Quat turn) noexcept
{
    const Quat parent = detail::jointModelRotation(skeleton, local, skeleton.joints[joint].parent);
    local[joint].rotation = glm::normalize(glm::inverse(parent) * turn * parent * local[joint].rotation);
}

} // namespace

namespace detail
{

Mat4 jointModelMatrix(const Skeleton& skeleton, std::span<const JointPose> local, u32 joint) noexcept
{
    Mat4 matrix = poseMatrix(local[joint]);
    for (i32 parent = skeleton.joints[joint].parent; parent >= 0;
         parent = skeleton.joints[static_cast<usize>(parent)].parent)
    {
        matrix = poseMatrix(local[static_cast<usize>(parent)]) * matrix;
    }
    return skeleton.rootTransform * matrix;
}

Quat jointModelRotation(const Skeleton& skeleton, std::span<const JointPose> local, i32 joint) noexcept
{
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    for (i32 j = joint; j >= 0; j = skeleton.joints[static_cast<usize>(j)].parent)
    {
        rotation = local[static_cast<usize>(j)].rotation * rotation;
    }
    return glm::normalize(rootRotation(skeleton) * rotation);
}

Quat shortestArc(Vec3 from, Vec3 to, Vec3 fallback) noexcept
{
    const f32 cosine = glm::dot(from, to);
    if (cosine > 1.0f - kEpsilon)
    {
        return Quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    if (cosine < -1.0f + kEpsilon)
    {
        return glm::angleAxis(kPi, perpendicularTo(from, fallback));
    }
    const Vec3 axis = glm::cross(from, to);
    return glm::normalize(Quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

Vec3 turnTowards(Vec3 from, Vec3 to, f32 maxAngle, Vec3 fallback) noexcept
{
    const f32 angle = std::acos(std::clamp(glm::dot(from, to), -1.0f, 1.0f));
    if (!(angle > maxAngle))
    {
        return to;
    }
    const Vec3 cross = glm::cross(from, to);
    const f32 length = glm::length(cross);
    const Vec3 axis = length > kEpsilon ? cross / length : perpendicularTo(from, fallback);
    return glm::normalize(glm::angleAxis(maxAngle, axis) * from);
}

} // namespace detail

f32 solveLookAt(const Skeleton& skeleton, std::span<const u32> chain, const LookAtSolve& solve,
                std::span<JointPose> local) noexcept
{
    const usize count = skeleton.joints.size();
    if (chain.empty() || chain.size() > kMaxLookAtChain || local.size() < count)
    {
        return 0.0f;
    }
    if (std::ranges::any_of(chain, [count](u32 joint) { return joint >= count; }))
    {
        return 0.0f;
    }
    const f32 weight = std::isfinite(solve.weight) ? std::clamp(solve.weight, 0.0f, 1.0f) : 0.0f;
    if (!(weight > 0.0f) || !finite(solve.target) || !finite(solve.forward))
    {
        return 0.0f;
    }
    const Mat4 head = detail::jointModelMatrix(skeleton, local, chain.back());
    const Vec3 position(head[3]);
    const Vec3 axisForward = Mat3(head) * solve.forward;
    const Vec3 toTarget = solve.target - position;
    const f32 forwardLength = glm::length(axisForward);
    const f32 distance = glm::length(toTarget);
    if (!(forwardLength > kEpsilon) || !(distance > kEpsilon) || !std::isfinite(forwardLength) ||
        !std::isfinite(distance))
    {
        return 0.0f;
    }
    const Vec3 forward = axisForward / forwardLength;
    const Vec3 direction = toTarget / distance;
    const f32 angle = std::acos(std::clamp(glm::dot(forward, direction), -1.0f, 1.0f));
    const f32 maxAngle = std::isfinite(solve.maxAngle) ? std::clamp(solve.maxAngle, 0.0f, kPi) : 0.0f;
    // Past maxAngle the turn holds, then fades to none for a target straight behind, so that a target
    // crossing behind the head does not flip it from one side to the other.
    const f32 fadeStart = (kPi + maxAngle) * 0.5f;
    f32 turn = std::min(angle, maxAngle);
    if (angle > fadeStart && kPi - fadeStart > kEpsilon)
    {
        turn = maxAngle * (kPi - angle) / (kPi - fadeStart);
    }
    turn *= weight;
    if (!(turn > kEpsilon))
    {
        return 0.0f;
    }
    const Vec3 cross = glm::cross(forward, direction);
    const f32 crossLength = glm::length(cross);
    const Vec3 axis = crossLength > kEpsilon ? cross / crossLength
                                             : perpendicularTo(forward, Mat3(head) * Vec3(0.0f, 1.0f, 0.0f));
    const Quat share = glm::angleAxis(turn / static_cast<f32>(chain.size()), axis);
    for (const u32 joint : chain)
    {
        rotateInModel(skeleton, local, joint, share);
    }
    return turn;
}

f32 solveTwoBoneIk(const Skeleton& skeleton, u32 end, const TwoBoneSolve& solve,
                   std::span<JointPose> local) noexcept
{
    constexpr f32 kUnsolved = std::numeric_limits<f32>::infinity();
    const usize count = skeleton.joints.size();
    if (end >= count || local.size() < count)
    {
        return kUnsolved;
    }
    const i32 middleIndex = skeleton.joints[end].parent;
    const i32 rootIndex = middleIndex >= 0 ? skeleton.joints[static_cast<usize>(middleIndex)].parent : -1;
    if (rootIndex < 0 || !finite(solve.target))
    {
        return kUnsolved;
    }
    const auto middle = static_cast<u32>(middleIndex);
    const auto root = static_cast<u32>(rootIndex);
    const Mat4 rootMatrix = detail::jointModelMatrix(skeleton, local, root);
    const Mat4 middleMatrix = rootMatrix * poseMatrix(local[middle]);
    const Vec3 a(rootMatrix[3]);
    const Vec3 b(middleMatrix[3]);
    const Vec3 c(middleMatrix * poseMatrix(local[end])[3]);
    const Vec3 target = solve.target;
    const f32 weight = std::isfinite(solve.weight) ? std::clamp(solve.weight, 0.0f, 1.0f) : 0.0f;
    const f32 upper = glm::length(b - a);
    const f32 lower = glm::length(c - b);
    if (!(weight > 0.0f) || !(upper > kEpsilon) || !(lower > kEpsilon) || !std::isfinite(upper + lower))
    {
        return glm::length(c - target);
    }

    // The end joint goes along the root-to-target direction, as far as the bones reach.
    const Vec3 toTarget = target - a;
    const f32 targetDistance = glm::length(toTarget);
    Vec3 direction = glm::normalize(b - a);
    if (targetDistance > kEpsilon)
    {
        direction = toTarget / targetDistance;
    }
    else if (glm::length(c - a) > kEpsilon)
    {
        direction = glm::normalize(c - a);
    }
    const f32 distance =
        std::clamp(targetDistance, std::max(std::abs(upper - lower), kEpsilon), upper + lower);
    // The middle joint bends towards the pole, else the way it bends now, else any way.
    Vec3 poleBend = perpendicularTo(direction, b - a);
    if (finite(solve.pole))
    {
        const Vec3 toPole = solve.pole - a;
        const Vec3 rest = toPole - direction * glm::dot(toPole, direction);
        const f32 length = glm::length(rest);
        if (length > kEpsilon)
        {
            poleBend = rest / length;
        }
    }
    const f32 cosine = std::clamp(
        (upper * upper + distance * distance - lower * lower) / (2.0f * upper * distance), -1.0f, 1.0f);
    const f32 sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    const Vec3 middleTarget = a + upper * (cosine * direction + sine * poleBend);
    const Vec3 endTarget = a + distance * direction;

    const JointPose rootPose = local[root];
    const JointPose middlePose = local[middle];
    const JointPose endPose = local[end];
    const Quat rootTurn = detail::shortestArc((b - a) / upper, (middleTarget - a) / upper, poleBend);
    rotateInModel(skeleton, local, root, rootTurn);
    const Vec3 turnedEnd = a + rootTurn * (c - a);
    const Vec3 lowerNow = turnedEnd - middleTarget;
    const Vec3 lowerWanted = endTarget - middleTarget;
    if (glm::length(lowerNow) > kEpsilon && glm::length(lowerWanted) > kEpsilon)
    {
        rotateInModel(skeleton, local, middle,
                      detail::shortestArc(glm::normalize(lowerNow), glm::normalize(lowerWanted), poleBend));
    }
    if (solve.alignEnd && finite(solve.endRotation) && glm::length(solve.endRotation) > kEpsilon)
    {
        const Quat parent = detail::jointModelRotation(skeleton, local, middleIndex);
        local[end].rotation = glm::normalize(glm::inverse(parent) * glm::normalize(solve.endRotation));
    }
    if (weight < 1.0f)
    {
        local[root].rotation = nlerpShortest(rootPose.rotation, local[root].rotation, weight);
        local[middle].rotation = nlerpShortest(middlePose.rotation, local[middle].rotation, weight);
        local[end].rotation = nlerpShortest(endPose.rotation, local[end].rotation, weight);
    }
    return glm::length(Vec3(detail::jointModelMatrix(skeleton, local, end)[3]) - target);
}

} // namespace illusion
