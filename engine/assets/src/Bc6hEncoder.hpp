#pragma once

// BC6H encoder of the assets module (ADR 0054): unsigned, single-region mode 11 only.

#include <illusion/core/Base.hpp>

#include <array>
#include <cstddef>

namespace illusion::detail
{

/// Unsigned half-float bits of @p value clamped to [0, 65504]; NaN becomes 0.
[[nodiscard]] u16 toUnsignedHalf(f32 value) noexcept;

/// Encodes 16 texels of unsigned half-float RGB bits (4x4, rows from the top) into the 16 bytes at @p block,
/// refining the principal-axis endpoints with @p refinementPasses least-squares passes.
void encodeBc6hBlock(const std::array<std::array<u16, 3>, 16>& texels, u32 refinementPasses,
                     std::byte* block) noexcept;

} // namespace illusion::detail
