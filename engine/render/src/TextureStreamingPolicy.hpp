#pragma once

// The budget policy of mip streaming (ADR 0054): which levels of each streamed texture should be resident.
// Pure and GPU-free; RenderAssetCache feeds it once per update.

#include <illusion/assets/Image.hpp>
#include <illusion/core/Base.hpp>

#include <array>
#include <span>

namespace illusion::detail
{

/// Most levels a streamed texture can have (a 32768-pixel side); larger textures are not streamed.
inline constexpr u32 kMaxStreamedMips = 16;

/// Budget and hysteresis of the policy (see RenderAssetCacheDesc).
struct TextureStreamingSettings
{
    u64 budgetBytes = 0; ///< Bytes the streamed textures may take; 0: every level resident.
    u32 cooldown = 120;  ///< Updates of lower demand before levels are dropped.
    u32 minSize = 64;    ///< Levels up to this many pixels on their longer side are always resident.
};

/// One streamed texture: its description (prepareStreamedTexture()), the inputs of each update and the
/// policy's state and result.
struct StreamedTexture
{
    u32 longestSide = 1;    ///< Pixels of level 0 on its longer side.
    u32 mipCount = 1;       ///< Levels of the image.
    u32 lowestFirstMip = 0; ///< The first level that is always resident (TextureStreamingSettings::minSize).
    /// [first]: GPU bytes of the levels from `first` to the last.
    std::array<u64, kMaxStreamedMips> residentBytes{};

    f32 demand = 0.0f;   ///< Input: largest projected size in pixels of the renderers that use it.
    bool pinned = false; ///< Input: keep every level resident, outside the budget.
    u32 firstMip = 0;    ///< Input: the first level resident now.

    u32 cooldown = 0; ///< State: consecutive updates that wanted fewer levels than are resident.
    u32 wanted = 0;   ///< Result: the first level the demand and the budget ask for.
    u32 target = 0;   ///< Result: the first level to make resident now (firstMip while cooling down).
};

/// Describes a texture of @p width x @p height with @p mipCount levels (at most kMaxStreamedMips) in
/// @p format for the policy; levels up to @p minSize pixels are always resident.
[[nodiscard]] StreamedTexture prepareStreamedTexture(u32 width, u32 height, u32 mipCount, ImageFormat format,
                                                     u32 minSize) noexcept;

/// The first level whose longer side covers @p demand pixels, or the lowest first level for less.
[[nodiscard]] u32 wantedFirstMip(const StreamedTexture& texture, f32 demand) noexcept;

/// Sets `wanted`, `target` and `cooldown` of every texture: the level its demand asks for plus a global bias,
/// raised until the wanted bytes of the unpinned textures fit @p settings.budgetBytes (or all are at their
/// lowest first level). Fewer levels are made resident only after `cooldown` updates. Returns the bias.
u32 updateTextureStreaming(std::span<StreamedTexture> textures,
                           const TextureStreamingSettings& settings) noexcept;

} // namespace illusion::detail
