#pragma once

/// @file
/// Reads rendered frames back to the CPU for screenshots and the images of saved games (ADR 0058).
///
/// @code
/// auto capture = illusion::FrameCapture::create(device);
/// saveDesc.thumbnail = [&](illusion::SaveThumbnail& out) {
///     out.width = illusion::kSaveThumbnailWidth;
///     out.height = illusion::kSaveThumbnailHeight;
///     return capture->readScaled(view, {out.width, out.height}, out.rgba);
/// };
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace illusion
{

class ReadbackRing;
class RenderDevice;

/// Scales the tightly packed RGBA8 pixels @p source (@p sourceSize, sRGB-encoded) to @p size into @p out
/// (`size.x * size.y * 4` bytes): crops the centre to the aspect ratio of @p size, then averages each
/// destination pixel's box of source pixels in linear light. Does not allocate.
void downscaleRgba8Srgb(std::span<const std::byte> source, UVec2 sourceSize, UVec2 size,
                        std::span<std::byte> out) noexcept;

/// Copies rendered RGBA8 textures to the CPU, waiting for the copy. Meant for captures that are asked for
/// (a screenshot, a save), not for every frame: its buffers are kept for the next capture. Use it on the
/// render thread; the RenderDevice outlives it.
class FrameCapture : NonCopyable
{
public:
    /// Creates a capture on @p device; null when a GPU object cannot be created.
    [[nodiscard]] static std::unique_ptr<FrameCapture> create(RenderDevice& device);
    ~FrameCapture();

    /// Copies @p texture (RGBA8_UNORM or SRGBA8_UNORM, its commands already submitted) into @p out as tightly
    /// packed rows, top to bottom. False for a null texture, another format or a GPU failure.
    bool read(nvrhi::ITexture* texture, std::vector<std::byte>& out);

    /// read() followed by downscaleRgba8Srgb() to @p size; @p out is resized to `size.x * size.y * 4` bytes.
    bool readScaled(nvrhi::ITexture* texture, UVec2 size, std::vector<std::byte>& out);

private:
    FrameCapture();

    std::unique_ptr<ReadbackRing> m_ring;
    std::vector<std::byte> m_pixels;
};

} // namespace illusion
