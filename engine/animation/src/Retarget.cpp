#include "PoseDetail.hpp"

#include <illusion/animation/Retarget.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace illusion
{
namespace
{

constexpr f32 kEpsilon = 1e-6f;

Mat4 poseMatrix(const JointPose& pose) noexcept
{
    Mat4 matrix = glm::mat4_cast(pose.rotation);
    matrix[0] *= pose.scale.x;
    matrix[1] *= pose.scale.y;
    matrix[2] *= pose.scale.z;
    matrix[3] = Vec4(pose.translation, 1.0f);
    return matrix;
}

// The rotation of @p matrix with its scale taken out (identity for a degenerate matrix).
Quat rotationOf(const Mat4& matrix) noexcept
{
    Vec3 axes[3] = {Vec3(matrix[0]), Vec3(matrix[1]), Vec3(matrix[2])};
    for (Vec3& axis : axes)
    {
        const f32 length = glm::length(axis);
        if (!(length > kEpsilon) || !std::isfinite(length))
        {
            return Quat{1.0f, 0.0f, 0.0f, 0.0f};
        }
        axis /= length;
    }
    return glm::normalize(glm::quat_cast(Mat3(axes[0], axes[1], axes[2])));
}

// The uniform scale of @p matrix (the length of its first axis); 1 for a degenerate matrix.
f32 scaleOf(const Mat4& matrix) noexcept
{
    const f32 length = glm::length(Vec3(matrix[0]));
    return length > kEpsilon && std::isfinite(length) ? length : 1.0f;
}

// The model-space rest matrix of @p joint, composed along its ancestors.
Mat4 restModel(const Skeleton& skeleton, usize joint) noexcept
{
    Mat4 matrix(1.0f);
    for (i32 j = static_cast<i32>(joint); j >= 0; j = skeleton.joints[static_cast<usize>(j)].parent)
    {
        const SkeletonJoint& rest = skeleton.joints[static_cast<usize>(j)];
        matrix = poseMatrix(JointPose{rest.translation, rest.rotation, rest.scale}) * matrix;
    }
    return skeleton.rootTransform * matrix;
}

const std::string* boneOf(const Skeleton& skeleton, usize joint) noexcept
{
    const RigMap& rig = skeleton.rig;
    return rig.bones.size() == skeleton.joints.size() && !rig.bones[joint].empty() ? &rig.bones[joint]
                                                                                   : nullptr;
}

RetargetTranslation translationOf(const Skeleton& skeleton, usize joint) noexcept
{
    const RigMap& rig = skeleton.rig;
    return rig.translation.size() == skeleton.joints.size() ? rig.translation[joint]
                                                            : RetargetTranslation::Skeleton;
}

// Which child gives a joint's bone direction: the earliest humanoid bone, the middle finger before the
// others (it runs along the hand), custom bones and named joints last by joint index.
usize directionRank(const Skeleton& skeleton, usize joint) noexcept
{
    const std::string* bone = boneOf(skeleton, joint);
    if (bone != nullptr)
    {
        if (*bone == "leftMiddleProximal" || *bone == "rightMiddleProximal")
        {
            return 0;
        }
        const auto found = std::ranges::find(kHumanoidBones, std::string_view(*bone));
        if (found != kHumanoidBones.end())
        {
            return 1 + static_cast<usize>(found - kHumanoidBones.begin());
        }
    }
    return 1 + kHumanoidBones.size() + joint;
}

// The paired descendant of @p joint that gives its bone direction: among the nearest paired joints below it
// (through joints without a pair), the one of the lowest directionRank(); -1 when there is none.
i32 directionChild(const Skeleton& target, std::span<const i32> pairs, usize joint) noexcept
{
    const usize count = target.joints.size();
    i32 best = -1;
    usize bestRank = std::numeric_limits<usize>::max();
    // A joint is below @p joint through unpaired joints when its parent is @p joint or such a joint; parents
    // precede children, so one forward pass decides it without storage: walk up from each candidate.
    for (usize c = joint + 1; c < count; ++c)
    {
        if (pairs[c] < 0)
        {
            continue;
        }
        i32 parent = target.joints[c].parent;
        while (parent >= 0 && static_cast<usize>(parent) != joint && pairs[static_cast<usize>(parent)] < 0)
        {
            parent = target.joints[static_cast<usize>(parent)].parent;
        }
        if (parent < 0 || static_cast<usize>(parent) != joint)
        {
            continue;
        }
        const usize rank = directionRank(target, c);
        if (rank < bestRank)
        {
            bestRank = rank;
            best = static_cast<i32>(c);
        }
    }
    return best;
}

Vec3 positionOf(const Mat4& matrix) noexcept
{
    return Vec3(matrix[3]);
}

Mat4 parentMatrix(const Skeleton& skeleton, std::span<const Mat4> model, usize joint) noexcept
{
    const i32 parent = skeleton.joints[joint].parent;
    return parent >= 0 ? model[static_cast<usize>(parent)] : skeleton.rootTransform;
}

} // namespace

std::string_view retargetErrorName(RetargetError error) noexcept
{
    switch (error)
    {
    case RetargetError::NoRig:
        return "NoRig";
    case RetargetError::NoPairs:
        return "NoPairs";
    case RetargetError::NoSkeleton:
        return "NoSkeleton";
    }
    return "Unknown";
}

i32 findRigJoint(const Skeleton& skeleton, std::string_view name) noexcept
{
    const i32 joint = detail::findJoint(skeleton, name);
    if (joint >= 0)
    {
        return joint;
    }
    const i32 bone = skeleton.rig.find(name);
    return bone >= 0 && static_cast<usize>(bone) < skeleton.joints.size() ? bone : -1;
}

std::vector<i32> matchJoints(const Skeleton& source, const Skeleton& target)
{
    std::vector<i32> pairs(target.joints.size(), -1);
    std::vector<u8> taken(source.joints.size(), 0);
    for (usize t = 0; t < target.joints.size(); ++t)
    {
        const std::string* bone = boneOf(target, t);
        i32 s = -1;
        if (bone != nullptr)
        {
            s = source.rig.find(*bone);
            s = s >= 0 && static_cast<usize>(s) < source.joints.size() ? s : -1;
        }
        else
        {
            s = detail::findJoint(source, target.joints[t].name);
        }
        if (s >= 0 && taken.at(static_cast<usize>(s)) == 0)
        {
            taken.at(static_cast<usize>(s)) = 1;
            pairs.at(t) = s;
        }
    }
    return pairs;
}

f32 rigHeightRatio(const Skeleton& source, const Skeleton& target) noexcept
{
    const i32 sourceHips = source.rig.find("hips");
    const i32 targetHips = target.rig.find("hips");
    if (sourceHips < 0 || targetHips < 0 || static_cast<usize>(sourceHips) >= source.joints.size() ||
        static_cast<usize>(targetHips) >= target.joints.size())
    {
        return 1.0f;
    }
    const f32 sourceHeight = restModel(source, static_cast<usize>(sourceHips))[3].y;
    const f32 targetHeight = restModel(target, static_cast<usize>(targetHips))[3].y;
    if (!(sourceHeight > kEpsilon) || !(targetHeight > kEpsilon) || !std::isfinite(sourceHeight) ||
        !std::isfinite(targetHeight))
    {
        return 1.0f;
    }
    return targetHeight / sourceHeight;
}

Expected<RetargetedClip, RetargetError> bindRetargetedClip(const AnimationClip& clip, const Skeleton& source,
                                                           const Skeleton& target)
{
    ILL_PROFILE_SCOPE();
    if (clip.skeleton < 0)
    {
        return unexpected(RetargetError::NoSkeleton);
    }
    if (source.rig.empty() || target.rig.empty())
    {
        return unexpected(RetargetError::NoRig);
    }
    const std::vector<i32> pairs = matchJoints(source, target);
    std::vector<i32> targetOf(source.joints.size(), -1);
    for (usize t = 0; t < pairs.size(); ++t)
    {
        if (pairs[t] >= 0)
        {
            targetOf.at(static_cast<usize>(pairs[t])) = static_cast<i32>(t);
        }
    }

    RetargetedClip result;
    const ClipBinding onSource = bindClip(clip, source);
    result.binding.joints.reserve(clip.channels.size());
    for (const i32 joint : onSource.joints)
    {
        const i32 t = joint >= 0 ? targetOf.at(static_cast<usize>(joint)) : -1;
        result.binding.joints.push_back(t);
        result.binding.boundChannels += t >= 0 ? 1u : 0u;
    }
    if (result.binding.boundChannels == 0)
    {
        return unexpected(RetargetError::NoPairs);
    }

    // The source's rest in model space, and the target's reference pose: each paired joint turned, parents
    // first, so that its bone points where the source's rest bone points.
    const usize sourceCount = source.joints.size();
    const usize targetCount = target.joints.size();
    std::vector<JointPose> sourceRest(sourceCount);
    std::vector<Mat4> sourceModel(sourceCount);
    restPose(source, sourceRest);
    localToModel(source, sourceRest, sourceModel);
    ClipRetarget& retarget = result.retarget;
    retarget.reference.resize(targetCount);
    restPose(target, retarget.reference);
    std::vector<Mat4> targetModel(targetCount);
    localToModel(target, retarget.reference, targetModel);
    for (usize t = 0; t < targetCount; ++t)
    {
        const i32 s = pairs[t];
        const i32 child = s >= 0 ? directionChild(target, pairs, t) : -1;
        if (child < 0)
        {
            continue;
        }
        const auto c = static_cast<usize>(child);
        const Vec3 targetBone = positionOf(targetModel[c]) - positionOf(targetModel[t]);
        const Vec3 sourceBone = positionOf(sourceModel[static_cast<usize>(pairs[c])]) -
                                positionOf(sourceModel[static_cast<usize>(s)]);
        const f32 targetLength = glm::length(targetBone);
        const f32 sourceLength = glm::length(sourceBone);
        if (!(targetLength > kEpsilon) || !(sourceLength > kEpsilon))
        {
            continue;
        }
        const Vec3 from = targetBone / targetLength;
        const Vec3 to = sourceBone / sourceLength;
        if (glm::dot(from, to) > 1.0f - kEpsilon)
        {
            continue;
        }
        const Quat parent = rotationOf(parentMatrix(target, targetModel, t));
        const Quat turn = detail::shortestArc(from, to, Vec3(0.0f, 0.0f, 1.0f));
        JointPose& pose = retarget.reference[t];
        pose.rotation = glm::normalize(glm::inverse(parent) * turn * parent * pose.rotation);
        localToModel(target, retarget.reference, targetModel);
    }

    retarget.heightRatio = rigHeightRatio(source, target);
    retarget.channels.resize(clip.channels.size());
    for (usize c = 0; c < clip.channels.size(); ++c)
    {
        const i32 t = result.binding.joints[c];
        if (t < 0)
        {
            continue;
        }
        const auto s = static_cast<usize>(onSource.joints[c]);
        const auto tj = static_cast<usize>(t);
        const Mat4 sourceParent = parentMatrix(source, sourceModel, s);
        const Mat4 targetParent = parentMatrix(target, targetModel, tj);
        ChannelRetarget& correction = retarget.channels[c];
        correction.pre = glm::normalize(glm::inverse(rotationOf(targetParent)) * rotationOf(sourceParent));
        correction.post =
            glm::normalize(glm::inverse(rotationOf(sourceModel[s])) * rotationOf(targetModel[tj]));
        correction.translation = translationOf(target, tj);
        const f32 units = scaleOf(sourceParent) / scaleOf(targetParent);
        correction.scale =
            correction.translation == RetargetTranslation::Scaled ? retarget.heightRatio * units : units;
        const bool scale = clip.channels[c].path == AnimationPath::Scale;
        correction.sourceRest = scale ? sourceRest[s].scale : sourceRest[s].translation;
        correction.targetRest = scale ? retarget.reference[tj].scale : retarget.reference[tj].translation;
    }
    return result;
}

} // namespace illusion
