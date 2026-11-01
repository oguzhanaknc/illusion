#include <illusion/assets/Image.hpp>
#include <illusion/core/Assert.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stb_image.h>
#include <stb_image_write.h>

namespace illusion
{
namespace
{

// sRGB <-> linear conversion through lookup tables (decode) and the exact formula (encode).
const std::array<f32, 256>& srgbToLinearTable()
{
    static const std::array<f32, 256> table = [] {
        std::array<f32, 256> values{};
        for (usize i = 0; i < values.size(); ++i)
        {
            const f32 c = static_cast<f32>(i) / 255.0f;
            values[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return values;
    }();
    return table;
}

u8 linearToSrgbByte(f32 linear) noexcept
{
    linear = std::clamp(linear, 0.0f, 1.0f);
    const f32 c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(c * 255.0f));
}

void appendLevel(Image& image, u32 width, u32 height)
{
    ImageMip mip;
    mip.width = width;
    mip.height = height;
    mip.offset = image.data.size();
    mip.size = static_cast<usize>(width) * height * bytesPerPixel(image.format);
    image.mips.push_back(mip);
    image.data.resize(image.data.size() + mip.size);
}

// Box-filters `source` (w x h) into `dest` ((w/2) x (h/2), each at least 1). Odd edges reuse the last
// row/column so 1-pixel dimensions are handled.
template <class Load, class Store>
void downsample(u32 srcWidth, u32 srcHeight, u32 dstWidth, u32 dstHeight, Load load, Store store)
{
    for (u32 y = 0; y < dstHeight; ++y)
    {
        const u32 y0 = std::min(y * 2, srcHeight - 1);
        const u32 y1 = std::min(y * 2 + 1, srcHeight - 1);
        for (u32 x = 0; x < dstWidth; ++x)
        {
            const u32 x0 = std::min(x * 2, srcWidth - 1);
            const u32 x1 = std::min(x * 2 + 1, srcWidth - 1);
            const Vec4 sum = load(x0, y0) + load(x1, y0) + load(x0, y1) + load(x1, y1);
            store(x, y, sum * 0.25f);
        }
    }
}

} // namespace

void limitImageSize(Image& image, u32 maxSize)
{
    if (maxSize == 0 || !image.isValid() || std::max(image.width(), image.height()) <= maxSize)
    {
        return;
    }
    const bool keepChain = image.mips.size() > 1;
    generateMips(image);
    usize first = 0;
    while (std::max(image.mips[first].width, image.mips[first].height) > maxSize)
    {
        ++first; // The chain ends at 1x1, and maxSize is at least 1.
    }
    const usize last = keepChain ? image.mips.size() : first + 1;
    const usize begin = image.mips[first].offset;
    const usize end = image.mips[last - 1].offset + image.mips[last - 1].size;
    std::vector<std::byte> data(image.data.begin() + static_cast<std::ptrdiff_t>(begin),
                                image.data.begin() + static_cast<std::ptrdiff_t>(end));
    std::vector<ImageMip> mips(image.mips.begin() + static_cast<std::ptrdiff_t>(first),
                               image.mips.begin() + static_cast<std::ptrdiff_t>(last));
    for (ImageMip& mip : mips)
    {
        mip.offset -= begin;
    }
    image.data = std::move(data);
    image.mips = std::move(mips);
}

u32 fullMipCount(u32 width, u32 height) noexcept
{
    u32 levels = 1;
    u32 size = std::max(width, height);
    while (size > 1)
    {
        size /= 2;
        ++levels;
    }
    return levels;
}

Image makeSolidImage(u32 width, u32 height, Vec4 color, ImageFormat format)
{
    Image image;
    image.format = format;
    appendLevel(image, std::max(width, 1u), std::max(height, 1u));
    const usize pixels = static_cast<usize>(image.mips[0].width) * image.mips[0].height;
    if (format == ImageFormat::RGBA32F)
    {
        for (usize i = 0; i < pixels; ++i)
        {
            std::memcpy(image.data.data() + i * 16, &color.x, 16);
        }
    }
    else
    {
        const std::array<u8, 4> bytes = {
            static_cast<u8>(std::lround(std::clamp(color.r, 0.0f, 1.0f) * 255.0f)),
            static_cast<u8>(std::lround(std::clamp(color.g, 0.0f, 1.0f) * 255.0f)),
            static_cast<u8>(std::lround(std::clamp(color.b, 0.0f, 1.0f) * 255.0f)),
            static_cast<u8>(std::lround(std::clamp(color.a, 0.0f, 1.0f) * 255.0f))};
        for (usize i = 0; i < pixels; ++i)
        {
            std::memcpy(image.data.data() + i * 4, bytes.data(), 4);
        }
    }
    return image;
}

std::optional<Image> decodeImage(std::span<const std::byte> encoded, bool srgb, std::string* error)
{
    if (encoded.empty() || encoded.size() > static_cast<usize>(std::numeric_limits<int>::max()))
    {
        if (error != nullptr)
        {
            *error = "image data is empty or too large";
        }
        return std::nullopt;
    }
    const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    const int length = static_cast<int>(encoded.size());
    int width = 0;
    int height = 0;
    int channels = 0;

    Image image;
    if (stbi_is_hdr_from_memory(bytes, length) != 0)
    {
        f32* pixels = stbi_loadf_from_memory(bytes, length, &width, &height, &channels, 4);
        if (pixels == nullptr)
        {
            if (error != nullptr)
            {
                *error = stbi_failure_reason();
            }
            return std::nullopt;
        }
        image.format = ImageFormat::RGBA32F;
        appendLevel(image, static_cast<u32>(width), static_cast<u32>(height));
        std::memcpy(image.data.data(), pixels, image.data.size());
        stbi_image_free(pixels);
        return image;
    }

    stbi_uc* pixels = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        if (error != nullptr)
        {
            *error = stbi_failure_reason();
        }
        return std::nullopt;
    }
    image.format = srgb ? ImageFormat::RGBA8Srgb : ImageFormat::RGBA8;
    appendLevel(image, static_cast<u32>(width), static_cast<u32>(height));
    std::memcpy(image.data.data(), pixels, image.data.size());
    stbi_image_free(pixels);
    return image;
}

std::vector<std::byte> encodePng(const Image& image)
{
    std::vector<std::byte> png;
    if (!image.isValid() || image.format == ImageFormat::RGBA32F)
    {
        return png;
    }
    const auto write = [](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<std::byte>*>(context);
        const auto* bytes = static_cast<const std::byte*>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    const int width = static_cast<int>(image.width());
    stbi_write_png_to_func(write, &png, width, static_cast<int>(image.height()), 4, image.data.data(),
                           width * 4);
    return png;
}

Vec4 readPixel(const Image& image, u32 x, u32 y) noexcept
{
    ILL_ASSERT(x < image.width() && y < image.height(), "readPixel outside the image");
    const usize index = static_cast<usize>(y) * image.width() + x;
    if (image.format == ImageFormat::RGBA32F)
    {
        Vec4 pixel;
        std::memcpy(&pixel.x, image.data.data() + index * 16, 16);
        return pixel;
    }
    const auto* p = reinterpret_cast<const u8*>(image.data.data()) + index * 4;
    if (image.format == ImageFormat::RGBA8Srgb)
    {
        const std::array<f32, 256>& toLinear = srgbToLinearTable();
        return Vec4(toLinear[p[0]], toLinear[p[1]], toLinear[p[2]], static_cast<f32>(p[3]) / 255.0f);
    }
    return Vec4(p[0], p[1], p[2], p[3]) / 255.0f;
}

void generateMips(Image& image)
{
    ILL_ASSERT(image.isValid(), "generateMips on an empty image");
    if (!image.isValid())
    {
        return;
    }
    image.mips.resize(1);
    image.data.resize(image.mips[0].size);
    const u32 levels = fullMipCount(image.width(), image.height());

    for (u32 level = 1; level < levels; ++level)
    {
        const ImageMip source = image.mips[level - 1];
        const u32 width = std::max(source.width / 2, 1u);
        const u32 height = std::max(source.height / 2, 1u);
        appendLevel(image, width, height);
        const ImageMip& dest = image.mips[level];
        std::byte* base = image.data.data(); // stable until the next appendLevel()

        if (image.format == ImageFormat::RGBA32F)
        {
            const auto* src = reinterpret_cast<const f32*>(base + source.offset);
            auto* dst = reinterpret_cast<f32*>(base + dest.offset);
            downsample(
                source.width, source.height, width, height,
                [&](u32 x, u32 y) {
                    const f32* p = src + (static_cast<usize>(y) * source.width + x) * 4;
                    return Vec4(p[0], p[1], p[2], p[3]);
                },
                [&](u32 x, u32 y, Vec4 v) {
                    std::memcpy(dst + (static_cast<usize>(y) * width + x) * 4, &v.x, 16);
                });
        }
        else
        {
            const bool srgb = image.format == ImageFormat::RGBA8Srgb;
            const std::array<f32, 256>& toLinear = srgbToLinearTable();
            const auto* src = reinterpret_cast<const u8*>(base + source.offset);
            auto* dst = reinterpret_cast<u8*>(base + dest.offset);
            downsample(
                source.width, source.height, width, height,
                [&](u32 x, u32 y) {
                    const u8* p = src + (static_cast<usize>(y) * source.width + x) * 4;
                    if (srgb)
                    {
                        return Vec4(toLinear[p[0]], toLinear[p[1]], toLinear[p[2]],
                                    static_cast<f32>(p[3]) / 255.0f);
                    }
                    return Vec4(p[0], p[1], p[2], p[3]) / 255.0f;
                },
                [&](u32 x, u32 y, Vec4 v) {
                    u8* p = dst + (static_cast<usize>(y) * width + x) * 4;
                    if (srgb)
                    {
                        p[0] = linearToSrgbByte(v.r);
                        p[1] = linearToSrgbByte(v.g);
                        p[2] = linearToSrgbByte(v.b);
                    }
                    else
                    {
                        p[0] = static_cast<u8>(std::lround(v.r * 255.0f));
                        p[1] = static_cast<u8>(std::lround(v.g * 255.0f));
                        p[2] = static_cast<u8>(std::lround(v.b * 255.0f));
                    }
                    p[3] = static_cast<u8>(std::lround(v.a * 255.0f));
                });
        }
    }
}

} // namespace illusion
