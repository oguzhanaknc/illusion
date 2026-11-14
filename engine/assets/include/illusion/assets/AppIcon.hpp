#pragma once

/// @file
/// Application icons of exported games (ADR 0055): resampling a square image to the icon sizes, the Windows
/// `.ico` format and the engine's own icon for games that have none.

#include <illusion/assets/Image.hpp>
#include <illusion/core/Base.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace illusion
{

/// Sizes in pixels of the images in a Windows icon of an exported game.
inline constexpr std::array<u32, 7> kWindowsIconSizes{16, 24, 32, 48, 64, 128, 256};

/// Resamples mip 0 of an RGBA8 or RGBA8Srgb @p image to @p width x @p height (area filter when shrinking,
/// bilinear when growing; in linear light with premultiplied alpha). Returns an empty image for other formats
/// or a zero size.
[[nodiscard]] Image resizeImage(const Image& image, u32 width, u32 height);

/// Encodes square RGBA8 or RGBA8Srgb images of 1 to 256 pixels as a Windows `.ico` file: PNG for 256 pixels,
/// 32-bit bitmaps below. Returns an empty vector if @p images is empty or holds another image.
[[nodiscard]] std::vector<std::byte> encodeIco(std::span<const Image> images);

/// The engine's icon at @p size x @p size pixels (RGBA8Srgb), used by games without an icon of their own.
[[nodiscard]] Image makeEngineIcon(u32 size);

} // namespace illusion
