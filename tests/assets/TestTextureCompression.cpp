#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/assets/TextureCompression.hpp>
#include <illusion/core/JobSystem.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::array<TextureQuality, 3> kQualities = {TextureQuality::Fast, TextureQuality::Normal,
                                                      TextureQuality::High};
constexpr f64 kPsnrMargin = 0.5;

u8* pixelsOf(Image& image, usize level = 0)
{
    return reinterpret_cast<u8*>(image.data.data() + image.mips[level].offset);
}

Image makeGradient(u32 width, u32 height, ImageFormat format)
{
    Image image = makeSolidImage(width, height, Vec4(0.0f), format);
    u8* pixels = pixelsOf(image);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            u8* p = pixels + (static_cast<usize>(y) * width + x) * 4;
            p[0] = static_cast<u8>(x * 255 / std::max(width - 1, 1u));
            p[1] = static_cast<u8>(y * 255 / std::max(height - 1, 1u));
            p[2] = static_cast<u8>((x + y) * 255 / std::max(width + height - 2, 1u));
            p[3] = static_cast<u8>(255 - x * 128 / std::max(width - 1, 1u));
        }
    }
    return image;
}

// Smoothed noise: a fixed-seed random field averaged over 2x2 pixels, so it has detail at several scales.
Image makeNoise(u32 width, u32 height, ImageFormat format, u32 seed)
{
    std::mt19937 random(seed);
    std::vector<u8> field(static_cast<usize>(width + 1) * (height + 1) * 4);
    for (u8& value : field)
    {
        value = static_cast<u8>(random() & 0xFFu);
    }
    Image image = makeSolidImage(width, height, Vec4(0.0f), format);
    u8* pixels = pixelsOf(image);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            for (u32 c = 0; c < 4; ++c)
            {
                const auto at = [&](u32 fx, u32 fy) {
                    return static_cast<u32>(field[(static_cast<usize>(fy) * (width + 1) + fx) * 4 + c]);
                };
                const u32 sum = at(x, y) + at(x + 1, y) + at(x, y + 1) + at(x + 1, y + 1);
                pixels[(static_cast<usize>(y) * width + x) * 4 + c] = static_cast<u8>(sum / 4);
            }
        }
    }
    return image;
}

// A tangent-space normal map of a bumpy surface, encoded as n * 0.5 + 0.5.
Image makeNormalMap(u32 width, u32 height)
{
    Image image = makeSolidImage(width, height, Vec4(0.0f), ImageFormat::RGBA8);
    u8* pixels = pixelsOf(image);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(width) * 6.2831853f * 3.0f;
            const f32 v = static_cast<f32>(y) / static_cast<f32>(height) * 6.2831853f * 2.0f;
            const Vec3 n = glm::normalize(
                Vec3(-0.6f * std::cos(u) * std::sin(v), -0.6f * std::sin(u) * std::cos(v), 1.0f));
            u8* p = pixels + (static_cast<usize>(y) * width + x) * 4;
            p[0] = static_cast<u8>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
            p[1] = static_cast<u8>(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
            p[2] = static_cast<u8>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
            p[3] = 255;
        }
    }
    return image;
}

// Values from 0.01 to 4 with a 1000x bright spot in the middle.
Image makeHdr(u32 width, u32 height)
{
    Image image = makeSolidImage(width, height, Vec4(0.0f), ImageFormat::RGBA32F);
    auto* pixels = reinterpret_cast<f32*>(image.data.data());
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const f32 fx = static_cast<f32>(x) / static_cast<f32>(width - 1);
            const f32 fy = static_cast<f32>(y) / static_cast<f32>(height - 1);
            Vec4 value(0.01f + 4.0f * fx, 0.01f + 2.0f * fy * fy, 0.5f + 0.5f * fx * fy, 1.0f);
            const f32 dx = static_cast<f32>(x) - static_cast<f32>(width) / 2.0f;
            const f32 dy = static_cast<f32>(y) - static_cast<f32>(height) / 2.0f;
            if (dx * dx + dy * dy < 9.0f)
            {
                value = Vec4(1000.0f, 900.0f, 800.0f, 1.0f);
            }
            std::memcpy(pixels + (static_cast<usize>(y) * width + x) * 4, &value.x, 16);
        }
    }
    return image;
}

// Peak signal-to-noise ratio (dB) of the channels [first, first + count) of mip 0 of two RGBA8 images.
f64 psnr(const Image& expected, const Image& actual, u32 first, u32 count)
{
    REQUIRE(expected.width() == actual.width());
    REQUIRE(expected.height() == actual.height());
    const auto* a = reinterpret_cast<const u8*>(expected.data.data());
    const auto* b = reinterpret_cast<const u8*>(actual.data.data());
    f64 sum = 0.0;
    const usize pixels = static_cast<usize>(expected.width()) * expected.height();
    for (usize i = 0; i < pixels; ++i)
    {
        for (u32 c = first; c < first + count; ++c)
        {
            const f64 d = static_cast<f64>(a[i * 4 + c]) - static_cast<f64>(b[i * 4 + c]);
            sum += d * d;
        }
    }
    const f64 mse = sum / static_cast<f64>(pixels * count);
    return mse == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Root mean square of log2(actual / expected) over the RGB channels of mip 0 of two RGBA32F images.
f64 logRmse(const Image& expected, const Image& actual)
{
    const auto* a = reinterpret_cast<const f32*>(expected.data.data());
    const auto* b = reinterpret_cast<const f32*>(actual.data.data());
    f64 sum = 0.0;
    const usize pixels = static_cast<usize>(expected.width()) * expected.height();
    for (usize i = 0; i < pixels; ++i)
    {
        for (usize c = 0; c < 3; ++c)
        {
            const f64 d =
                std::log2((static_cast<f64>(b[i * 4 + c]) + 1e-3) / (static_cast<f64>(a[i * 4 + c]) + 1e-3));
            sum += d * d;
        }
    }
    return std::sqrt(sum / static_cast<f64>(pixels * 3));
}

Image compressed(const Image& image, TextureCompression compression,
                 TextureQuality quality = TextureQuality::Normal, JobSystem* jobs = nullptr)
{
    std::optional<Image> result = compressImage(image, compression, quality, jobs);
    REQUIRE(result.has_value());
    return std::move(*result);
}

Image decompressed(const Image& image)
{
    std::optional<Image> result = decompressImage(image);
    REQUIRE(result.has_value());
    return std::move(*result);
}

BakedEnvironment makeBakedEnvironment()
{
    BakedEnvironment baked;
    baked.backgroundSize = 16;
    baked.backgroundMips = 5;
    baked.specularSize = 8;
    baked.specularMips = 4;
    baked.irradiance.coefficients[0] = Vec3(1.0f, 2.0f, 3.0f);
    // A smooth sky per face: brighter towards the top, warmer towards one side.
    const auto fill = [](std::vector<u64>& texels, u32 size, u32 mips, f32 scale) {
        for (u32 mip = 0; mip < mips; ++mip)
        {
            const u32 width = std::max(size >> mip, 1u);
            for (u32 face = 0; face < 6; ++face)
            {
                for (u32 y = 0; y < width; ++y)
                {
                    for (u32 x = 0; x < width; ++x)
                    {
                        const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(width);
                        const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(width);
                        const f32 sky = scale * (0.2f + 0.8f * (1.0f - v) * (1.0f - v)) *
                                        (1.0f + 0.1f * static_cast<f32>(face));
                        texels.push_back(
                            glm::packHalf4x16(Vec4(sky * (0.8f + 0.4f * u), sky, sky * 1.3f, 1.0f)));
                    }
                }
            }
        }
    };
    fill(baked.background, 16, 5, 8.0f);
    fill(baked.specular, 8, 4, 2.0f);
    return baked;
}

} // namespace

TEST_CASE("texture compression and quality names are the sidecar spellings")
{
    CHECK(textureCompressionName(TextureCompression::None) == "none");
    CHECK(textureCompressionName(TextureCompression::BC7) == "bc7");
    CHECK(textureCompressionName(TextureCompression::BC5) == "bc5");
    CHECK(textureCompressionName(TextureCompression::BC4) == "bc4");
    CHECK(textureCompressionName(TextureCompression::BC6H) == "bc6h");
    CHECK(textureQualityName(TextureQuality::Fast) == "fast");
    CHECK(textureQualityName(TextureQuality::Normal) == "normal");
    CHECK(textureQualityName(TextureQuality::High) == "high");
}

TEST_CASE("resolveCompression keeps None, sends float images to BC6H and 8-bit images away from it")
{
    for (const ImageFormat source : {ImageFormat::RGBA8, ImageFormat::RGBA8Srgb, ImageFormat::RGBA32F})
    {
        CHECK(resolveCompression(TextureCompression::None, source) == TextureCompression::None);
    }
    for (const TextureCompression requested : {TextureCompression::BC7, TextureCompression::BC5,
                                               TextureCompression::BC4, TextureCompression::BC6H})
    {
        CHECK(resolveCompression(requested, ImageFormat::RGBA32F) == TextureCompression::BC6H);
    }
    for (const ImageFormat source : {ImageFormat::RGBA8, ImageFormat::RGBA8Srgb})
    {
        CHECK(resolveCompression(TextureCompression::BC7, source) == TextureCompression::BC7);
        CHECK(resolveCompression(TextureCompression::BC5, source) == TextureCompression::BC5);
        CHECK(resolveCompression(TextureCompression::BC4, source) == TextureCompression::BC4);
        CHECK(resolveCompression(TextureCompression::BC6H, source) == TextureCompression::BC7);
    }

    CHECK(compressedFormat(TextureCompression::None, ImageFormat::RGBA8Srgb) == ImageFormat::RGBA8Srgb);
    CHECK(compressedFormat(TextureCompression::BC7, ImageFormat::RGBA8) == ImageFormat::BC7);
    CHECK(compressedFormat(TextureCompression::BC7, ImageFormat::RGBA8Srgb) == ImageFormat::BC7Srgb);
    CHECK(compressedFormat(TextureCompression::BC5, ImageFormat::RGBA8) == ImageFormat::BC5);
    CHECK(compressedFormat(TextureCompression::BC4, ImageFormat::RGBA8Srgb) == ImageFormat::BC4);
    CHECK(compressedFormat(TextureCompression::BC6H, ImageFormat::RGBA32F) == ImageFormat::BC6H);
}

TEST_CASE("BC level sizes count whole 4x4 blocks")
{
    CHECK(isBlockCompressed(ImageFormat::BC7));
    CHECK_FALSE(isBlockCompressed(ImageFormat::RGBA32F));
    CHECK(bytesPerPixel(ImageFormat::BC6H) == 0);
    CHECK(bytesPerBlock(ImageFormat::RGBA8) == 0);
    CHECK(bytesPerBlock(ImageFormat::BC4) == 8);
    CHECK(bytesPerBlock(ImageFormat::BC5) == 16);
    CHECK(imageLevelSize(ImageFormat::BC7, 1, 1) == 16);
    CHECK(imageLevelSize(ImageFormat::BC4, 5, 7) == 2 * 2 * 8);
    CHECK(imageLevelSize(ImageFormat::BC6H, 8, 4) == 2 * 16);
    CHECK(imageLevelSize(ImageFormat::RGBA32F, 5, 7) == 5 * 7 * 16);
    CHECK(imageRowPitch(ImageFormat::BC5, 9) == 3 * 16);
    CHECK(imageRowPitch(ImageFormat::RGBA8, 9) == 9 * 4);
    CHECK(cubeBlockBytes(16, 5) == 6 * (16 + 4 + 1 + 1 + 1) * 16);
}

TEST_CASE("BC7 round trips colour images within the error bound of each quality")
{
    constexpr std::array<f64, 3> kMeasuredLinearGradientPsnr = {41.43, 41.72, 41.86};
    constexpr std::array<f64, 3> kMeasuredLinearNoisePsnr = {22.61, 22.70, 22.72};
    constexpr std::array<f64, 3> kMeasuredSrgbGradientPsnr = {40.93, 40.95, 41.25};
    constexpr std::array<f64, 3> kMeasuredSrgbNoisePsnr = {21.53, 21.52, 21.53};
    for (const ImageFormat format : {ImageFormat::RGBA8, ImageFormat::RGBA8Srgb})
    {
        const Image gradient = makeGradient(64, 48, format);
        const Image noise = makeNoise(64, 48, format, 1234);
        for (usize q = 0; q < kQualities.size(); ++q)
        {
            CAPTURE(q);
            const Image encoded = compressed(gradient, TextureCompression::BC7, kQualities[q]);
            CHECK(encoded.format ==
                  (format == ImageFormat::RGBA8Srgb ? ImageFormat::BC7Srgb : ImageFormat::BC7));
            CHECK(encoded.data.size() == imageLevelSize(ImageFormat::BC7, 64, 48));
            const Image decoded = decompressed(encoded);
            CHECK(decoded.format == format);
            const f64 gradientPsnr = psnr(gradient, decoded, 0, 4);
            const f64 noisePsnr =
                psnr(noise, decompressed(compressed(noise, TextureCompression::BC7, kQualities[q])), 0, 4);
            const bool srgb = format == ImageFormat::RGBA8Srgb;
            CHECK(gradientPsnr >=
                  (srgb ? kMeasuredSrgbGradientPsnr : kMeasuredLinearGradientPsnr)[q] - kPsnrMargin);
            CHECK(noisePsnr >= (srgb ? kMeasuredSrgbNoisePsnr : kMeasuredLinearNoisePsnr)[q] - kPsnrMargin);
        }
    }
}

TEST_CASE("BC5 keeps the two normal channels and decodes B = 0, A = 255")
{
    constexpr std::array<f64, 3> kMeasuredNormalPsnr = {43.88, 44.81, 45.30};
    const Image normals = makeNormalMap(64, 64);
    for (usize q = 0; q < kQualities.size(); ++q)
    {
        const Image encoded = compressed(normals, TextureCompression::BC5, kQualities[q]);
        CHECK(encoded.format == ImageFormat::BC5);
        const Image decoded = decompressed(encoded);
        CHECK(decoded.format == ImageFormat::RGBA8);
        const f64 measured = psnr(normals, decoded, 0, 2);
        CHECK(measured >= kMeasuredNormalPsnr[q] - kPsnrMargin);
        const u8* pixels = reinterpret_cast<const u8*>(decoded.data.data());
        CHECK(pixels[2] == 0);
        CHECK(pixels[3] == 255);
    }
}

TEST_CASE("BC4 keeps the red channel and decodes G = B = 0, A = 255")
{
    constexpr std::array<f64, 3> kMeasuredGradientPsnr = {54.15, 61.42, 99.0};
    constexpr f64 kLosslessPsnrAtLeast = 60.0;
    constexpr std::array<f64, 3> kMeasuredNoisePsnr = {34.63, 35.08, 35.69};
    const Image gradient = makeGradient(64, 32, ImageFormat::RGBA8);
    const Image noise = makeNoise(64, 32, ImageFormat::RGBA8, 99);
    for (usize q = 0; q < kQualities.size(); ++q)
    {
        const Image decoded = decompressed(compressed(gradient, TextureCompression::BC4, kQualities[q]));
        const f64 gradientPsnr = psnr(gradient, decoded, 0, 1);
        const f64 noisePsnr =
            psnr(noise, decompressed(compressed(noise, TextureCompression::BC4, kQualities[q])), 0, 1);
        CHECK(gradientPsnr >= std::min(kMeasuredGradientPsnr[q] - kPsnrMargin, kLosslessPsnrAtLeast));
        CHECK(noisePsnr >= kMeasuredNoisePsnr[q] - kPsnrMargin);
        const u8* pixels = reinterpret_cast<const u8*>(decoded.data.data());
        CHECK(pixels[1] == 0);
        CHECK(pixels[2] == 0);
        CHECK(pixels[3] == 255);
    }
}

TEST_CASE("BC6H round trips HDR images within the error bound of each quality")
{
    constexpr std::array<f64, 3> kMeasuredLogRmse = {0.0486, 0.0478, 0.0478};
    constexpr f64 kLogRmseMargin = 0.005;
    constexpr f64 kSpotRelativeError = 0.05;
    const Image hdr = makeHdr(64, 64);
    for (usize q = 0; q < kQualities.size(); ++q)
    {
        const Image encoded = compressed(hdr, TextureCompression::BC7, kQualities[q]);
        CHECK(encoded.format == ImageFormat::BC6H);
        const Image decoded = decompressed(encoded);
        CHECK(decoded.format == ImageFormat::RGBA32F);
        const f64 measured = logRmse(hdr, decoded);
        CHECK(measured <= kMeasuredLogRmse[q] + kLogRmseMargin);
        const Vec4 spot = readPixel(decoded, 32, 32);
        CHECK(std::abs(static_cast<f64>(spot.r) - 1000.0) <= 1000.0 * kSpotRelativeError);
        CHECK(spot.a == 1.0f);
    }
}

TEST_CASE("BC6H clamps negative and non-finite values")
{
    Image image = makeSolidImage(4, 4, Vec4(0.0f), ImageFormat::RGBA32F);
    auto* pixels = reinterpret_cast<f32*>(image.data.data());
    for (usize i = 0; i < 16; ++i)
    {
        pixels[i * 4 + 0] = -5.0f;
        pixels[i * 4 + 1] = 1.0e9f;
        pixels[i * 4 + 2] = std::nanf("");
    }
    const Image decoded = decompressed(compressed(image, TextureCompression::BC6H));
    const Vec4 value = readPixel(decoded, 1, 2);
    CHECK(value.r == 0.0f);
    CHECK(value.g == doctest::Approx(65504.0).epsilon(0.01));
    CHECK(value.b == 0.0f);
}

TEST_CASE("Images of odd sizes and their mip chains are padded to whole blocks")
{
    for (const TextureCompression compression : {TextureCompression::BC7, TextureCompression::BC5,
                                                 TextureCompression::BC4, TextureCompression::BC6H})
    {
        CAPTURE(textureCompressionName(compression));
        const ImageFormat source =
            compression == TextureCompression::BC6H ? ImageFormat::RGBA32F : ImageFormat::RGBA8;
        for (const UVec2 size : {UVec2(1, 1), UVec2(2, 2), UVec2(5, 7), UVec2(13, 6)})
        {
            CAPTURE(size.x);
            CAPTURE(size.y);
            Image image = makeSolidImage(size.x, size.y, Vec4(0.25f, 0.5f, 0.75f, 1.0f), source);
            generateMips(image);
            const Image encoded = compressed(image, compression);
            REQUIRE(encoded.mips.size() == image.mips.size());
            for (usize level = 0; level < encoded.mips.size(); ++level)
            {
                const ImageMip& mip = encoded.mips[level];
                CHECK(mip.width == image.mips[level].width);
                CHECK(mip.height == image.mips[level].height);
                CHECK(mip.size == imageLevelSize(encoded.format, mip.width, mip.height));
            }
            const Image decoded = decompressed(encoded);
            REQUIRE(decoded.mips.size() == image.mips.size());
            for (usize level = 0; level < decoded.mips.size(); ++level)
            {
                CHECK(decoded.mips[level].size == image.mips[level].size);
            }
            const Vec4 last = readPixel(decoded, size.x - 1, size.y - 1);
            CHECK(last.r == doctest::Approx(0.25).epsilon(0.02));
            if (compression != TextureCompression::BC4)
            {
                CHECK(last.g == doctest::Approx(0.5).epsilon(0.02));
            }
        }
    }
}

TEST_CASE("compressImage gives the same bytes with and without jobs and with any worker count")
{
    JobSystem one(JobSystemDesc{1, 256});
    JobSystem four(JobSystemDesc{4, 256});
    Image colour = makeNoise(61, 37, ImageFormat::RGBA8Srgb, 7);
    generateMips(colour);
    Image hdr = makeHdr(61, 37);
    generateMips(hdr);
    for (const TextureCompression compression : {TextureCompression::BC7, TextureCompression::BC5,
                                                 TextureCompression::BC4, TextureCompression::BC6H})
    {
        CAPTURE(textureCompressionName(compression));
        const Image& source = compression == TextureCompression::BC6H ? hdr : colour;
        const Image serial = compressed(source, compression, TextureQuality::Fast);
        CHECK(compressed(source, compression, TextureQuality::Fast, &one).data == serial.data);
        CHECK(compressed(source, compression, TextureQuality::Fast, &four).data == serial.data);
    }
}

TEST_CASE("Compressing and decompressing reject invalid images and copy what needs no work")
{
    CHECK_FALSE(compressImage(Image{}, TextureCompression::BC7).has_value());
    CHECK_FALSE(decompressImage(Image{}).has_value());

    const Image image = makeGradient(8, 8, ImageFormat::RGBA8);
    Image truncated = image;
    truncated.data.resize(truncated.data.size() - 1);
    CHECK_FALSE(compressImage(truncated, TextureCompression::BC7).has_value());
    Image wrongSize = image;
    wrongSize.mips[0].size -= 4;
    CHECK_FALSE(compressImage(wrongSize, TextureCompression::BC4).has_value());

    const Image encoded = compressed(image, TextureCompression::BC7);
    CHECK_FALSE(compressImage(encoded, TextureCompression::BC7).has_value());
    Image badBlocks = encoded;
    badBlocks.data.pop_back();
    CHECK_FALSE(decompressImage(badBlocks).has_value());

    const Image copy = compressed(image, TextureCompression::None);
    CHECK(copy.format == image.format);
    CHECK(copy.data == image.data);
    const Image same = decompressed(image);
    CHECK(same.data == image.data);
}

TEST_CASE("Compressed images survive the cooked image format and bad block sizes are rejected")
{
    Image image = makeNoise(20, 12, ImageFormat::RGBA8Srgb, 3);
    generateMips(image);
    const Image encoded = compressed(image, TextureCompression::BC7);
    const std::optional<Image> read = deserializeImage(serializeImage(encoded));
    REQUIRE(read.has_value());
    CHECK(read->format == ImageFormat::BC7Srgb);
    CHECK(read->data == encoded.data);
    CHECK(read->mips.size() == encoded.mips.size());

    Image broken = encoded;
    broken.mips[1].size = 4 * 3 * 4;
    CHECK_FALSE(deserializeImage(serializeImage(broken)).has_value());
}

TEST_CASE("Environments compress to BC6H, survive the cooked format and decompress within the error bound")
{
    constexpr f64 kMeasuredMeanRelativeError = 0.0182;
    constexpr f64 kMeasuredWorstRelativeError = 0.1472;
    constexpr f64 kRelativeErrorMargin = 1.1;
    const BakedEnvironment baked = makeBakedEnvironment();
    REQUIRE(baked.isValid());
    CHECK_FALSE(decompressEnvironment(baked).has_value());

    JobSystem jobs(JobSystemDesc{3, 256});
    const std::optional<BakedEnvironment> encoded = compressEnvironment(baked, TextureQuality::Normal, &jobs);
    REQUIRE(encoded.has_value());
    CHECK(encoded->compressed);
    CHECK(encoded->isValid());
    CHECK(encoded->background.empty());
    CHECK(encoded->specular.empty());
    CHECK(encoded->backgroundBlocks.size() == cubeBlockBytes(16, 5));
    CHECK(encoded->specularBlocks.size() == cubeBlockBytes(8, 4));
    CHECK(encoded->irradiance.coefficients[0] == baked.irradiance.coefficients[0]);
    CHECK_FALSE(compressEnvironment(*encoded).has_value());
    CHECK(compressEnvironment(baked)->backgroundBlocks == encoded->backgroundBlocks);

    const std::optional<BakedEnvironment> read = deserializeEnvironment(serializeEnvironment(*encoded));
    REQUIRE(read.has_value());
    CHECK(read->compressed);
    CHECK(read->specularBlocks == encoded->specularBlocks);

    const std::optional<BakedEnvironment> decoded = decompressEnvironment(*read);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded->compressed);
    CHECK(decoded->isValid());
    REQUIRE(decoded->background.size() == baked.background.size());
    f64 worst = 0.0;
    f64 sum = 0.0;
    for (usize i = 0; i < baked.background.size(); ++i)
    {
        const Vec4 expected = glm::unpackHalf4x16(baked.background[i]);
        const Vec4 actual = glm::unpackHalf4x16(decoded->background[i]);
        for (int c = 0; c < 3; ++c)
        {
            const f64 error = static_cast<f64>(std::abs(actual[c] - expected[c]) / expected[c]);
            worst = std::max(worst, error);
            sum += error;
        }
        CHECK(actual.a == 1.0f);
    }
    CHECK(sum / static_cast<f64>(baked.background.size() * 3) <=
          kMeasuredMeanRelativeError * kRelativeErrorMargin);
    CHECK(worst <= kMeasuredWorstRelativeError * kRelativeErrorMargin);

    BakedEnvironment invalid = *encoded;
    invalid.specularBlocks.pop_back();
    CHECK_FALSE(invalid.isValid());
    CHECK_FALSE(decompressEnvironment(invalid).has_value());
}

TEST_CASE("Materials ask for the compression of each texture use and sidecars override it")
{
    MaterialTextures textures;
    textures.baseColor = AssetRef("game:/t/albedo.png");
    textures.metallicRoughness = AssetRef("game:/t/orm.png");
    textures.normal = AssetRef("game:/t/normal.png");
    textures.occlusion = AssetRef("game:/t/ao.png");
    std::array<MaterialTextureUse, 5> uses = materialTextureUses(textures);
    CHECK(uses[0].compression == TextureCompression::BC7);
    CHECK(uses[1].compression == TextureCompression::BC7);
    CHECK(uses[2].compression == TextureCompression::BC5);
    CHECK(uses[3].compression == TextureCompression::BC4);
    CHECK(uses[4].compression == TextureCompression::BC7);
    textures.occlusion = textures.metallicRoughness;
    uses = materialTextureUses(textures);
    CHECK(uses[3].compression == TextureCompression::BC7);

    ImageImportSettings requested;
    requested.compression = TextureCompression::BC5;
    TextureImportSettings sidecar;
    CHECK(effectiveImageImport(requested, sidecar).compression == TextureCompression::BC5);
    sidecar.compression = ImportCompression::BC7;
    CHECK(effectiveImageImport(requested, sidecar).compression == TextureCompression::BC7);
    sidecar.compression = ImportCompression::None;
    CHECK(effectiveImageImport(requested, sidecar).compression == TextureCompression::None);
    requested.compression = TextureCompression::None;
    sidecar.compression = ImportCompression::BC4;
    CHECK(effectiveImageImport(requested, sidecar).compression == TextureCompression::None);
}
