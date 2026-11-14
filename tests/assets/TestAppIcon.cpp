#include <illusion/assets/AppIcon.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

using namespace illusion;

namespace
{

u32 read16(const std::vector<std::byte>& data, usize offset)
{
    return static_cast<u32>(data[offset]) | (static_cast<u32>(data[offset + 1]) << 8);
}

u32 read32(const std::vector<std::byte>& data, usize offset)
{
    return read16(data, offset) | (read16(data, offset + 2) << 16);
}

} // namespace

TEST_CASE("resizing keeps a solid colour and averages when shrinking")
{
    const Image solid = makeSolidImage(300, 300, Vec4(0.25f, 0.5f, 0.75f, 1.0f), ImageFormat::RGBA8);
    const Image small = resizeImage(solid, 16, 16);
    REQUIRE(small.width() == 16);
    REQUIRE(small.height() == 16);
    CHECK(small.format == ImageFormat::RGBA8);
    for (const Vec4 pixel : {readPixel(small, 0, 0), readPixel(small, 15, 15), readPixel(small, 7, 3)})
    {
        CHECK(pixel.x == doctest::Approx(0.25f).epsilon(0.01));
        CHECK(pixel.y == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(pixel.z == doctest::Approx(0.75f).epsilon(0.01));
        CHECK(pixel.w == doctest::Approx(1.0f));
    }

    // Two columns, black and white: one pixel is their average in linear light.
    Image halves = makeSolidImage(2, 1, Vec4(0.0f, 0.0f, 0.0f, 1.0f), ImageFormat::RGBA8);
    for (int c = 0; c < 3; ++c)
    {
        halves.data[4 + static_cast<usize>(c)] = std::byte{255};
    }
    const Image one = resizeImage(halves, 1, 1);
    CHECK(readPixel(one, 0, 0).x == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("resizing does not bleed the colour of transparent pixels")
{
    Image image = makeSolidImage(2, 1, Vec4(1.0f, 0.0f, 0.0f, 1.0f), ImageFormat::RGBA8Srgb);
    // The second pixel is transparent green.
    image.data[4] = std::byte{0};
    image.data[5] = std::byte{255};
    image.data[7] = std::byte{0};
    const Image one = resizeImage(image, 1, 1);
    const Vec4 pixel = readPixel(one, 0, 0);
    CHECK(pixel.x == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(pixel.y == doctest::Approx(0.0f));
    CHECK(pixel.w == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("resizing grows an image bilinearly and rejects what it cannot resample")
{
    const Image solid = makeSolidImage(4, 4, Vec4(0.5f, 0.5f, 0.5f, 1.0f), ImageFormat::RGBA8Srgb);
    const Image large = resizeImage(solid, 64, 32);
    CHECK(large.width() == 64);
    CHECK(large.height() == 32);
    CHECK(readPixel(large, 63, 31).x == doctest::Approx(readPixel(solid, 3, 3).x).epsilon(0.01));
    CHECK_FALSE(resizeImage(solid, 0, 4).isValid());
    CHECK_FALSE(resizeImage(Image{}, 4, 4).isValid());
    CHECK_FALSE(resizeImage(makeSolidImage(4, 4, Vec4(1.0f), ImageFormat::RGBA32F), 2, 2).isValid());
}

TEST_CASE("an ico file holds every size, bitmaps below 256 pixels and a PNG at 256")
{
    const Image source = makeEngineIcon(512);
    std::vector<Image> images;
    for (const u32 size : kWindowsIconSizes)
    {
        images.push_back(resizeImage(source, size, size));
    }
    const std::vector<std::byte> ico = encodeIco(images);
    REQUIRE(ico.size() > 6 + 16 * images.size());
    CHECK(read16(ico, 0) == 0);
    CHECK(read16(ico, 2) == 1);
    REQUIRE(read16(ico, 4) == images.size());
    for (usize i = 0; i < images.size(); ++i)
    {
        CAPTURE(i);
        const usize entry = 6 + 16 * i;
        const u32 size = kWindowsIconSizes[i];
        CHECK(static_cast<u32>(ico[entry]) == (size == 256 ? 0u : size));
        CHECK(read16(ico, entry + 6) == 32);
        const u32 bytes = read32(ico, entry + 8);
        const u32 offset = read32(ico, entry + 12);
        REQUIRE(static_cast<usize>(offset) + bytes <= ico.size());
        if (size == 256)
        {
            CHECK(static_cast<u8>(ico[offset + 1]) == 'P');
            CHECK(static_cast<u8>(ico[offset + 2]) == 'N');
            CHECK(static_cast<u8>(ico[offset + 3]) == 'G');
        }
        else
        {
            CHECK(read32(ico, offset) == 40);
            CHECK(read32(ico, offset + 4) == size);
            CHECK(read32(ico, offset + 8) == size * 2);
            CHECK(bytes == 40 + size * size * 4 + (size + 31) / 32 * 4 * size);
        }
    }
}

TEST_CASE("an ico file needs square images of at most 256 pixels")
{
    CHECK(encodeIco({}).empty());
    const std::vector<Image> wide{makeSolidImage(32, 16, Vec4(1.0f), ImageFormat::RGBA8)};
    CHECK(encodeIco(wide).empty());
    const std::vector<Image> large{makeSolidImage(512, 512, Vec4(1.0f), ImageFormat::RGBA8)};
    CHECK(encodeIco(large).empty());
}

TEST_CASE("the engine icon is opaque in the middle and transparent in the corners")
{
    const Image icon = makeEngineIcon(64);
    REQUIRE(icon.width() == 64);
    CHECK(icon.format == ImageFormat::RGBA8Srgb);
    CHECK(readPixel(icon, 0, 0).w == doctest::Approx(0.0f));
    CHECK(readPixel(icon, 32, 32).w == doctest::Approx(1.0f));
    CHECK(readPixel(icon, 10, 32).w == doctest::Approx(1.0f));
    CHECK_FALSE(makeEngineIcon(0).isValid());
}
