#include "TestSupport.hpp"

#include <illusion/animation/Pose.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace illusion;

namespace
{

ModelData loadRigged()
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    std::optional<ModelData> model = loadGltf(files, "test:/gltf/rigged.gltf", &error);
    INFO(error);
    REQUIRE(model.has_value());
    return std::move(*model);
}

const AnimationClip& clipNamed(const ModelData& model, std::string_view name)
{
    const auto found = std::find_if(model.animations.begin(), model.animations.end(),
                                    [&](const AnimationClip& clip) { return clip.name == name; });
    REQUIRE(found != model.animations.end());
    return *found;
}

// The pose of @p clip at @p time on @p skeleton.
std::vector<JointPose> sample(const AnimationClip& clip, const Skeleton& skeleton, f32 time)
{
    std::vector<JointPose> pose(skeleton.joints.size());
    sampleClip(clip, bindClip(clip, skeleton), skeleton, time, pose);
    return pose;
}

void checkVec(Vec3 actual, Vec3 expected)
{
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-5));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(1e-5));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-5));
}

// A one-channel rotation clip on a one-joint skeleton.
AnimationClip rotationClip(Vec4 from, Vec4 to)
{
    AnimationClip clip;
    clip.name = "turn";
    clip.duration = 1.0f;
    clip.channels.push_back(AnimationChannel{
        "hip", AnimationPath::Rotation, AnimationInterpolation::Linear, {0.0f, 1.0f}, {from, to}});
    return clip;
}

Skeleton oneJoint()
{
    Skeleton skeleton;
    skeleton.joints.resize(1);
    skeleton.joints.at(0).name = "hip";
    return skeleton;
}

// Skin matrices of @p pose.
std::vector<SkinMatrix> skin(const Skeleton& skeleton, std::span<const JointPose> pose)
{
    std::vector<Mat4> model(skeleton.joints.size());
    std::vector<SkinMatrix> matrices(skeleton.joints.size());
    localToModel(skeleton, pose, model);
    skinMatrices(skeleton, model, matrices);
    return matrices;
}

// Linear blend skinning of vertex @p v on the CPU.
Vec3 skinVertex(const MeshData& mesh, std::span<const SkinMatrix> matrices, usize v)
{
    const Vec4 position(mesh.vertices[v].position, 1.0f);
    Vec3 result(0.0f);
    for (u32 i = 0; i < kMaxJointInfluences; ++i)
    {
        const f32 weight = static_cast<f32>(mesh.skin[v].weights[i]) / 65535.0f;
        const SkinMatrix& matrix = matrices[mesh.skin[v].joints[i]];
        result += weight * Vec3(glm::dot(matrix.rows[0], position), glm::dot(matrix.rows[1], position),
                                glm::dot(matrix.rows[2], position));
    }
    return result;
}

} // namespace

TEST_CASE("the rest pose skins every vertex onto itself")
{
    const ModelData model = loadRigged();
    const Skeleton& skeleton = model.skins[0];
    std::vector<JointPose> pose(skeleton.joints.size());
    restPose(skeleton, pose);
    const std::vector<SkinMatrix> matrices = skin(skeleton, pose);
    const MeshData& mesh = model.meshes[0];
    for (usize v = 0; v < mesh.vertices.size(); ++v)
    {
        checkVec(skinVertex(mesh, matrices, v), mesh.vertices[v].position);
    }
}

TEST_CASE("Step channels hold each key until the next and clamp outside the clip")
{
    const ModelData model = loadRigged();
    const AnimationClip& step = clipNamed(model, "step");
    const Skeleton& skeleton = model.skins[0];
    CHECK(sample(step, skeleton, 0.0f)[1].translation == Vec3(0, 1, 0));
    CHECK(sample(step, skeleton, 0.99f)[1].translation == Vec3(0, 1, 0));
    CHECK(sample(step, skeleton, 1.0f)[1].translation == Vec3(0, 2, 0));
    CHECK(sample(step, skeleton, 1.5f)[1].translation == Vec3(0, 2, 0));
    CHECK(sample(step, skeleton, -3.0f)[1].translation == Vec3(0, 1, 0));
    CHECK(sample(step, skeleton, 9.0f)[1].translation == Vec3(0, 3, 0));
    CHECK(sample(step, skeleton, 0.5f)[2].translation == Vec3(1, 0, 0)); // unanimated joints rest
}

TEST_CASE("Linear channels interpolate translations and slerp rotations")
{
    const ModelData model = loadRigged();
    const AnimationClip& linear = clipNamed(model, "linear");
    const Skeleton& skeleton = model.skins[0];
    const std::vector<JointPose> half = sample(linear, skeleton, 0.5f);
    const f32 angle = std::numbers::pi_v<f32> / 8.0f; // half of 45 degrees
    CHECK(half[0].rotation.z == doctest::Approx(std::sin(angle)));
    CHECK(half[0].rotation.w == doctest::Approx(std::cos(angle)));
    checkVec(half[3].translation, Vec3(0, 1, 0)); // tip starts at 0.5
    checkVec(sample(linear, skeleton, 1.0f)[3].translation, Vec3(0, 2, 0));
    checkVec(sample(linear, skeleton, 1.25f)[3].translation, Vec3(0, 2.5f, 0));
    checkVec(sample(linear, skeleton, 2.0f)[3].translation, Vec3(0, 3, 0));
    const Quat end = sample(linear, skeleton, 1.0f)[0].rotation;
    checkVec(end * Vec3(1, 0, 0), Vec3(0, 1, 0));
}

TEST_CASE("rotations interpolate along the shorter arc")
{
    const Skeleton skeleton = oneJoint();
    const f32 s = std::sqrt(0.5f);
    // The second key is the negated quaternion of +90 degrees about z: the same rotation.
    const AnimationClip clip = rotationClip(Vec4(0, 0, 0, 1), Vec4(0, 0, -s, -s));
    const Quat half = sample(clip, skeleton, 0.5f)[0].rotation;
    checkVec(half * Vec3(1, 0, 0), Vec3(s, s, 0));
    CHECK(glm::length(Vec4(half.x, half.y, half.z, half.w)) == doctest::Approx(1.0f));

    const AnimationClip nearly = rotationClip(Vec4(0, 0, 0, 1), Vec4(0, 0, 0.001f, 1));
    const Quat small = sample(nearly, skeleton, 0.5f)[0].rotation;
    CHECK(glm::length(Vec4(small.x, small.y, small.z, small.w)) == doctest::Approx(1.0f));
}

TEST_CASE("CubicSpline channels follow the glTF Hermite formula")
{
    const ModelData model = loadRigged();
    const AnimationClip& cubic = clipNamed(model, "cubic");
    const Skeleton& skeleton = model.skins[0];
    // Keys at 0 and 2 (delta 2): p0 (1,1,1), out-tangent (1,0,0); p1 (2,2,2), in-tangent (0,1,0).
    // At t = 0.5: 0.5 p0 + 0.125 * 2 * out0 + 0.5 p1 - 0.125 * 2 * in1.
    checkVec(sample(cubic, skeleton, 1.0f)[2].scale, Vec3(1.75f, 1.25f, 1.5f));
    // At t = 0.25: h00 = 0.84375, h10 = 0.140625, h01 = 0.15625, h11 = -0.046875.
    checkVec(sample(cubic, skeleton, 0.5f)[2].scale,
             Vec3(0.84375f + 0.28125f + 0.3125f, 0.84375f + 0.3125f - 0.09375f, 0.84375f + 0.3125f));
    checkVec(sample(cubic, skeleton, 0.0f)[2].scale, Vec3(1, 1, 1)); // the value, not a tangent
    checkVec(sample(cubic, skeleton, 5.0f)[2].scale, Vec3(2, 2, 2));
}

TEST_CASE("clip time loops or clamps")
{
    AnimationClip clip;
    clip.duration = 2.0f;
    CHECK(clipTime(clip, 0.5f, true) == 0.5f);
    CHECK(clipTime(clip, 2.5f, true) == doctest::Approx(0.5f));
    CHECK(clipTime(clip, 7.0f, true) == doctest::Approx(1.0f));
    CHECK(clipTime(clip, -0.5f, true) == doctest::Approx(1.5f));
    CHECK(clipTime(clip, 2.5f, false) == 2.0f);
    CHECK(clipTime(clip, -1.0f, false) == 0.0f);
    CHECK(clipTime(clip, std::numeric_limits<f32>::quiet_NaN(), true) == 0.0f);
    clip.duration = 0.0f;
    CHECK(clipTime(clip, 3.0f, true) == 0.0f);
}

TEST_CASE("a clip binds by joint name and skips channels whose joint the skeleton lacks")
{
    const ModelData model = loadRigged();
    const AnimationClip& linear = clipNamed(model, "linear");
    const ClipBinding full = bindClip(linear, model.skins[0]);
    CHECK(full.boundChannels == 2);
    CHECK(full.joints == std::vector<i32>{0, 3});

    const Skeleton& arm = model.skins[1]; // only "side"
    const ClipBinding none = bindClip(linear, arm);
    CHECK(none.boundChannels == 0);
    CHECK(none.joints == std::vector<i32>{-1, -1});
    const std::vector<JointPose> pose = sample(linear, arm, 0.75f);
    CHECK(pose[0].translation == Vec3(1, 0, 0)); // rest pose
    CHECK(bindClip(clipNamed(model, "cubic"), arm).boundChannels == 1);

    AnimationClip broken = clipNamed(model, "step");
    broken.channels[0].values.pop_back();
    CHECK(bindClip(broken, model.skins[0]).boundChannels == 0);
}

TEST_CASE("localToModel composes parents before children from the root transform")
{
    const ModelData model = loadRigged();
    const Skeleton& skeleton = model.skins[0];
    const std::vector<JointPose> pose = sample(clipNamed(model, "linear"), skeleton, 1.0f); // root turned 90
    std::vector<Mat4> matrices(skeleton.joints.size());
    localToModel(skeleton, pose, matrices);
    checkVec(Vec3(matrices[0][3]), Vec3(0, 0, 2));
    checkVec(Vec3(matrices[1][3]), Vec3(-1, 0, 2)); // mid: (0,1,0) turned about z
    checkVec(Vec3(matrices[3][3]), Vec3(-3, 0, 2)); // tip: translation (0,2,0) at t = 1
    checkVec(Vec3(matrices[4][3]), Vec3(0, 2, 2));  // extra: side (1,0,0) + (1,0,0), turned
}

TEST_CASE("skinned bounds contain every CPU-skinned vertex in several poses")
{
    const ModelData model = loadRigged();
    const Skeleton& skeleton = model.skins[0];
    const MeshData& mesh = model.meshes[0];
    std::vector<std::vector<JointPose>> poses;
    for (const AnimationClip& clip : model.animations)
    {
        for (const f32 time : {0.0f, 0.3f, 0.75f, 1.3f, 2.0f})
        {
            poses.push_back(sample(clip, skeleton, time));
        }
    }
    u32 state = 7u;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<f32>(state >> 8) / static_cast<f32>(1u << 24) * 2.0f - 1.0f;
    };
    for (int i = 0; i < 8; ++i)
    {
        std::vector<JointPose> pose(skeleton.joints.size());
        for (JointPose& joint : pose)
        {
            joint.translation = Vec3(next(), next(), next());
            joint.rotation = glm::normalize(Quat(next(), next(), next(), next() + 1.5f));
            joint.scale = Vec3(1.0f + 0.5f * next());
        }
        poses.push_back(pose);
    }
    for (const std::vector<JointPose>& pose : poses)
    {
        const std::vector<SkinMatrix> matrices = skin(skeleton, pose);
        const Aabb bounds = skinnedBounds(mesh.jointBounds, matrices);
        REQUIRE(bounds.isValid());
        for (usize v = 0; v < mesh.vertices.size(); ++v)
        {
            const Vec3 p = skinVertex(mesh, matrices, v);
            CHECK(glm::all(glm::greaterThanEqual(p, bounds.min - Vec3(1e-4f))));
            CHECK(glm::all(glm::lessThanEqual(p, bounds.max + Vec3(1e-4f))));
        }
    }
    std::vector<Aabb> empty(skeleton.joints.size());
    CHECK_FALSE(skinnedBounds(empty, skin(skeleton, poses[0])).isValid());
}

TEST_CASE("a scaled model skins to scaled positions")
{
    const ModelData original = loadRigged();
    ModelData scaled = original;
    scaleModel(scaled, 2.0f);
    for (const AnimationClip& clip : original.animations)
    {
        const std::vector<SkinMatrix> a = skin(original.skins[0], sample(clip, original.skins[0], 0.8f));
        const std::vector<SkinMatrix> b =
            skin(scaled.skins[0], sample(clipNamed(scaled, clip.name), scaled.skins[0], 0.8f));
        for (usize v = 0; v < original.meshes[0].vertices.size(); ++v)
        {
            checkVec(skinVertex(scaled.meshes[0], b, v), skinVertex(original.meshes[0], a, v) * 2.0f);
        }
    }
}

TEST_CASE("sampling, posing and skinning do not allocate")
{
    const ModelData model = loadRigged();
    const Skeleton& skeleton = model.skins[0];
    const AnimationClip& clip = clipNamed(model, "linear");
    const ClipBinding binding = bindClip(clip, skeleton);
    std::vector<JointPose> pose(skeleton.joints.size());
    std::vector<Mat4> matrices(skeleton.joints.size());
    std::vector<SkinMatrix> skinning(skeleton.joints.size());
    Aabb bounds;
    {
        const test::ScopedAllocationCounter allocations;
        for (const f32 time : {0.2f, 0.9f, 3.1f})
        {
            sampleClip(clip, binding, skeleton, clipTime(clip, time, true), pose);
            localToModel(skeleton, pose, matrices);
            skinMatrices(skeleton, matrices, skinning);
            bounds.expand(skinnedBounds(model.meshes[0].jointBounds, skinning));
        }
        CHECK(allocations.count() == 0);
    }
    CHECK(bounds.isValid());
}
