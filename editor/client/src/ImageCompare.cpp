#include <illusion/assets/Image.hpp>
#include <illusion/editor/client/ImageCompare.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace illusion::editor::client
{
namespace
{

// The value of base64 digit @p c, or -1.
int digitOf(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    if (c == '+')
    {
        return 62;
    }
    if (c == '/')
    {
        return 63;
    }
    return -1;
}

std::optional<Image> decode(std::span<const std::byte> encoded)
{
    std::optional<Image> image = decodeImage(encoded, false);
    if (!image || image->format != ImageFormat::RGBA8 || !image->isValid())
    {
        return std::nullopt;
    }
    return image;
}

} // namespace

std::optional<std::vector<std::byte>> decodeBase64(std::string_view text)
{
    while (!text.empty() && text.back() == '=')
    {
        text.remove_suffix(1);
    }
    if (text.size() % 4 == 1)
    {
        return std::nullopt;
    }
    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3 + 2);
    u32 bits = 0;
    u32 count = 0;
    for (const char c : text)
    {
        const int digit = digitOf(c);
        if (digit < 0)
        {
            return std::nullopt;
        }
        bits = (bits << 6) | static_cast<u32>(digit);
        count += 6;
        if (count >= 8)
        {
            count -= 8;
            out.push_back(static_cast<std::byte>((bits >> count) & 0xFFu));
        }
    }
    return out;
}

bool isUniformImage(std::span<const std::byte> png)
{
    const std::optional<Image> image = decode(png);
    if (!image)
    {
        return false;
    }
    const std::byte* pixels = image->data.data();
    for (usize offset = 4; offset < image->mips[0].size; offset += 4)
    {
        if (std::memcmp(pixels, pixels + offset, 4) != 0)
        {
            return false;
        }
    }
    return true;
}

Expected<ImageComparison, std::string> compareImages(std::span<const std::byte> actual,
                                                     std::span<const std::byte> golden, u32 channelTolerance)
{
    const std::optional<Image> a = decode(actual);
    if (!a)
    {
        return unexpected(std::string("the image cannot be decoded"));
    }
    const std::optional<Image> b = decode(golden);
    if (!b)
    {
        return unexpected(std::string("the golden image cannot be decoded"));
    }
    const ImageMip& mip = a->mips[0];
    if (mip.width != b->mips[0].width || mip.height != b->mips[0].height)
    {
        return unexpected("the image is " + std::to_string(mip.width) + "x" + std::to_string(mip.height) +
                          ", the golden image " + std::to_string(b->mips[0].width) + "x" +
                          std::to_string(b->mips[0].height));
    }
    ImageComparison out;
    out.width = mip.width;
    out.height = mip.height;
    const usize pixels = static_cast<usize>(mip.width) * mip.height;
    usize different = 0;
    for (usize i = 0; i < pixels; ++i)
    {
        u32 largest = 0;
        for (usize c = 0; c < 4; ++c)
        {
            const int x = std::to_integer<int>(a->data[i * 4 + c]);
            const int y = std::to_integer<int>(b->data[i * 4 + c]);
            largest = std::max(largest, static_cast<u32>(std::abs(x - y)));
        }
        out.largestDifference = std::max(out.largestDifference, largest);
        different += largest > channelTolerance ? 1u : 0u;
    }
    out.differentFraction = static_cast<f64>(different) / static_cast<f64>(pixels);
    return out;
}

} // namespace illusion::editor::client
