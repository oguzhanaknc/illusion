#include "PoseDetail.hpp"

#include <illusion/animation/Pose.hpp>
#include <illusion/animation/Retarget.hpp>
#include <illusion/core/Assert.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace illusion
{
namespace
{

Quat toQuat(Vec4 value) noexcept
{
    return Quat(value.w, value.x, value.y, value.z);
}

Vec4 fromQuat(Quat rotation) noexcept
{
    return Vec4(rotation.x, rotation.y, rotation.z, rotation.w);
}

// Spherical linear interpolation along the shorter of the two arcs between @p a and @p b.
Quat slerpShortest(Quat a, Quat b, f32 t) noexcept
{
    f32 cosine = glm::dot(a, b);
    if (cosine < 0.0f)
    {
        b = -b;
        cosine = -cosine;
    }
    // Nearly parallel: the sine below would lose precision, and a normalized lerp is as accurate.
    if (cosine > 0.9995f)
    {
        return glm::normalize(a * (1.0f - t) + b * t);
    }
    const f32 angle = std::acos(cosine);
    const f32 sine = std::sin(angle);
    return a * (std::sin((1.0f - t) * angle) / sine) + b * (std::sin(t * angle) / sine);
}

bool isConsistent(const AnimationChannel& channel) noexcept
{
    const usize perKey = channel.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
    return !channel.times.empty() && channel.values.size() == channel.times.size() * perKey;
}

// The value of @p channel at @p time; rotations as x, y, z, w (not normalized).
Vec4 sampleChannel(const AnimationChannel& channel, f32 time) noexcept
{
    const std::span<const f32> times = channel.times;
    const bool cubic = channel.interpolation == AnimationInterpolation::CubicSpline;
    const auto value = [&](usize key) {
        return channel.values[cubic ? key * 3 + 1 : key];
    };
    const usize keys = times.size();
    if (keys == 1 || !(time > times.front()))
    {
        return value(0);
    }
    if (time >= times.back())
    {
        return value(keys - 1);
    }
    const auto next = static_cast<usize>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
    const usize key = next - 1;
    const f32 delta = times[next] - times[key];
    const f32 t = (time - times[key]) / delta;
    switch (channel.interpolation)
    {
    case AnimationInterpolation::Step:
        return value(key);
    case AnimationInterpolation::Linear:
        if (channel.path == AnimationPath::Rotation)
        {
            return fromQuat(slerpShortest(toQuat(value(key)), toQuat(value(next)), t));
        }
        return glm::mix(value(key), value(next), t);
    case AnimationInterpolation::CubicSpline:
    {
        // glTF 2.0, Appendix C: Hermite spline with tangents scaled by the keyframe interval.
        const f32 t2 = t * t;
        const f32 t3 = t2 * t;
        const Vec4 outTangent = channel.values[key * 3 + 2] * delta;
        const Vec4 inTangent = channel.values[next * 3] * delta;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * value(key) + (t3 - 2.0f * t2 + t) * outTangent +
               (-2.0f * t3 + 3.0f * t2) * value(next) + (t3 - t2) * inTangent;
    }
    }
    return value(key);
}

void applyValue(JointPose& pose, AnimationPath path, Vec4 value) noexcept
{
    switch (path)
    {
    case AnimationPath::Translation:
        pose.translation = Vec3(value);
        break;
    case AnimationPath::Rotation:
    {
        const f32 length = glm::length(value);
        pose.rotation = length > 0.0f ? toQuat(value / length) : Quat(1.0f, 0.0f, 0.0f, 0.0f);
        break;
    }
    case AnimationPath::Scale:
        pose.scale = Vec3(value);
        break;
    }
}

// The source / rest ratio of a scale channel, 1 on an axis whose rest scale is zero.
Vec3 scaleRatio(Vec3 value, Vec3 rest) noexcept
{
    Vec3 ratio(1.0f);
    for (glm::length_t axis = 0; axis < 3; ++axis)
    {
        ratio[axis] = std::abs(rest[axis]) > 1e-6f ? value[axis] / rest[axis] : 1.0f;
    }
    return ratio;
}

// Writes @p value of a channel of @p path through its retargeting @p correction into @p pose (ADR 0065).
void applyRetargeted(JointPose& pose, AnimationPath path, const ChannelRetarget& correction,
                     Vec4 value) noexcept
{
    switch (path)
    {
    case AnimationPath::Rotation:
    {
        // pre and post are unit rotations, so one normalization after the product suffices.
        const Quat turned = correction.pre * toQuat(value) * correction.post;
        const f32 length = glm::length(turned);
        pose.rotation = length > 0.0f ? turned / length : correction.pre * correction.post;
        break;
    }
    case AnimationPath::Translation:
        switch (correction.translation)
        {
        case RetargetTranslation::Skeleton:
            break;
        case RetargetTranslation::Scaled:
            pose.translation = correction.targetRest +
                               (correction.pre * (Vec3(value) - correction.sourceRest)) * correction.scale;
            break;
        case RetargetTranslation::Animation:
            pose.translation = (correction.pre * Vec3(value)) * correction.scale;
            break;
        }
        break;
    case AnimationPath::Scale:
        pose.scale = correction.targetRest * scaleRatio(Vec3(value), correction.sourceRest);
        break;
    }
}

} // namespace

ClipBinding bindClip(const AnimationClip& clip, const Skeleton& skeleton)
{
    // Joint names sorted, ties by index, so the first joint of a repeated name wins.
    std::vector<std::pair<std::string_view, i32>> names;
    names.reserve(skeleton.joints.size());
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        names.emplace_back(skeleton.joints[j].name, static_cast<i32>(j));
    }
    std::sort(names.begin(), names.end());
    ClipBinding binding;
    binding.joints.reserve(clip.channels.size());
    for (const AnimationChannel& channel : clip.channels)
    {
        const auto found =
            std::lower_bound(names.begin(), names.end(), std::string_view(channel.joint),
                             [](const auto& entry, std::string_view name) { return entry.first < name; });
        // A channel with inconsistent keyframes stays unbound, like one whose joint is missing.
        const bool bound = found != names.end() && found->first == channel.joint && isConsistent(channel);
        binding.joints.push_back(bound ? found->second : -1);
        binding.boundChannels += bound ? 1u : 0u;
    }
    return binding;
}

f32 clipTime(const AnimationClip& clip, f32 time, bool loop) noexcept
{
    const f32 duration = clip.duration;
    if (!(duration > 0.0f) || !std::isfinite(duration) || !std::isfinite(time))
    {
        return 0.0f;
    }
    if (!loop)
    {
        return std::clamp(time, 0.0f, duration);
    }
    const f32 wrapped = std::fmod(time, duration);
    return wrapped < 0.0f ? std::min(wrapped + duration, duration) : wrapped;
}

void restPose(const Skeleton& skeleton, std::span<JointPose> out) noexcept
{
    ILL_ASSERT(out.size() >= skeleton.joints.size(), "restPose needs one JointPose per joint");
    const usize count = std::min(out.size(), skeleton.joints.size());
    for (usize j = 0; j < count; ++j)
    {
        const SkeletonJoint& joint = skeleton.joints[j];
        out[j] = JointPose{joint.translation, joint.rotation, joint.scale};
    }
}

void sampleClip(const AnimationClip& clip, const ClipBinding& binding, const Skeleton& skeleton, f32 time,
                std::span<JointPose> out) noexcept
{
    restPose(skeleton, out);
    const usize joints = std::min(out.size(), skeleton.joints.size());
    const usize channels = std::min(clip.channels.size(), binding.joints.size());
    for (usize c = 0; c < channels; ++c)
    {
        const i32 joint = binding.joints[c];
        if (joint < 0 || static_cast<usize>(joint) >= joints)
        {
            continue;
        }
        const AnimationChannel& channel = clip.channels[c];
        const Vec4 value = sampleChannel(channel, time);
        JointPose& pose = out[static_cast<usize>(joint)];
        applyValue(pose, channel.path, value);
    }
}

void sampleClip(const AnimationClip& clip, const ClipBinding& binding, const ClipRetarget& retarget,
                const Skeleton& target, f32 time, std::span<JointPose> out) noexcept
{
    if (!retarget.active())
    {
        sampleClip(clip, binding, target, time, out);
        return;
    }
    ILL_ASSERT(out.size() >= target.joints.size() && retarget.reference.size() == target.joints.size(),
               "sampleClip needs one JointPose per joint and a retarget made for this skeleton");
    const usize joints = std::min({out.size(), target.joints.size(), retarget.reference.size()});
    std::copy_n(retarget.reference.begin(), joints, out.begin());
    const usize channels = std::min({clip.channels.size(), binding.joints.size(), retarget.channels.size()});
    for (usize c = 0; c < channels; ++c)
    {
        const i32 joint = binding.joints[c];
        if (joint < 0 || static_cast<usize>(joint) >= joints)
        {
            continue;
        }
        const AnimationChannel& channel = clip.channels[c];
        const ChannelRetarget& correction = retarget.channels[c];
        if (channel.path == AnimationPath::Translation &&
            correction.translation == RetargetTranslation::Skeleton)
        {
            continue;
        }
        applyRetargeted(out[static_cast<usize>(joint)], channel.path, correction,
                        sampleChannel(channel, time));
    }
}

void localToModel(const Skeleton& skeleton, std::span<const JointPose> local, std::span<Mat4> out) noexcept
{
    const usize count = skeleton.joints.size();
    ILL_ASSERT(local.size() >= count && out.size() >= count,
               "localToModel needs one pose and matrix per joint");
    for (usize j = 0; j < count && j < local.size() && j < out.size(); ++j)
    {
        const JointPose& pose = local[j];
        Mat4 matrix = glm::mat4_cast(pose.rotation);
        matrix[0] *= pose.scale.x;
        matrix[1] *= pose.scale.y;
        matrix[2] *= pose.scale.z;
        matrix[3] = Vec4(pose.translation, 1.0f);
        const i32 parent = skeleton.joints[j].parent;
        ILL_ASSERT(parent < static_cast<i32>(j), "joint parents must precede their children");
        out[j] = (parent >= 0 ? out[static_cast<usize>(parent)] : skeleton.rootTransform) * matrix;
    }
}

void skinMatrices(const Skeleton& skeleton, std::span<const Mat4> model, std::span<SkinMatrix> out) noexcept
{
    const usize count = std::min({skeleton.joints.size(), model.size(), out.size()});
    ILL_ASSERT(count == skeleton.joints.size(), "skinMatrices needs one matrix and output per joint");
    for (usize j = 0; j < count; ++j)
    {
        const Mat4 skin = model[j] * skeleton.joints[j].inverseBind;
        for (glm::length_t row = 0; row < 3; ++row)
        {
            out[j].rows[static_cast<usize>(row)] =
                Vec4(skin[0][row], skin[1][row], skin[2][row], skin[3][row]);
        }
    }
}

void blendPoses(std::span<const JointPose> from, std::span<const JointPose> to, f32 weight,
                std::span<const f32> mask, std::span<JointPose> out) noexcept
{
    const usize count = std::min({from.size(), to.size(), out.size()});
    ILL_ASSERT(mask.empty() || mask.size() >= count, "blendPoses needs one mask weight per joint");
    const bool masked = !mask.empty() && mask.size() >= count;
    for (usize j = 0; j < count; ++j)
    {
        const f32 w = masked ? weight * mask[j] : weight;
        if (!(w > 0.0f) || !(w < 1.0f))
        {
            out[j] = w > 0.0f ? to[j] : from[j];
            continue;
        }
        const JointPose& a = from[j];
        const JointPose& b = to[j];
        Quat target = b.rotation;
        if (glm::dot(a.rotation, target) < 0.0f)
        {
            target = -target;
        }
        const JointPose blended{glm::mix(a.translation, b.translation, w),
                                glm::normalize(a.rotation * (1.0f - w) + target * w),
                                glm::mix(a.scale, b.scale, w)};
        out[j] = blended;
    }
}

void addPose(std::span<JointPose> base, std::span<const JointPose> additive,
             std::span<const JointPose> reference, f32 weight, std::span<const f32> mask) noexcept
{
    const usize count = std::min({base.size(), additive.size(), reference.size()});
    ILL_ASSERT(mask.empty() || mask.size() >= count, "addPose needs one mask weight per joint");
    const bool masked = !mask.empty() && mask.size() >= count;
    const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    for (usize j = 0; j < count; ++j)
    {
        const f32 w = masked ? weight * mask[j] : weight;
        if (w == 0.0f)
        {
            continue;
        }
        JointPose& pose = base[j];
        const JointPose& add = additive[j];
        const JointPose& ref = reference[j];
        pose.translation += (add.translation - ref.translation) * w;
        pose.scale += (add.scale - ref.scale) * w;
        const Quat delta = glm::inverse(ref.rotation) * add.rotation;
        pose.rotation = glm::normalize(pose.rotation * slerpShortest(identity, delta, w));
    }
}

namespace detail
{

JointPose sampleJoint(const AnimationClip& clip, const ClipBinding& binding, const Skeleton& skeleton,
                      u32 joint, f32 time) noexcept
{
    const SkeletonJoint& rest = skeleton.joints[joint];
    JointPose pose{rest.translation, rest.rotation, rest.scale};
    const usize channels = std::min(clip.channels.size(), binding.joints.size());
    for (usize c = 0; c < channels; ++c)
    {
        if (binding.joints[c] != static_cast<i32>(joint))
        {
            continue;
        }
        const AnimationChannel& channel = clip.channels[c];
        const Vec4 value = sampleChannel(channel, time);
        applyValue(pose, channel.path, value);
    }
    return pose;
}

JointPose sampleJoint(const AnimationClip& clip, const ClipBinding& binding, const ClipRetarget& retarget,
                      const Skeleton& skeleton, u32 joint, f32 time) noexcept
{
    if (!retarget.active() || joint >= retarget.reference.size())
    {
        return sampleJoint(clip, binding, skeleton, joint, time);
    }
    JointPose pose = retarget.reference[joint];
    const usize channels = std::min({clip.channels.size(), binding.joints.size(), retarget.channels.size()});
    for (usize c = 0; c < channels; ++c)
    {
        if (binding.joints[c] != static_cast<i32>(joint))
        {
            continue;
        }
        const AnimationChannel& channel = clip.channels[c];
        applyRetargeted(pose, channel.path, retarget.channels[c], sampleChannel(channel, time));
    }
    return pose;
}

i32 findJoint(const Skeleton& skeleton, std::string_view name) noexcept
{
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        if (skeleton.joints[j].name == name)
        {
            return static_cast<i32>(j);
        }
    }
    return -1;
}

} // namespace detail

Aabb skinnedBounds(std::span<const Aabb> jointBounds, std::span<const SkinMatrix> skin) noexcept
{
    Aabb bounds;
    const usize count = std::min(jointBounds.size(), skin.size());
    for (usize j = 0; j < count; ++j)
    {
        const Aabb& box = jointBounds[j];
        if (!box.isValid())
        {
            continue;
        }
        const SkinMatrix& matrix = skin[j];
        for (u32 corner = 0; corner < 8; ++corner)
        {
            const Vec4 point((corner & 1u) != 0 ? box.max.x : box.min.x,
                             (corner & 2u) != 0 ? box.max.y : box.min.y,
                             (corner & 4u) != 0 ? box.max.z : box.min.z, 1.0f);
            bounds.expand(Vec3(glm::dot(matrix.rows[0], point), glm::dot(matrix.rows[1], point),
                               glm::dot(matrix.rows[2], point)));
        }
    }
    return bounds;
}

} // namespace illusion
