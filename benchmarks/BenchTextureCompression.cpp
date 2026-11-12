#include "Budget.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/assets/TextureCompression.hpp>

#include <cstring>
#include <random>

using namespace illusion;

namespace
{

constexpr u32 kSize = 256;
constexpr usize kTexels = static_cast<usize>(kSize) * kSize;

// Smooth gradients with fixed-seed noise on top, so the encoders search as they do on real textures.
Image makeColourImage(ImageFormat format)
{
    Image image = makeSolidImage(kSize, kSize, Vec4(0.0f), format);
    auto* pixels = reinterpret_cast<u8*>(image.data.data());
    std::mt19937 random(42);
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            u8* p = pixels + (static_cast<usize>(y) * kSize + x) * 4;
            const u32 noise = random() & 0x1Fu;
            p[0] = static_cast<u8>(x + noise / 2);
            p[1] = static_cast<u8>(y ^ (noise & 0x0Fu));
            p[2] = static_cast<u8>((x + y) / 2);
            p[3] = 255;
        }
    }
    return image;
}

Image makeHdrImage()
{
    Image image = makeSolidImage(kSize, kSize, Vec4(0.0f), ImageFormat::RGBA32F);
    auto* pixels = reinterpret_cast<f32*>(image.data.data());
    std::mt19937 random(42);
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            const f32 noise = static_cast<f32>(random() & 0xFFu) / 2550.0f;
            const Vec4 value(static_cast<f32>(x) / 16.0f + noise, static_cast<f32>(y) / 64.0f, 0.5f + noise,
                             1.0f);
            std::memcpy(pixels + (static_cast<usize>(y) * kSize + x) * 4, &value.x, 16);
        }
    }
    return image;
}

void encode(ankerl::nanobench::Bench& bench, const char* name, const Image& image,
            TextureCompression compression, TextureQuality quality)
{
    bench.batch(kTexels).unit("texel").run(
        name, [&] { ankerl::nanobench::doNotOptimizeAway(compressImage(image, compression, quality)); });
}

} // namespace

// Budgets are roughly 5-10x a desktop result (single thread, 256x256, one level); see Budget.hpp.

ILL_BENCHMARK("assets/compress-bc7-fast", 2'500.0)
{
    encode(bench, "BC7 Fast (sRGB, 256x256)", makeColourImage(ImageFormat::RGBA8Srgb),
           TextureCompression::BC7, TextureQuality::Fast);
}

ILL_BENCHMARK("assets/compress-bc7-normal", 5'000.0)
{
    encode(bench, "BC7 Normal (sRGB, 256x256)", makeColourImage(ImageFormat::RGBA8Srgb),
           TextureCompression::BC7, TextureQuality::Normal);
}

ILL_BENCHMARK("assets/compress-bc5", 2'000.0)
{
    encode(bench, "BC5 Normal (256x256)", makeColourImage(ImageFormat::RGBA8), TextureCompression::BC5,
           TextureQuality::Normal);
}

ILL_BENCHMARK("assets/compress-bc4", 1'000.0)
{
    encode(bench, "BC4 Normal (256x256)", makeColourImage(ImageFormat::RGBA8), TextureCompression::BC4,
           TextureQuality::Normal);
}

ILL_BENCHMARK("assets/compress-bc6h", 600.0)
{
    encode(bench, "BC6H Normal (256x256)", makeHdrImage(), TextureCompression::BC6H, TextureQuality::Normal);
}
