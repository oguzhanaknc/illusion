#include "Bc6hEncoder.hpp"

#include <illusion/assets/Environment.hpp>
#include <illusion/assets/TextureCompression.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Profiler.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <bc7enc.h>
#include <bcdec.h>
#include <cstring>
#include <mutex>
#include <span>
#include <vector>

// rgbcx.h disables an MSVC warning for the rest of the translation unit; keep that to the header.
#if defined(_MSC_VER)
#pragma warning(push)
#endif
#include <rgbcx.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace illusion
{
namespace
{

constexpr u16 kHalfOne = 0x3C00;

enum class SourceKind : u8
{
    Rgba8,
    Rgba32F,
    Half4, ///< BakedEnvironment texels (glm::packHalf4x16).
};

// One level to encode: its pixels, where its blocks go, and the first row of blocks in the flattened list.
struct LevelTask
{
    SourceKind kind = SourceKind::Rgba8;
    const std::byte* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;
    std::byte* blocks = nullptr;
    u32 firstRow = 0;
};

struct Encoder
{
    ImageFormat target = ImageFormat::BC7;
    bc7enc_compress_block_params bc7{};
    u32 bc6hPasses = 0;
    u32 bc4SearchRadius = 0; ///< 0: rgbcx's fast BC4/BC5 encoder; otherwise its endpoint search radius.
};

void initializeEncoders()
{
    static std::once_flag once;
    std::call_once(once, [] {
        bc7enc_compress_block_init();
        rgbcx::init();
    });
}

Encoder makeEncoder(ImageFormat target, TextureQuality quality)
{
    Encoder encoder;
    encoder.target = target;
    bc7enc_compress_block_params_init(&encoder.bc7);
    switch (quality)
    {
    case TextureQuality::Fast:
        encoder.bc7.m_uber_level = 0;
        encoder.bc7.m_max_partitions = 16;
        encoder.bc6hPasses = 0;
        encoder.bc4SearchRadius = 0;
        break;
    case TextureQuality::Normal:
        encoder.bc7.m_uber_level = 1;
        encoder.bc7.m_max_partitions = 64;
        encoder.bc6hPasses = 1;
        encoder.bc4SearchRadius = 1;
        break;
    case TextureQuality::High:
        encoder.bc7.m_uber_level = 4;
        encoder.bc7.m_max_partitions = 64;
        encoder.bc6hPasses = 3;
        encoder.bc4SearchRadius = 3;
        break;
    }
    if (target == ImageFormat::BC7Srgb)
    {
        bc7enc_compress_block_params_init_perceptual_weights(&encoder.bc7);
    }
    else
    {
        bc7enc_compress_block_params_init_linear_weights(&encoder.bc7);
    }
    return encoder;
}

// Levels smaller than a block, and the right and bottom blocks of levels that are not a multiple of four,
// repeat their last column and row.
void encodeBlock(const Encoder& encoder, const LevelTask& level, u32 blockX, u32 blockY,
                 std::byte* out) noexcept
{
    std::array<u32, 16> offsets{};
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 x = std::min(blockX * 4 + i % 4, level.width - 1);
        const u32 y = std::min(blockY * 4 + i / 4, level.height - 1);
        offsets[i] = y * level.width + x;
    }
    if (encoder.target == ImageFormat::BC6H)
    {
        std::array<std::array<u16, 3>, 16> texels{};
        for (u32 i = 0; i < 16; ++i)
        {
            if (level.kind == SourceKind::Half4)
            {
                u64 texel = 0;
                std::memcpy(&texel, level.pixels + static_cast<usize>(offsets[i]) * 8, 8);
                texels[i] = {static_cast<u16>(texel), static_cast<u16>(texel >> 16),
                             static_cast<u16>(texel >> 32)};
            }
            else
            {
                std::array<f32, 3> rgb{};
                std::memcpy(rgb.data(), level.pixels + static_cast<usize>(offsets[i]) * 16, 12);
                texels[i] = {detail::toUnsignedHalf(rgb[0]), detail::toUnsignedHalf(rgb[1]),
                             detail::toUnsignedHalf(rgb[2])};
            }
        }
        detail::encodeBc6hBlock(texels, encoder.bc6hPasses, out);
        return;
    }
    std::array<u8, 64> rgba{};
    for (u32 i = 0; i < 16; ++i)
    {
        std::memcpy(rgba.data() + i * 4, level.pixels + static_cast<usize>(offsets[i]) * 4, 4);
    }
    switch (encoder.target)
    {
    case ImageFormat::BC5:
        if (encoder.bc4SearchRadius == 0)
        {
            rgbcx::encode_bc5(out, rgba.data(), 0, 1, 4);
        }
        else
        {
            rgbcx::encode_bc5_hq(out, rgba.data(), 0, 1, 4, encoder.bc4SearchRadius);
        }
        break;
    case ImageFormat::BC4:
        if (encoder.bc4SearchRadius == 0)
        {
            rgbcx::encode_bc4(out, rgba.data(), 4);
        }
        else
        {
            rgbcx::encode_bc4_hq(out, rgba.data(), 4, encoder.bc4SearchRadius);
        }
        break;
    default:
        bc7enc_compress_block(out, rgba.data(), &encoder.bc7);
        break;
    }
}

void encodeRow(const Encoder& encoder, std::span<const LevelTask> levels, u32 row) noexcept
{
    usize index = 0;
    while (index + 1 < levels.size() && levels[index + 1].firstRow <= row)
    {
        ++index;
    }
    const LevelTask& level = levels[index];
    const u32 blockY = row - level.firstRow;
    const u32 blocksX = (level.width + 3) / 4;
    const usize blockBytes = bytesPerBlock(encoder.target);
    std::byte* out = level.blocks + static_cast<usize>(blockY) * blocksX * blockBytes;
    for (u32 blockX = 0; blockX < blocksX; ++blockX)
    {
        encodeBlock(encoder, level, blockX, blockY, out + blockX * blockBytes);
    }
}

// Rows of blocks are independent, so any split over workers produces the same bytes.
void encodeLevels(const Encoder& encoder, std::vector<LevelTask>& levels, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    initializeEncoders();
    u32 rows = 0;
    for (LevelTask& level : levels)
    {
        level.firstRow = rows;
        rows += (level.height + 3) / 4;
    }
    const std::span<const LevelTask> view(levels);
    if (jobs != nullptr)
    {
        jobs->parallelFor(rows, 1, [&encoder, view](u32 begin, u32 end) {
            for (u32 row = begin; row < end; ++row)
            {
                encodeRow(encoder, view, row);
            }
        });
        return;
    }
    for (u32 row = 0; row < rows; ++row)
    {
        encodeRow(encoder, view, row);
    }
}

bool levelsValid(const Image& image) noexcept
{
    if (!image.isValid())
    {
        return false;
    }
    for (const ImageMip& mip : image.mips)
    {
        if (mip.width == 0 || mip.height == 0 ||
            mip.size != imageLevelSize(image.format, mip.width, mip.height) ||
            mip.offset > image.data.size() || mip.size > image.data.size() - mip.offset)
        {
            return false;
        }
    }
    return true;
}

ImageFormat decompressedFormat(ImageFormat format) noexcept
{
    switch (format)
    {
    case ImageFormat::BC7Srgb:
        return ImageFormat::RGBA8Srgb;
    case ImageFormat::BC6H:
        return ImageFormat::RGBA32F;
    case ImageFormat::BC7:
    case ImageFormat::BC5:
    case ImageFormat::BC4:
        return ImageFormat::RGBA8;
    default:
        return format;
    }
}

// Decodes one block of @p format into 16 RGBA texels: 4 bytes each for the 8-bit formats, 16 for BC6H.
void decodeBlock(ImageFormat format, const std::byte* block, std::array<std::byte, 256>& texels) noexcept
{
    switch (format)
    {
    case ImageFormat::BC6H:
    {
        std::array<f32, 48> rgb{};
        bcdec_bc6h_float(block, rgb.data(), 12, 0);
        for (usize i = 0; i < 16; ++i)
        {
            const std::array<f32, 4> rgba = {rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2], 1.0f};
            std::memcpy(texels.data() + i * 16, rgba.data(), 16);
        }
        break;
    }
    case ImageFormat::BC5:
    {
        std::array<u8, 32> rg{};
        bcdec_bc5(block, rg.data(), 8);
        for (usize i = 0; i < 16; ++i)
        {
            const std::array<u8, 4> rgba = {rg[i * 2], rg[i * 2 + 1], 0, 255};
            std::memcpy(texels.data() + i * 4, rgba.data(), 4);
        }
        break;
    }
    case ImageFormat::BC4:
    {
        std::array<u8, 16> r{};
        bcdec_bc4(block, r.data(), 4);
        for (usize i = 0; i < 16; ++i)
        {
            const std::array<u8, 4> rgba = {r[i], 0, 0, 255};
            std::memcpy(texels.data() + i * 4, rgba.data(), 4);
        }
        break;
    }
    default:
        bcdec_bc7(block, texels.data(), 16);
        break;
    }
}

void decodeLevel(ImageFormat format, const std::byte* blocks, u32 width, u32 height,
                 std::byte* pixels) noexcept
{
    const usize pixelBytes = bytesPerPixel(decompressedFormat(format));
    const usize blockBytes = bytesPerBlock(format);
    const u32 blocksX = (width + 3) / 4;
    const u32 blocksY = (height + 3) / 4;
    std::array<std::byte, 256> texels{};
    for (u32 blockY = 0; blockY < blocksY; ++blockY)
    {
        for (u32 blockX = 0; blockX < blocksX; ++blockX)
        {
            decodeBlock(format, blocks + (static_cast<usize>(blockY) * blocksX + blockX) * blockBytes,
                        texels);
            const u32 columns = std::min(4u, width - blockX * 4);
            const u32 rows = std::min(4u, height - blockY * 4);
            for (u32 y = 0; y < rows; ++y)
            {
                const usize target = (static_cast<usize>(blockY * 4 + y) * width + blockX * 4) * pixelBytes;
                std::memcpy(pixels + target, texels.data() + y * 4 * pixelBytes, columns * pixelBytes);
            }
        }
    }
}

// The levels of a cube map in BakedEnvironment order (mip, then face) as encode tasks.
void appendCubeTasks(std::vector<LevelTask>& tasks, const std::vector<u64>& texels, u32 size, u32 mips,
                     std::vector<std::byte>& blocks)
{
    blocks.assign(cubeBlockBytes(size, mips), std::byte{0});
    usize texel = 0;
    usize offset = 0;
    for (u32 mip = 0; mip < mips; ++mip)
    {
        const u32 width = std::max(size >> mip, 1u);
        for (u32 face = 0; face < 6; ++face)
        {
            LevelTask task;
            task.kind = SourceKind::Half4;
            task.pixels = reinterpret_cast<const std::byte*>(texels.data() + texel);
            task.width = width;
            task.height = width;
            task.blocks = blocks.data() + offset;
            tasks.push_back(task);
            texel += static_cast<usize>(width) * width;
            offset += imageLevelSize(ImageFormat::BC6H, width, width);
        }
    }
}

std::vector<u64> decodeCube(const std::vector<std::byte>& blocks, u32 size, u32 mips)
{
    std::vector<u64> texels(cubeTexelCount(size, mips));
    std::vector<f32> face;
    usize texel = 0;
    usize offset = 0;
    for (u32 mip = 0; mip < mips; ++mip)
    {
        const u32 width = std::max(size >> mip, 1u);
        const usize count = static_cast<usize>(width) * width;
        face.resize(count * 4);
        for (u32 f = 0; f < 6; ++f)
        {
            decodeLevel(ImageFormat::BC6H, blocks.data() + offset, width, width,
                        reinterpret_cast<std::byte*>(face.data()));
            for (usize i = 0; i < count; ++i)
            {
                const u64 r = glm::packHalf1x16(face[i * 4]);
                const u64 g = glm::packHalf1x16(face[i * 4 + 1]);
                const u64 b = glm::packHalf1x16(face[i * 4 + 2]);
                texels[texel + i] = r | (g << 16) | (b << 32) | (static_cast<u64>(kHalfOne) << 48);
            }
            texel += count;
            offset += imageLevelSize(ImageFormat::BC6H, width, width);
        }
    }
    return texels;
}

} // namespace

std::string_view textureCompressionName(TextureCompression compression) noexcept
{
    switch (compression)
    {
    case TextureCompression::None:
        return "none";
    case TextureCompression::BC7:
        return "bc7";
    case TextureCompression::BC5:
        return "bc5";
    case TextureCompression::BC4:
        return "bc4";
    case TextureCompression::BC6H:
        return "bc6h";
    }
    return "none";
}

std::string_view textureQualityName(TextureQuality quality) noexcept
{
    switch (quality)
    {
    case TextureQuality::Fast:
        return "fast";
    case TextureQuality::Normal:
        return "normal";
    case TextureQuality::High:
        return "high";
    }
    return "normal";
}

TextureCompression resolveCompression(TextureCompression requested, ImageFormat source) noexcept
{
    ILL_ASSERT(!isBlockCompressed(source), "resolveCompression of a block-compressed format");
    if (requested == TextureCompression::None)
    {
        return TextureCompression::None;
    }
    if (source == ImageFormat::RGBA32F)
    {
        return TextureCompression::BC6H;
    }
    return requested == TextureCompression::BC6H ? TextureCompression::BC7 : requested;
}

ImageFormat compressedFormat(TextureCompression compression, ImageFormat source) noexcept
{
    switch (compression)
    {
    case TextureCompression::None:
        return source;
    case TextureCompression::BC7:
        return source == ImageFormat::RGBA8Srgb ? ImageFormat::BC7Srgb : ImageFormat::BC7;
    case TextureCompression::BC5:
        return ImageFormat::BC5;
    case TextureCompression::BC4:
        return ImageFormat::BC4;
    case TextureCompression::BC6H:
        return ImageFormat::BC6H;
    }
    return source;
}

std::optional<Image> compressImage(const Image& image, TextureCompression compression, TextureQuality quality,
                                   JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    if (isBlockCompressed(image.format) || !levelsValid(image))
    {
        return std::nullopt;
    }
    const TextureCompression resolved = resolveCompression(compression, image.format);
    if (resolved == TextureCompression::None)
    {
        return image;
    }
    Image result;
    result.format = compressedFormat(resolved, image.format);
    result.mips.reserve(image.mips.size());
    usize offset = 0;
    for (const ImageMip& source : image.mips)
    {
        ImageMip mip;
        mip.width = source.width;
        mip.height = source.height;
        mip.offset = offset;
        mip.size = imageLevelSize(result.format, source.width, source.height);
        offset += mip.size;
        result.mips.push_back(mip);
    }
    result.data.resize(offset);

    std::vector<LevelTask> levels;
    levels.reserve(image.mips.size());
    for (usize level = 0; level < image.mips.size(); ++level)
    {
        LevelTask task;
        task.kind = image.format == ImageFormat::RGBA32F ? SourceKind::Rgba32F : SourceKind::Rgba8;
        task.pixels = image.data.data() + image.mips[level].offset;
        task.width = image.mips[level].width;
        task.height = image.mips[level].height;
        task.blocks = result.data.data() + result.mips[level].offset;
        levels.push_back(task);
    }
    encodeLevels(makeEncoder(result.format, quality), levels, jobs);
    return result;
}

std::optional<Image> decompressImage(const Image& image)
{
    ILL_PROFILE_SCOPE();
    if (!levelsValid(image))
    {
        return std::nullopt;
    }
    if (!isBlockCompressed(image.format))
    {
        return image;
    }
    Image result;
    result.format = decompressedFormat(image.format);
    result.mips.reserve(image.mips.size());
    usize offset = 0;
    for (const ImageMip& source : image.mips)
    {
        ImageMip mip;
        mip.width = source.width;
        mip.height = source.height;
        mip.offset = offset;
        mip.size = imageLevelSize(result.format, source.width, source.height);
        offset += mip.size;
        result.mips.push_back(mip);
    }
    result.data.resize(offset);
    for (usize level = 0; level < image.mips.size(); ++level)
    {
        const ImageMip& mip = image.mips[level];
        decodeLevel(image.format, image.data.data() + mip.offset, mip.width, mip.height,
                    result.data.data() + result.mips[level].offset);
    }
    return result;
}

std::optional<BakedEnvironment> compressEnvironment(const BakedEnvironment& baked, TextureQuality quality,
                                                    JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    if (baked.compressed || !baked.isValid())
    {
        return std::nullopt;
    }
    BakedEnvironment result;
    result.backgroundSize = baked.backgroundSize;
    result.backgroundMips = baked.backgroundMips;
    result.specularSize = baked.specularSize;
    result.specularMips = baked.specularMips;
    result.irradiance = baked.irradiance;
    result.compressed = true;
    std::vector<LevelTask> levels;
    appendCubeTasks(levels, baked.background, baked.backgroundSize, baked.backgroundMips,
                    result.backgroundBlocks);
    appendCubeTasks(levels, baked.specular, baked.specularSize, baked.specularMips, result.specularBlocks);
    encodeLevels(makeEncoder(ImageFormat::BC6H, quality), levels, jobs);
    return result;
}

std::optional<BakedEnvironment> decompressEnvironment(const BakedEnvironment& baked)
{
    ILL_PROFILE_SCOPE();
    if (!baked.compressed || !baked.isValid())
    {
        return std::nullopt;
    }
    BakedEnvironment result;
    result.backgroundSize = baked.backgroundSize;
    result.backgroundMips = baked.backgroundMips;
    result.specularSize = baked.specularSize;
    result.specularMips = baked.specularMips;
    result.irradiance = baked.irradiance;
    result.background = decodeCube(baked.backgroundBlocks, baked.backgroundSize, baked.backgroundMips);
    result.specular = decodeCube(baked.specularBlocks, baked.specularSize, baked.specularMips);
    return result;
}

} // namespace illusion
