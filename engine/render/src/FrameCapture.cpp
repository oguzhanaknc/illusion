#include <illusion/core/Profiler.hpp>
#include <illusion/render/FrameCapture.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace illusion
{
namespace
{

const std::array<f32, 256>& srgbToLinearTable() noexcept
{
    static const std::array<f32, 256> table = [] {
        std::array<f32, 256> values{};
        for (usize i = 0; i < values.size(); ++i)
        {
            const f32 c = static_cast<f32>(i) / 255.0f;
            values.at(i) = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return values;
    }();
    return table;
}

std::byte linearToSrgb(f32 linear) noexcept
{
    const f32 c = std::clamp(linear, 0.0f, 1.0f);
    const f32 encoded = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::byte>(static_cast<u8>(std::lround(encoded * 255.0f)));
}

// The source span [first, last) of destination index @p i of @p count over @p length source pixels at @p
// offset.
void boxOf(u32 i, u32 count, u32 length, u32 offset, u32& first, u32& last) noexcept
{
    first = offset + static_cast<u32>(static_cast<u64>(i) * length / count);
    last = offset + static_cast<u32>(static_cast<u64>(i + 1) * length / count);
    last = std::min(std::max(last, first + 1), offset + length);
}

} // namespace

void downscaleRgba8Srgb(std::span<const std::byte> source, UVec2 sourceSize, UVec2 size,
                        std::span<std::byte> out) noexcept
{
    ILL_PROFILE_SCOPE();
    if (sourceSize.x == 0 || sourceSize.y == 0 || size.x == 0 || size.y == 0 ||
        source.size() < static_cast<usize>(sourceSize.x) * sourceSize.y * 4 ||
        out.size() < static_cast<usize>(size.x) * size.y * 4)
    {
        return;
    }
    // The largest centred region with the destination's aspect ratio.
    u32 cropWidth = sourceSize.x;
    u32 cropHeight = sourceSize.y;
    if (static_cast<u64>(sourceSize.x) * size.y > static_cast<u64>(sourceSize.y) * size.x)
    {
        cropWidth = std::max(1u, static_cast<u32>(static_cast<u64>(sourceSize.y) * size.x / size.y));
    }
    else
    {
        cropHeight = std::max(1u, static_cast<u32>(static_cast<u64>(sourceSize.x) * size.y / size.x));
    }
    const u32 offsetX = (sourceSize.x - cropWidth) / 2;
    const u32 offsetY = (sourceSize.y - cropHeight) / 2;
    const std::array<f32, 256>& toLinear = srgbToLinearTable();
    for (u32 y = 0; y < size.y; ++y)
    {
        u32 top = 0;
        u32 bottom = 0;
        boxOf(y, size.y, cropHeight, offsetY, top, bottom);
        for (u32 x = 0; x < size.x; ++x)
        {
            u32 left = 0;
            u32 right = 0;
            boxOf(x, size.x, cropWidth, offsetX, left, right);
            f32 r = 0.0f;
            f32 g = 0.0f;
            f32 b = 0.0f;
            f32 a = 0.0f;
            for (u32 sy = top; sy < bottom; ++sy)
            {
                const usize row = static_cast<usize>(sy) * sourceSize.x;
                for (u32 sx = left; sx < right; ++sx)
                {
                    const std::byte* pixel = source.data() + (row + sx) * 4;
                    r += toLinear[static_cast<u8>(pixel[0])];
                    g += toLinear[static_cast<u8>(pixel[1])];
                    b += toLinear[static_cast<u8>(pixel[2])];
                    a += static_cast<f32>(static_cast<u8>(pixel[3]));
                }
            }
            const f32 scale = 1.0f / static_cast<f32>((bottom - top) * (right - left));
            std::byte* target = out.data() + (static_cast<usize>(y) * size.x + x) * 4;
            target[0] = linearToSrgb(r * scale);
            target[1] = linearToSrgb(g * scale);
            target[2] = linearToSrgb(b * scale);
            target[3] = static_cast<std::byte>(static_cast<u8>(std::lround(a * scale)));
        }
    }
}

FrameCapture::FrameCapture() = default;
FrameCapture::~FrameCapture() = default;

std::unique_ptr<FrameCapture> FrameCapture::create(RenderDevice& device)
{
    std::unique_ptr<ReadbackRing> ring = ReadbackRing::create(device, 1);
    if (!ring)
    {
        return nullptr;
    }
    std::unique_ptr<FrameCapture> capture(new FrameCapture());
    capture->m_ring = std::move(ring);
    return capture;
}

bool FrameCapture::read(nvrhi::ITexture* texture, std::vector<std::byte>& out)
{
    ILL_PROFILE_SCOPE();
    if (texture == nullptr)
    {
        return false;
    }
    const nvrhi::TextureDesc& desc = texture->getDesc();
    if (desc.format != nvrhi::Format::RGBA8_UNORM && desc.format != nvrhi::Format::SRGBA8_UNORM)
    {
        return false;
    }
    const std::optional<u64> ticket = m_ring->request(texture);
    if (!ticket)
    {
        return false;
    }
    out.resize(static_cast<usize>(desc.width) * desc.height * 4);
    return m_ring->read(*ticket, out, true) == ReadbackStatus::Ready;
}

bool FrameCapture::readScaled(nvrhi::ITexture* texture, UVec2 size, std::vector<std::byte>& out)
{
    if (size.x == 0 || size.y == 0 || !read(texture, m_pixels))
    {
        return false;
    }
    const nvrhi::TextureDesc& desc = texture->getDesc();
    out.resize(static_cast<usize>(size.x) * size.y * 4);
    downscaleRgba8Srgb(m_pixels, UVec2(desc.width, desc.height), size, out);
    return true;
}

} // namespace illusion
