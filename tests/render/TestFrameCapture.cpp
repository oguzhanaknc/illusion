// The CPU scaling of FrameCapture (slot images, ADR 0058): centre crop to the destination's aspect ratio and
// a box filter in linear light.

#include "TestSupport.hpp"

#include <illusion/render/FrameCapture.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace illusion;

namespace
{

std::vector<std::byte> solid(UVec2 size, u8 r, u8 g, u8 b, u8 a = 255)
{
    std::vector<std::byte> pixels;
    for (u32 i = 0; i < size.x * size.y; ++i)
    {
        for (const u8 value : {r, g, b, a})
        {
            pixels.push_back(static_cast<std::byte>(value));
        }
    }
    return pixels;
}

u8 at(const std::vector<std::byte>& pixels, UVec2 size, u32 x, u32 y, u32 channel)
{
    return static_cast<u8>(pixels.at((static_cast<usize>(y) * size.x + x) * 4 + channel));
}

} // namespace

TEST_CASE("downscaling keeps a solid colour")
{
    const std::vector<std::byte> source = solid({64, 36}, 10, 120, 250, 77);
    std::vector<std::byte> out(16 * 9 * 4);
    downscaleRgba8Srgb(source, {64, 36}, {16, 9}, out);
    CHECK(out == solid({16, 9}, 10, 120, 250, 77));
}

TEST_CASE("downscaling averages black and white in linear light")
{
    // Columns alternate black and white: half the light, sRGB 188.
    std::vector<std::byte> source = solid({4, 2}, 0, 0, 0);
    for (u32 y = 0; y < 2; ++y)
    {
        for (u32 x = 1; x < 4; x += 2)
        {
            for (u32 c = 0; c < 3; ++c)
            {
                source.at((static_cast<usize>(y) * 4 + x) * 4 + c) = std::byte{255};
            }
        }
    }
    std::vector<std::byte> out(2 * 1 * 4);
    downscaleRgba8Srgb(source, {4, 2}, {2, 1}, out);
    CHECK(at(out, {2, 1}, 0, 0, 0) == 188);
    CHECK(at(out, {2, 1}, 1, 0, 1) == 188);
    CHECK(at(out, {2, 1}, 1, 0, 3) == 255);
}

TEST_CASE("downscaling a wider frame crops its sides, a taller one its top and bottom")
{
    // 32 x 9 with red outer quarters and a green centre: a 16:9 destination shows only the centre.
    std::vector<std::byte> wide = solid({32, 9}, 255, 0, 0);
    for (u32 y = 0; y < 9; ++y)
    {
        for (u32 x = 8; x < 24; ++x)
        {
            wide.at((static_cast<usize>(y) * 32 + x) * 4) = std::byte{0};
            wide.at((static_cast<usize>(y) * 32 + x) * 4 + 1) = std::byte{255};
        }
    }
    std::vector<std::byte> out(16 * 9 * 4);
    downscaleRgba8Srgb(wide, {32, 9}, {16, 9}, out);
    CHECK(out == solid({16, 9}, 0, 255, 0));

    std::vector<std::byte> tall = solid({16, 27}, 255, 0, 0);
    for (u32 y = 9; y < 18; ++y)
    {
        for (u32 x = 0; x < 16; ++x)
        {
            tall.at((static_cast<usize>(y) * 16 + x) * 4) = std::byte{0};
            tall.at((static_cast<usize>(y) * 16 + x) * 4 + 2) = std::byte{255};
        }
    }
    downscaleRgba8Srgb(tall, {16, 27}, {16, 9}, out);
    CHECK(out == solid({16, 9}, 0, 0, 255));
}

TEST_CASE("downscaling a smaller frame repeats its pixels, and it does not allocate")
{
    const std::vector<std::byte> source = solid({2, 1}, 40, 50, 60);
    std::vector<std::byte> out(8 * 4 * 4);
    {
        const test::ScopedAllocationCounter counter;
        downscaleRgba8Srgb(source, {2, 1}, {8, 4}, out);
        const usize allocations = counter.count();
        CHECK(allocations == 0);
    }
    CHECK(out == solid({8, 4}, 40, 50, 60));

    // Sizes that do not fit leave the output alone.
    std::vector<std::byte> untouched(4, std::byte{7});
    downscaleRgba8Srgb(source, {2, 1}, {8, 4}, untouched);
    CHECK(untouched == std::vector<std::byte>(4, std::byte{7}));
}
