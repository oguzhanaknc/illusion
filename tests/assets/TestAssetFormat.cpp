#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cstring>

using namespace illusion;

namespace
{

ModelData loadTestModel()
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    std::optional<ModelData> model = loadGltf(files, "test:/gltf/scene/two_nodes.gltf", &error);
    INFO(error);
    REQUIRE(model.has_value());
    return std::move(*model);
}

} // namespace

TEST_CASE("asset ids are stable hashes of normalized paths")
{
    CHECK(AssetId::fromPath("game:/textures/rock.png") ==
          AssetId::fromPath("game:/models/../textures/./rock.png"));
    CHECK(AssetId::fromPath("game:/a.png") != AssetId::fromPath("game:/b.png"));
    CHECK(static_cast<bool>(AssetId::fromPath("game:/a.png")));
    CHECK_FALSE(static_cast<bool>(AssetId::fromPath("not a path")));
    CHECK_FALSE(static_cast<bool>(AssetId::fromPath("game:/../escape.png")));
}

TEST_CASE("asset files round-trip and reject corruption")
{
    AssetFile file;
    file.type = AssetType::Model;
    file.version = 7;
    file.dependencies = {{"game:/a.gltf", 11}, {"game:/a.bin", 22}};
    file.payload = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::vector<std::byte> bytes = writeAssetFile(file);

    std::string error;
    const std::optional<AssetFile> read = readAssetFile(bytes, &error);
    REQUIRE(read.has_value());
    CHECK(read->type == AssetType::Model);
    CHECK(read->version == 7);
    CHECK(read->dependencies == file.dependencies);
    CHECK(read->payload == file.payload);

    std::vector<std::byte> corrupted = bytes;
    corrupted.back() ^= std::byte{0xFF};
    CHECK_FALSE(readAssetFile(corrupted, &error).has_value());
    CHECK(error.find("checksum") != std::string::npos);
    corrupted = bytes;
    corrupted[0] = std::byte{0};
    CHECK_FALSE(readAssetFile(corrupted, &error).has_value());
    CHECK(error.find("not an asset") != std::string::npos);
    for (usize size = 0; size < bytes.size(); ++size)
    {
        CHECK_FALSE(readAssetFile(std::span(bytes).first(size)).has_value());
    }
}

TEST_CASE("cooked images round-trip with their mips and reject inconsistent data")
{
    Image image = makeSolidImage(8, 4, Vec4(0.25f, 0.5f, 1.0f, 1.0f), ImageFormat::RGBA32F);
    generateMips(image);
    const std::vector<std::byte> bytes = serializeImage(image);
    const std::optional<Image> read = deserializeImage(bytes);
    REQUIRE(read.has_value());
    CHECK(read->format == ImageFormat::RGBA32F);
    CHECK(read->mips.size() == image.mips.size());
    CHECK(read->data == image.data);

    Image broken = image;
    broken.mips.back().offset = broken.data.size(); // points past the pixels
    CHECK_FALSE(deserializeImage(serializeImage(broken)).has_value());
    for (usize size = 0; size < bytes.size(); size += 7)
    {
        CHECK_FALSE(deserializeImage(std::span(bytes).first(size)).has_value());
    }
}

TEST_CASE("cooked models round-trip exactly and deterministically")
{
    const ModelData model = loadTestModel();
    const std::vector<std::byte> bytes = serializeModel(model);
    CHECK(serializeModel(model) == bytes); // no padding or uninitialized bytes reach the file
    const std::optional<ModelData> read = deserializeModel(bytes);
    REQUIRE(read.has_value());
    CHECK(serializeModel(*read) == bytes);
    REQUIRE(read->meshes.size() == model.meshes.size());
    CHECK(read->meshes[0].indices == model.meshes[0].indices);
    CHECK(read->materials[0].name == model.materials[0].name);
    CHECK(read->materials[0].baseColorTexture.image == model.materials[0].baseColorTexture.image);
    CHECK(read->nodes[1].parent == 0);
    CHECK(read->images[0].data == model.images[0].data);

    // Every truncation fails cleanly.
    for (usize size = 0; size < bytes.size(); size += 13)
    {
        CHECK_FALSE(deserializeModel(std::span(bytes).first(size)).has_value());
    }
}

TEST_CASE("cooked models with out-of-range indices are rejected")
{
    ModelData model;
    model.meshes.push_back(makeCube());
    CHECK(deserializeModel(serializeModel(model)).has_value());

    ModelData badIndex = model;
    badIndex.meshes[0].indices[0] = 100000;
    CHECK_FALSE(deserializeModel(serializeModel(badIndex)).has_value());

    ModelData badMaterial = model;
    badMaterial.meshes[0].submeshes[0].material = 3; // no materials
    CHECK_FALSE(deserializeModel(serializeModel(badMaterial)).has_value());

    ModelData badNode = model;
    badNode.nodes.push_back(ModelNode{"child", Mat4(1.0f), 0, 0}); // parent after child
    CHECK_FALSE(deserializeModel(serializeModel(badNode)).has_value());

    ModelData badTexture = model;
    badTexture.materials.push_back(MaterialData{});
    badTexture.materials[0].normalTexture.image = 2; // no images
    CHECK_FALSE(deserializeModel(serializeModel(badTexture)).has_value());
}

TEST_CASE("baked environments round-trip and invalid ones are rejected")
{
    BakedEnvironment environment;
    environment.backgroundSize = 4;
    environment.backgroundMips = 3;
    environment.specularSize = 2;
    environment.specularMips = 2;
    environment.background.resize(cubeTexelCount(4, 3));
    environment.specular.resize(cubeTexelCount(2, 2));
    for (usize i = 0; i < environment.background.size(); ++i)
    {
        environment.background[i] = i * 0x0001000100010001ull;
    }
    environment.specular.back() = 42;
    environment.irradiance = ShIrradiance::uniform(Vec3(0.5f, 1.0f, 2.0f));
    CHECK(cubeTexelCount(4, 3) == (16 + 4 + 1) * 6);
    CHECK(cubeTexelCount(1, 1) == 6);
    REQUIRE(environment.isValid());

    const std::vector<std::byte> bytes = serializeEnvironment(environment);
    const std::optional<BakedEnvironment> read = deserializeEnvironment(bytes);
    REQUIRE(read.has_value());
    CHECK(read->backgroundSize == 4);
    CHECK(read->specularMips == 2);
    CHECK(read->background == environment.background);
    CHECK(read->specular == environment.specular);
    CHECK(read->irradiance.coefficients == environment.irradiance.coefficients);

    // Truncated or padded data, and sizes that do not match the texels.
    CHECK_FALSE(deserializeEnvironment(std::span(bytes).first(bytes.size() - 1)).has_value());
    std::vector<std::byte> padded = bytes;
    padded.push_back(std::byte{0});
    CHECK_FALSE(deserializeEnvironment(padded).has_value());
    BakedEnvironment broken = environment;
    broken.backgroundMips = 4; // 4x4 has three mips
    CHECK_FALSE(broken.isValid());
    CHECK_FALSE(deserializeEnvironment(serializeEnvironment(broken)).has_value());
    broken = environment;
    broken.specularSize = 3;
    CHECK_FALSE(broken.isValid());
    broken = environment;
    broken.background.pop_back();
    CHECK_FALSE(broken.isValid());
    CHECK_FALSE(BakedEnvironment{}.isValid());
}
