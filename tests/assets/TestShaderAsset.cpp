#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/ShaderAsset.hpp>

#include <doctest/doctest.h>

#include <cstring>

using namespace illusion;

namespace
{

ShaderAsset sampleShader()
{
    ShaderAsset shader;
    shader.source = "game:/materials/glow.shader";
    shader.shadingModel = ShadingModel::Subsurface;
    shader.worldOffset = true;
    shader.parameterBytes = 48;
    shader.parameters = {
        ShaderParameter{.name = "tint",
                        .type = ShaderParameterType::Float4,
                        .offset = 0,
                        .defaultValue = {1.0f, 0.5f, 0.25f, 1.0f},
                        .color = true,
                        .hasRange = false,
                        .minimum = 0.0f,
                        .maximum = 0.0f},
        ShaderParameter{.name = "intensity",
                        .type = ShaderParameterType::Float,
                        .offset = 16,
                        .defaultValue = {2.5f, 0.0f, 0.0f, 0.0f},
                        .color = false,
                        .hasRange = true,
                        .minimum = 0.0f,
                        .maximum = 10.0f},
        ShaderParameter{.name = "count",
                        .type = ShaderParameterType::Int,
                        .offset = 20,
                        .defaultValue = {-3.0f, 0.0f, 0.0f, 0.0f},
                        .color = false,
                        .hasRange = false,
                        .minimum = 0.0f,
                        .maximum = 0.0f},
        ShaderParameter{.name = "pulse",
                        .type = ShaderParameterType::Bool,
                        .offset = 24,
                        .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f},
                        .color = false,
                        .hasRange = false,
                        .minimum = 0.0f,
                        .maximum = 0.0f},
    };
    shader.textures = {ShaderTexture{.name = "mask",
                                     .offset = 28,
                                     .slot = 0,
                                     .usage = ShaderTextureUsage::Mask,
                                     .fallback = ShaderTextureFallback::Black,
                                     .defaultTexture = {}},
                       ShaderTexture{.name = "detail",
                                     .offset = 32,
                                     .slot = 1,
                                     .usage = ShaderTextureUsage::Normal,
                                     .fallback = ShaderTextureFallback::FlatNormal,
                                     .defaultTexture = "game:/textures/detail_n.png"}};
    const std::array<std::byte, 8> words{std::byte{0x03}, std::byte{0x02}, std::byte{0x23}, std::byte{0x07},
                                         std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}};
    shader.variants = {
        ShaderVariant{.pass = ShaderPass::Forward,
                      .alphaMode = AlphaMode::Opaque,
                      .spirv = std::vector<std::byte>(words.begin(), words.end())},
        ShaderVariant{.pass = ShaderPass::Forward,
                      .alphaMode = AlphaMode::Blend,
                      .spirv = std::vector<std::byte>(words.begin(), words.begin() + 4)},
        ShaderVariant{.pass = ShaderPass::ShadowCaster,
                      .alphaMode = AlphaMode::Mask,
                      .spirv = std::vector<std::byte>(words.begin(), words.end())},
    };
    shader.libraryHash = 0x1234'5678'9abc'def0ull;
    shader.compilerVersion = "2026.19";
    return shader;
}

template <class T>
T readAt(const std::vector<std::byte>& block, u32 offset)
{
    T value{};
    std::memcpy(&value, block.data() + offset, sizeof(T));
    return value;
}

} // namespace

TEST_CASE("a serialized shader reads back equal")
{
    const ShaderAsset shader = sampleShader();
    const std::vector<std::byte> bytes = serializeShader(shader);
    const std::optional<ShaderAsset> read = deserializeShader(bytes);
    REQUIRE(read.has_value());
    CHECK(*read == shader);
    CHECK(serializeShader(*read) == bytes);
}

TEST_CASE("a shader without parameters, textures or variants reads back equal")
{
    ShaderAsset shader;
    shader.source = "game:/a.shader";
    const std::optional<ShaderAsset> read = deserializeShader(serializeShader(shader));
    REQUIRE(read.has_value());
    CHECK(*read == shader);
}

TEST_CASE("truncated or corrupted shader data is rejected")
{
    const std::vector<std::byte> bytes = serializeShader(sampleShader());
    for (usize size = 0; size < bytes.size(); ++size)
    {
        CAPTURE(size);
        CHECK_FALSE(deserializeShader(std::span(bytes.data(), size)).has_value());
    }
    std::vector<std::byte> trailing = bytes;
    trailing.push_back(std::byte{0});
    CHECK_FALSE(deserializeShader(trailing).has_value());
    std::vector<std::byte> magic = bytes;
    magic[0] = std::byte{0};
    CHECK_FALSE(deserializeShader(magic).has_value());
}

TEST_CASE("shader data with values outside their limits is rejected")
{
    SUBCASE("a parameter past the end of the block")
    {
        ShaderAsset shader = sampleShader();
        shader.parameters[0].offset = 40;
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("a block larger than 256 bytes")
    {
        ShaderAsset shader = sampleShader();
        shader.parameterBytes = kMaxShaderParameterBytes + 16;
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("a texture whose slot is not its index")
    {
        ShaderAsset shader = sampleShader();
        shader.textures[1].slot = 3;
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("more than eight textures")
    {
        ShaderAsset shader = sampleShader();
        shader.parameterBytes = 64;
        shader.textures.clear();
        for (u32 slot = 0; slot <= kMaxShaderTextures; ++slot)
        {
            shader.textures.push_back(ShaderTexture{.name = "t" + std::to_string(slot),
                                                    .offset = 28 + slot * 4,
                                                    .slot = slot,
                                                    .usage = ShaderTextureUsage::Color,
                                                    .fallback = ShaderTextureFallback::White,
                                                    .defaultTexture = {}});
        }
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("variants out of order")
    {
        ShaderAsset shader = sampleShader();
        std::swap(shader.variants[0], shader.variants[1]);
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("a blended variant of a pass that never draws blended materials")
    {
        ShaderAsset shader = sampleShader();
        shader.variants[2].alphaMode = AlphaMode::Blend;
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("SPIR-V that is not whole words")
    {
        ShaderAsset shader = sampleShader();
        shader.variants[0].spirv.pop_back();
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
    SUBCASE("an unnamed parameter")
    {
        ShaderAsset shader = sampleShader();
        shader.parameters[1].name.clear();
        CHECK_FALSE(deserializeShader(serializeShader(shader)).has_value());
    }
}

TEST_CASE("a shader finds its variants, parameters and textures by key and name")
{
    const ShaderAsset shader = sampleShader();
    REQUIRE(shader.variant(ShaderPass::Forward, AlphaMode::Blend) != nullptr);
    CHECK(shader.variant(ShaderPass::Forward, AlphaMode::Blend)->spirv.size() == 4);
    CHECK(shader.variant(ShaderPass::DepthPrepass, AlphaMode::Opaque) == nullptr);
    REQUIRE(shader.findParameter("intensity") != nullptr);
    CHECK(shader.findParameter("intensity")->maximum == 10.0f);
    CHECK(shader.findParameter("mask") == nullptr);
    REQUIRE(shader.findTexture("detail") != nullptr);
    CHECK(shader.findTexture("detail")->slot == 1);
    CHECK(shader.findTexture("tint") == nullptr);
}

TEST_CASE("the default parameter block holds every default and the texture slots")
{
    const std::vector<std::byte> block = defaultParameterBlock(sampleShader());
    REQUIRE(block.size() == 48);
    CHECK(readAt<f32>(block, 0) == 1.0f);
    CHECK(readAt<f32>(block, 4) == 0.5f);
    CHECK(readAt<f32>(block, 12) == 1.0f);
    CHECK(readAt<f32>(block, 16) == 2.5f);
    CHECK(readAt<i32>(block, 20) == -3);
    CHECK(readAt<u32>(block, 24) == 1u);
    CHECK(readAt<u32>(block, 28) == 0u);
    CHECK(readAt<u32>(block, 32) == 1u);
    CHECK(readAt<u32>(block, 40) == 0u);
}

TEST_CASE("blended materials have variants only in the forward passes and for Entity ids")
{
    u32 variants = 0;
    for (u32 pass = 0; pass < kShaderPassCount; ++pass)
    {
        for (const AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
        {
            variants += shaderVariantExists(static_cast<ShaderPass>(pass), mode) ? 1u : 0u;
        }
    }
    CHECK(variants == 15);
    CHECK(shaderVariantExists(ShaderPass::EntityIds, AlphaMode::Blend));
    CHECK_FALSE(shaderVariantExists(ShaderPass::ShadowCaster, AlphaMode::Blend));
    CHECK_FALSE(shaderVariantExists(ShaderPass::MotionVectors, AlphaMode::Blend));
}

TEST_CASE("shader enumerations have stable names")
{
    CHECK(shadingModelName(ShadingModel::TwoSidedFoliage) == "TwoSidedFoliage");
    CHECK(parseShadingModel("ClearCoat") == ShadingModel::ClearCoat);
    CHECK(parseShadingModel("Unlit") == ShadingModel::Unlit);
    CHECK_FALSE(parseShadingModel("unlit").has_value());
    CHECK(shaderPassName(ShaderPass::ForwardBindless) == "forwardBindless");
    CHECK(shaderPassName(ShaderPass::EntityIds) == "entityIds");
    CHECK(alphaModeName(AlphaMode::Mask) == "mask");
    CHECK(shaderParameterTypeName(ShaderParameterType::Float3) == "float3");
    CHECK(shaderParameterComponents(ShaderParameterType::Float2) == 2);
    CHECK(shaderParameterComponents(ShaderParameterType::Bool) == 1);
    CHECK(shaderTextureUsageName(ShaderTextureUsage::Linear) == "linear");
}

TEST_CASE("cooked shaders are a type of their own in asset files")
{
    AssetFile file{.type = AssetType::Shader,
                   .version = kShaderFormatVersion,
                   .dependencies = {AssetDependency{.path = "game:/a.shader", .contentHash = 7}},
                   .payload = serializeShader(sampleShader())};
    const std::optional<AssetFile> read = readAssetFile(writeAssetFile(file));
    REQUIRE(read.has_value());
    CHECK(read->type == AssetType::Shader);
    CHECK(deserializeShader(read->payload) == sampleShader());
}
