#pragma once

/// @file
/// CPU-side images with mip chains: decoding (PNG, JPEG, Radiance HDR), mip generation and simple
/// generated images.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace illusion
{

/// Pixel format of an Image.
enum class ImageFormat : u8
{
    RGBA8,     ///< 8-bit linear RGBA (normal maps, roughness/metallic, masks).
    RGBA8Srgb, ///< 8-bit sRGB-encoded RGBA (colour textures).
    RGBA32F,   ///< 32-bit float RGBA (HDR environments).
};

/// Bytes per pixel of @p format.
[[nodiscard]] constexpr u32 bytesPerPixel(ImageFormat format) noexcept
{
    return format == ImageFormat::RGBA32F ? 16u : 4u;
}

/// One level of a mip chain inside Image::data.
struct ImageMip
{
    u32 width = 0;    ///< Width in pixels.
    u32 height = 0;   ///< Height in pixels.
    usize offset = 0; ///< Byte offset in Image::data.
    usize size = 0;   ///< Size in bytes.
};

/// A 2D image with zero or more mip levels stored contiguously.
struct Image
{
    ImageFormat format = ImageFormat::RGBA8; ///< Pixel format.
    std::vector<ImageMip> mips;              ///< Mip levels, largest first.
    std::vector<std::byte> data;             ///< Pixels of all mips.

    /// Returns true if the image has at least one non-empty level.
    [[nodiscard]] bool isValid() const noexcept
    {
        return !mips.empty() && mips[0].width > 0 && mips[0].height > 0;
    }
    /// Width of mip 0.
    [[nodiscard]] u32 width() const noexcept { return mips.empty() ? 0 : mips[0].width; }
    /// Height of mip 0.
    [[nodiscard]] u32 height() const noexcept { return mips.empty() ? 0 : mips[0].height; }
    /// Pixels of mip @p level.
    [[nodiscard]] std::span<const std::byte> mip(usize level) const noexcept
    {
        return {data.data() + mips[level].offset, mips[level].size};
    }
};

/// Creates a single-level image of @p width x @p height filled with @p color (components in [0, 1],
/// stored as given; for sRGB formats pass sRGB-encoded values).
[[nodiscard]] Image makeSolidImage(u32 width, u32 height, Vec4 color, ImageFormat format);

/// Decodes PNG, JPEG, TGA, BMP or Radiance HDR data into a single-level image. 8-bit sources become
/// RGBA8 or RGBA8Srgb (per @p srgb); HDR sources become RGBA32F. Returns std::nullopt and sets @p error
/// on failure.
[[nodiscard]] std::optional<Image> decodeImage(std::span<const std::byte> encoded, bool srgb,
                                               std::string* error = nullptr);

/// Encodes mip 0 of an RGBA8 or RGBA8Srgb image as PNG. Returns an empty vector for other formats.
[[nodiscard]] std::vector<std::byte> encodePng(const Image& image);

/// Pixel (@p x, @p y) of mip 0 as linear RGBA: sRGB images are decoded, 8-bit values are scaled to
/// [0, 1]. Coordinates must be inside the image.
[[nodiscard]] Vec4 readPixel(const Image& image, u32 x, u32 y) noexcept;

/// Replaces the mip chain below level 0 with a full chain down to 1x1, using a 2x2 box filter.
/// sRGB images are filtered in linear space.
void generateMips(Image& image);

/// Number of levels in a full mip chain for @p width x @p height.
[[nodiscard]] u32 fullMipCount(u32 width, u32 height) noexcept;

} // namespace illusion
