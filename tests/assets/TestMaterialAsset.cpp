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
    asset.material.diffuseTransmission = 0.5f;
    asset.material.diffuseTransmissionColor = Vec3(0.75f, 1.0f, 0.25f);
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
    CHECK(read->material.diffuseTransmission == 0.5f);
    CHECK(read->material.diffuseTransmissionColor == asset.material.diffuseTransmissionColor);
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
    CHECK(parseMaterialAsset(R"({"format": "illusion.material", "version": 3})")
              .error()
              .starts_with("unsupported material version"));
    CHECK(parseMaterialAsset(R"({"format": "illusion.material", "version": 2, "shader": 4})")
              .error()
              .starts_with("/shader"));
    CHECK(parseMaterialAsset(R"({"format": "illusion.material", "version": 2, "parameters": [1]})").error() ==
          "/parameters: must be an object");
    CHECK(parseMaterialAsset(
              R"({"format": "illusion.material", "version": 1, "material": {"metallicFactor": 2}})")
              .error()
              .starts_with("/material/metallicFactor"));
    CHECK(parseMaterialAsset(R"({"format": "illusion.material", "version": 1, "textures": {"normal": 5}})")
              .error()
              .starts_with("/textures/normal"));
}

namespace
{

// A shader as the compiler's reflection describes it, without SPIR-V: numbers, colours, vectors, an int, a
// bool and two textures.
ShaderAsset glowShader()
{
    ShaderAsset shader;
    shader.source = "assets:/shaders/glow.shader";
    const auto parameter = [&](std::string name, ShaderParameterType type, std::array<f32, 4> value,
                               bool color = false) {
        ShaderParameter added;
        added.name = std::move(name);
        added.type = type;
        added.offset = 16 * static_cast<u32>(shader.parameters.size());
        added.defaultValue = value;
        added.color = color;
        shader.parameters.push_back(std::move(added));
    };
    parameter("intensity", ShaderParameterType::Float, {2.5f, 0, 0, 0});
    parameter("tint", ShaderParameterType::Float4, {1, 0.5f, 0.25f, 1}, true);
    parameter("scroll", ShaderParameterType::Float2, {0.5f, 0.25f, 0, 0});
    parameter("steps", ShaderParameterType::Int, {-3, 0, 0, 0});
    parameter("pulse", ShaderParameterType::Bool, {1, 0, 0, 0});
    parameter("base", ShaderParameterType::Float3, {0.1f, 0.2f, 0.3f, 0}, true);
    ShaderTexture mask;
    mask.name = "mask";
    mask.offset = 128;
    mask.slot = 0;
    mask.usage = ShaderTextureUsage::Mask;
    mask.fallback = ShaderTextureFallback::Black;
    shader.textures.push_back(mask);
    ShaderTexture detail;
    detail.name = "detail";
    detail.offset = 136;
    detail.slot = 1;
    detail.usage = ShaderTextureUsage::Normal;
    detail.fallback = ShaderTextureFallback::FlatNormal;
    detail.defaultTexture = "game:/textures/detail_n.png";
    shader.textures.push_back(detail);
    return shader;
}

MaterialAsset withParameters(std::string_view json)
{
    MaterialAsset asset;
    asset.shader = AssetRef("assets:/shaders/glow.shader");
    asset.parameters.text = std::string(json);
    return asset;
}

} // namespace

TEST_CASE("material files name a shader and its parameters and read version 1 files as they are")
{
    reflectAssetStructs();
    const Expected<MaterialAsset, std::string> v1 = parseMaterialAsset(kPaint);
    REQUIRE(v1.hasValue());
    CHECK(v1->shader.empty());
    CHECK(v1->parameters.text == "{}");

    const MaterialAsset asset = withParameters(R"({"intensity":4,"tint":[1,0,0,1],"mask":"game:/m.png"})");
    const Expected<std::string, std::string> text = writeMaterialAsset(asset);
    REQUIRE(text.hasValue());
    CHECK(text->find("\"version\": 2") != std::string::npos);
    CHECK(text->find("\"shader\": \"assets:/shaders/glow.shader\"") != std::string::npos);
    CHECK(text->find("\"intensity\": 4") != std::string::npos);
    const Expected<MaterialAsset, std::string> read = parseMaterialAsset(*text);
    REQUIRE_MESSAGE(read.hasValue(), read.error());
    CHECK(read->shader == asset.shader);
    CHECK(read->parameters.text == R"({"intensity":4,"tint":[1,0,0,1],"mask":"game:/m.png"})");
    CHECK(writeMaterialAsset(*read).value() == *text);

    // Without a shader or parameters the members are not written.
    const Expected<std::string, std::string> plain = writeMaterialAsset(MaterialAsset{});
    REQUIRE(plain.hasValue());
    CHECK(plain->find("shader") == std::string::npos);
    CHECK(plain->find("parameters") == std::string::npos);
}

TEST_CASE("material parameters are checked against the shader and missing ones take its defaults")
{
    reflectAssetStructs();
    const ShaderAsset shader = glowShader();
    const Expected<MaterialParameters, std::string> resolved = resolveMaterialParameters(
        withParameters(R"({"intensity": 7, "tint": "#ff8000", "steps": 12, "pulse": false,)"
                       R"( "base": [0.5, 0.25, 1], "mask": "game:/m.png"})"),
        shader);
    REQUIRE_MESSAGE(resolved.hasValue(), resolved.error());
    REQUIRE(resolved->values.size() == 6);
    CHECK(resolved->values[0].name == "intensity");
    CHECK(resolved->values[0].value[0] == 7.0f);
    CHECK(resolved->values[1].value[0] == doctest::Approx(1.0f)); // "#ff8000" as linear colour
    CHECK(resolved->values[1].value[1] == doctest::Approx(0.2158605f));
    CHECK(resolved->values[1].value[3] == 1.0f);
    CHECK(resolved->values[2].value[0] == 0.5f); // scroll: the default
    CHECK(resolved->values[2].value[1] == 0.25f);
    CHECK(resolved->values[3].value[0] == 12.0f);
    CHECK(resolved->values[4].value[0] == 0.0f);
    CHECK(resolved->values[5].value[1] == 0.25f);
    REQUIRE(resolved->textures.size() == 2);
    CHECK(resolved->textures[0].texture.path() == "game:/m.png");
    CHECK(resolved->textures[0].use.compression == TextureCompression::BC4);
    CHECK_FALSE(resolved->textures[0].use.srgb);
    CHECK(resolved->textures[1].texture.path() == "game:/textures/detail_n.png"); // the default image
    CHECK(resolved->textures[1].use.compression == TextureCompression::BC5);

    const Expected<MaterialParameters, std::string> none = resolveMaterialParameters(MaterialAsset{}, shader);
    REQUIRE(none.hasValue());
    CHECK(none->values[0].value[0] == 2.5f);
    CHECK(none->values[3].value[0] == -3.0f);
    CHECK(none->textures[0].texture.empty());

    const Expected<MaterialParameters, std::string> cleared =
        resolveMaterialParameters(withParameters(R"({"detail": ""})"), shader);
    REQUIRE(cleared.hasValue());
    CHECK(cleared->textures[1].texture.empty()); // "" asks for the fallback, not the default image
}

TEST_CASE("material parameters that do not fit the shader are errors with a JSON Pointer")
{
    reflectAssetStructs();
    const ShaderAsset shader = glowShader();
    const auto error = [&](std::string_view json) {
        const Expected<MaterialParameters, std::string> resolved =
            resolveMaterialParameters(withParameters(json), shader);
        REQUIRE_FALSE(resolved.hasValue());
        return resolved.error();
    };
    CHECK(error(R"({"glow": 1})") == "/parameters/glow: the shader 'assets:/shaders/glow.shader' declares no "
                                     "parameter 'glow' (it declares: intensity, tint, scroll, steps, pulse, "
                                     "base, mask, detail)");
    CHECK(error(R"({"intensity": "high"})") == "/parameters/intensity: must be a number");
    CHECK(error(R"({"steps": 1.5})") == "/parameters/steps: must be an integer");
    CHECK(error(R"({"pulse": 1})") == "/parameters/pulse: must be true or false");
    CHECK(error(R"({"scroll": [1]})") == "/parameters/scroll: must be an array of 2 numbers");
    CHECK(error(R"({"scroll": [1, "a"]})") == "/parameters/scroll/1: must be a number");
    CHECK(error(R"({"base": "#ff8000ff"})") ==
          "/parameters/base: must be \"#rrggbb\" or an array of 3 numbers");
    CHECK(error(R"({"tint": "red"})") ==
          "/parameters/tint: must be \"#rrggbb\" or \"#rrggbbaa\" or an array of 4 numbers");
    CHECK(error(R"({"mask": 3})") == "/parameters/mask: must be an asset path (\"\" for no image)");
    CHECK(error(R"({"a/b~c": 1})").starts_with("/parameters/a~1b~0c: "));
    MaterialAsset notObject;
    notObject.parameters.text = "[]";
    CHECK(resolveMaterialParameters(notObject, shader).error() == "/parameters: must be an object");
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

    // The normal map was cooked as linear BC5 data: loading it from the cache imports nothing.
    AssetDatabase database(project.files, "cache");
    CHECK(
        database
            .loadImage("game:/textures/checker.png", {.srgb = false, .compression = TextureCompression::BC5})
            .has_value());
    CHECK(database.statistics().imports == 0);

    REQUIRE(project.files.writeFile("game:/bad.material", bytesOf("[]")) == FileError::None);
    const CookResult failed = cookAssets(project.files, "game:/", "cache");
    CHECK(failed.errors.size() == 3);
    CHECK(failed.materials == 1);
    CHECK(std::any_of(failed.errors.begin(), failed.errors.end(),
                      [](const std::string& error) { return error.starts_with("game:/bad.material: "); }));
}
