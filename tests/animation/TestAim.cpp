// Aim (ADR 0065): `.animgraph` version 2 (layer references, model-space additive layers, their parse errors
// and the version 1 writer), a nine-pose aim offset over a leaning base in model and in local space,
// single-frame poses against a reference, solveAim() (the yaw/pitch split without roll, the spread over the
// chain, clamps, degenerate input), the Aim stage of AnimationWorld on both humanoid test rigs (smoothing,
// clamping, graph parameters), the off hand on a joint of the aimed pose through TwoBoneIk::targetJoint, the
// AnimationService calls and their errors, determinism and allocation.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/animation/Aim.hpp>
#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/animation/Pose.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kDegree = std::numbers::pi_v<f32> / 180.0f;
constexpr std::string_view kSkeletonA = "game:/humanoid_a.gltf#skin/0";
constexpr std::string_view kSkeletonB = "game:/humanoid_b.gltf#skin/0";
constexpr std::string_view kWalk = "game:/humanoid_a.gltf#animation/walk";
constexpr std::string_view kWave = "game:/humanoid_b.gltf#animation/wave";

// Joints of the procedural body: hips, a spine of three, the head and a hand ahead of the chest.
constexpr u32 kHips = 0;
constexpr u32 kSpine = 1;
constexpr u32 kChest = 2;
constexpr u32 kNeck = 3;
constexpr u32 kHead = 4;
constexpr u32 kHand = 5;

Skeleton body()
{
    Skeleton skeleton;
    const auto add = [&](std::string_view name, i32 parent, Vec3 translation) {
        SkeletonJoint joint;
        joint.name = name;
        joint.parent = parent;
        joint.translation = translation;
        skeleton.joints.push_back(std::move(joint));
    };
    add("hips", -1, Vec3(0.0f, 1.0f, 0.0f));
    add("spine", 0, Vec3(0.0f, 0.1f, 0.0f));
    add("chest", 1, Vec3(0.0f, 0.15f, 0.0f));
    add("neck", 2, Vec3(0.0f, 0.15f, 0.0f));
    add("head", 3, Vec3(0.0f, 0.1f, 0.0f));
    add("hand", 2, Vec3(0.2f, 0.05f, 0.25f));
    return skeleton;
}

std::vector<JointPose> restOf(const Skeleton& skeleton)
{
    std::vector<JointPose> local(skeleton.joints.size());
    restPose(skeleton, local);
    return local;
}

std::vector<Mat4> modelOf(const Skeleton& skeleton, std::span<const JointPose> local)
{
    std::vector<Mat4> model(skeleton.joints.size());
    localToModel(skeleton, local, model);
    return model;
}

// The rotation that aims +z at yaw @p yaw and pitch @p pitch (ADR 0065: yaw about +y, then pitch up).
Quat aimRotation(f32 yaw, f32 pitch)
{
    return glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch, Vec3(-1.0f, 0.0f, 0.0f));
}

Vec3 direction(f32 yaw, f32 pitch)
{
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

f32 angleBetween(Vec3 a, Vec3 b)
{
    const Vec3 x = glm::normalize(a);
    const Vec3 y = glm::normalize(b);
    return std::atan2(glm::length(glm::cross(x, y)), glm::dot(x, y));
}

Vec3 axisOf(const Mat4& joint, Vec3 axis)
{
    return glm::normalize(Mat3(joint) * axis);
}

Quat rotationOf(const Mat4& joint)
{
    return glm::normalize(glm::quat_cast(Mat3(glm::normalize(Vec3(joint[0])), glm::normalize(Vec3(joint[1])),
                                              glm::normalize(Vec3(joint[2])))));
}

f32 quatAngle(Quat a, Quat b)
{
    return 2.0f * std::acos(std::clamp(std::abs(glm::dot(a, b)), 0.0f, 1.0f));
}

bool finitePose(std::span<const JointPose> pose)
{
    for (const JointPose& joint : pose)
    {
        const Quat q = joint.rotation;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
        {
            return false;
        }
    }
    return true;
}

// A clip of one frame that sets the rotations @p rotations ((joint, rotation) pairs).
AnimationClip pose(const std::vector<std::pair<std::string, Quat>>& rotations)
{
    AnimationClip clip;
    clip.duration = 1.0f;
    for (const auto& [joint, rotation] : rotations)
    {
        AnimationChannel channel;
        channel.joint = joint;
        channel.path = AnimationPath::Rotation;
        channel.times = {0.0f};
        channel.values = {Vec4(rotation.x, rotation.y, rotation.z, rotation.w)};
        clip.channels.push_back(std::move(channel));
    }
    return clip;
}

constexpr std::array<f32, 3> kYaws{-1.0f, 0.0f, 1.0f};
constexpr std::array<f32, 3> kPitches{-0.6f, 0.0f, 0.6f};

std::string clipPath(std::string_view name)
{
    return std::format("game:/aim.glb#animation/{}", name);
}

// The clips of the aim tests by path: "lean" (hips forward, the neck back upright), nine aim poses
// "aim_<i>_<j>" (the chest at kYaws[i], kPitches[j]), "sweep" (the chest from straight to aim_2_1 over a
// second).
std::map<std::string, AnimationClip> aimClips()
{
    std::map<std::string, AnimationClip> clips;
    clips[clipPath("lean")] = pose({{"hips", glm::angleAxis(0.5f, Vec3(1.0f, 0.0f, 0.0f))},
                                    {"neck", glm::angleAxis(-0.5f, Vec3(1.0f, 0.0f, 0.0f))}});
    clips[clipPath("rest")] = pose({{"hips", Quat(1.0f, 0.0f, 0.0f, 0.0f)}});
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < 3; ++j)
        {
            clips[clipPath(std::format("aim_{}_{}", i, j))] =
                pose({{"chest", aimRotation(kYaws.at(i), kPitches.at(j))}});
        }
    }
    AnimationClip sweep = pose({{"chest", Quat(1.0f, 0.0f, 0.0f, 0.0f)}});
    const Quat end = aimRotation(1.0f, 0.0f);
    sweep.channels[0].times.push_back(1.0f);
    sweep.channels[0].values.emplace_back(end.x, end.y, end.z, end.w);
    clips[clipPath("sweep")] = std::move(sweep);
    return clips;
}

std::string aimGraphJson(std::string_view space)
{
    std::string points;
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < 3; ++j)
        {
            points += std::format(R"({}{{"x": {}, "y": {}, "clip": "{}"}})", points.empty() ? "" : ", ",
                                  kYaws.at(i), kPitches.at(j), clipPath(std::format("aim_{}_{}", i, j)));
        }
    }
    return std::format(R"({{"format": "illusion.animgraph", "version": 2,
"parameters": [{{"name": "aimYaw", "type": "Float"}}, {{"name": "aimPitch", "type": "Float"}}],
"layers": [{{"name": "base", "states": [{{"name": "lean", "clip": "{}"}}]}},
  {{"name": "aim", "blend": "Additive", "space": "{}", "reference": "{}", "mask": [{{"joint": "chest"}}],
   "states": [{{"name": "aim", "blend2d": {{"x": "aimYaw", "y": "aimPitch", "points": [{}]}}}}]}}]}})",
                       clipPath("lean"), space, clipPath("aim_1_1"), points);
}

AnimationGraph parseGraph(const std::string& json)
{
    Expected<AnimationGraph, std::string> graph = parseAnimationGraph(json);
    INFO((graph ? std::string() : graph.error()));
    REQUIRE(graph.hasValue());
    return std::move(*graph);
}

// Evaluates @p graph on @p skeleton with the Float parameters @p values, at time 0.
std::vector<JointPose> evaluate(const AnimationGraph& graph, const Skeleton& skeleton,
                                const std::map<std::string, AnimationClip>& clips,
                                const std::vector<std::pair<std::string, f32>>& values = {})
{
    Expected<GraphBinding, std::string> binding = bindGraph(
        graph, skeleton, std::function<const AnimationClip*(const AssetRef&)>([&](const AssetRef& ref) {
            const auto found = clips.find(std::string(ref.path()));
            return found != clips.end() ? &found->second : nullptr;
        }));
    INFO((binding ? std::string() : binding.error()));
    REQUIRE(binding.hasValue());
    AnimationGraphState state = makeGraphState(*binding);
    for (const auto& [name, value] : values)
    {
        state.setValue(graph.findParameter(name), value);
    }
    LinearAllocator scratch(graphScratchBytes(*binding) + 256);
    std::vector<JointPose> local(skeleton.joints.size());
    evaluateGraph(*binding, state, scratch, local, nullptr);
    return local;
}

// The aim error of the head of the body evaluated by @p graph at (@p yaw, @p pitch): its +z against the aim
// direction, in model space.
f32 headError(const AnimationGraph& graph, const Skeleton& skeleton,
              const std::map<std::string, AnimationClip>& clips, f32 yaw, f32 pitch)
{
    const std::vector<JointPose> local =
        evaluate(graph, skeleton, clips, {{"aimYaw", yaw}, {"aimPitch", pitch}});
    const std::vector<Mat4> model = modelOf(skeleton, local);
    return angleBetween(axisOf(model[kHead], Vec3(0.0f, 0.0f, 1.0f)), direction(yaw, pitch));
}

u64 writtenVersion(const std::string& json)
{
    const Expected<JsonDocument, JsonError> document = JsonDocument::parse(json);
    REQUIRE(document.hasValue());
    return document->root().find("version").asU64().value_or(0);
}

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

Animator animator(std::string_view skeleton, std::string_view clip, f32 time)
{
    Animator result;
    result.skeleton = AssetRef(skeleton);
    result.clip = AssetRef(clip);
    result.time = time;
    result.playing = false;
    return result;
}

Aim headAim(Vec3 point)
{
    Aim aim;
    aim.joint = "head";
    aim.spine = "upperChest";
    aim.chainLength = 3;
    aim.point = point;
    aim.speed = 0.0f;
    return aim;
}

struct Fixture
{
    Fixture() { reflectAll(); }

    [[nodiscard]] u32 slot(Entity entity) const { return world.get<AnimationPose>(entity).slot; }

    [[nodiscard]] Mat4 joint(Entity entity, std::string_view name) const
    {
        const u32 index = slot(entity);
        const Skeleton* skeleton = animation.skeleton(index);
        REQUIRE(skeleton != nullptr);
        const i32 found = findRigJoint(*skeleton, name);
        REQUIRE(found >= 0);
        return animation.jointMatrices(index)[static_cast<usize>(found)];
    }

    [[nodiscard]] std::vector<SkinMatrix> skin(Entity entity) const
    {
        const std::span<const SkinMatrix> matrices = animation.skinMatrices(slot(entity));
        return {matrices.begin(), matrices.end()};
    }

    void step(u32 count, f32 seconds = 1.0f / 60.0f)
    {
        for (u32 i = 0; i < count; ++i)
        {
            animation.update(seconds, &jobs, true);
        }
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

// A graph on humanoid_a with an aim layer in model space over the walk: the walk and wave clips stand in for
// aim poses (the code path, not the look, is under test).
constexpr std::string_view kAimGraph = R"({"format": "illusion.animgraph", "version": 2,
"parameters": [{"name": "aimYaw", "type": "Float"}, {"name": "aimPitch", "type": "Float"}],
"layers": [{"name": "base", "states": [{"name": "walk", "clip": "game:/humanoid_a.gltf#animation/walk"}]},
  {"name": "aim", "blend": "Additive", "space": "Model", "weight": 0.5,
   "reference": "game:/humanoid_a.gltf#animation/walk", "referenceTime": 0.25, "mask": [{"joint": "upperChest"}],
   "states": [{"name": "aim", "blend2d": {"x": "aimYaw", "y": "aimPitch", "points": [
     {"x": -1, "y": 0, "clip": "game:/humanoid_a.gltf#animation/walk"},
     {"x": 1, "y": 0, "clip": "game:/humanoid_b.gltf#animation/wave"},
     {"x": 0, "y": 1, "clip": "game:/humanoid_a.gltf#animation/walk"}]}}]}]})";

} // namespace

TEST_CASE("version 2 layers read and write their reference, reference time and space")
{
    const AnimationGraph graph = parseGraph(aimGraphJson("Model"));
    const AnimationLayer& aim = graph.layers.at(1);
    CHECK(aim.blend == AnimationLayerBlend::Additive);
    CHECK(aim.space == AnimationAdditiveSpace::Model);
    CHECK(aim.reference.path() == clipPath("aim_1_1"));
    CHECK(aim.referenceTime == 0.0f);
    CHECK(graph.layers.at(0).space == AnimationAdditiveSpace::Local);

    AnimationGraph timed = graph;
    timed.layers.at(1).referenceTime = 0.25f;
    const std::string written = writeAnimationGraph(timed);
    CHECK(writtenVersion(written) == 2);
    const AnimationGraph again = parseGraph(written);
    CHECK(again.layers.at(1).space == AnimationAdditiveSpace::Model);
    CHECK(again.layers.at(1).reference.path() == clipPath("aim_1_1"));
    CHECK(again.layers.at(1).referenceTime == 0.25f);
    CHECK(writeAnimationGraph(again) == written);
}

TEST_CASE("a graph without version 2 features is written as version 1")
{
    const AnimationGraph graph = parseGraph(R"({"format": "illusion.animgraph", "version": 1, "layers": [
        {"name": "base", "states": [{"name": "s", "clip": "game:/a.glb#animation/s"}]},
        {"name": "add", "blend": "Additive", "states": [{"name": "s", "clip": "game:/a.glb#animation/s"}]}]})");
    const std::string written = writeAnimationGraph(graph);
    CHECK(writtenVersion(written) == 1);
    CHECK(parseGraph(written).layers.at(1).space == AnimationAdditiveSpace::Local);
}

TEST_CASE("parseAnimationGraph names the JSON path of every version 2 error")
{
    const auto layer = [](std::string_view version, std::string_view keys) {
        return std::format(R"({{"format": "illusion.animgraph", "version": {}, "layers": [
            {{"name": "base", "states": [{{"name": "s", "clip": "game:/a.glb#animation/s"}}]}},
            {{"name": "aim", {}"states": [{{"name": "s", "clip": "game:/a.glb#animation/s"}}]}}]}})",
                           version, keys);
    };
    const std::vector<std::pair<std::string, std::string_view>> cases = {
        {layer("1", R"("blend": "Additive", "space": "Model", )"), "layers[1]: unknown key 'space'"},
        {layer("1", R"("blend": "Additive", "reference": "game:/a.glb#animation/s", )"),
         "layers[1]: unknown key 'reference'"},
        {layer("1", R"("blend": "Additive", "referenceTime": 0.5, )"),
         "layers[1]: unknown key 'referenceTime'"},
        {layer("2", R"("space": "Model", )"), "layers[1].space: only an Additive layer takes it"},
        {layer("2", R"("reference": "game:/a.glb#animation/s", )"),
         "layers[1].reference: only an Additive layer takes it"},
        {layer("2", R"("blend": "Override", "referenceTime": 0, )"),
         "layers[1].referenceTime: only an Additive layer takes it"},
        {layer("2", R"("blend": "Additive", "space": "World", )"),
         "layers[1].space: unknown space 'World' (Local or Model)"},
        {layer("2", R"("blend": "Additive", "space": 1, )"),
         "layers[1].space: expected a string, found integer"},
        {layer("2", R"("blend": "Additive", "reference": "game:/a.glb#mesh/0", )"),
         "layers[1].reference: 'game:/a.glb#mesh/0' is not an animation clip path"},
        {layer("2", R"("blend": "Additive", "reference": "", )"), "layers[1].reference: must not be empty"},
        {layer("2", R"("blend": "Additive", "referenceTime": 0.5, )"),
         "layers[1].referenceTime: needs a reference"},
        {layer("2", R"("blend": "Additive", "reference": "game:/a.glb#animation/s", "referenceTime": 1.5, )"),
         "layers[1].referenceTime: must be in [0, 1]"},
        {layer("2", R"("blend": "Additive", "reference": "game:/a.glb#animation/s", "referenceTime": "0", )"),
         "layers[1].referenceTime: expected a number, found string"},
    };
    for (const auto& [json, message] : cases)
    {
        CAPTURE(json);
        const Expected<AnimationGraph, std::string> graph = parseAnimationGraph(json);
        REQUIRE_FALSE(graph.hasValue());
        CAPTURE(graph.error());
        CHECK(graph.error().starts_with(message));
    }
}

TEST_CASE("bindGraph names a missing reference clip")
{
    const AnimationGraph graph = parseGraph(aimGraphJson("Model"));
    std::map<std::string, AnimationClip> clips = aimClips();
    clips.erase(clipPath("aim_1_1"));
    const Skeleton skeleton = body();
    const Expected<GraphBinding, std::string> binding = bindGraph(
        graph, skeleton, std::function<const AnimationClip*(const AssetRef&)>([&](const AssetRef& ref) {
            const auto found = clips.find(std::string(ref.path()));
            return found != clips.end() ? &found->second : nullptr;
        }));
    REQUIRE_FALSE(binding.hasValue());
    // The blend space names it first: it is also one of the nine points.
    CHECK(binding.error().find("aim_1_1' not found") != std::string::npos);

    AnimationGraph referenceOnly = graph;
    referenceOnly.layers.at(1).reference = AssetRef(clipPath("sweep"));
    clips = aimClips();
    clips.erase(clipPath("sweep"));
    const Expected<GraphBinding, std::string> missing =
        bindGraph(referenceOnly, skeleton,
                  std::function<const AnimationClip*(const AssetRef&)>([&](const AssetRef& ref) {
                      const auto found = clips.find(std::string(ref.path()));
                      return found != clips.end() ? &found->second : nullptr;
                  }));
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().starts_with("layers[1].reference: clip"));
}

TEST_CASE(
    "a nine-pose aim offset over a leaning base aims within 2 degrees in model space, in local space not")
{
    const Skeleton skeleton = body();
    const std::map<std::string, AnimationClip> clips = aimClips();
    const AnimationGraph model = parseGraph(aimGraphJson("Model"));
    const AnimationGraph local = parseGraph(aimGraphJson("Local"));
    // The grid points, the edges between them and a point inside a triangle.
    const std::vector<std::pair<f32, f32>> exact = {{1.0f, 0.6f}, {-1.0f, -0.6f}, {0.0f, 0.0f}, {1.0f, 0.0f},
                                                    {0.5f, 0.0f}, {1.0f, 0.3f},   {-0.5f, 0.6f}};
    for (const auto& [yaw, pitch] : exact)
    {
        CAPTURE(yaw);
        CAPTURE(pitch);
        CHECK(headError(model, skeleton, clips, yaw, pitch) < 0.1f * kDegree);
    }
    CHECK(headError(model, skeleton, clips, 0.5f, 0.3f) < 2.0f * kDegree);
    CHECK(headError(model, skeleton, clips, -0.7f, -0.2f) < 2.0f * kDegree);
    // In local space the lean turns the offset's axes: the yaw is taken about the leaning chest.
    CHECK(headError(local, skeleton, clips, 1.0f, 0.0f) > 10.0f * kDegree);
    CHECK(headError(local, skeleton, clips, 1.0f, 0.6f) > 10.0f * kDegree);
}

TEST_CASE("single-frame aim poses against a reference give their offsets, against their own first frame none")
{
    const Skeleton skeleton = body();
    const std::map<std::string, AnimationClip> clips = aimClips();
    const auto graphWith = [](std::string_view reference) {
        return std::format(R"({{"format": "illusion.animgraph", "version": 2, "layers": [
            {{"name": "base", "states": [{{"name": "rest", "clip": "{}"}}]}},
            {{"name": "aim", "blend": "Additive", {}"states": [{{"name": "s", "clip": "{}"}}]}}]}})",
                           clipPath("rest"), reference, clipPath("aim_2_1"));
    };
    const Quat offset = aimRotation(1.0f, 0.0f);
    const std::vector<JointPose> withReference = evaluate(
        parseGraph(graphWith(std::format(R"("reference": "{}", )", clipPath("aim_1_1")))), skeleton, clips);
    CHECK(quatAngle(withReference[kChest].rotation, offset) < 1e-4f);
    CHECK(quatAngle(withReference[kHips].rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f)) < 1e-4f);

    const std::vector<JointPose> withoutReference = evaluate(parseGraph(graphWith("")), skeleton, clips);
    CHECK(quatAngle(withoutReference[kChest].rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f)) < 1e-4f);

    // The sweep reaches aim_2_1 at its end: a reference at time 1 cancels the offset, at time 0 keeps it.
    const std::vector<JointPose> atEnd = evaluate(
        parseGraph(graphWith(std::format(R"("reference": "{}", "referenceTime": 1, )", clipPath("sweep")))),
        skeleton, clips);
    CHECK(quatAngle(atEnd[kChest].rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f)) < 1e-4f);
    const std::vector<JointPose> atStart = evaluate(
        parseGraph(graphWith(std::format(R"("reference": "{}", "referenceTime": 0, )", clipPath("sweep")))),
        skeleton, clips);
    CHECK(quatAngle(atStart[kChest].rotation, offset) < 1e-4f);
}

TEST_CASE("addPoseInModelSpace adds model-space deltas by weight and mask and keeps the rest")
{
    const Skeleton skeleton = body();
    std::vector<JointPose> base = restOf(skeleton);
    base[kHips].rotation = glm::angleAxis(0.5f, Vec3(1.0f, 0.0f, 0.0f));
    std::vector<JointPose> reference = restOf(skeleton);
    std::vector<JointPose> additive = restOf(skeleton);
    additive[kChest].rotation = aimRotation(0.8f, 0.0f);
    additive[kChest].translation += Vec3(0.0f, 0.02f, 0.0f);
    std::vector<f32> mask(skeleton.joints.size(), 0.0f);
    mask.at(kChest) = 1.0f;
    mask.at(kNeck) = 1.0f;
    mask.at(kHead) = 1.0f;
    std::vector<Quat> scratch(skeleton.joints.size() * 3);
    const std::vector<Mat4> before = modelOf(skeleton, base);

    std::vector<JointPose> full = base;
    addPoseInModelSpace(skeleton, full, additive, reference, 1.0f, mask, scratch);
    const std::vector<Mat4> after = modelOf(skeleton, full);
    // The chest's model rotation gains the yaw about model +y; the head keeps its place on the chest.
    CHECK(quatAngle(rotationOf(after[kChest]), aimRotation(0.8f, 0.0f) * rotationOf(before[kChest])) < 1e-4f);
    CHECK(quatAngle(rotationOf(after[kHead]), aimRotation(0.8f, 0.0f) * rotationOf(before[kHead])) < 1e-4f);
    CHECK(quatAngle(full[kHips].rotation, base[kHips].rotation) < 1e-6f);
    CHECK(full[kChest].translation.y == doctest::Approx(base[kChest].translation.y + 0.02f));
    // Outside the mask a joint keeps its model rotation, so the hand turns back against the chest.
    CHECK(quatAngle(rotationOf(after[kHand]), rotationOf(before[kHand])) < 1e-4f);

    std::vector<JointPose> half = base;
    addPoseInModelSpace(skeleton, half, additive, reference, 0.5f, {}, scratch);
    CHECK(quatAngle(rotationOf(modelOf(skeleton, half)[kChest]),
                    aimRotation(0.4f, 0.0f) * rotationOf(before[kChest])) < 1e-4f);

    std::vector<JointPose> none = base;
    addPoseInModelSpace(skeleton, none, additive, reference, 0.0f, {}, scratch);
    CHECK(std::memcmp(none.data(), base.data(), base.size() * sizeof(JointPose)) == 0);
}

TEST_CASE("aimAngles measures yaw from +z towards +x and pitch above the horizontal plane")
{
    const Vec3 origin(1.0f, 2.0f, 3.0f);
    CHECK(aimAngles(origin, origin + Vec3(0.0f, 0.0f, 2.0f)).yaw == doctest::Approx(0.0f));
    CHECK(aimAngles(origin, origin + Vec3(2.0f, 0.0f, 0.0f)).yaw ==
          doctest::Approx(std::numbers::pi_v<f32> / 2.0f));
    CHECK(aimAngles(origin, origin + Vec3(-1.0f, 0.0f, 0.0f)).yaw ==
          doctest::Approx(-std::numbers::pi_v<f32> / 2.0f));
    CHECK(aimAngles(origin, origin + Vec3(0.0f, 1.0f, 1.0f)).pitch ==
          doctest::Approx(std::numbers::pi_v<f32> / 4.0f));
    const AimAngles near = aimAngles(origin, origin + Vec3(0.0f, 0.0f, 0.0005f));
    CHECK(near.yaw == 0.0f);
    CHECK(near.pitch == 0.0f);
    const AimAngles up = aimAngles(origin, origin + Vec3(0.0f, 3.0f, 0.0f));
    CHECK(up.yaw == 0.0f);
    CHECK(up.pitch == doctest::Approx(std::numbers::pi_v<f32> / 2.0f));
}

TEST_CASE("solveAim keeps yaw and pitch apart: the head aims at the target without rolling")
{
    const Skeleton skeleton = body();
    std::vector<JointPose> local = restOf(skeleton);
    const Vec3 head(modelOf(skeleton, local)[kHead][3]);
    const std::array<u32, 3> chain{kSpine, kChest, kNeck};
    const std::array<f32, 3> weights{0.3f, 0.3f, 0.4f};
    for (const auto& [yaw, pitch] :
         std::vector<std::pair<f32, f32>>{{1.0f, 0.5f}, {-1.2f, -0.4f}, {0.3f, 0.9f}})
    {
        CAPTURE(yaw);
        CAPTURE(pitch);
        local = restOf(skeleton);
        AimSolve solve;
        solve.target = head + direction(yaw, pitch) * 5.0f;
        solve.maxPitch = 1.2f;
        const AimResult result = solveAim(skeleton, chain, weights, kHead, solve, local);
        const std::vector<Mat4> model = modelOf(skeleton, local);
        const f32 error =
            angleBetween(axisOf(model[kHead], Vec3(0.0f, 0.0f, 1.0f)), solve.target - Vec3(model[kHead][3]));
        CHECK(error < 0.5f * kDegree);
        CHECK(std::abs(result.error - error) < 1e-3f);
        CHECK_FALSE(result.clamped);
        // The head's right axis stays in the horizontal plane: no roll.
        CHECK(std::abs(axisOf(model[kHead], Vec3(1.0f, 0.0f, 0.0f)).y) < 1e-4f);
        CHECK(result.applied.yaw == doctest::Approx(yaw).epsilon(0.02));
    }
}

TEST_CASE("solveAim spreads the turn over the chain by its weights")
{
    const Skeleton skeleton = body();
    const Vec3 head(modelOf(skeleton, restOf(skeleton))[kHead][3]);
    const std::array<u32, 3> chain{kSpine, kChest, kNeck};
    const auto check = [&](const std::array<f32, 3>& weights, const std::array<f32, 3>& shares) {
        std::vector<JointPose> local = restOf(skeleton);
        AimSolve solve;
        solve.target = head + direction(0.9f, 0.0f) * 4.0f;
        static_cast<void>(solveAim(skeleton, chain, weights, kHead, solve, local));
        for (usize i = 0; i < chain.size(); ++i)
        {
            CAPTURE(i);
            CHECK(quatAngle(local[chain.at(i)].rotation, aimRotation(0.9f * shares.at(i), 0.0f)) < 1e-4f);
        }
    };
    check({1.0f, 1.0f, 2.0f}, {0.25f, 0.25f, 0.5f});
    check({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});
    // Zero, negative and non-finite weights share the turn evenly.
    check({0.0f, -1.0f, std::numeric_limits<f32>::quiet_NaN()}, {1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f});
}

TEST_CASE("solveAim clamps the yaw and pitch, scales by weight and stays finite for degenerate input")
{
    const Skeleton skeleton = body();
    const std::vector<JointPose> rest = restOf(skeleton);
    const Vec3 head(modelOf(skeleton, rest)[kHead][3]);
    const std::array<u32, 3> chain{kSpine, kChest, kNeck};
    const std::array<f32, 3> weights{0.3f, 0.3f, 0.4f};
    const auto aimed = [&](const AimSolve& solve, AimResult* result = nullptr) {
        std::vector<JointPose> local = rest;
        const AimResult solved = solveAim(skeleton, chain, weights, kHead, solve, local);
        if (result != nullptr)
        {
            *result = solved;
        }
        CHECK(finitePose(local));
        return local;
    };

    AimSolve wide;
    wide.target = head + direction(2.5f, 0.2f) * 5.0f;
    wide.maxYaw = 1.0f;
    AimResult result;
    std::vector<JointPose> local = aimed(wide, &result);
    CHECK(result.clamped);
    Vec3 axis = axisOf(modelOf(skeleton, local)[kHead], Vec3(0.0f, 0.0f, 1.0f));
    CHECK(std::atan2(axis.x, axis.z) == doctest::Approx(1.0f).epsilon(0.002));

    AimSolve steep;
    steep.target = head + direction(0.0f, 1.3f) * 5.0f;
    steep.maxPitch = 0.5f;
    local = aimed(steep, &result);
    CHECK(result.clamped);
    axis = axisOf(modelOf(skeleton, local)[kHead], Vec3(0.0f, 0.0f, 1.0f));
    CHECK(std::asin(axis.y) == doctest::Approx(0.5f).epsilon(0.01));

    AimSolve half;
    half.target = head + direction(1.0f, 0.0f) * 5.0f;
    half.weight = 0.5f;
    local = aimed(half, &result);
    CHECK(result.applied.yaw == doctest::Approx(0.5f).epsilon(0.01));
    axis = axisOf(modelOf(skeleton, local)[kHead], Vec3(0.0f, 0.0f, 1.0f));
    CHECK(std::atan2(axis.x, axis.z) == doctest::Approx(0.5f).epsilon(0.01));

    // Behind: the yaw clamps to one side; at the joint, a zero axis, no weight, NaN: nothing moves.
    AimSolve behind;
    behind.target = head + Vec3(0.0f, 0.0f, -5.0f);
    local = aimed(behind, &result);
    CHECK(result.clamped);
    CHECK(std::isfinite(result.error));
    for (AimSolve solve : {AimSolve{.target = head}, AimSolve{.target = head + Vec3(0.0f, 0.0f, 5.0f)}})
    {
        solve.forward = Vec3(0.0f);
        CHECK(std::memcmp(aimed(solve).data(), rest.data(), rest.size() * sizeof(JointPose)) == 0);
    }
    AimSolve atJoint;
    atJoint.target = head;
    CHECK(std::memcmp(aimed(atJoint).data(), rest.data(), rest.size() * sizeof(JointPose)) == 0);
    AimSolve off;
    off.target = head + Vec3(3.0f, 0.0f, 3.0f);
    off.weight = 0.0f;
    CHECK(std::memcmp(aimed(off).data(), rest.data(), rest.size() * sizeof(JointPose)) == 0);
    AimSolve broken;
    broken.target = Vec3(std::numeric_limits<f32>::quiet_NaN());
    broken.maxYaw = std::numeric_limits<f32>::infinity();
    CHECK(std::memcmp(aimed(broken).data(), rest.data(), rest.size() * sizeof(JointPose)) == 0);
    AimSolve nanLimits;
    nanLimits.target = head + Vec3(3.0f, 1.0f, 3.0f);
    nanLimits.maxYaw = std::numeric_limits<f32>::quiet_NaN();
    nanLimits.maxPitch = std::numeric_limits<f32>::quiet_NaN();
    nanLimits.weight = std::numeric_limits<f32>::quiet_NaN();
    static_cast<void>(aimed(nanLimits));
}

TEST_CASE("both test rigs aim their heads while walking with the same bone-name Aim")
{
    Fixture f;
    const Vec3 target(1.5f, 2.2f, 3.0f);
    const Entity a = f.world.create(Transform{}, animator(kSkeletonA, kWalk, 0.3f), headAim(target));
    const Entity b = f.world.create(Transform{}, animator(kSkeletonB, kWalk, 0.3f), headAim(target));
    f.animation.waitIdle();
    f.step(3);
    CHECK(f.animation.statistics().failures == 0);
    for (const Entity entity : {a, b})
    {
        const Mat4 head = f.joint(entity, "head");
        CHECK(angleBetween(axisOf(head, Vec3(0.0f, 0.0f, 1.0f)), target - Vec3(head[3])) < 1.0f * kDegree);
        // The angles are those from the spine joint to the target.
        const Mat4 spine = f.joint(entity, "upperChest");
        const Expected<AimAngles, AnimationError> angles = f.service.aimAngles(entity);
        REQUIRE(angles.hasValue());
        const AimAngles expected = aimAngles(Vec3(spine[3]), target);
        CHECK(angles->yaw == doctest::Approx(expected.yaw).epsilon(0.02));
        CHECK(angles->pitch == doctest::Approx(expected.pitch).epsilon(0.05));
    }
    // A weight of 0 leaves the walk's head alone.
    REQUIRE(f.service.setAimWeight(a, 0.0f).hasValue());
    f.step(1);
    const Mat4 head = f.joint(a, "head");
    CHECK(angleBetween(axisOf(head, Vec3(0.0f, 0.0f, 1.0f)), target - Vec3(head[3])) > 10.0f * kDegree);
}

TEST_CASE("the Aim follows its target at its speed and clamps to its largest yaw")
{
    Fixture f;
    Aim aim = headAim(Vec3(0.0f, 1.45f, 5.0f));
    aim.speed = 1.0f;
    aim.maxYaw = 0.5f;
    const Entity entity = f.world.create(Transform{}, animator(kSkeletonA, kWalk, 0.3f), aim);
    f.animation.waitIdle(); // outside play the aim is reached at once
    f.step(1);
    const AimAngles start = *f.service.aimAngles(entity);
    CHECK(std::abs(start.yaw) < 0.01f);

    REQUIRE(
        f.service.setAimTarget(entity, Vec3(5.0f * std::sin(1.0f), 1.45f, 5.0f * std::cos(1.0f))).hasValue());
    f.step(1, 0.1f);
    const AimAngles first = *f.service.aimAngles(entity);
    CHECK(first.yaw == doctest::Approx(0.1f).epsilon(0.05));
    f.step(10, 0.1f);
    const AimAngles settled = *f.service.aimAngles(entity);
    CHECK(settled.yaw == doctest::Approx(0.5f));
    CHECK(std::abs(settled.pitch - start.pitch) < 0.02f);

    // Turned around, the Entity's Transform carries the target into model space.
    f.world.get<Transform>(entity).rotation = glm::angleAxis(0.5f, Vec3(0.0f, 1.0f, 0.0f));
    f.animation.update(0.0f, nullptr, false);
    CHECK(f.service.aimAngles(entity)->yaw == doctest::Approx(0.5f));
    f.world.get<Transform>(entity).rotation = glm::angleAxis(0.8f, Vec3(0.0f, 1.0f, 0.0f));
    f.animation.update(0.0f, nullptr, false);
    CHECK(f.service.aimAngles(entity)->yaw == doctest::Approx(0.2f).epsilon(0.02));
}

TEST_CASE("the Aim writes its angles to the graph's parameters before the graph steps")
{
    Fixture f;
    REQUIRE(f.project.files.writeText("game:/aim.animgraph", kAimGraph) == FileError::None);
    Animator graphAnimator = animator(kSkeletonA, kWalk, 0.0f);
    graphAnimator.clip = AssetRef();
    graphAnimator.graph = AssetRef("game:/aim.animgraph");
    graphAnimator.playing = true;
    Aim aim = headAim(Vec3(2.0f, 2.5f, 3.0f));
    aim.yawParameter = "aimYaw";
    aim.pitchParameter = "aimPitch";
    const Entity entity = f.world.create(Transform{}, graphAnimator, aim);
    f.animation.waitIdle();
    REQUIRE(f.animation.graph(f.slot(entity)) != nullptr);
    for (u32 i = 0; i < 3; ++i)
    {
        f.step(1);
        const AimAngles angles = *f.service.aimAngles(entity);
        CHECK(angles.yaw > 0.2f);
        CHECK(*f.service.parameter(entity, "aimYaw") == angles.yaw);
        CHECK(*f.service.parameter(entity, "aimPitch") == angles.pitch);
    }
    // Outside play the graph restarts at the Animator's time, and the aim still reaches the parameters.
    f.animation.update(0.0f, nullptr, false);
    CHECK(*f.service.parameter(entity, "aimYaw") == f.service.aimAngles(entity)->yaw);

    // A name that is no Float parameter of the graph is ignored (and logged once).
    f.world.get<Aim>(entity).yawParameter = "speed";
    f.step(1);
    CHECK(f.service.aimAngles(entity)->yaw > 0.2f);
}

TEST_CASE("an off hand on a joint of the aimed pose stays on its grip with the animated hand rotation")
{
    Fixture f;
    const Vec3 target(-1.0f, 2.0f, 3.0f);
    const f32 time = 0.3f;
    const Entity character = f.world.create(Transform{}, animator(kSkeletonA, kWalk, time), headAim(target));
    TwoBoneIk ik;
    ik.joint = "leftHand";
    ik.targetJoint = "rightHand";
    ik.point = Vec3(50.0f, -10.0f, 25.0f); // centimetres in the hand's space (humanoid_a)
    ik.polePoint = Vec3(0.5f, 1.0f, -1.0f);
    ik.alignToTarget = true;
    const Entity limb = f.world.create(Transform{}, ik);
    REQUIRE(setParent(f.world, limb, character, ParentMode::KeepLocal).hasValue());
    f.animation.waitIdle();
    f.step(2);

    const Mat4 grip = f.joint(character, "rightHand");
    const Mat4 hand = f.joint(character, "leftHand");
    CHECK(glm::length(Vec3(hand[3]) - Vec3(grip * Vec4(ik.point, 1.0f))) < 1e-3f);
    // The grip moved with the aimed spine.
    const Mat4 head = f.joint(character, "head");
    CHECK(angleBetween(axisOf(head, Vec3(0.0f, 0.0f, 1.0f)), target - Vec3(head[3])) < 1.0f * kDegree);

    // The left hand keeps the turn relative to the right hand that the walk gives it.
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    std::optional<ModelData> model = loadGltf(files, "test:/gltf/humanoid_a.gltf", &error);
    REQUIRE(model.has_value());
    const Skeleton& skeleton = model->skins.at(0);
    std::vector<JointPose> local(skeleton.joints.size());
    sampleClip(model->animations.at(0), bindClip(model->animations.at(0), skeleton), skeleton, time, local);
    const std::vector<Mat4> walk = modelOf(skeleton, local);
    const auto left = static_cast<usize>(findRigJoint(skeleton, "leftHand"));
    const auto right = static_cast<usize>(findRigJoint(skeleton, "rightHand"));
    const Quat animated = glm::inverse(rotationOf(walk[right])) * rotationOf(walk[left]);
    CHECK(quatAngle(glm::inverse(rotationOf(grip)) * rotationOf(hand), animated) < 1e-3f);

    // A target joint the skeleton lacks leaves the limb alone.
    f.world.get<TwoBoneIk>(limb).targetJoint = "tail";
    f.step(1);
    CHECK(glm::length(Vec3(f.joint(character, "leftHand")[3]) - Vec3(grip * Vec4(ik.point, 1.0f))) > 0.05f);
}

TEST_CASE("setAimTarget, setAimWeight and aimAngles return every error and never assert")
{
    Fixture f;
    const Entity aimed =
        f.world.create(Transform{}, animator(kSkeletonA, kWalk, 0.3f), headAim(Vec3(0.0f, 1.5f, 4.0f)));
    const Entity bare = f.world.create(Transform{}, animator(kSkeletonA, kWalk, 0.3f));
    const Entity plain = f.world.create(Transform{});
    f.animation.waitIdle();
    const test::ScopedAssertCatcher asserts; // an assert would throw and fail the test

    REQUIRE(f.service.setAimTarget(aimed, Vec3(1.0f, 2.0f, 3.0f)).hasValue());
    CHECK(f.world.get<Aim>(aimed).point == Vec3(1.0f, 2.0f, 3.0f));
    f.world.get<Aim>(aimed).target = plain;
    REQUIRE(f.service.setAimTarget(aimed, Vec3(1.0f, 2.0f, 3.0f)).hasValue());
    CHECK(f.world.get<Aim>(aimed).target.isNull());
    REQUIRE(f.service.setAimWeight(aimed, 2.0f).hasValue());
    CHECK(f.world.get<Aim>(aimed).weight == 1.0f);
    REQUIRE(f.service.setAimWeight(aimed, -1.0f).hasValue());
    CHECK(f.world.get<Aim>(aimed).weight == 0.0f);
    CHECK(f.service.aimAngles(aimed).hasValue());

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    CHECK(f.service.setAimTarget(aimed, Vec3(nan, 0.0f, 0.0f)).error() == AnimationError::InvalidValue);
    CHECK(f.service.setAimWeight(aimed, nan).error() == AnimationError::InvalidValue);
    CHECK(f.service.setAimTarget(bare, Vec3(0.0f)).error() == AnimationError::UnknownName);
    CHECK(f.service.setAimWeight(bare, 1.0f).error() == AnimationError::UnknownName);
    CHECK(f.service.aimAngles(bare).error() == AnimationError::UnknownName);
    for (const Entity entity : {plain, Entity{}})
    {
        CHECK(f.service.setAimTarget(entity, Vec3(0.0f)).error() == AnimationError::NoAnimator);
        CHECK(f.service.setAimWeight(entity, 1.0f).error() == AnimationError::NoAnimator);
        CHECK(f.service.aimAngles(entity).error() == AnimationError::NoAnimator);
    }
    f.world.destroy(aimed);
    CHECK(f.service.aimAngles(aimed).error() == AnimationError::NoAnimator);
}

TEST_CASE("an Aim with a missing joint or spine poses nothing and reports zero angles")
{
    Fixture f;
    Aim aim = headAim(Vec3(2.0f, 2.0f, 2.0f));
    aim.spine = "wing";
    const Entity entity = f.world.create(Transform{}, animator(kSkeletonA, kWalk, 0.3f), aim);
    f.animation.waitIdle();
    f.step(1);
    const AimAngles angles = *f.service.aimAngles(entity);
    CHECK(angles.yaw == 0.0f);
    CHECK(angles.pitch == 0.0f);
    f.world.get<Aim>(entity).spine = "upperChest";
    f.world.get<Aim>(entity).joint = "";
    f.step(1);
    CHECK(f.service.aimAngles(entity)->yaw == 0.0f);
}

TEST_CASE("aiming playback repeats bit for bit and a steady update allocates nothing")
{
    const auto run = [](std::vector<std::vector<SkinMatrix>>& frames) {
        Fixture f;
        REQUIRE(f.project.files.writeText("game:/aim.animgraph", kAimGraph) == FileError::None);
        std::vector<Entity> characters;
        for (u32 i = 0; i < 8; ++i)
        {
            const bool rigA = i % 2 == 0;
            Animator played =
                animator(rigA ? kSkeletonA : kSkeletonB, rigA ? kWalk : kWave, 0.1f * static_cast<f32>(i));
            played.playing = true;
            if (i % 4 == 0)
            {
                played.clip = AssetRef();
                played.graph = AssetRef("game:/aim.animgraph");
            }
            Aim aim = headAim(Vec3(-2.0f + static_cast<f32>(i), 2.0f, 3.0f));
            aim.speed = 2.0f;
            aim.weight = 0.8f;
            aim.yawParameter = "aimYaw";
            aim.pitchParameter = "aimPitch";
            const Entity character = f.world.create(Transform{}, played, aim);
            if (rigA)
            {
                TwoBoneIk ik;
                ik.joint = "leftHand";
                ik.targetJoint = "rightHand";
                ik.point = Vec3(50.0f, -10.0f, 25.0f);
                ik.alignToTarget = true;
                const Entity limb = f.world.create(Transform{}, ik);
                REQUIRE(setParent(f.world, limb, character, ParentMode::KeepLocal).hasValue());
            }
            characters.push_back(character);
        }
        f.animation.waitIdle();
        f.step(3);
        usize allocations = 0;
        {
            const test::ScopedAllocationCounter counter;
            for (u32 i = 0; i < 10; ++i)
            {
                f.animation.update(1.0f / 60.0f, &f.jobs, true);
            }
            allocations = counter.count();
        }
        CHECK(allocations == 0);
        for (const Entity entity : characters)
        {
            frames.push_back(f.skin(entity));
        }
    };
    std::vector<std::vector<SkinMatrix>> first;
    std::vector<std::vector<SkinMatrix>> second;
    run(first);
    run(second);
    REQUIRE(first.size() == second.size());
    for (usize i = 0; i < first.size(); ++i)
    {
        CHECK(first[i].size() == second[i].size());
        CHECK(std::memcmp(first[i].data(), second[i].data(), first[i].size() * sizeof(SkinMatrix)) == 0);
    }
}
