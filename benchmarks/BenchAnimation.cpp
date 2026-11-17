#include "Budget.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/Pose.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <string_view>
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

// Appends the bytes of @p values to @p buffer, 4-byte aligned, and returns their offset.
template <class T>
usize append(std::vector<std::byte>& buffer, const std::vector<T>& values)
{
    buffer.resize((buffer.size() + 3) & ~usize{3});
    const usize offset = buffer.size();
    buffer.resize(offset + values.size() * sizeof(T));
    std::memcpy(buffer.data() + offset, values.data(), values.size() * sizeof(T));
    return offset;
}

// The glTF text of a model of makeSkeleton() with a triangle on every joint and a rotation clip "wave" on
// every joint; its buffer is written into @p buffer (the file "rig.bin").
std::string rigGltf(std::vector<std::byte>& buffer)
{
    const Skeleton skeleton = makeSkeleton();
    std::vector<Vec3> positions;
    std::vector<u8> joints;
    std::vector<Vec4> weights;
    std::vector<u16> indices;
    for (u32 j = 0; j < kJoints; ++j)
    {
        const f32 y = 0.1f * static_cast<f32>(j % 10);
        const f32 z = 0.1f * static_cast<f32>(j / 10);
        for (const Vec3 corner : {Vec3(-0.05f, y, z), Vec3(0.05f, y, z), Vec3(0.0f, y + 0.1f, z)})
        {
            indices.push_back(static_cast<u16>(positions.size()));
            positions.push_back(corner);
            joints.insert(joints.end(), {static_cast<u8>(j), u8{0}, u8{0}, u8{0}});
            weights.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    std::vector<f32> times;
    for (u32 k = 0; k < kKeys; ++k)
    {
        times.push_back(static_cast<f32>(k) / static_cast<f32>(kKeys - 1));
    }
    std::string views;
    u32 viewCount = 0;
    const auto view = [&](usize offset, usize bytes) {
        views += std::format(R"({}{{"buffer": 0, "byteOffset": {}, "byteLength": {}}})",
                             viewCount == 0 ? "" : ", ", offset, bytes);
        return viewCount++;
    };
    const usize vertices = positions.size();
    const u32 positionView = view(append(buffer, positions), vertices * sizeof(Vec3));
    const u32 jointView = view(append(buffer, joints), joints.size());
    const u32 weightView = view(append(buffer, weights), vertices * sizeof(Vec4));
    const u32 indexView = view(append(buffer, indices), indices.size() * sizeof(u16));
    const u32 timeView = view(append(buffer, times), times.size() * sizeof(f32));
    std::string accessors = std::format(
        R"({{"bufferView": {}, "componentType": 5126, "count": {}, "type": "VEC3", "min": [-0.05, 0, 0], "max": [0.05, 1, 0.5]}}, )"
        R"({{"bufferView": {}, "componentType": 5121, "count": {}, "type": "VEC4"}}, )"
        R"({{"bufferView": {}, "componentType": 5126, "count": {}, "type": "VEC4"}}, )"
        R"({{"bufferView": {}, "componentType": 5123, "count": {}, "type": "SCALAR"}}, )"
        R"({{"bufferView": {}, "componentType": 5126, "count": {}, "type": "SCALAR", "min": [0], "max": [1]}})",
        positionView, vertices, jointView, vertices, weightView, vertices, indexView, indices.size(),
        timeView, kKeys);
    std::string nodes;
    std::string skinJoints;
    std::string samplers;
    std::string channels;
    for (u32 j = 0; j < kJoints; ++j)
    {
        std::string children;
        for (u32 c = 0; c < kJoints; ++c)
        {
            if (skeleton.joints[c].parent == static_cast<i32>(j))
            {
                children += std::format("{}{}", children.empty() ? "" : ", ", c);
            }
        }
        nodes += std::format(R"({{"name": "{}", "translation": [0, 0.1, 0], "children": [{}]}}, )",
                             skeleton.joints[j].name, children);
        skinJoints += std::format("{}{}", j == 0 ? "" : ", ", j);
        std::vector<Vec4> rotations;
        for (u32 k = 0; k < kKeys; ++k)
        {
            const f32 angle = 0.5f * std::sin(6.28f * times[k] + static_cast<f32>(j));
            rotations.emplace_back(0.0f, 0.0f, std::sin(angle), std::cos(angle));
        }
        const u32 rotationView = view(append(buffer, rotations), rotations.size() * sizeof(Vec4));
        accessors +=
            std::format(R"(, {{"bufferView": {}, "componentType": 5126, "count": {}, "type": "VEC4"}})",
                        rotationView, kKeys);
        const std::string_view comma = j == 0 ? "" : ", ";
        samplers += std::format(R"({}{{"input": 4, "output": {}, "interpolation": "LINEAR"}})", comma, 5 + j);
        channels +=
            std::format(R"({}{{"sampler": {}, "target": {{"node": {}, "path": "rotation"}}}})", comma, j, j);
    }
    return std::format(
        R"({{"asset": {{"version": "2.0"}}, "scene": 0, "scenes": [{{"nodes": [0, {}]}}], )"
        R"("nodes": [{}{{"name": "body", "mesh": 0, "skin": 0}}], )"
        R"("meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}}, "indices": 3}}]}}], )"
        R"("skins": [{{"joints": [{}]}}], "animations": [{{"name": "wave", "samplers": [{}], "channels": [{}]}}], )"
        R"("buffers": [{{"uri": "rig.bin", "byteLength": {}}}], "bufferViews": [{}], "accessors": [{}]}})",
        kJoints, nodes, skinJoints, samplers, channels, buffer.size(), views, accessors);
}

// rigGltf() as "bench:/rig.gltf" in a temporary directory.
struct RigFiles
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 std::format("illusion-bench-animation-{}", std::random_device{}());
    VirtualFileSystem files;
    bool ready = false;

    RigFiles()
    {
        std::filesystem::create_directories(root);
        std::vector<std::byte> buffer;
        const std::string gltf = rigGltf(buffer);
        ready = files.mount("bench", root, MountAccess::ReadWrite) == FileError::None &&
                files.writeFile("bench:/rig.bin", buffer) == FileError::None &&
                files.writeText("bench:/rig.gltf", gltf) == FileError::None;
    }
    ~RigFiles()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    RigFiles(const RigFiles&) = delete;
    RigFiles& operator=(const RigFiles&) = delete;
};

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

// Budget: ns per AnimationWorld::update() of 300 Animators of 60 joints with the Job System (time, sample,
// compose, skin matrices, bounds), about 8x a desktop result (0.25 ms); see Budget.hpp.
ILL_BENCHMARK("animation/world-update-300x60", 2'000'000.0)
{
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
    RigFiles rig;
    if (!rig.ready)
    {
        return;
    }
    JobSystem jobs;
    AssetStreamer streamer(rig.files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    AnimationWorld animation(world, streamer);
    for (u32 i = 0; i < kSkeletons; ++i)
    {
        Transform transform;
        transform.position = Vec3(static_cast<f32>(i % 20), 0.0f, static_cast<f32>(i / 20));
        MeshRenderer renderer;
        renderer.mesh = AssetRef("bench:/rig.gltf#mesh/0");
        Animator animator;
        animator.skeleton = AssetRef("bench:/rig.gltf#skin/0");
        animator.clip = AssetRef("bench:/rig.gltf#animation/wave");
        animator.time = 0.01f * static_cast<f32>(i);
        world.create(transform, renderer, animator);
    }
    animation.waitIdle();
    bench.run("AnimationWorld::update 300 x 60 joints", [&] {
        animation.update(1.0f / 60.0f, &jobs, true);
        ankerl::nanobench::doNotOptimizeAway(animation.statistics().joints);
    });
}
