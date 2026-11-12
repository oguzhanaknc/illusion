#pragma once

/// @file
/// Block compression of images (BC4, BC5, BC6H, BC7) for the GPU, and decompression on the CPU. See
/// docs/adr/0054-texture-compression.md.

#include <illusion/assets/Image.hpp>
#include <illusion/core/Base.hpp>

#include <optional>
#include <string_view>

namespace illusion
{

class JobSystem;
struct BakedEnvironment;

/// Block compression of a texture.
enum class TextureCompression : u8
{
    None, ///< Uncompressed (RGBA8, RGBA8Srgb or RGBA32F).
    BC7,  ///< Colour with or without alpha, 8 bits per pixel.
    BC5,  ///< Two channels (R, G), 8 bits per pixel: tangent-space normals.
    BC4,  ///< One channel (R), 4 bits per pixel: masks.
    BC6H, ///< Unsigned HDR colour without alpha, 8 bits per pixel.
};

/// Encoder effort: each step is several times slower than the one before.
enum class TextureQuality : u8
{
    Fast,   ///< For iteration.
    Normal, ///< The default.
    High,   ///< For final builds.
};

/// Name of @p compression in sidecars and logs ("none", "bc7", "bc5", "bc4", "bc6h").
[[nodiscard]] std::string_view textureCompressionName(TextureCompression compression) noexcept;

/// Name of @p quality in sidecars and logs ("fast", "normal", "high").
[[nodiscard]] std::string_view textureQualityName(TextureQuality quality) noexcept;

/// The compression an image of format @p source gets when @p requested is asked for: None stays None, float
/// images always become BC6H and 8-bit images BC7 instead of BC6H. @p source must not be block-compressed.
[[nodiscard]] TextureCompression resolveCompression(TextureCompression requested,
                                                    ImageFormat source) noexcept;

/// The format compressImage() produces for @p compression (already resolved) from @p source.
[[nodiscard]] ImageFormat compressedFormat(TextureCompression compression, ImageFormat source) noexcept;

/// Compresses every level of @p image (RGBA8, RGBA8Srgb or RGBA32F) with resolveCompression(@p compression);
/// None returns a copy. BC5 keeps R and G, BC4 keeps R, BC6H drops alpha and clamps negative values to zero.
/// With @p jobs the rows of blocks are encoded in parallel; the result is the same with any worker count.
/// Returns std::nullopt for an invalid or block-compressed image.
[[nodiscard]] std::optional<Image> compressImage(const Image& image, TextureCompression compression,
                                                 TextureQuality quality = TextureQuality::Normal,
                                                 JobSystem* jobs = nullptr);

/// Decodes every level of a block-compressed @p image: BC7 and BC7Srgb to RGBA8 and RGBA8Srgb, BC5 to RGBA8
/// with B = 0 and A = 255, BC4 to RGBA8 with G = B = 0 and A = 255, BC6H to RGBA32F with A = 1. An
/// uncompressed image is copied. Returns std::nullopt for an invalid image.
[[nodiscard]] std::optional<Image> decompressImage(const Image& image);

/// Compresses the cube maps of a half-float @p baked environment to BC6H (BakedEnvironment::compressed).
/// Returns std::nullopt if @p baked is invalid or compressed already.
[[nodiscard]] std::optional<BakedEnvironment>
compressEnvironment(const BakedEnvironment& baked, TextureQuality quality = TextureQuality::Normal,
                    JobSystem* jobs = nullptr);

/// Decodes a compressed @p baked environment back to half-float texels. Returns std::nullopt if @p baked is
/// invalid or not compressed.
[[nodiscard]] std::optional<BakedEnvironment> decompressEnvironment(const BakedEnvironment& baked);

} // namespace illusion
