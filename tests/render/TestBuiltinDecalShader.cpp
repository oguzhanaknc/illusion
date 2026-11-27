// The standard decal shader, builtin:/shader/decal (ADR 0066), as illusion_shaderc embeds it in the render
// library.

#include <illusion/assets/ShaderAsset.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>
#include <span>

namespace illusion::shaders
{
std::span<const std::byte> decalShader() noexcept;
} // namespace illusion::shaders

using namespace illusion;

TEST_CASE("the render library embeds the standard decal shader with every channel")
{
    const std::optional<ShaderAsset> decal = deserializeShader(shaders::decalShader());
    REQUIRE(decal.has_value());
    CHECK(decal->source == "builtin:/shader/decal");
    CHECK(decal->decalChannels == kAllDecalChannels);
    CHECK(decal->shadingModel == ShadingModel::Default);
    CHECK_FALSE(decal->worldOffset);
    REQUIRE(decal->variants.size() == 2);
    CHECK(decal->variant(ShaderPass::Decal, AlphaMode::Blend) != nullptr);
    CHECK(decal->variant(ShaderPass::DecalEmission, AlphaMode::Blend) != nullptr);
    CHECK(decal->findParameter("atlasColumns") != nullptr);
    CHECK(decal->textures.size() == 4);
}
