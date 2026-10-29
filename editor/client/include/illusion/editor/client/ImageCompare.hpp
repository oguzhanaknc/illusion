#pragma once

/// @file
/// Comparing screenshots with golden images, for the `image` option of transcript steps (ADR 0038): a
/// pixel differs when one of its channels is off by more than a tolerance, and an image matches when few
/// enough pixels differ.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::client
{

/// How two images of the same size compare.
struct ImageComparison
{
    u32 width = 0;               ///< Width of both images.
    u32 height = 0;              ///< Height of both images.
    f64 differentFraction = 0.0; ///< Fraction of pixels with a channel off by more than the tolerance.
    u32 largestDifference = 0;   ///< Largest channel difference (0-255).
};

/// The bytes of standard base64 @p text (padding optional), or std::nullopt if it is not base64.
[[nodiscard]] std::optional<std::vector<std::byte>> decodeBase64(std::string_view text);

/// True if every pixel of the encoded image @p png has the same colour (also false if it cannot be decoded).
[[nodiscard]] bool isUniformImage(std::span<const std::byte> png);

/// Compares the encoded images @p actual and @p golden (PNG, JPEG, ...) with @p channelTolerance (0-255).
/// Fails with a message when one cannot be decoded or their sizes differ.
[[nodiscard]] Expected<ImageComparison, std::string>
compareImages(std::span<const std::byte> actual, std::span<const std::byte> golden, u32 channelTolerance);

} // namespace illusion::editor::client
