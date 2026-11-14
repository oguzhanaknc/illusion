#include "TextureStreamingPolicy.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace illusion;
using namespace illusion::detail;

namespace
{

// A 1024x1024 RGBA8 texture with a full chain (11 levels); levels up to 64 pixels always resident (from 4).
StreamedTexture texture1024(f32 demand = 0.0f, u32 firstMip = 0)
{
    StreamedTexture texture = prepareStreamedTexture(1024, 1024, 11, ImageFormat::RGBA8, 64);
    texture.demand = demand;
    texture.firstMip = firstMip;
    return texture;
}

u64 levelsFrom(u32 first)
{
    u64 bytes = 0;
    for (u32 level = first; level < 11; ++level)
    {
        const u64 side = 1024u >> level;
        bytes += side * side * 4;
    }
    return bytes;
}

} // namespace

TEST_CASE("a streamed texture's description counts the bytes of every resident range")
{
    const StreamedTexture texture = texture1024();
    CHECK(texture.longestSide == 1024);
    CHECK(texture.mipCount == 11);
    CHECK(texture.lowestFirstMip == 4); // 64 pixels
    for (u32 first = 0; first < 11; ++first)
    {
        CHECK(texture.residentBytes[first] == levelsFrom(first));
    }

    // Block formats round small levels up to a block; non-square sides keep at least one pixel.
    const StreamedTexture bc = prepareStreamedTexture(256, 64, 9, ImageFormat::BC7, 16);
    CHECK(bc.longestSide == 256);
    CHECK(bc.lowestFirstMip == 4);
    CHECK(bc.residentBytes[8] == 16); // 1x1: one block
    // 256x64 to 16x4 take a byte per pixel; 8x2 is two blocks; 4x1, 2x1 and 1x1 one each.
    CHECK(bc.residentBytes[0] == 16384u + 4096u + 1024u + 256u + 64u + 32u + 16u + 16u + 16u);

    // A texture no larger than the minimum size is never reduced.
    CHECK(prepareStreamedTexture(64, 32, 7, ImageFormat::RGBA8, 64).lowestFirstMip == 0);
    CHECK(prepareStreamedTexture(1024, 1024, 11, ImageFormat::RGBA8, 0).lowestFirstMip == 10);
}

TEST_CASE("the wanted first level is the smallest one whose longer side covers the projected size")
{
    const StreamedTexture texture = texture1024();
    CHECK(wantedFirstMip(texture, 0.0f) == 4);
    CHECK(wantedFirstMip(texture, -5.0f) == 4);
    CHECK(wantedFirstMip(texture, 2000.0f) == 0);
    CHECK(wantedFirstMip(texture, 1024.0f) == 0);
    CHECK(wantedFirstMip(texture, 1000.0f) == 0);
    CHECK(wantedFirstMip(texture, 512.0f) == 1);
    CHECK(wantedFirstMip(texture, 513.0f) == 0);
    CHECK(wantedFirstMip(texture, 300.0f) == 1);
    CHECK(wantedFirstMip(texture, 256.0f) == 2);
    CHECK(wantedFirstMip(texture, 100.0f) == 3);
    CHECK(wantedFirstMip(texture, 10.0f) == 4); // clamped by the minimum size
}

TEST_CASE("texture streaming with no textures changes nothing")
{
    std::vector<StreamedTexture> none;
    CHECK(updateTextureStreaming(none, TextureStreamingSettings{1024, 3, 64}) == 0);
}

TEST_CASE("without a budget every level of every texture is resident")
{
    std::vector<StreamedTexture> textures{texture1024(0.0f, 3), texture1024(5000.0f, 0)};
    CHECK(updateTextureStreaming(textures, TextureStreamingSettings{0, 3, 64}) == 0);
    for (const StreamedTexture& texture : textures)
    {
        CHECK(texture.wanted == 0);
        CHECK(texture.target == 0);
    }
}

TEST_CASE("one texture raises at once and drops only after the cooldown")
{
    const TextureStreamingSettings settings{1ull << 30, 3, 64};
    std::vector<StreamedTexture> textures{texture1024(300.0f, 4)};

    // More demand: raised in the same update.
    CHECK(updateTextureStreaming(textures, settings) == 0);
    CHECK(textures[0].wanted == 1);
    CHECK(textures[0].target == 1);
    textures[0].firstMip = textures[0].target;

    // Less demand: kept for two updates, dropped on the third.
    textures[0].demand = 0.0f;
    for (u32 update = 1; update < 3; ++update)
    {
        updateTextureStreaming(textures, settings);
        CHECK(textures[0].wanted == 4);
        CHECK(textures[0].target == 1);
        CHECK(textures[0].cooldown == update);
    }
    updateTextureStreaming(textures, settings);
    CHECK(textures[0].target == 4);
    CHECK(textures[0].cooldown == 0);
    textures[0].firstMip = 4;

    // Demand that comes back during the cooldown restarts it.
    textures[0].demand = 300.0f;
    updateTextureStreaming(textures, settings);
    textures[0].firstMip = textures[0].target;
    textures[0].demand = 0.0f;
    updateTextureStreaming(textures, settings);
    updateTextureStreaming(textures, settings);
    CHECK(textures[0].cooldown == 2);
    textures[0].demand = 300.0f;
    updateTextureStreaming(textures, settings);
    CHECK(textures[0].cooldown == 0);
    CHECK(textures[0].target == 1);
    textures[0].demand = 0.0f;
    updateTextureStreaming(textures, settings);
    CHECK(textures[0].target == 1);
    CHECK(textures[0].cooldown == 1);

    // A cooldown of zero drops at once.
    updateTextureStreaming(textures, TextureStreamingSettings{1ull << 30, 0, 64});
    CHECK(textures[0].target == 4);
}

TEST_CASE("a budget smaller than the wanted bytes moves every texture down by the same bias")
{
    // Both want everything: 2 x levelsFrom(0) bytes.
    std::vector<StreamedTexture> textures{texture1024(5000.0f, 0), texture1024(400.0f, 0)};
    const u64 full = levelsFrom(0) + levelsFrom(1);

    CHECK(updateTextureStreaming(textures, TextureStreamingSettings{full, 0, 64}) == 0);
    CHECK(textures[0].target == 0);
    CHECK(textures[1].target == 1);

    // One byte less: both move one level down.
    CHECK(updateTextureStreaming(textures, TextureStreamingSettings{full - 1, 0, 64}) == 1);
    CHECK(textures[0].target == 1);
    CHECK(textures[1].target == 2);

    // A budget below the minimum of both: every texture at its lowest first level, no further.
    CHECK(updateTextureStreaming(textures, TextureStreamingSettings{1, 0, 64}) == 4);
    CHECK(textures[0].target == 4);
    CHECK(textures[1].target == 4);
}

TEST_CASE("pinned textures stay fully resident and outside the budget")
{
    std::vector<StreamedTexture> textures{texture1024(0.0f, 4), texture1024(5000.0f, 0)};
    textures[0].pinned = true;
    CHECK(updateTextureStreaming(textures, TextureStreamingSettings{levelsFrom(0), 3, 64}) == 0);
    CHECK(textures[0].wanted == 0);
    CHECK(textures[0].target == 0); // raised at once
    CHECK(textures[1].target == 0);
}

TEST_CASE("many textures settle within the budget with the smallest bias")
{
    std::vector<StreamedTexture> textures;
    for (u32 i = 0; i < 1000; ++i)
    {
        textures.push_back(texture1024(static_cast<f32>(i % 1100), 4));
    }
    const u64 budget = 64ull << 20;
    const u32 bias = updateTextureStreaming(textures, TextureStreamingSettings{budget, 120, 64});
    u64 total = 0;
    u64 smallerBias = 0;
    for (const StreamedTexture& texture : textures)
    {
        CHECK(texture.target <= texture.lowestFirstMip);
        CHECK(texture.target == texture.wanted); // every one raises
        total += texture.residentBytes[texture.target];
        const u32 unbiased = wantedFirstMip(texture, texture.demand);
        CHECK(texture.wanted == std::min(unbiased + bias, texture.lowestFirstMip));
        smallerBias += texture.residentBytes[std::min(unbiased + bias - 1, texture.lowestFirstMip)];
    }
    CHECK(bias > 0);
    CHECK(total <= budget);
    CHECK(smallerBias > budget);

    // The same inputs give the same result.
    std::vector<StreamedTexture> again = textures;
    for (StreamedTexture& texture : again)
    {
        texture.cooldown = 0;
    }
    CHECK(updateTextureStreaming(again, TextureStreamingSettings{budget, 120, 64}) == bias);
}
