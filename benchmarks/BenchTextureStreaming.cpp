#include "Budget.hpp"
#include "TextureStreamingPolicy.hpp"

#include <bit>
#include <random>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kTextures = 4096;

// Textures from 256 to 4096 pixels in the formats of material textures, with demands that need a bias of
// several levels to fit the budget.
std::vector<detail::StreamedTexture> makeTextures()
{
    std::mt19937 random(7);
    constexpr ImageFormat kFormats[] = {ImageFormat::BC7Srgb, ImageFormat::BC5, ImageFormat::BC4,
                                        ImageFormat::BC7};
    std::vector<detail::StreamedTexture> textures;
    textures.reserve(kTextures);
    for (u32 i = 0; i < kTextures; ++i)
    {
        const u32 side = 256u << (random() % 5u);
        const u32 levels = static_cast<u32>(std::bit_width(side));
        detail::StreamedTexture texture =
            detail::prepareStreamedTexture(side, side, levels, kFormats[i % 4], 64);
        texture.demand = static_cast<f32>(random() % 3000u);
        texture.firstMip = texture.lowestFirstMip;
        textures.push_back(texture);
    }
    return textures;
}

} // namespace

// Budget roughly 5-10x a desktop result; see Budget.hpp.
ILL_BENCHMARK("render/texture-streaming-policy", 40.0)
{
    std::vector<detail::StreamedTexture> textures = makeTextures();
    const detail::TextureStreamingSettings settings{256ull << 20, 120, 64};
    bench.batch(kTextures).unit("texture").run("texture streaming policy (4096 textures, biased)", [&] {
        ankerl::nanobench::doNotOptimizeAway(detail::updateTextureStreaming(textures, settings));
    });
}
