#include "TextureStreamingPolicy.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace illusion::detail
{

StreamedTexture prepareStreamedTexture(u32 width, u32 height, u32 mipCount, ImageFormat format,
                                       u32 minSize) noexcept
{
    StreamedTexture texture;
    texture.longestSide = std::max({width, height, 1u});
    texture.mipCount = std::clamp(mipCount, 1u, kMaxStreamedMips);
    const u32 last = texture.mipCount - 1;
    while (texture.lowestFirstMip < last && (texture.longestSide >> texture.lowestFirstMip) > minSize)
    {
        ++texture.lowestFirstMip;
    }
    u64 bytes = 0;
    for (u32 level = texture.mipCount; level-- > 0;)
    {
        bytes += imageLevelSize(format, std::max(width >> level, 1u), std::max(height >> level, 1u));
        texture.residentBytes[level] = bytes;
    }
    return texture;
}

u32 wantedFirstMip(const StreamedTexture& texture, f32 demand) noexcept
{
    if (!(demand > 1.0f))
    {
        return texture.lowestFirstMip; // also NaN
    }
    if (demand >= static_cast<f32>(texture.longestSide))
    {
        return 0;
    }
    // The largest level whose longer side (longestSide >> level) is at least ceil(demand) pixels.
    const auto pixels = static_cast<u32>(std::ceil(demand));
    const u32 level = static_cast<u32>(std::bit_width(texture.longestSide / pixels)) - 1;
    return std::min(level, texture.lowestFirstMip);
}

u32 updateTextureStreaming(std::span<StreamedTexture> textures,
                           const TextureStreamingSettings& settings) noexcept
{
    if (settings.budgetBytes == 0)
    {
        for (StreamedTexture& texture : textures)
        {
            texture.wanted = 0;
            texture.target = 0;
            texture.cooldown = 0;
        }
        return 0;
    }
    for (StreamedTexture& texture : textures)
    {
        texture.wanted = texture.pinned ? 0 : wantedFirstMip(texture, texture.demand);
    }
    // Each step moves every texture that is not yet at its lowest first level one level down.
    u32 bias = 0;
    for (; bias < kMaxStreamedMips; ++bias)
    {
        u64 total = 0;
        bool lower = false;
        for (const StreamedTexture& texture : textures)
        {
            if (!texture.pinned)
            {
                const u32 first = std::min(texture.wanted + bias, texture.lowestFirstMip);
                total += texture.residentBytes[first];
                lower = lower || first < texture.lowestFirstMip;
            }
        }
        if (total <= settings.budgetBytes || !lower)
        {
            break;
        }
    }
    for (StreamedTexture& texture : textures)
    {
        if (!texture.pinned)
        {
            texture.wanted = std::min(texture.wanted + bias, texture.lowestFirstMip);
        }
        if (texture.wanted < texture.firstMip)
        {
            texture.cooldown = 0;
            texture.target = texture.wanted;
        }
        else if (texture.wanted > texture.firstMip && ++texture.cooldown >= settings.cooldown)
        {
            texture.cooldown = 0;
            texture.target = texture.wanted;
        }
        else
        {
            texture.cooldown = texture.wanted == texture.firstMip ? 0 : texture.cooldown;
            texture.target = texture.firstMip;
        }
    }
    return bias;
}

} // namespace illusion::detail
