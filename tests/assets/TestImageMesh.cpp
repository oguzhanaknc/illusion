#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/AssetId.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <string_view>

using namespace illusion;

TEST_CASE("full mip counts")
{
    CHECK(fullMipCount(1, 1) == 1);
    CHECK(fullMipCount(2, 2) == 2);
    CHECK(fullMipCount(256, 256) == 9);
    CHECK(fullMipCount(300, 20) == 9); // 300 -> 150 -> 75 -> 37 -> 18 -> 9 -> 4 -> 2 -> 1
}

TEST_CASE("solid images and mip chains keep their colour")
{
    Image image = makeSolidImage(8, 4, Vec4(1.0f, 0.5f, 0.0f, 1.0f), ImageFormat::RGBA8);
    generateMips(image);
    REQUIRE(image.mips.size() == 4); // 8x4, 4x2, 2x1, 1x1
    CHECK(image.mips[3].width == 1);
    CHECK(image.mips[3].height == 1);
    const auto last = image.mip(3);
    CHECK(static_cast<u8>(last[0]) == 255);
    CHECK(static_cast<u8>(last[1]) == 128);
    CHECK(static_cast<u8>(last[2]) == 0);
    usize expected = 0;
    for (const ImageMip& mip : image.mips)
    {
        CHECK(mip.offset == expected);
        expected += mip.size;
    }
    CHECK(image.data.size() == expected);
}

TEST_CASE("sRGB mips are averaged in linear space")
{
    // Black and white pixels average to linear 0.5, which is sRGB 188, not 128.
    Image image = makeSolidImage(2, 1, Vec4(0.0f), ImageFormat::RGBA8Srgb);
    auto* pixels = reinterpret_cast<u8*>(image.data.data());
    std::memset(pixels + 4, 255, 4);
    generateMips(image);
    REQUIRE(image.mips.size() == 2);
    CHECK(static_cast<u8>(image.mip(1)[0]) == 188);

    Image linear = makeSolidImage(2, 1, Vec4(0.0f), ImageFormat::RGBA8);
    std::memset(reinterpret_cast<u8*>(linear.data.data()) + 4, 255, 4);
    generateMips(linear);
    CHECK(static_cast<u8>(linear.mip(1)[0]) == 128);
}

TEST_CASE("float images generate mips")
{
    Image image = makeSolidImage(4, 4, Vec4(2.0f, 4.0f, 8.0f, 1.0f), ImageFormat::RGBA32F);
    generateMips(image);
    REQUIRE(image.mips.size() == 3);
    f32 pixel[4];
    std::memcpy(pixel, image.mip(2).data(), sizeof(pixel));
    CHECK(pixel[2] == doctest::Approx(8.0));
}

TEST_CASE("PNG encoding round-trips through decodeImage")
{
    Image image = makeSolidImage(3, 2, Vec4(0.2f, 0.4f, 0.6f, 1.0f), ImageFormat::RGBA8);
    const std::vector<std::byte> png = encodePng(image);
    REQUIRE_FALSE(png.empty());
    const auto decoded = decodeImage(png, false);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width() == 3);
    CHECK(decoded->height() == 2);
    CHECK(decoded->data == image.data);
    CHECK(encodePng(makeSolidImage(1, 1, Vec4(1.0f), ImageFormat::RGBA32F)).empty());
}

TEST_CASE("decodeImage rejects garbage")
{
    const std::byte garbage[] = {std::byte{1}, std::byte{2}, std::byte{3}};
    std::string error;
    CHECK_FALSE(decodeImage(garbage, false, &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(decodeImage({}, false).has_value());
}

TEST_CASE("procedural meshes are closed, consistent and bounded")
{
    for (const MeshData& mesh : {makeCube(2.0f), makeSphere(1.0f, 16, 8), makePlane(4.0f, 2)})
    {
        REQUIRE(mesh.submeshes.size() == 1);
        CHECK(mesh.indices.size() % 3 == 0);
        CHECK(mesh.submeshes[0].indexCount == mesh.indices.size());
        for (const u32 index : mesh.indices)
        {
            CHECK(index < mesh.vertices.size());
        }
        for (const Vertex& v : mesh.vertices)
        {
            CHECK(glm::length(v.normal) == doctest::Approx(1.0));
            CHECK(v.tangent.w != 0.0f);
        }
        // Counter-clockwise winding: each face normal agrees with its vertex normals.
        for (usize i = 0; i < mesh.indices.size(); i += 3)
        {
            const Vertex& a = mesh.vertices[mesh.indices[i]];
            const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
            const Vec3 face = glm::cross(b.position - a.position, c.position - a.position);
            if (glm::length(face) > 1e-6f)
            {
                CHECK(glm::dot(face, a.normal + b.normal + c.normal) > 0.0f);
            }
        }
    }
    CHECK(makeCube(2.0f).bounds.max == Vec3(1.0f));
    CHECK(makeSphere(1.0f).bounds.max.y == doctest::Approx(1.0));
    CHECK(makePlane(4.0f).bounds.min.x == doctest::Approx(-2.0));
}

TEST_CASE("capsules and cylinders are closed, face outwards and match their collider sizes")
{
    for (const MeshData& mesh : {makeCapsule(0.5f, 1.0f, 12, 6), makeCylinder(0.5f, 1.0f, 12)})
    {
        REQUIRE(mesh.submeshes.size() == 1);
        CHECK(mesh.indices.size() % 3 == 0);
        CHECK(mesh.submeshes[0].indexCount == mesh.indices.size());
        for (usize i = 0; i < mesh.indices.size(); i += 3)
        {
            REQUIRE(mesh.indices[i + 2] < mesh.vertices.size());
            const Vertex& a = mesh.vertices[mesh.indices[i]];
            const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
            const Vec3 face = glm::cross(b.position - a.position, c.position - a.position);
            if (glm::length(face) > 1e-6f)
            {
                CHECK(glm::dot(face, a.normal + b.normal + c.normal) > 0.0f);
                // Closed around the origin: every face points away from the centre.
                CHECK(glm::dot(face, a.position + b.position + c.position) > 0.0f);
            }
        }
        CHECK(mesh.bounds.max.x == doctest::Approx(0.5));
    }
    CHECK(makeCapsule(0.5f, 1.0f).bounds.max.y == doctest::Approx(1.5));
    CHECK(makeCylinder(0.5f, 1.0f).bounds.min.y == doctest::Approx(-1.0));
}

TEST_CASE("built-in meshes are made without files")
{
    for (const std::string_view path : builtinAssetPaths())
    {
        CHECK(makeBuiltinMesh(path).has_value() == path.starts_with("builtin:/mesh/"));
    }
    CHECK(makeBuiltinMesh("builtin:/mesh/cube")->vertices.size() == makeCube().vertices.size());
    CHECK_FALSE(makeBuiltinMesh("builtin:/mesh/torus").has_value());
    CHECK_FALSE(makeBuiltinMesh("game:/mesh/cube").has_value());
}

TEST_CASE("computeNormals produces face normals")
{
    MeshData mesh;
    mesh.vertices.resize(3);
    mesh.vertices[1].position = Vec3(1, 0, 0);
    mesh.vertices[2].position = Vec3(0, 0, -1);
    mesh.indices = {0, 1, 2};
    computeNormals(mesh);
    CHECK(mesh.vertices[0].normal.y == doctest::Approx(1.0));
}
