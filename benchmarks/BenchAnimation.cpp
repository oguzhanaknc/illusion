#include "Budget.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/animation/Pose.hpp>
#include <illusion/animation/Retarget.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Rig.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
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

// Linear translation, rotation and scale keys on every joint; @p phase shifts the motion.
AnimationClip makeClip(f32 phase = 0.0f)
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
                const f32 angle = 0.5f * std::sin(6.28f * time + static_cast<f32>(j) + phase);
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

// The glTF text of a model of makeSkeleton() with a triangle on every joint and, per name of @p clips, a clip
// of that name rotating every joint (each with another phase); its buffer is written into @p buffer (the file
// @p bin). @p names renames the joints (empty keeps makeSkeleton()'s) and @p length is every joint's offset.
std::string rigGltf(std::vector<std::byte>& buffer, const std::vector<std::string>& clips,
                    const std::vector<std::string>& names = {}, f32 length = 0.1f,
                    std::string_view bin = "rig.bin")
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
        nodes += std::format(R"({{"name": "{}", "translation": [0, {}, 0], "children": [{}]}}, )",
                             names.empty() ? skeleton.joints[j].name : names.at(j), length, children);
        skinJoints += std::format("{}{}", j == 0 ? "" : ", ", j);
    }
    std::string animations;
    u32 accessorCount = 5;
    for (usize c = 0; c < clips.size(); ++c)
    {
        std::string samplers;
        std::string channels;
        for (u32 j = 0; j < kJoints; ++j)
        {
            std::vector<Vec4> rotations;
            for (u32 k = 0; k < kKeys; ++k)
            {
                const f32 angle =
                    0.5f * std::sin(6.28f * times[k] + static_cast<f32>(j) + static_cast<f32>(c));
                rotations.emplace_back(0.0f, 0.0f, std::sin(angle), std::cos(angle));
            }
            const u32 rotationView = view(append(buffer, rotations), rotations.size() * sizeof(Vec4));
            accessors +=
                std::format(R"(, {{"bufferView": {}, "componentType": 5126, "count": {}, "type": "VEC4"}})",
                            rotationView, kKeys);
            const std::string_view comma = j == 0 ? "" : ", ";
            samplers += std::format(R"({}{{"input": 4, "output": {}, "interpolation": "LINEAR"}})", comma,
                                    accessorCount++);
            channels += std::format(R"({}{{"sampler": {}, "target": {{"node": {}, "path": "rotation"}}}})",
                                    comma, j, j);
        }
        animations += std::format(R"({}{{"name": "{}", "samplers": [{}], "channels": [{}]}})",
                                  c == 0 ? "" : ", ", clips[c], samplers, channels);
    }
    return std::format(
        R"({{"asset": {{"version": "2.0"}}, "scene": 0, "scenes": [{{"nodes": [0, {}]}}], )"
        R"("nodes": [{}{{"name": "body", "mesh": 0, "skin": 0}}], )"
        R"("meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}}, "indices": 3}}]}}], )"
        R"("skins": [{{"joints": [{}]}}], "animations": [{}], )"
        R"("buffers": [{{"uri": "{}", "byteLength": {}}}], "bufferViews": [{}], "accessors": [{}]}})",
        kJoints, nodes, skinJoints, animations, bin, buffer.size(), views, accessors);
}

// Humanoid joint names for makeSkeleton()'s tree in one of two naming styles (Mixamo-like with @p mixamo,
// else Unreal-like), so that both get rig maps: the spine is hips to head, the five limbs are two legs, two
// arms with two fingers and a tail; joints without a humanoid bone are named alike in both styles and pair by
// name.
std::vector<std::string> humanoidNames(bool mixamo)
{
    std::vector<std::string> names(kJoints);
    const auto set = [&](u32 joint, std::string_view a, std::string_view b) {
        names.at(joint) = mixamo ? "mixamorig:" + std::string(a) : std::string(b);
    };
    const std::array<std::pair<std::string_view, std::string_view>, 6> spine{{{"Hips", "pelvis"},
                                                                              {"Spine", "spine_01"},
                                                                              {"Spine1", "spine_02"},
                                                                              {"Spine2", "spine_03"},
                                                                              {"Neck", "neck_01"},
                                                                              {"Head", "head"}}};
    for (u32 j = 0; j < kJoints; ++j)
    {
        names.at(j) = std::format("extra_{:02}", j);
    }
    for (u32 j = 0; j < spine.size(); ++j)
    {
        set(j, spine.at(j).first, spine.at(j).second);
    }
    for (const auto& [limb, a, b] : std::array<std::tuple<u32, std::string_view, std::string_view>, 2>{
             {{10, "Left", "l"}, {20, "Right", "r"}}})
    {
        set(limb, std::format("{}UpLeg", a), std::format("thigh_{}", b));
        set(limb + 1, std::format("{}Leg", a), std::format("calf_{}", b));
        set(limb + 2, std::format("{}Foot", a), std::format("foot_{}", b));
        set(limb + 3, std::format("{}ToeBase", a), std::format("ball_{}", b));
        const u32 arm = limb + 20;
        set(arm, std::format("{}Shoulder", a), std::format("clavicle_{}", b));
        set(arm + 1, std::format("{}Arm", a), std::format("upperarm_{}", b));
        set(arm + 2, std::format("{}ForeArm", a), std::format("lowerarm_{}", b));
        set(arm + 3, std::format("{}Hand", a), std::format("hand_{}", b));
        for (u32 i = 0; i < 3; ++i)
        {
            set(arm + 4 + i, std::format("{}HandIndex{}", a, i + 1), std::format("index_{:02}_{}", i + 1, b));
        }
    }
    return names;
}

// rigGltf() as "bench:/rig.gltf" in a temporary directory, with the clips @p clips.
struct RigFiles
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 std::format("illusion-bench-animation-{}", std::random_device{}());
    VirtualFileSystem files;
    bool ready = false;

    explicit RigFiles(const std::vector<std::string>& clips = {"wave"},
                      const std::vector<std::string>& names = {})
    {
        std::filesystem::create_directories(root);
        std::vector<std::byte> buffer;
        const std::string gltf = rigGltf(buffer, clips, names);
        ready = files.mount("bench", root, MountAccess::ReadWrite) == FileError::None &&
                files.writeFile("bench:/rig.bin", buffer) == FileError::None &&
                files.writeText("bench:/rig.gltf", gltf) == FileError::None;
    }
    // Writes rigGltf() without clips as "bench:/<name>.gltf" with joint @p names and offset @p length.
    bool addRig(std::string_view name, const std::vector<std::string>& names, f32 length)
    {
        std::vector<std::byte> buffer;
        const std::string gltf = rigGltf(buffer, {}, names, length, std::format("{}.bin", name));
        return files.writeFile(std::format("bench:/{}.bin", name), buffer) == FileError::None &&
               files.writeText(std::format("bench:/{}.gltf", name), gltf) == FileError::None;
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

// Budget: ns to step and evaluate 300 graph states of 60 joints on one thread, each with two layers (a masked
// Override layer over a base layer cross-fading between a three-clip 2D blend space and a clip) and root
// motion, about 7.5x a desktop result (6.5 ms; AnimationWorld spreads it over the Job System); see
// Budget.hpp.
ILL_BENCHMARK("animation/graph-300", 50'000'000.0)
{
    const Skeleton skeleton = makeSkeleton();
    std::vector<AnimationClip> clips;
    for (u32 c = 0; c < 5; ++c)
    {
        clips.push_back(makeClip(static_cast<f32>(c)));
        clips.back().name = std::format("c{}", c);
    }
    const Expected<AnimationGraph, std::string> graph = parseAnimationGraph(R"({
        "format": "illusion.animgraph", "version": 1, "rootMotion": "joint00",
        "parameters": [{"name": "x", "type": "Float"}, {"name": "y", "type": "Float"}, {"name": "go", "type": "Trigger"}],
        "layers": [
          {"name": "base", "states": [
             {"name": "move", "blend2d": {"x": "x", "y": "y", "points": [
                {"x": 0, "y": 0, "clip": "bench:/rig.glb#animation/c0"}, {"x": 1, "y": 0, "clip": "bench:/rig.glb#animation/c1"},
                {"x": 0, "y": 1, "clip": "bench:/rig.glb#animation/c2"}]},
              "events": [{"name": "step", "time": 0.25}, {"name": "step", "time": 0.75}]},
             {"name": "other", "clip": "bench:/rig.glb#animation/c3"}],
           "transitions": [{"from": "move", "to": "other", "duration": 0.5, "conditions": [{"parameter": "go"}]},
                           {"from": "other", "to": "move", "duration": 0.5, "conditions": [{"parameter": "go"}]}]},
          {"name": "upper", "weight": 0.8, "mask": [{"joint": "joint10"}],
           "states": [{"name": "aim", "clip": "bench:/rig.glb#animation/c4"}]}]})");
    if (!graph)
    {
        return;
    }
    const Expected<GraphBinding, std::string> binding =
        bindGraph(*graph, skeleton, [&](const AssetRef& ref) -> const AnimationClip* {
            for (const AnimationClip& clip : clips)
            {
                if (ref.path().ends_with("/" + clip.name))
                {
                    return &clip;
                }
            }
            return nullptr;
        });
    if (!binding)
    {
        return;
    }
    const i32 x = graph->findParameter("x");
    const i32 y = graph->findParameter("y");
    const i32 go = graph->findParameter("go");
    std::vector<AnimationGraphState> states;
    std::array<GraphEvent, 8> events{};
    for (u32 i = 0; i < kSkeletons; ++i)
    {
        AnimationGraphState state = makeGraphState(*binding);
        state.setValue(x, 0.3f * static_cast<f32>(i % 3) / 2.0f + 0.1f);
        state.setValue(y, 0.3f * static_cast<f32>(i % 5) / 4.0f + 0.1f);
        // Staggers the cross-fades, so that every step sees states at every point of a fade.
        state.setTrigger(go);
        (void)stepGraph(*binding, state, 0.0f, events);
        (void)stepGraph(*binding, state, 0.0017f * static_cast<f32>(i), events);
        states.push_back(std::move(state));
    }
    LinearAllocator scratch(usize{1} << 16);
    std::vector<JointPose> pose(kJoints);
    RootMotionDelta root;
    bench.run("step + evaluate 300 graphs x 60 joints", [&] {
        for (AnimationGraphState& state : states)
        {
            state.setTrigger(go);
            (void)stepGraph(*binding, state, 1.0f / 60.0f, events);
            evaluateGraph(*binding, state, scratch, pose, &root);
        }
        ankerl::nanobench::doNotOptimizeAway(root.yaw);
    });
}

namespace
{

// The graph of animation/world-graph-300; with @p aim also an aim offset (an Additive layer in model space
// over the upper body: a 2D blend space of nine poses against a reference, ADR 0065).
std::string worldGraph(bool aim)
{
    std::string layers = R"({"name": "base", "states": [
             {"name": "move", "blend2d": {"x": "x", "y": "y", "points": [
                {"x": 0, "y": 0, "clip": "bench:/rig.gltf#animation/c0"}, {"x": 1, "y": 0, "clip": "bench:/rig.gltf#animation/c1"},
                {"x": 0, "y": 1, "clip": "bench:/rig.gltf#animation/c2"}]},
              "events": [{"name": "step", "time": 0.25}, {"name": "step", "time": 0.75}]},
             {"name": "other", "clip": "bench:/rig.gltf#animation/c3"}],
           "transitions": [{"from": "move", "to": "other", "duration": 0.5, "conditions": [{"parameter": "go"}]},
                           {"from": "other", "to": "move", "duration": 0.5, "conditions": [{"parameter": "go"}]}]},
          {"name": "upper", "weight": 0.8, "mask": [{"joint": "joint10"}],
           "states": [{"name": "aim", "clip": "bench:/rig.gltf#animation/c4"}]})";
    std::string parameters =
        R"({"name": "x", "type": "Float"}, {"name": "y", "type": "Float"}, {"name": "go", "type": "Trigger"})";
    if (aim)
    {
        parameters += R"(, {"name": "aimYaw", "type": "Float"}, {"name": "aimPitch", "type": "Float"})";
        std::string points;
        for (u32 i = 0; i < 9; ++i)
        {
            points += std::format(R"({}{{"x": {}, "y": {}, "clip": "bench:/rig.gltf#animation/a{}"}})",
                                  i == 0 ? "" : ", ", static_cast<i32>(i % 3) - 1,
                                  0.5f * static_cast<f32>(static_cast<i32>(i / 3) - 1), i);
        }
        layers += std::format(R"(,
          {{"name": "aim", "blend": "Additive", "space": "Model", "reference": "bench:/rig.gltf#animation/a4",
           "mask": [{{"joint": "joint01"}}],
           "states": [{{"name": "aim", "blend2d": {{"x": "aimYaw", "y": "aimPitch", "points": [{}]}}}}]}})",
                              points);
    }
    return std::format(R"({{"format": "illusion.animgraph", "version": {}, "rootMotion": "joint00",
        "parameters": [{}], "layers": [{}]}})",
                       aim ? 2 : 1, parameters, layers);
}

// 300 graph Animators of 60 joints playing worldGraph(@p aim), each with an Aim on its spine when @p aim.
void runWorldGraph(ankerl::nanobench::Bench& bench, std::string_view name, bool aim)
{
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
    RigFiles rig({"c0", "c1", "c2", "c3", "c4", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8"});
    if (!rig.ready || rig.files.writeText("bench:/rig.animgraph", worldGraph(aim)) != FileError::None)
    {
        return;
    }
    JobSystem jobs;
    AssetStreamer streamer(rig.files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    AnimationWorld animation(world, streamer);
    std::vector<Entity> entities;
    for (u32 i = 0; i < kSkeletons; ++i)
    {
        Transform transform;
        transform.position = Vec3(static_cast<f32>(i % 20), 0.0f, static_cast<f32>(i / 20));
        MeshRenderer renderer;
        renderer.mesh = AssetRef("bench:/rig.gltf#mesh/0");
        Animator animator;
        animator.skeleton = AssetRef("bench:/rig.gltf#skin/0");
        animator.graph = AssetRef("bench:/rig.animgraph");
        animator.time = 0.01f * static_cast<f32>(i);
        const Entity entity = world.create(transform, renderer, animator);
        if (aim)
        {
            // The end of the limb hanging from joint03, turned by joint01 to joint03.
            Aim component;
            component.joint = "joint39";
            component.spine = "joint03";
            component.chainLength = 3;
            component.point = Vec3(static_cast<f32>(i % 7) - 3.0f, 1.5f, 10.0f);
            component.speed = 3.0f;
            component.yawParameter = "aimYaw";
            component.pitchParameter = "aimPitch";
            world.emplace<Aim>(entity, component);
        }
        entities.push_back(entity);
    }
    animation.waitIdle();
    AnimationWorldService service(animation);
    for (u32 i = 0; i < kSkeletons; ++i)
    {
        (void)service.setFloat(entities[i], "x", 0.3f * static_cast<f32>(i % 3) / 2.0f + 0.1f);
        (void)service.setFloat(entities[i], "y", 0.3f * static_cast<f32>(i % 5) / 4.0f + 0.1f);
    }
    u32 frame = 0;
    bench.run(std::string(name), [&] {
        // Staggers the cross-fades, so that every update sees Animators at every point of a fade.
        (void)service.setTrigger(entities[frame % kSkeletons], "go");
        (void)service.setTrigger(entities[(frame + kSkeletons / 2) % kSkeletons], "go");
        if (aim)
        {
            // The targets sweep, so that the aims keep following.
            const f32 sweep = std::sin(0.05f * static_cast<f32>(frame));
            (void)service.setAimTarget(entities[frame % kSkeletons], Vec3(8.0f * sweep, 1.5f, 10.0f));
        }
        ++frame;
        animation.update(1.0f / 60.0f, &jobs, true);
        ankerl::nanobench::doNotOptimizeAway(animation.events().size());
    });
}

} // namespace

// Budget: ns per AnimationWorld::update() of 300 graph Animators of 60 joints with the Job System, each with
// the two layers of animation/graph-300 (a masked Override layer over a base layer cross-fading between a
// three-clip 2D blend space and a clip), root motion and events, about 8x a desktop result (0.65 ms; ADR 0060
// asks for less than 3 ms); see Budget.hpp.
ILL_BENCHMARK("animation/world-graph-300", 5'000'000.0)
{
    runWorldGraph(bench, "AnimationWorld::update 300 graphs x 60 joints", false);
}

// Budget: ns per AnimationWorld::update() of animation/world-graph-300 with an aim offset of nine poses in
// model space over the upper body and the Aim stage (parameters, a three-joint spine turn) on every Animator,
// about 8x a desktop result (1.17 ms, 0.50 ms over animation/world-graph-300; ADR 0065 asks for under 1 ms
// more); see Budget.hpp.
ILL_BENCHMARK("animation/aim-300", 9'000'000.0)
{
    runWorldGraph(bench, "AnimationWorld::update 300 aiming graphs x 60 joints", true);
}

// Budget: ns per AnimationWorld::update() of animation/world-update-300x60 with two Entities attached to
// joints of every Animator (600 sockets placed after the poses), about 8x a desktop result (0.30 ms, of which
// the sockets take 0.05 ms); see Budget.hpp.
ILL_BENCHMARK("animation/sockets-300", 2'500'000.0)
{
    reflectCoreComponents();
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
        const Entity character = world.create(transform, renderer, animator);
        for (const std::string_view joint : {std::string_view("joint09"), std::string_view("joint19")})
        {
            BoneAttachment attachment;
            attachment.joint = joint;
            attachment.translation = Vec3(0.0f, 0.1f, 0.05f);
            const Entity item = world.create(Transform{}, attachment);
            (void)setParent(world, item, character, ParentMode::KeepLocal);
        }
    }
    animation.waitIdle();
    bench.run("AnimationWorld::update 300 x 60 joints, 600 sockets", [&] {
        animation.update(1.0f / 60.0f, &jobs, true);
        ankerl::nanobench::doNotOptimizeAway(animation.statistics().joints);
    });
}

// Budget: ns per AnimationWorld::update() of animation/world-update-300x60 with the stages of ADR 0061 on
// every Animator: a spring chain of eight joints with a collider, a look-at over three joints, two two-bone
// IK limbs (one on a child Entity) and a FootPlacement whose foot is held on the ground (the session's part
// is not timed), about 6x a desktop result (0.43 ms, of which the stages take 0.17 ms; ADR 0061 allows 1 ms
// on top of animation/world-update-300x60); see Budget.hpp.
ILL_BENCHMARK("animation/ik-300", 2'500'000.0)
{
    reflectCoreComponents();
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
    std::vector<Entity> characters;
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
        LookAt look;
        look.joint = "joint09";
        look.chainLength = 3;
        look.point = transform.position + Vec3(1.0f, 1.0f, 2.0f);
        look.speed = 3.0f;
        TwoBoneIk foot;
        foot.joint = "joint12"; // its chain hangs from joint01, the pelvis
        foot.point = transform.position + Vec3(0.05f, 0.1f, 0.1f);
        FootPlacement placement;
        placement.pelvis = "joint01";
        SpringBone spring;
        spring.root = "joint42"; // joint42 to joint49
        spring.colliders[0] =
            SpringCollider{.joint = "joint40", .offset = Vec3(0.0f, 0.3f, 0.1f), .radius = 0.1f};
        const Entity character = world.create(transform, renderer, animator, look, foot, placement, spring);
        TwoBoneIk hand;
        hand.joint = "joint22";
        hand.point = transform.position + Vec3(-0.1f, 0.25f, 0.1f);
        const Entity limb = world.create(Transform{}, hand);
        (void)setParent(world, limb, character, ParentMode::KeepLocal);
        characters.push_back(character);
    }
    animation.waitIdle();
    for (const Entity character : characters)
    {
        FootPlacementPose& pose = world.get<FootPlacementPose>(character);
        pose.feet[0].weight = 1.0f;
        pose.feet[0].lift = -0.05f;
        pose.pelvisOffset = -0.05f;
    }
    bench.run("AnimationWorld::update 300 x 60 joints, a spring chain, look-at and two limbs each", [&] {
        animation.update(1.0f / 60.0f, &jobs, true);
        ankerl::nanobench::doNotOptimizeAway(animation.statistics().joints);
    });
}

namespace
{

// The rig of makeSkeleton() with humanoid names in both styles, every joint mapped by bone or paired by name.
Skeleton humanoidSkeleton(bool mixamo, f32 length)
{
    Skeleton skeleton = makeSkeleton();
    const std::vector<std::string> names = humanoidNames(mixamo);
    for (u32 j = 0; j < kJoints; ++j)
    {
        skeleton.joints.at(j).name = names.at(j);
        skeleton.joints.at(j).translation = Vec3(0.0f, length, 0.0f);
    }
    skeleton.rig = generateRigMap(skeleton);
    return skeleton;
}

// A three-clip 1D blend over the clips c0, c1 and c2 of @p model.
std::string blendGraph(std::string_view model)
{
    return std::format(
        R"({{"format": "illusion.animgraph", "version": 1, "parameters": [{{"name": "speed", "type": "Float"}}],
        "layers": [{{"name": "base", "states": [{{"name": "move", "blend1d": {{"parameter": "speed", "points": [
            {{"value": 0, "clip": "bench:/{0}#animation/c0"}}, {{"value": 1, "clip": "bench:/{0}#animation/c1"}},
            {{"value": 2, "clip": "bench:/{0}#animation/c2"}}]}}}}]}}]}})",
        model);
}

// AnimationWorld::update() of 300 Animators of @p skeleton playing blendGraph() (the clips of rig.gltf), each
// at another blend position.
void runBlend(ankerl::nanobench::Bench& bench, std::string_view name, std::string_view skeleton,
              bool retargeted)
{
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
    RigFiles rig({"c0", "c1", "c2"}, humanoidNames(true));
    if (!rig.ready || !rig.addRig("rig_b", humanoidNames(false), 0.08f) ||
        rig.files.writeText("bench:/blend.animgraph", blendGraph("rig.gltf")) != FileError::None)
    {
        return;
    }
    JobSystem jobs;
    AssetStreamer streamer(rig.files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    AnimationWorld animation(world, streamer);
    std::vector<Entity> entities;
    for (u32 i = 0; i < kSkeletons; ++i)
    {
        Transform transform;
        transform.position = Vec3(static_cast<f32>(i % 20), 0.0f, static_cast<f32>(i / 20));
        Animator animator;
        animator.skeleton = AssetRef(skeleton);
        animator.graph = AssetRef("bench:/blend.animgraph");
        animator.time = 0.01f * static_cast<f32>(i);
        entities.push_back(world.create(transform, animator));
    }
    animation.waitIdle();
    AnimationWorldService service(animation);
    for (u32 i = 0; i < kSkeletons; ++i)
    {
        (void)service.setFloat(entities[i], "speed",
                               2.0f * static_cast<f32>(i) / static_cast<f32>(kSkeletons));
    }
    const std::optional<u32> slot = animation.findSlot(entities.front());
    const GraphBinding* binding = slot ? animation.graphBinding(*slot) : nullptr;
    ILL_CHECK(binding != nullptr && binding->clipRetargets.at(0).active() == retargeted,
              "the blend benchmark's clips are not bound as intended");
    bench.run(std::string(name), [&] {
        animation.update(1.0f / 60.0f, &jobs, true);
        ankerl::nanobench::doNotOptimizeAway(animation.statistics().joints);
    });
}

} // namespace

// Budget: ns per AnimationWorld::update() of 300 graph Animators of 60 joints with the Job System playing a
// three-clip 1D blend of their own skin's clips (bound by name), the baseline of animation/retarget-300x60,
// about 8x a desktop result (0.37 ms); see Budget.hpp.
ILL_BENCHMARK("animation/blend-300x60", 3'000'000.0)
{
    runBlend(bench, "AnimationWorld::update 300 blends x 60 joints by name", "bench:/rig.gltf#skin/0", false);
}

// Budget: ns per AnimationWorld::update() of animation/blend-300x60 with the clips retargeted onto another
// skin (other joint names, shorter bones; ADR 0065 allows 15 % over the clips bound by name), about 8x a
// desktop result (0.41 ms, 11 % over animation/blend-300x60); see Budget.hpp.
ILL_BENCHMARK("animation/retarget-300x60", 3'500'000.0)
{
    runBlend(bench, "AnimationWorld::update 300 retargeted blends x 60 joints", "bench:/rig_b.gltf#skin/0",
             true);
}

// Budget: ns of one bindRetargetedClip() of a clip moving all three paths of 60 joints between two 60-joint
// rigs (pairing, reference pose, 180 corrections), done once per clip and skeleton at load, about 8x a
// desktop result (41 us); see Budget.hpp.
ILL_BENCHMARK("animation/retarget-bind-60", 350'000.0)
{
    const Skeleton source = humanoidSkeleton(true, 0.1f);
    const Skeleton target = humanoidSkeleton(false, 0.08f);
    AnimationClip clip = makeClip();
    for (usize c = 0; c < clip.channels.size(); ++c)
    {
        clip.channels[c].joint = source.joints.at(c / 3).name; // three paths per joint
    }
    clip.skeleton = 0;
    bench.run("bindRetargetedClip 60 joints x 3 paths", [&] {
        Expected<RetargetedClip, RetargetError> bound = bindRetargetedClip(clip, source, target);
        ankerl::nanobench::doNotOptimizeAway(bound);
    });
}
