#include "AssetTestProject.hpp"

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetReflection.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/core/JobSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr std::string_view kPaint = R"({
  "format": "illusion.material",
  "version": 1,
  "material": {"baseColorFactor": [0.8, 0.1, 0.1, 1], "metallicFactor": 0, "alphaMode": "Mask",
               "normalTexture": {"wrapU": "ClampToEdge"}},
  "textures": {"baseColor": "game:/textures/./checker.png", "normal": "game:/textures/checker.png"}
})";

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

} // namespace

TEST_CASE("material files are read by field name with texture references")
{
    reflectAssetStructs();
    const Expected<MaterialAsset, std::string> paint = parseMaterialAsset(kPaint);
    REQUIRE_MESSAGE(paint.hasValue(), paint.error());
    CHECK(paint->material.baseColorFactor == Vec4(0.8f, 0.1f, 0.1f, 1.0f));
    CHECK(paint->material.metallicFactor == 0.0f);
    CHECK(paint->material.roughnessFactor == 1.0f); // default
    CHECK(paint->material.alphaMode == AlphaMode::Mask);
    CHECK(paint->material.normalTexture.wrapU == TextureWrap::ClampToEdge);
    CHECK(paint->textures.baseColor.path() == "game:/textures/checker.png");
    CHECK(paint->textures.normal == paint->textures.baseColor);
    CHECK(paint->textures.emissive.empty());

    const std::array<MaterialTextureUse, 5> uses = materialTextureUses(paint->textures);
    CHECK(uses[0].srgb);       // base colour
    CHECK_FALSE(uses[2].srgb); // normal
    CHECK(uses[4].srgb);       // emissive
    CHECK(uses[2].texture == paint->textures.normal);
}

TEST_CASE("material files round-trip")
{
    reflectAssetStructs();
    MaterialAsset asset;
    asset.material.name = "Hull";
    asset.material.roughnessFactor = 0.25f;
    asset.material.emissiveFactor = Vec3(1.0f, 0.5f, 0.0f);
    asset.material.doubleSided = true;
    asset.textures.metallicRoughness = AssetRef("game:/textures/orm.png");
    const Expected<std::string, std::string> text = writeMaterialAsset(asset);
    REQUIRE(text.hasValue());
    CHECK(text->find("\"format\": \"illusion.material\"") != std::string::npos);
    CHECK(text->find("\"metallicRoughness\": \"game:/textures/orm.png\"") != std::string::npos);

    const Expected<MaterialAsset, std::string> read = parseMaterialAsset(*text);
    REQUIRE(read.hasValue());
    CHECK(read->material.name == "Hull");
    CHECK(read->material.roughnessFactor == 0.25f);
    CHECK(read->material.emissiveFactor == asset.material.emissiveFactor);
    CHECK(read->material.doubleSided);
    CHECK(read->textures == asset.textures);
    CHECK(writeMaterialAsset(*read).value() == *text);
}

TEST_CASE("invalid material files report why")
{
    reflectAssetStructs();
    CHECK(parseMaterialAsset("{").error().starts_with("invalid JSON"));
    CHECK(parseMaterialAsset(R"({"format": "illusion.scene", "version": 1})")
              .error()
              .starts_with("not a material"));
    CHECK(parseMaterialAsset(R"({"format": "illusion.material", "version": 2})")
              .error()
              .starts_with("unsupported material version"));
    CHECK(parseMaterialAsset(
              R"({"format": "illusion.material", "version": 1, "material": {"metallicFactor": 2}})")
              .error()
              .starts_with("/material/metallicFactor"));
    CHECK(parseMaterialAsset(R"({"format": "illusion.material", "version": 1, "textures": {"normal": 5}})")
              .error()
              .starts_with("/textures/normal"));
}

TEST_CASE("material files stream through the asset streamer")
{
    reflectAssetStructs();
    test::AssetProject project;
    REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kPaint)) == FileError::None);
    REQUIRE(project.files.writeFile("game:/broken.material", bytesOf("{}")) == FileError::None);
    JobSystem jobs(JobSystemDesc{2, 256});
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<MaterialAsset> paint = streamer.loadFile<MaterialAsset>(
        "game:/paint.material", kMaterialDecoderName, materialAssetDecoder());
    const AssetHandle<MaterialAsset> broken = streamer.loadFile<MaterialAsset>(
        "game:/broken.material", kMaterialDecoderName, materialAssetDecoder());
    streamer.waitIdle();
    REQUIRE(paint.ready());
    CHECK(paint.value().material.alphaMode == AlphaMode::Mask);
    CHECK(broken.state() == AssetState::Failed);
    CHECK(broken.error().starts_with("not a material"));
}

TEST_CASE("cooking checks material files and cooks their textures with their colour spaces")
{
    reflectAssetStructs();
    test::AssetProject project;
    REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kPaint)) == FileError::None);
    const CookResult cooked = cookAssets(project.files, "game:/", "cache");
    CHECK(cooked.errors.size() == 2); // the broken glTF files of the test data
    CHECK(cooked.materials == 1);

    // The normal map was cooked as linear data: loading it from the cache imports nothing.
    AssetDatabase database(project.files, "cache");
    CHECK(database.loadImage("game:/textures/checker.png", {.srgb = false}).has_value());
    CHECK(database.statistics().imports == 0);

    REQUIRE(project.files.writeFile("game:/bad.material", bytesOf("[]")) == FileError::None);
    const CookResult failed = cookAssets(project.files, "game:/", "cache");
    CHECK(failed.errors.size() == 3);
    CHECK(failed.materials == 1);
    CHECK(std::any_of(failed.errors.begin(), failed.errors.end(),
                      [](const std::string& error) { return error.starts_with("game:/bad.material: "); }));
}
