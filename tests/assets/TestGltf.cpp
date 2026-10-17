#include <illusion/assets/Model.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

using namespace illusion;

namespace
{

void mountTestData(VirtualFileSystem& files)
{
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
}

void checkQuadModel(const ModelData& model)
{
    REQUIRE(model.meshes.size() == 1);
    const MeshData& mesh = model.meshes[0];
    CHECK(mesh.vertices.size() == 4);
    CHECK(mesh.indices.size() == 6);
    REQUIRE(mesh.submeshes.size() == 1);
    CHECK(mesh.submeshes[0].material == 0);
    CHECK(mesh.vertices[2].position == Vec3(1, 1, 0));
    CHECK(mesh.vertices[0].normal == Vec3(0, 0, 1));
    CHECK(mesh.vertices[0].uv == Vec2(0, 1));
    CHECK(mesh.bounds.min == Vec3(-1, -1, 0));
    CHECK(mesh.bounds.max == Vec3(1, 1, 0));

    REQUIRE(model.materials.size() == 1);
    const MaterialData& material = model.materials[0];
    CHECK(material.name == "checker");
    CHECK(material.baseColorFactor == Vec4(0.5f, 1.0f, 1.0f, 1.0f));
    CHECK(material.metallicFactor == doctest::Approx(0.25));
    CHECK(material.roughnessFactor == doctest::Approx(0.75));
    CHECK(material.emissiveFactor.z == doctest::Approx(0.3));
    CHECK(material.alphaMode == AlphaMode::Mask);
    CHECK(material.alphaCutoff == doctest::Approx(0.3));
    CHECK(material.doubleSided);
    REQUIRE(material.baseColorTexture.isSet());
    CHECK(material.baseColorTexture.wrapU == TextureWrap::ClampToEdge);
    CHECK(material.baseColorTexture.wrapV == TextureWrap::MirrorRepeat);
    CHECK_FALSE(material.normalTexture.isSet());

    REQUIRE(model.images.size() == 1);
    const Image& image = model.images[0];
    CHECK(image.format == ImageFormat::RGBA8Srgb); // used as base colour
    CHECK(image.width() == 2);
    CHECK(image.mips.size() == 2); // 2x2 and 1x1
    const auto pixels = image.mip(0);
    CHECK(static_cast<u8>(pixels[0]) == 255); // red top-left
    CHECK(static_cast<u8>(pixels[1]) == 0);

    REQUIRE(model.nodes.size() == 2);
    CHECK(model.nodes[0].name == "root");
    CHECK(model.nodes[1].parent == 0);
    CHECK(model.nodes[1].mesh == 0);
    const std::vector<Mat4> world = model.worldMatrices();
    CHECK(Vec3(world[1][3]) == Vec3(1, 2, 3));
    CHECK(world[1][0][0] == doctest::Approx(2.0));

    const Aabb bounds = model.bounds();
    CHECK(bounds.min.x == doctest::Approx(-1.0)); // 1 + 2 * -1
    CHECK(bounds.max.y == doctest::Approx(4.0));  // 2 + 2 * 1
}

} // namespace

TEST_CASE("glTF with an external buffer and a relative image path loads")
{
    VirtualFileSystem files;
    mountTestData(files);
    std::string error;
    const auto model = loadGltf(files, "test:/gltf/scene/two_nodes.gltf", &error);
    REQUIRE_MESSAGE(model.has_value(), error);
    checkQuadModel(*model);
}

TEST_CASE("binary glTF with an embedded image loads")
{
    VirtualFileSystem files;
    mountTestData(files);
    std::string error;
    const auto model = loadGltf(files, "test:/gltf/binary.glb", &error);
    REQUIRE_MESSAGE(model.has_value(), error);
    checkQuadModel(*model);
}

TEST_CASE("glTF with a data URI buffer and missing normals loads")
{
    VirtualFileSystem files;
    mountTestData(files);
    std::string error;
    const auto model = loadGltf(files, "test:/gltf/embedded.gltf", &error);
    REQUIRE_MESSAGE(model.has_value(), error);
    REQUIRE(model->meshes.size() == 1);
    const MeshData& mesh = model->meshes[0];
    CHECK(mesh.indices == std::vector<u32>{0, 1, 2});
    CHECK(mesh.submeshes[0].material == -1);
    for (const Vertex& vertex : mesh.vertices)
    {
        CHECK(vertex.normal.z == doctest::Approx(1.0)); // computed face normal
        CHECK(vertex.tangent.w == 0.0f);                // no tangents
    }
    CHECK(model->materials.empty());
}

TEST_CASE("glTF errors are reported instead of crashing")
{
    VirtualFileSystem files;
    mountTestData(files);
    std::string error;
    CHECK_FALSE(loadGltf(files, "test:/gltf/missing.gltf", &error).has_value());
    CHECK(error.find("NotFound") != std::string::npos);
    CHECK_FALSE(loadGltf(files, "test:/gltf/unsupported.gltf", &error).has_value());
    CHECK(error.find("KHR_draco_mesh_compression") != std::string::npos);
    CHECK_FALSE(loadGltf(files, "test:/gltf/textures/checker.png", &error).has_value());
    // Regression: a padding-only base64 image used to underflow the decoded size.
    CHECK_FALSE(loadGltf(files, "test:/gltf/scene/bad_image_uri.gltf", &error).has_value());
    CHECK(error.find("base64") != std::string::npos);
}
