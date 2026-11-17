#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <numeric>

using namespace illusion;

namespace
{

// tests/data/gltf/rigged.gltf (see generate.py): Skeleton 0 joints after reordering are
// root 0, mid 1, side 2, tip 3, extra 4.
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

void checkTranslation(const Mat4& matrix, Vec3 expected)
{
    CHECK(matrix[3].x == doctest::Approx(expected.x));
    CHECK(matrix[3].y == doctest::Approx(expected.y));
    CHECK(matrix[3].z == doctest::Approx(expected.z));
}

u32 weightSum(const SkinVertex& vertex)
{
    return std::accumulate(vertex.weights.begin(), vertex.weights.end(), 0u);
}

} // namespace

TEST_CASE("glTF skins import with parents before children and their bind data")
{
    const ModelData model = loadRigged();
    REQUIRE(model.skins.size() == 2);
    const Skeleton& body = model.skins[0];
    CHECK(body.name == "body");
    REQUIRE(body.joints.size() == 5);
    const std::array<std::string_view, 5> names{"root", "mid", "side", "tip", "extra"};
    const std::array<i32, 5> parents{-1, 0, 0, 1, 2};
    for (usize j = 0; j < 5; ++j)
    {
        CHECK(body.joints[j].name == names[j]);
        CHECK(body.joints[j].parent == parents[j]);
    }
    CHECK(body.joints[1].translation == Vec3(0, 1, 0));
    CHECK(body.joints[2].translation == Vec3(1, 0, 0));
    CHECK(body.joints[0].rotation == Quat(1, 0, 0, 0));
    CHECK(body.joints[0].scale == Vec3(1));
    checkTranslation(body.rootTransform, Vec3(0, 0, 2)); // the Armature node above the skeleton
    checkTranslation(body.joints[0].inverseBind, Vec3(0, 0, -2));
    checkTranslation(body.joints[3].inverseBind, Vec3(0, -2, -2));
    checkTranslation(body.joints[4].inverseBind, Vec3(-2, 0, -2));

    const Skeleton& arm = model.skins[1];
    REQUIRE(arm.joints.size() == 1);
    CHECK(arm.joints[0].name == "side");
    CHECK(arm.joints[0].parent == -1);
    CHECK(arm.joints[0].inverseBind == Mat4(1.0f)); // no inverseBindMatrices
    checkTranslation(arm.rootTransform, Vec3(0, 0, 2));
}

TEST_CASE("glTF joints and weights import as a parallel stream of four normalized influences")
{
    const ModelData model = loadRigged();
    REQUIRE(model.meshes.size() == 1);
    const MeshData& mesh = model.meshes[0];
    CHECK(mesh.skeleton == 0);
    REQUIRE(mesh.skin.size() == mesh.vertices.size());
    REQUIRE(mesh.vertices.size() == 8);
    for (const SkinVertex& vertex : mesh.skin)
    {
        CHECK(weightSum(vertex) == 65535);
        CHECK(std::is_sorted(vertex.weights.rbegin(), vertex.weights.rend()));
    }
    CHECK(mesh.skin[0].joints[0] == 0);
    CHECK(mesh.skin[0].weights[0] == 65535);
    CHECK(mesh.skin[4].joints[0] == 3); // tip 0.75
    CHECK(mesh.skin[4].joints[1] == 1); // mid 0.25
    CHECK(mesh.skin[4].weights[0] == 49151);
    CHECK(mesh.skin[4].weights[2] == 0);

    // Five influences from JOINTS_0/1 and WEIGHTS_0/1 (float and unorm16): extra (0.05) is dropped and the
    // others renormalized.
    const SkinVertex& five = mesh.skin[7];
    CHECK(five.joints == std::array<u16, 4>{2, 0, 1, 3});
    CHECK(static_cast<f32>(five.weights[0]) / 65535.0f == doctest::Approx(0.4f / 0.95f).epsilon(0.001));
    CHECK(static_cast<f32>(five.weights[3]) / 65535.0f == doctest::Approx(0.1f / 0.95f).epsilon(0.001));

    REQUIRE(mesh.jointBounds.size() == 5);
    CHECK(mesh.jointBounds[4].min == Vec3(1.5f, 0.0f, 2.0f)); // extra moves only vertex 6
    CHECK(mesh.jointBounds[4].max == Vec3(1.5f, 0.0f, 2.0f));
    CHECK(mesh.jointBounds[3].min == Vec3(-0.5f, 1.0f, 2.0f));
    CHECK(mesh.jointBounds[3].max == Vec3(1.0f, 2.0f, 2.0f));
    CHECK(mesh.vertices[2].position == Vec3(-0.5f, 1.0f, 2.0f)); // the rigid data is unchanged
}

TEST_CASE("glTF animations import the joint channels sorted, with their keyframes")
{
    const ModelData model = loadRigged();
    REQUIRE(model.animations.size() == 3);

    const AnimationClip& step = model.animations[0];
    CHECK(step.name == "step");
    CHECK(step.duration == 2.0f);
    REQUIRE(step.channels.size() == 1);
    CHECK(step.channels[0].joint == "mid");
    CHECK(step.channels[0].path == AnimationPath::Translation);
    CHECK(step.channels[0].interpolation == AnimationInterpolation::Step);
    CHECK(step.channels[0].times == std::vector<f32>{0.0f, 1.0f, 2.0f});
    CHECK(step.channels[0].values[2] == Vec4(0, 3, 0, 0));

    // The channels of the non-joint node Armature and of the morph weights are skipped.
    const AnimationClip& linear = model.animations[1];
    CHECK(linear.name == "linear");
    CHECK(linear.duration == 1.5f);
    REQUIRE(linear.channels.size() == 2);
    CHECK(linear.channels[0].joint == "root");
    CHECK(linear.channels[0].path == AnimationPath::Rotation);
    CHECK(linear.channels[0].values[1].z == doctest::Approx(0.70710678));
    CHECK(linear.channels[0].values[1].w == doctest::Approx(0.70710678));
    CHECK(linear.channels[1].joint == "tip");

    const AnimationClip& cubic = model.animations[2];
    CHECK(cubic.name == "cubic");
    REQUIRE(cubic.channels.size() == 1);
    CHECK(cubic.channels[0].interpolation == AnimationInterpolation::CubicSpline);
    CHECK(cubic.channels[0].path == AnimationPath::Scale);
    REQUIRE(cubic.channels[0].values.size() == 6); // in-tangent, value, out-tangent per key
    CHECK(cubic.channels[0].values[1] == Vec4(1, 1, 1, 0));
    CHECK(cubic.channels[0].values[2] == Vec4(1, 0, 0, 0));
}

TEST_CASE("skinned models round-trip through the cooked format")
{
    const ModelData model = loadRigged();
    const std::vector<std::byte> bytes = serializeModel(model);
    const std::optional<ModelData> read = deserializeModel(bytes);
    REQUIRE(read.has_value());
    CHECK(serializeModel(*read) == bytes);
    REQUIRE(read->skins.size() == 2);
    CHECK(read->skins[0].joints[3].name == "tip");
    CHECK(read->skins[0].joints[3].inverseBind == model.skins[0].joints[3].inverseBind);
    CHECK(read->skins[0].rootTransform == model.skins[0].rootTransform);
    CHECK(read->meshes[0].skin.size() == 8);
    CHECK(read->meshes[0].skin[7].joints == model.meshes[0].skin[7].joints);
    CHECK(read->meshes[0].jointBounds[3].max == model.meshes[0].jointBounds[3].max);
    CHECK(read->meshes[0].skeleton == 0);
    REQUIRE(read->animations.size() == 3);
    CHECK(read->animations[2].channels[0].values == model.animations[2].channels[0].values);
    CHECK(read->animations[1].duration == 1.5f);
    for (usize size = 0; size < bytes.size(); size += 17)
    {
        CHECK_FALSE(deserializeModel(std::span(bytes).first(size)).has_value());
    }
}

TEST_CASE("scaleModel scales skeletons, joint bounds and translation keys with the vertices")
{
    const ModelData original = loadRigged();
    ModelData model = original;
    scaleModel(model, 2.0f);
    const Skeleton& skeleton = model.skins[0];
    CHECK(skeleton.joints[1].translation == Vec3(0, 2, 0));
    checkTranslation(skeleton.rootTransform, Vec3(0, 0, 4));
    checkTranslation(skeleton.joints[3].inverseBind, Vec3(0, -4, -4));
    CHECK(model.meshes[0].jointBounds[3].max == Vec3(2.0f, 4.0f, 4.0f));
    CHECK(model.animations[0].channels[0].values[2] == Vec4(0, 6, 0, 0));
    CHECK(model.animations[2].channels[0].values[1] == Vec4(1, 1, 1, 0)); // scale keys stay
    CHECK(model.meshes[0].skin.size() == model.meshes[0].vertices.size());
}
