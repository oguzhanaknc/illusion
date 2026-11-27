// Retargeting (ADR 0065): joints paired through rig bones, the reference pose and per-channel corrections of
// bindRetargetedClip(), bone directions and heights of a clip of one test rig played on the other and back,
// root motion scaled by the hips height, names resolved through the rig, graphs on both rigs, the
// bit-identical path of a clip on its own skin, AnimationWorld binding retargeted clips, determinism and
// allocation.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/animation/Retarget.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
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
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kSkeletonA = "game:/humanoid_a.gltf#skin/0";
constexpr std::string_view kSkeletonB = "game:/humanoid_b.gltf#skin/0";
constexpr std::string_view kWalk = "game:/humanoid_a.gltf#animation/walk";
constexpr std::string_view kWave = "game:/humanoid_b.gltf#animation/wave";
constexpr f32 kAngleTolerance = 1e-3f; // radians
constexpr f32 kHeightRatio = 0.8f;     // humanoid_b's hips (0.8 m) over humanoid_a's (1 m)

// tests/data/gltf/humanoid_a.gltf or humanoid_b.gltf (see generate.py): one skin and one clip.
ModelData loadModel(std::string_view file)
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    std::optional<ModelData> model = loadGltf(files, std::string("test:/gltf/") + std::string(file), &error);
    INFO(error);
    REQUIRE(model.has_value());
    REQUIRE(model->skins.size() == 1);
    REQUIRE(model->animations.size() == 1);
    REQUIRE(model->animations[0].skeleton == 0);
    return std::move(*model);
}

struct Rigs
{
    ModelData a = loadModel("humanoid_a.gltf");
    ModelData b = loadModel("humanoid_b.gltf");

    [[nodiscard]] const Skeleton& skeletonA() const { return a.skins[0]; }
    [[nodiscard]] const Skeleton& skeletonB() const { return b.skins[0]; }
    [[nodiscard]] const AnimationClip& walk() const { return a.animations[0]; }
    [[nodiscard]] const AnimationClip& wave() const { return b.animations[0]; }
};

SkeletonJoint named(std::string_view name)
{
    SkeletonJoint joint;
    joint.name = name;
    return joint;
}

i32 jointOf(const Skeleton& skeleton, std::string_view bone)
{
    const i32 joint = skeleton.rig.find(bone);
    REQUIRE_MESSAGE(joint >= 0, "no joint with bone ", bone);
    return joint;
}

Vec3 positionOf(std::span<const Mat4> model, i32 joint)
{
    return Vec3(model[static_cast<usize>(joint)][3]);
}

std::vector<Mat4> modelPose(const Skeleton& skeleton, std::span<const JointPose> local)
{
    std::vector<Mat4> model(skeleton.joints.size());
    localToModel(skeleton, local, model);
    return model;
}

// The model-space pose of @p clip at @p time on its own skeleton.
std::vector<Mat4> sourcePose(const AnimationClip& clip, const Skeleton& skeleton, f32 time)
{
    std::vector<JointPose> local(skeleton.joints.size());
    sampleClip(clip, bindClip(clip, skeleton), skeleton, time, local);
    return modelPose(skeleton, local);
}

std::vector<Mat4> retargetedPose(const AnimationClip& clip, const RetargetedClip& bound,
                                 const Skeleton& target, f32 time)
{
    std::vector<JointPose> local(target.joints.size());
    sampleClip(clip, bound.binding, bound.retarget, target, time, local);
    return modelPose(target, local);
}

// The bone of each joint whose direction retargeting keeps: (joint, the child it points to).
std::vector<std::pair<std::string, std::string>> bones()
{
    std::vector<std::pair<std::string, std::string>> list{{"hips", "spine"},
                                                          {"spine", "chest"},
                                                          {"chest", "upperChest"},
                                                          {"upperChest", "neck"},
                                                          {"neck", "head"}};
    for (const std::string side : {"left", "right"})
    {
        list.emplace_back(side + "Shoulder", side + "UpperArm");
        list.emplace_back(side + "UpperArm", side + "LowerArm");
        list.emplace_back(side + "LowerArm", side + "Hand");
        list.emplace_back(side + "Hand", side + "MiddleProximal");
        list.emplace_back(side + "UpperLeg", side + "LowerLeg");
        list.emplace_back(side + "LowerLeg", side + "Foot");
        list.emplace_back(side + "Foot", side + "Toes");
        for (const std::string finger : {"Thumb", "Index", "Middle", "Ring", "Little"})
        {
            list.emplace_back(side + finger + "Proximal", side + finger + "Intermediate");
            list.emplace_back(side + finger + "Intermediate", side + finger + "Distal");
        }
    }
    return list;
}

f32 angleBetween(Vec3 a, Vec3 b)
{
    const f32 cosine = glm::dot(glm::normalize(a), glm::normalize(b));
    const f32 sine = glm::length(glm::cross(glm::normalize(a), glm::normalize(b)));
    return std::atan2(sine, cosine);
}

// Checks that every bone of bones() points the same way on both poses, in model space.
void checkDirections(const Skeleton& source, std::span<const Mat4> sourceModel, const Skeleton& target,
                     std::span<const Mat4> targetModel)
{
    for (const auto& [from, to] : bones())
    {
        INFO(from, " -> ", to);
        const Vec3 sourceBone =
            positionOf(sourceModel, jointOf(source, to)) - positionOf(sourceModel, jointOf(source, from));
        const Vec3 targetBone =
            positionOf(targetModel, jointOf(target, to)) - positionOf(targetModel, jointOf(target, from));
        CHECK(angleBetween(sourceBone, targetBone) < kAngleTolerance);
    }
}

bool equalPoses(std::span<const JointPose> a, std::span<const JointPose> b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

Animator animator(std::string_view skeleton, std::string_view clip, f32 time = 0.0f)
{
    Animator result;
    result.skeleton = AssetRef(skeleton);
    result.clip = AssetRef(clip);
    result.time = time;
    return result;
}

struct Fixture
{
    Fixture() { reflectAll(); }

    [[nodiscard]] u32 slot(Entity entity) const { return world.get<AnimationPose>(entity).slot; }

    [[nodiscard]] std::vector<SkinMatrix> skin(Entity entity) const
    {
        const std::span<const SkinMatrix> matrices = animation.skinMatrices(slot(entity));
        return {matrices.begin(), matrices.end()};
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

bool equalSkins(std::span<const SkinMatrix> a, std::span<const SkinMatrix> b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

std::vector<SkinMatrix> skinOf(const Skeleton& skeleton, std::span<const Mat4> model)
{
    std::vector<SkinMatrix> skin(skeleton.joints.size());
    skinMatrices(skeleton, model, skin);
    return skin;
}

// A graph of one state playing @p clip, with root motion on the bone "hips" and an upper-body layer masked by
// bone name.
std::string graphJson(std::string_view clip)
{
    return std::string(
               R"({"format": "illusion.animgraph", "version": 1, "rootMotion": "hips", "parameters": [],
"layers": [{"name": "base", "states": [{"name": "walk", "clip": ")") +
           std::string(clip) +
           R"("}]}, {"name": "legs", "weight": 0.5, "mask": [{"joint": "leftUpperLeg"}], "states": [{"name": "walk", "clip": ")" +
           std::string(clip) + R"("}]}]})";
}

AnimationGraph parseGraph(std::string_view clip)
{
    Expected<AnimationGraph, std::string> graph = parseAnimationGraph(graphJson(clip));
    INFO((graph ? std::string() : graph.error()));
    REQUIRE(graph.hasValue());
    return std::move(*graph);
}

// Plays @p graph on @p target for @p steps steps of @p seconds and returns the root motion summed.
Vec3 rootMotion(const AnimationGraph& graph, const Skeleton& target, const AnimationClip& clip,
                const Skeleton* source, u32 steps, f32 seconds)
{
    Expected<GraphBinding, std::string> binding =
        bindGraph(graph, target, std::function<ClipSource(const AssetRef&)>([&](const AssetRef&) {
                      return ClipSource{&clip, source};
                  }));
    INFO((binding ? std::string() : binding.error()));
    REQUIRE(binding.hasValue());
    AnimationGraphState state = makeGraphState(*binding);
    LinearAllocator scratch(graphScratchBytes(*binding) + 256);
    std::vector<JointPose> pose(target.joints.size());
    Vec3 total(0.0f);
    for (u32 i = 0; i < steps; ++i)
    {
        static_cast<void>(stepGraph(*binding, state, seconds, {}));
        RootMotionDelta root;
        evaluateGraph(*binding, state, scratch, pose, &root);
        total += root.translation;
    }
    return total;
}

} // namespace

TEST_CASE("findRigJoint finds a joint by name, then by rig bone")
{
    const Rigs rigs;
    const Skeleton& b = rigs.skeletonB();
    CHECK(findRigJoint(b, "pelvis") == jointOf(b, "hips"));
    CHECK(findRigJoint(b, "hips") == jointOf(b, "hips"));
    CHECK(findRigJoint(b, "rightHand") == jointOf(b, "rightHand"));
    CHECK(findRigJoint(b, "root") == jointOf(b, "root")); // a name and a bone of the same joint
    CHECK(findRigJoint(b, "upperarm_twist_01_l") >= 0);
    CHECK(findRigJoint(b, "tail") == -1);
    CHECK(findRigJoint(b, "") == -1);
    Skeleton unmapped = b;
    unmapped.rig = {};
    CHECK(findRigJoint(unmapped, "hips") == -1);
    CHECK(findRigJoint(unmapped, "pelvis") == jointOf(b, "hips"));
}

TEST_CASE("matchJoints pairs joints by rig bone, else by name, each source joint once")
{
    const Rigs rigs;
    const Skeleton& a = rigs.skeletonA();
    const Skeleton& b = rigs.skeletonB();
    const std::vector<i32> pairs = matchJoints(a, b);
    REQUIRE(pairs.size() == b.joints.size());
    for (const std::string_view bone : kRequiredHumanoidBones)
    {
        INFO(bone);
        CHECK(pairs[static_cast<usize>(jointOf(b, bone))] == jointOf(a, bone));
    }
    CHECK(pairs[static_cast<usize>(findRigJoint(b, "upperarm_twist_01_l"))] == -1);
    CHECK(pairs[static_cast<usize>(jointOf(b, "root"))] == -1); // humanoid_a has no root joint

    // Joints without a bone pair by name; a source joint is taken once.
    Skeleton source;
    source.joints = {named("tail"), named("hips")};
    source.rig.bones = {"", "hips"};
    source.rig.translation = {RetargetTranslation::Skeleton, RetargetTranslation::Scaled};
    Skeleton target;
    target.joints = {named("pelvis"), named("tail"), named("tail")};
    target.rig.bones = {"hips", "", ""};
    target.rig.translation = {RetargetTranslation::Scaled, RetargetTranslation::Skeleton,
                              RetargetTranslation::Skeleton};
    CHECK(matchJoints(source, target) == std::vector<i32>{1, 0, -1});
}

TEST_CASE("rigHeightRatio divides the hips heights in model space")
{
    const Rigs rigs;
    CHECK(rigHeightRatio(rigs.skeletonA(), rigs.skeletonB()) == doctest::Approx(kHeightRatio));
    CHECK(rigHeightRatio(rigs.skeletonB(), rigs.skeletonA()) == doctest::Approx(1.0f / kHeightRatio));
    CHECK(rigHeightRatio(rigs.skeletonA(), rigs.skeletonA()) == doctest::Approx(1.0f));
    Skeleton noHips = rigs.skeletonB();
    noHips.rig.bones[static_cast<usize>(jointOf(noHips, "hips"))].clear();
    CHECK(rigHeightRatio(rigs.skeletonA(), noHips) == 1.0f);
    Skeleton sunk = rigs.skeletonB();
    sunk.rootTransform[3] = Vec4(0.0f, -5.0f, 0.0f, 1.0f);
    CHECK(rigHeightRatio(rigs.skeletonA(), sunk) == 1.0f);
}

TEST_CASE("bindRetargetedClip reports clips without a source skeleton, rigs or pairs")
{
    const Rigs rigs;
    AnimationClip orphan = rigs.walk();
    orphan.skeleton = -1;
    CHECK(bindRetargetedClip(orphan, rigs.skeletonA(), rigs.skeletonB()).error() ==
          RetargetError::NoSkeleton);
    Skeleton unmapped = rigs.skeletonB();
    unmapped.rig = {};
    CHECK(bindRetargetedClip(rigs.walk(), rigs.skeletonA(), unmapped).error() == RetargetError::NoRig);
    CHECK(bindRetargetedClip(rigs.walk(), unmapped, rigs.skeletonA()).error() == RetargetError::NoRig);
    AnimationClip tail = rigs.walk();
    tail.channels.resize(1);
    tail.channels[0].joint = "mixamorig:HeadTop_End"; // a joint of humanoid_a without a bone or a pair
    CHECK(bindRetargetedClip(tail, rigs.skeletonA(), rigs.skeletonB()).error() == RetargetError::NoPairs);
    CHECK(retargetErrorName(RetargetError::NoRig) == "NoRig");
    CHECK(retargetErrorName(RetargetError::NoPairs) == "NoPairs");
    CHECK(retargetErrorName(RetargetError::NoSkeleton) == "NoSkeleton");
}

TEST_CASE("a T-pose walk on the A-pose rig keeps every bone direction, the T-pose included")
{
    const Rigs rigs;
    const Expected<RetargetedClip, RetargetError> bound =
        bindRetargetedClip(rigs.walk(), rigs.skeletonA(), rigs.skeletonB());
    REQUIRE(bound.hasValue());
    CHECK(bound->retarget.active());
    CHECK(bound->binding.boundChannels == 3);
    CHECK(bound->retarget.heightRatio == doctest::Approx(kHeightRatio));
    REQUIRE(bound->retarget.reference.size() == rigs.skeletonB().joints.size());
    for (const f32 time : {0.0f, 0.2f, 0.5f, 0.75f, 1.0f})
    {
        INFO("time ", time);
        checkDirections(rigs.skeletonA(), sourcePose(rigs.walk(), rigs.skeletonA(), time), rigs.skeletonB(),
                        retargetedPose(rigs.walk(), *bound, rigs.skeletonB(), time));
    }
    // The reference pose holds the arms horizontal, as humanoid_a's rest does.
    const std::vector<Mat4> reference = modelPose(rigs.skeletonB(), bound->retarget.reference);
    const Vec3 arm = positionOf(reference, jointOf(rigs.skeletonB(), "leftLowerArm")) -
                     positionOf(reference, jointOf(rigs.skeletonB(), "leftUpperArm"));
    CHECK(angleBetween(arm, Vec3(1.0f, 0.0f, 0.0f)) < kAngleTolerance);
}

TEST_CASE("a clip of the A-pose rig on the T-pose rig keeps every bone direction")
{
    const Rigs rigs;
    const Expected<RetargetedClip, RetargetError> bound =
        bindRetargetedClip(rigs.wave(), rigs.skeletonB(), rigs.skeletonA());
    REQUIRE(bound.hasValue());
    CHECK(bound->retarget.heightRatio == doctest::Approx(1.0f / kHeightRatio));
    for (const f32 time : {0.0f, 0.3f, 1.0f})
    {
        INFO("time ", time);
        checkDirections(rigs.skeletonB(), sourcePose(rigs.wave(), rigs.skeletonB(), time), rigs.skeletonA(),
                        retargetedPose(rigs.wave(), *bound, rigs.skeletonA(), time));
    }
    // The wave raises the right arm on humanoid_a as it does on humanoid_b.
    const std::vector<Mat4> start = retargetedPose(rigs.wave(), *bound, rigs.skeletonA(), 0.0f);
    const std::vector<Mat4> end = retargetedPose(rigs.wave(), *bound, rigs.skeletonA(), 1.0f);
    const i32 upper = jointOf(rigs.skeletonA(), "rightUpperArm");
    const i32 lower = jointOf(rigs.skeletonA(), "rightLowerArm");
    CHECK(angleBetween(positionOf(start, lower) - positionOf(start, upper),
                       positionOf(end, lower) - positionOf(end, upper)) ==
          doctest::Approx(glm::radians(30.0f)).epsilon(1e-3));
}

TEST_CASE("hips and feet heights follow the hips height ratio and the leg lengths")
{
    const Rigs rigs;
    const Skeleton& a = rigs.skeletonA();
    const Skeleton& b = rigs.skeletonB();
    const Expected<RetargetedClip, RetargetError> bound = bindRetargetedClip(rigs.walk(), a, b);
    REQUIRE(bound.hasValue());
    const std::vector<Mat4> restA = sourcePose(rigs.walk(), a, 0.0f);
    const std::vector<Mat4> restB = retargetedPose(rigs.walk(), *bound, b, 0.0f);
    const auto legLength = [](std::span<const Mat4> model, const Skeleton& skeleton) {
        return glm::length(positionOf(model, jointOf(skeleton, "leftFoot")) -
                           positionOf(model, jointOf(skeleton, "leftUpperLeg")));
    };
    const f32 legRatio = legLength(restB, b) / legLength(restA, a);
    for (const f32 time : {0.0f, 0.25f, 0.5f, 1.0f})
    {
        INFO("time ", time);
        const std::vector<Mat4> source = sourcePose(rigs.walk(), a, time);
        const std::vector<Mat4> target = retargetedPose(rigs.walk(), *bound, b, time);
        const f32 hipsA = positionOf(source, jointOf(a, "hips")).y;
        const f32 hipsB = positionOf(target, jointOf(b, "hips")).y;
        CHECK(hipsB == doctest::Approx(hipsA * kHeightRatio).epsilon(1e-5));
        for (const std::string_view side : {"left", "right"})
        {
            const std::string hip = std::string(side) + "UpperLeg";
            const std::string foot = std::string(side) + "Foot";
            const Vec3 legA = positionOf(source, jointOf(a, foot)) - positionOf(source, jointOf(a, hip));
            const Vec3 legB = positionOf(target, jointOf(b, foot)) - positionOf(target, jointOf(b, hip));
            CHECK(std::abs(legB.y - legA.y * legRatio) < 1e-4f);
            CHECK(std::abs(legB.z - legA.z * legRatio) < 1e-4f);
        }
    }
    // At the hips' lowest point (time 0.5), humanoid_a's hips are 2 cm down and humanoid_b's 1.6 cm.
    CHECK(positionOf(retargetedPose(rigs.walk(), *bound, b, 0.5f), jointOf(b, "hips")).y ==
          doctest::Approx(0.784f).epsilon(1e-5));
}

TEST_CASE("joints without a pair hold the reference pose")
{
    const Rigs rigs;
    const Skeleton& b = rigs.skeletonB();
    const Expected<RetargetedClip, RetargetError> bound =
        bindRetargetedClip(rigs.walk(), rigs.skeletonA(), b);
    REQUIRE(bound.hasValue());
    const std::vector<i32> pairs = matchJoints(rigs.skeletonA(), b);
    std::vector<JointPose> local(b.joints.size());
    for (const f32 time : {0.0f, 0.4f, 0.9f})
    {
        sampleClip(rigs.walk(), bound->binding, bound->retarget, b, time, local);
        for (usize j = 0; j < b.joints.size(); ++j)
        {
            if (pairs[j] < 0)
            {
                INFO(b.joints[j].name);
                CHECK(std::memcmp(&local[j], &bound->retarget.reference[j], sizeof(JointPose)) == 0);
            }
        }
    }
    // An unpaired joint is not turned: the twist joint keeps its rest pose relative to its upper arm.
    const auto twist = static_cast<usize>(findRigJoint(b, "upperarm_twist_01_l"));
    CHECK(bound->retarget.reference[twist].rotation == b.joints[twist].rotation);
    CHECK(bound->retarget.reference[twist].translation == b.joints[twist].translation);
}

TEST_CASE("a clip on its own skin samples exactly as when bound by name")
{
    const Rigs rigs;
    const Skeleton& a = rigs.skeletonA();
    const ClipBinding binding = bindClip(rigs.walk(), a);
    const ClipRetarget none;
    CHECK_FALSE(none.active());
    std::vector<JointPose> byName(a.joints.size());
    std::vector<JointPose> overload(a.joints.size());
    for (const f32 time : {0.0f, 0.33f, 0.71f, 1.0f})
    {
        sampleClip(rigs.walk(), binding, a, time, byName);
        sampleClip(rigs.walk(), binding, none, a, time, overload);
        CHECK(equalPoses(byName, overload));
    }

    // A graph whose clips come without a source skeleton evaluates as the bindGraph() of ANIM-2.
    const AnimationGraph graph = parseGraph("game:/humanoid_a.gltf#animation/walk");
    Expected<GraphBinding, std::string> plain =
        bindGraph(graph, a, [&](const AssetRef&) -> const AnimationClip* { return &rigs.walk(); });
    Expected<GraphBinding, std::string> sourced =
        bindGraph(graph, a, std::function<ClipSource(const AssetRef&)>([&](const AssetRef&) {
                      return ClipSource{&rigs.walk(), nullptr};
                  }));
    REQUIRE(plain.hasValue());
    REQUIRE(sourced.hasValue());
    CHECK(sourced->basePose.empty());
    AnimationGraphState plainState = makeGraphState(*plain);
    AnimationGraphState sourcedState = makeGraphState(*sourced);
    LinearAllocator scratch(graphScratchBytes(*plain) + 256);
    std::vector<JointPose> plainPose(a.joints.size());
    std::vector<JointPose> sourcedPose(a.joints.size());
    for (int i = 0; i < 4; ++i)
    {
        static_cast<void>(stepGraph(*plain, plainState, 0.3f, {}));
        static_cast<void>(stepGraph(*sourced, sourcedState, 0.3f, {}));
        RootMotionDelta plainRoot;
        RootMotionDelta sourcedRoot;
        evaluateGraph(*plain, plainState, scratch, plainPose, &plainRoot);
        evaluateGraph(*sourced, sourcedState, scratch, sourcedPose, &sourcedRoot);
        CHECK(equalPoses(plainPose, sourcedPose));
        CHECK(std::memcmp(&plainRoot, &sourcedRoot, sizeof(RootMotionDelta)) == 0);
    }
}

TEST_CASE(
    "a graph with bone-name masks and root motion plays on both rigs, root motion scaled by hips height")
{
    const Rigs rigs;
    const AnimationGraph graph = parseGraph("game:/humanoid_a.gltf#animation/walk");
    for (const Skeleton* target : {&rigs.skeletonA(), &rigs.skeletonB()})
    {
        Expected<GraphBinding, std::string> binding = bindGraph(
            graph, *target, std::function<ClipSource(const AssetRef&)>([&](const AssetRef&) {
                return ClipSource{&rigs.walk(), target == &rigs.skeletonA() ? nullptr : &rigs.skeletonA()};
            }));
        REQUIRE(binding.hasValue());
        CHECK(binding->rootJoint == jointOf(*target, "hips"));
        const std::vector<f32>& mask = binding->masks.at(1);
        REQUIRE(mask.size() == target->joints.size());
        CHECK(mask[static_cast<usize>(jointOf(*target, "leftUpperLeg"))] == 1.0f);
        CHECK(mask[static_cast<usize>(jointOf(*target, "leftFoot"))] == 1.0f);
        CHECK(mask[static_cast<usize>(jointOf(*target, "rightUpperLeg"))] == 0.0f);
        CHECK(binding->clipRetargets.size() == binding->clips.size());
        CHECK(binding->clipRetargets.at(0).active() == (target == &rigs.skeletonB()));
    }
    // humanoid_a's hips move 1 m forward in the 1 s walk; humanoid_b's 0.8 m at the same cadence.
    const Vec3 onA = rootMotion(graph, rigs.skeletonA(), rigs.walk(), nullptr, 4, 0.2f);
    const Vec3 onB = rootMotion(graph, rigs.skeletonB(), rigs.walk(), &rigs.skeletonA(), 4, 0.2f);
    CHECK(onA.z == doctest::Approx(0.8f).epsilon(1e-4));
    CHECK(onB.z == doctest::Approx(0.8f * kHeightRatio).epsilon(1e-4));
    CHECK(std::abs(onB.x) < 1e-5f);
}

TEST_CASE("sampling a retargeted clip does not allocate")
{
    const Rigs rigs;
    const Expected<RetargetedClip, RetargetError> bound =
        bindRetargetedClip(rigs.walk(), rigs.skeletonA(), rigs.skeletonB());
    REQUIRE(bound.hasValue());
    std::vector<JointPose> local(rigs.skeletonB().joints.size());
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 10; ++i)
        {
            sampleClip(rigs.walk(), bound->binding, bound->retarget, rigs.skeletonB(),
                       0.1f * static_cast<f32>(i), local);
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}

TEST_CASE("AnimationWorld retargets a clip of another skin and binds a clip of its own skin by name")
{
    Fixture f;
    const Entity own = f.world.create(Transform{}, animator(kSkeletonA, kWalk, 0.5f));
    const Entity other = f.world.create(Transform{}, animator(kSkeletonB, kWalk, 0.5f));
    const Entity back = f.world.create(Transform{}, animator(kSkeletonA, kWave, 0.5f));
    f.animation.waitIdle();
    CHECK(f.animation.statistics().failures == 0);
    const Rigs rigs;

    CHECK(equalSkins(f.skin(own), skinOf(rigs.skeletonA(), sourcePose(rigs.walk(), rigs.skeletonA(), 0.5f))));
    const Expected<RetargetedClip, RetargetError> walkOnB =
        bindRetargetedClip(rigs.walk(), rigs.skeletonA(), rigs.skeletonB());
    REQUIRE(walkOnB.hasValue());
    CHECK(equalSkins(f.skin(other), skinOf(rigs.skeletonB(),
                                           retargetedPose(rigs.walk(), *walkOnB, rigs.skeletonB(), 0.5f))));
    const Expected<RetargetedClip, RetargetError> waveOnA =
        bindRetargetedClip(rigs.wave(), rigs.skeletonB(), rigs.skeletonA());
    REQUIRE(waveOnA.hasValue());
    CHECK(equalSkins(f.skin(back), skinOf(rigs.skeletonA(),
                                          retargetedPose(rigs.wave(), *waveOnA, rigs.skeletonA(), 0.5f))));
}

TEST_CASE("sockets and jointTransform() take bone names on every mapped rig")
{
    Fixture f;
    const Entity character = f.world.create(Transform{}, animator(kSkeletonB, kWalk, 0.25f));
    BoneAttachment socket;
    socket.joint = "rightHand";
    const Entity hand = f.world.create(Transform{}, socket);
    REQUIRE(setParent(f.world, hand, character, ParentMode::KeepLocal).hasValue());
    f.animation.waitIdle();
    const u32 slot = f.slot(character);
    const Skeleton* skeleton = f.animation.skeleton(slot);
    REQUIRE(skeleton != nullptr);
    const auto handJoint = static_cast<usize>(findRigJoint(*skeleton, "hand_r"));
    const Mat4 joint = f.animation.jointMatrices(slot)[handJoint];
    CHECK(glm::length(f.world.get<LocalTransform>(hand).position - Vec3(joint[3])) < 1e-5f);
    const Expected<Mat4, AnimationError> byBone = f.service.jointTransform(character, "rightHand");
    const Expected<Mat4, AnimationError> byName = f.service.jointTransform(character, "hand_r");
    REQUIRE(byBone.hasValue());
    REQUIRE(byName.hasValue());
    CHECK(std::memcmp(&*byBone, &*byName, sizeof(Mat4)) == 0);
    CHECK(f.service.jointTransform(character, "tail").error() == AnimationError::UnknownName);
}

TEST_CASE("retargeted playback repeats bit for bit and a steady update allocates nothing")
{
    const auto run = [](std::vector<std::vector<SkinMatrix>>& frames) {
        Fixture f;
        std::vector<Entity> characters;
        for (u32 i = 0; i < 8; ++i)
        {
            characters.push_back(f.world.create(Transform{}, animator(i % 2 == 0 ? kSkeletonB : kSkeletonA,
                                                                      i % 2 == 0 ? kWalk : kWave,
                                                                      0.1f * static_cast<f32>(i))));
        }
        f.animation.waitIdle();
        for (int i = 0; i < 3; ++i)
        {
            f.animation.update(1.0f / 60.0f, &f.jobs, true);
        }
        usize allocations = 0;
        {
            const test::ScopedAllocationCounter counter;
            for (int i = 0; i < 10; ++i)
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
        CHECK(equalSkins(first[i], second[i]));
    }
}
