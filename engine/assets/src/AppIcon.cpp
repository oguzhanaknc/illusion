#include <illusion/assets/AppIcon.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{
namespace
{

struct Tap
{
    u32 index;
    f32 weight;
};

// Source taps of every destination pixel of one axis: the covered area when shrinking, two bilinear taps
// when growing.
std::vector<std::vector<Tap>> axisTaps(u32 source, u32 destination)
{
    std::vector<std::vector<Tap>> taps(destination);
    const f64 scale = static_cast<f64>(source) / destination;
    for (u32 i = 0; i < destination; ++i)
    {
        if (source > destination)
        {
            const f64 begin = i * scale;
            const f64 end = begin + scale;
            for (u32 j = static_cast<u32>(begin); j < source && j < end; ++j)
            {
                const f64 overlap = std::min<f64>(end, j + 1.0) - std::max<f64>(begin, j);
                if (overlap > 0.0)
                {
                    taps[i].push_back(Tap{.index = j, .weight = static_cast<f32>(overlap / scale)});
                }
            }
        }
        else
        {
            const f64 centre = std::max(0.0, (i + 0.5) * scale - 0.5);
            const u32 j0 = std::min(static_cast<u32>(centre), source - 1);
            const u32 j1 = std::min(j0 + 1, source - 1);
            const f32 t = static_cast<f32>(centre - j0);
            taps[i].push_back(Tap{.index = j0, .weight = 1.0f - t});
            taps[i].push_back(Tap{.index = j1, .weight = t});
        }
    }
    return taps;
}

u8 encodeChannel(f32 linear, bool srgb) noexcept
{
    linear = std::clamp(linear, 0.0f, 1.0f);
    if (srgb)
    {
        linear = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }
    return static_cast<u8>(std::lround(linear * 255.0f));
}

// Stores premultiplied linear colours as an image of @p format.
Image storePremultiplied(const std::vector<Vec4>& pixels, u32 width, u32 height, ImageFormat format)
{
    Image image = makeSolidImage(width, height, Vec4(0.0f), format);
    const bool srgb = format == ImageFormat::RGBA8Srgb;
    for (usize i = 0; i < pixels.size(); ++i)
    {
        const Vec4 p = pixels[i];
        const f32 alpha = std::clamp(p.w, 0.0f, 1.0f);
        const f32 inverse = alpha > 0.0f ? 1.0f / alpha : 0.0f;
        image.data[i * 4 + 0] = std::byte{encodeChannel(p.x * inverse, srgb)};
        image.data[i * 4 + 1] = std::byte{encodeChannel(p.y * inverse, srgb)};
        image.data[i * 4 + 2] = std::byte{encodeChannel(p.z * inverse, srgb)};
        image.data[i * 4 + 3] = std::byte{encodeChannel(alpha, false)};
    }
    return image;
}

bool isIconImage(const Image& image) noexcept
{
    return image.isValid() &&
           (image.format == ImageFormat::RGBA8 || image.format == ImageFormat::RGBA8Srgb) &&
           image.width() == image.height() && image.width() <= 256;
}

void put16(std::vector<std::byte>& out, u32 value)
{
    out.push_back(static_cast<std::byte>(value & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
}

void put32(std::vector<std::byte>& out, u32 value)
{
    put16(out, value & 0xffffu);
    put16(out, value >> 16);
}

// A 32-bit BGRA bitmap, bottom-up, with an all-zero AND mask: the "DIB" form of an icon image.
std::vector<std::byte> iconBitmap(const Image& image)
{
    const u32 size = image.width();
    const u32 maskRow = (size + 31) / 32 * 4;
    std::vector<std::byte> out;
    put32(out, 40);
    put32(out, size);
    put32(out, size * 2); // colour and mask rows
    put16(out, 1);
    put16(out, 32);
    put32(out, 0);
    put32(out, size * size * 4 + maskRow * size);
    for (int i = 0; i < 4; ++i)
    {
        put32(out, 0);
    }
    const std::span<const std::byte> pixels = image.mip(0);
    for (u32 y = size; y-- > 0;)
    {
        for (u32 x = 0; x < size; ++x)
        {
            const usize p = (static_cast<usize>(y) * size + x) * 4;
            out.push_back(pixels[p + 2]);
            out.push_back(pixels[p + 1]);
            out.push_back(pixels[p + 0]);
            out.push_back(pixels[p + 3]);
        }
    }
    out.resize(out.size() + static_cast<usize>(maskRow) * size, std::byte{0});
    return out;
}

f32 smoothCoverage(f32 distance, f32 pixel) noexcept
{
    return std::clamp(0.5f - distance / pixel, 0.0f, 1.0f);
}

} // namespace

Image resizeImage(const Image& image, u32 width, u32 height)
{
    if (!image.isValid() || width == 0 || height == 0 ||
        (image.format != ImageFormat::RGBA8 && image.format != ImageFormat::RGBA8Srgb))
    {
        return Image{};
    }
    const u32 sourceWidth = image.width();
    const u32 sourceHeight = image.height();
    std::vector<Vec4> source(static_cast<usize>(sourceWidth) * sourceHeight);
    for (u32 y = 0; y < sourceHeight; ++y)
    {
        for (u32 x = 0; x < sourceWidth; ++x)
        {
            const Vec4 p = readPixel(image, x, y);
            source[static_cast<usize>(y) * sourceWidth + x] = Vec4(Vec3(p) * p.w, p.w);
        }
    }
    const std::vector<std::vector<Tap>> columns = axisTaps(sourceWidth, width);
    const std::vector<std::vector<Tap>> rows = axisTaps(sourceHeight, height);
    std::vector<Vec4> horizontal(static_cast<usize>(width) * sourceHeight, Vec4(0.0f));
    for (u32 y = 0; y < sourceHeight; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            Vec4 sum(0.0f);
            for (const Tap& tap : columns[x])
            {
                sum += source[static_cast<usize>(y) * sourceWidth + tap.index] * tap.weight;
            }
            horizontal[static_cast<usize>(y) * width + x] = sum;
        }
    }
    std::vector<Vec4> result(static_cast<usize>(width) * height, Vec4(0.0f));
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            Vec4 sum(0.0f);
            for (const Tap& tap : rows[y])
            {
                sum += horizontal[static_cast<usize>(tap.index) * width + x] * tap.weight;
            }
            result[static_cast<usize>(y) * width + x] = sum;
        }
    }
    return storePremultiplied(result, width, height, image.format);
}

std::vector<std::byte> encodeIco(std::span<const Image> images)
{
    if (images.empty() || images.size() > 0xffffu ||
        !std::all_of(images.begin(), images.end(), [](const Image& image) { return isIconImage(image); }))
    {
        return {};
    }
    std::vector<std::vector<std::byte>> payloads;
    for (const Image& image : images)
    {
        payloads.push_back(image.width() == 256 ? encodePng(image) : iconBitmap(image));
        if (payloads.back().empty())
        {
            return {};
        }
    }
    std::vector<std::byte> out;
    put16(out, 0);
    put16(out, 1);
    put16(out, static_cast<u32>(images.size()));
    u32 offset = 6 + 16 * static_cast<u32>(images.size());
    for (usize i = 0; i < images.size(); ++i)
    {
        const u32 size = images[i].width();
        out.push_back(static_cast<std::byte>(size & 0xffu)); // 256 is written as 0
        out.push_back(static_cast<std::byte>(size & 0xffu));
        out.push_back(std::byte{0});
        out.push_back(std::byte{0});
        put16(out, 1);
        put16(out, 32);
        put32(out, static_cast<u32>(payloads[i].size()));
        put32(out, offset);
        offset += static_cast<u32>(payloads[i].size());
    }
    for (const std::vector<std::byte>& payload : payloads)
    {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

Image makeEngineIcon(u32 size)
{
    if (size == 0)
    {
        return Image{};
    }
    constexpr u32 kSamples = 4;
    const f32 pixel = 1.0f / static_cast<f32>(size * kSamples);
    // Linear colours of the background gradient, top to bottom, and of the mark.
    const Vec3 top(0.16f, 0.04f, 0.62f);
    const Vec3 bottom(0.01f, 0.18f, 0.86f);
    const Vec3 mark(1.0f);
    std::vector<Vec4> pixels(static_cast<usize>(size) * size, Vec4(0.0f));
    for (u32 y = 0; y < size; ++y)
    {
        for (u32 x = 0; x < size; ++x)
        {
            Vec4 sum(0.0f);
            for (u32 sy = 0; sy < kSamples; ++sy)
            {
                for (u32 sx = 0; sx < kSamples; ++sx)
                {
                    const f32 u = (static_cast<f32>(x * kSamples + sx) + 0.5f) * pixel;
                    const f32 v = (static_cast<f32>(y * kSamples + sy) + 0.5f) * pixel;
                    // A rounded square with a margin of 4% and corners of 20%.
                    const f32 qx = std::max(std::abs(u - 0.5f) - 0.26f, 0.0f);
                    const f32 qy = std::max(std::abs(v - 0.5f) - 0.26f, 0.0f);
                    const f32 square = std::sqrt(qx * qx + qy * qy) - 0.20f;
                    const f32 background = smoothCoverage(square, pixel);
                    // A ring and an off-centre disc: an eye that seems to look aside.
                    const f32 d = std::hypot(u - 0.5f, v - 0.5f);
                    const f32 ring = smoothCoverage(std::abs(d - 0.27f) - 0.05f, pixel);
                    const f32 disc = smoothCoverage(std::hypot(u - 0.56f, v - 0.45f) - 0.10f, pixel);
                    const f32 markCoverage = std::max(ring, disc) * background;
                    const Vec3 colour = top + (bottom - top) * v;
                    const Vec3 mixed = colour + (mark - colour) * markCoverage;
                    sum += Vec4(mixed * background, background);
                }
            }
            pixels[static_cast<usize>(y) * size + x] = sum / static_cast<f32>(kSamples * kSamples);
        }
    }
    return storePremultiplied(pixels, size, size, ImageFormat::RGBA8Srgb);
}

} // namespace illusion
