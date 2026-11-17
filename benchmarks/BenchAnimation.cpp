#include "Budget.hpp"

#include <illusion/animation/Pose.hpp>

#include <cmath>
#include <format>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kSkeletons = 300;
constexpr u32 kJoints = 60;
constexpr u32 kKeys = 31; // one second at 30 keys per second

// A humanoid-sized tree: a spine of 10 joints with five limbs of 10 joints each hanging off it.
Skeleton makeSkeleton()
{
    Skeleton skeleton;
    skeleton.joints.resize(kJoints);
    for (u32 j = 0; j < kJoints; ++j)
    {
        SkeletonJoint& joint = skeleton.joints.at(j);
        joint.name = std::format("joint{:02}", j);
        joint.parent = j == 0 ? -1 : (j % 10 == 0 ? static_cast<i32>(j / 10) : static_cast<i32>(j) - 1);
        joint.translation = Vec3(0.0f, 0.1f, 0.0f);
        joint.inverseBind = glm::translate(Mat4(1.0f), Vec3(0.0f, -0.1f * static_cast<f32>(j % 10), 0.0f));
    }
    return skeleton;
}

// Linear translation, rotation and scale keys on every joint.
AnimationClip makeClip()
{
    AnimationClip clip;
    clip.duration = 1.0f;
    for (u32 j = 0; j < kJoints; ++j)
    {
        for (const AnimationPath path :
             {AnimationPath::Translation, AnimationPath::Rotation, AnimationPath::Scale})
        {
            AnimationChannel channel;
            channel.joint = std::format("joint{:02}", j);
            channel.path = path;
            for (u32 k = 0; k < kKeys; ++k)
            {
                const f32 time = static_cast<f32>(k) / static_cast<f32>(kKeys - 1);
                const f32 angle = 0.5f * std::sin(6.28f * time + static_cast<f32>(j));
                channel.times.push_back(time);
                channel.values.push_back(path == AnimationPath::Rotation
                                             ? Vec4(0.0f, 0.0f, std::sin(angle), std::cos(angle))
                                             : Vec4(1.0f + 0.1f * angle, 0.1f, 1.0f, 0.0f));
            }
            clip.channels.push_back(std::move(channel));
        }
    }
    return clip;
}

} // namespace

// Budget: ns to evaluate 300 skeletons of 60 joints on one thread (sample, compose, skin matrices), about
// 6.5x a desktop result (1.5 ms); see Budget.hpp.
ILL_BENCHMARK("animation/evaluate-300x60", 10'000'000.0)
{
    const Skeleton skeleton = makeSkeleton();
    const AnimationClip clip = makeClip();
    const ClipBinding binding = bindClip(clip, skeleton);
    std::vector<JointPose> local(kJoints);
    std::vector<Mat4> model(kJoints);
    std::vector<SkinMatrix> skin(usize{kSkeletons} * kJoints);
    f32 time = 0.0f;
    bench.run("evaluate 300 skeletons x 60 joints", [&] {
        time += 1.0f / 60.0f;
        for (u32 s = 0; s < kSkeletons; ++s)
        {
            const f32 at = clipTime(clip, time + 0.01f * static_cast<f32>(s), true);
            sampleClip(clip, binding, skeleton, at, local);
            localToModel(skeleton, local, model);
            skinMatrices(skeleton, model, std::span(skin).subspan(usize{s} * kJoints, kJoints));
        }
        ankerl::nanobench::doNotOptimizeAway(skin.back());
    });
}
