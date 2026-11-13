#include "TestSupport.hpp"

#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/TextureCompression.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kSize = 64;

RenderDeviceDesc headless(bool compressedTextures = true)
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    desc.compressedTextures = compressedTextures;
    return desc;
}

std::unique_ptr<SceneRenderer> makeRenderer(RenderDevice& device, RenderResources& resources)
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    desc.ambient = Vec3(0.6f);
    desc.post.bloom.enabled = false;
    desc.post.ambientOcclusion.enabled = false;
    desc.post.temporalAntiAliasing.enabled = false;
    desc.occlusionCulling = false;
    return SceneRenderer::create(device, resources, desc);
}

// Linear values in [0, 1] (scaled by @p range for float images): smooth gradients with sharp checker edges.
Vec4 patternTexel(u32 x, u32 y, u32 width, u32 height)
{
    const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(width);
    const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height);
    Vec4 texel(u, v, 0.5f + 0.5f * std::sin(2.0f * std::numbers::pi_v<f32> * (u + v)), 1.0f);
    if ((x / 8 + y / 8) % 2 == 0)
    {
        texel.b *= 0.4f;
    }
    return texel;
}

// A tangent-space normal tilted by waves, encoded to [0, 1].
Vec4 normalTexel(u32 x, u32 y, u32 width, u32 height)
{
    const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(width);
    const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height);
    const f32 nx = 0.5f * std::sin(4.0f * std::numbers::pi_v<f32> * u);
    const f32 ny = 0.5f * std::cos(4.0f * std::numbers::pi_v<f32> * v);
    const Vec3 n = glm::normalize(Vec3(nx, ny, 1.0f));
    return Vec4(n * 0.5f + 0.5f, 1.0f);
}

// An image of @p width x @p height in @p format filled by @p texel, with a full mip chain.
template <class Texel>
Image makeImage(u32 width, u32 height, ImageFormat format, f32 range, Texel texel)
{
    Image image;
    image.format = format;
    const usize size = imageLevelSize(format, width, height);
    image.mips.push_back(ImageMip{width, height, 0, size});
    image.data.resize(size);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const Vec4 value = texel(x, y, width, height);
            const usize pixel = static_cast<usize>(y) * width + x;
            if (format == ImageFormat::RGBA32F)
            {
                const Vec4 scaled(Vec3(value) * range, value.a);
                std::memcpy(image.data.data() + pixel * 16, &scaled, 16);
                continue;
            }
            for (u32 c = 0; c < 4; ++c)
            {
                const f32 encoded = format == ImageFormat::RGBA8Srgb && c < 3
                                        ? std::pow(value[static_cast<int>(c)], 1.0f / 2.2f)
                                        : value[static_cast<int>(c)];
                image.data[pixel * 4 + c] = static_cast<std::byte>(std::lround(encoded * 255.0f));
            }
        }
    }
    generateMips(image);
    return image;
}

// The texture slot of a material a test case fills.
enum class Slot : u8
{
    BaseColor,
    MetallicRoughness,
    Normal,
    Occlusion,
};

struct QuadScene
{
    RenderDevice& device;
    RenderResources& resources;
    SceneRenderer& renderer;
    MeshHandle plane;

    // Draws a quad filling the view with @p image in @p slot, lit by a sun and ambient light.
    std::vector<u8> draw(const Image& image, Slot slot)
    {
        const TextureHandle texture = resources.createTexture(image, "Test texture");
        MaterialDesc material;
        material.parameters.metallicFactor = slot == Slot::MetallicRoughness ? 1.0f : 0.0f;
        material.parameters.roughnessFactor = slot == Slot::MetallicRoughness ? 1.0f : 0.5f;
        switch (slot)
        {
        case Slot::BaseColor:
            material.baseColor = texture;
            break;
        case Slot::MetallicRoughness:
            material.metallicRoughness = texture;
            break;
        case Slot::Normal:
            material.normal = texture;
            break;
        case Slot::Occlusion:
            material.occlusion = texture;
            break;
        }
        const MaterialHandle handle = resources.createMaterial(material);

        World world;
        Transform quad;
        quad.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f)); // faces +z
        world.create(quad, RenderMesh{plane, 0, handle});
        Transform sun;
        sun.rotation = glm::angleAxis(0.6f, glm::normalize(Vec3(1.0f, -1.0f, 0.0f)));
        world.create(sun, DirectionalLight{.castShadows = false});
        std::vector<u8> pixels = render(world);

        resources.releaseMaterial(handle);
        resources.releaseTexture(texture);
        return pixels;
    }

    std::vector<u8> render(World& world)
    {
        nvrhi::IDevice* gpu = device.device();
        nvrhi::TextureDesc targetDesc;
        targetDesc.width = kSize;
        targetDesc.height = kSize;
        targetDesc.format = nvrhi::Format::RGBA8_UNORM;
        targetDesc.isRenderTarget = true;
        targetDesc.debugName = "Texture upload test target";
        targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        nvrhi::TextureHandle target = gpu->createTexture(targetDesc);
        nvrhi::TextureDesc readbackDesc = targetDesc;
        readbackDesc.isRenderTarget = false;
        nvrhi::StagingTextureHandle readback =
            gpu->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

        Transform cameraTransform;
        cameraTransform.position = Vec3(0.0f, 0.0f, 3.0f);
        Camera camera;
        camera.projection = CameraProjection::Orthographic;
        camera.orthoHeight = 0.9f;
        const RenderView view = RenderView::fromCamera(cameraTransform, camera, 1.0f);
        nvrhi::CommandListHandle commands = gpu->createCommandList();
        commands->open();
        renderer.render(commands, target, world, view, nullptr);
        commands->copyTexture(readback, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
        commands->close();
        gpu->executeCommandList(commands);
        gpu->waitForIdle();

        size_t rowPitch = 0;
        const auto* bytes = static_cast<const u8*>(
            gpu->mapStagingTexture(readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
        std::vector<u8> pixels(static_cast<usize>(kSize) * kSize * 4);
        for (u32 y = 0; y < kSize; ++y)
        {
            std::memcpy(pixels.data() + static_cast<usize>(y) * kSize * 4, bytes + y * rowPitch, kSize * 4);
        }
        gpu->unmapStagingTexture(readback);
        return pixels;
    }
};

struct Difference
{
    int max = 0;
    f64 mean = 0.0;
};

// Per-channel difference of the colour channels of two RGBA8 images.
Difference difference(const std::vector<u8>& a, const std::vector<u8>& b)
{
    REQUIRE(a.size() == b.size());
    Difference result;
    u64 sum = 0;
    usize count = 0;
    for (usize i = 0; i < a.size(); ++i)
    {
        if (i % 4 == 3)
        {
            continue;
        }
        const int d = std::abs(int(a[i]) - int(b[i]));
        result.max = std::max(result.max, d);
        sum += static_cast<u64>(d);
        ++count;
    }
    result.mean = static_cast<f64>(sum) / static_cast<f64>(count);
    return result;
}

// Average of the colour channels, to check that a draw shows something.
f64 brightness(const std::vector<u8>& pixels)
{
    u64 sum = 0;
    for (usize i = 0; i < pixels.size(); ++i)
    {
        sum += i % 4 == 3 ? 0u : pixels[i];
    }
    return static_cast<f64>(sum) / static_cast<f64>(pixels.size() / 4 * 3);
}

void settle(RenderDevice& device, RenderResources& resources)
{
    device.waitIdle();
    static_cast<void>(resources.collect());
}

struct FormatCase
{
    const char* name;
    ImageFormat source;
    TextureCompression compression;
    Slot slot;
    bool normal;
    int maxError;
    f64 meanError;
};

// The encoders' error against the source, measured at TextureQuality::Normal with headroom (the checker edges
// of patternTexel() inside a block are the worst case for BC7 and BC6H). The upload itself is checked against
// the CPU decoder, which every GPU matches within a rounding step.
constexpr FormatCase kFormats[] = {
    {"BC7Srgb base colour", ImageFormat::RGBA8Srgb, TextureCompression::BC7, Slot::BaseColor, false, 48, 2.5},
    {"BC7 metallic-roughness", ImageFormat::RGBA8, TextureCompression::BC7, Slot::MetallicRoughness, false,
     32, 1.0},
    {"BC5 normal", ImageFormat::RGBA8, TextureCompression::BC5, Slot::Normal, true, 4, 0.5},
    {"BC4 occlusion", ImageFormat::RGBA8, TextureCompression::BC4, Slot::Occlusion, false, 4, 0.5},
    {"BC6H base colour", ImageFormat::RGBA32F, TextureCompression::BC6H, Slot::BaseColor, false, 64, 3.5},
};
constexpr int kDecoderError = 2;

Image formatSource(const FormatCase& format, u32 width, u32 height)
{
    return format.normal ? makeImage(width, height, format.source, 1.0f, normalTexel)
                         : makeImage(width, height, format.source, 1.0f, patternTexel);
}

} // namespace

TEST_CASE("block-compressed textures draw like their uncompressed source within the encoder's error")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        REQUIRE(device->gpu().textureCompressionBC); // every supported platform samples BC
        RenderResources resources(*device);
        auto renderer = makeRenderer(*device, resources);
        REQUIRE(renderer != nullptr);
        QuadScene scene{*device, resources, *renderer, resources.createMesh(makePlane(1.0f))};

        for (const FormatCase& format : kFormats)
        {
            // A multiple of the block size, and one that is not: the last row and column of blocks are
            // partial and the smallest levels are one block.
            for (const UVec2 size : {UVec2(64, 64), UVec2(37, 23)})
            {
                const u32 width = size.x;
                const u32 height = size.y;
                CAPTURE(format.name);
                CAPTURE(width);
                const Image source = formatSource(format, width, height);
                const std::optional<Image> compressed = compressImage(source, format.compression);
                REQUIRE(compressed.has_value());
                CHECK(isBlockCompressed(compressed->format));
                CHECK(compressed->mips.size() == fullMipCount(width, height));

                const std::vector<u8> expected = scene.draw(source, format.slot);
                const std::vector<u8> actual = scene.draw(*compressed, format.slot);
                const Difference error = difference(actual, expected);
                MESSAGE(std::string(format.name), " ", width, "x", height, ": max ", error.max, ", mean ",
                        error.mean);
                const std::optional<Image> decoded = decompressImage(*compressed);
                REQUIRE(decoded.has_value());
                CHECK(difference(actual, scene.draw(*decoded, format.slot)).max <= kDecoderError);
                CHECK(brightness(expected) > 20.0);
                CHECK(error.max <= format.maxError);
                CHECK(error.mean <= format.meanError);
            }
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("texture statistics count compressed bytes and return to their value after release")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        const RenderResourceStatistics before = resources.statistics();
        CHECK(before.textures == resources.textureCount());
        CHECK(before.textureBytes == 8); // the two 1x1 RGBA8 default textures
        CHECK(before.textureBytesUncompressed == 8);
        CHECK(before.streamingSavedBytes == 0);

        // One level: exactly a quarter (BC7, BC5) or an eighth (BC4) of RGBA8.
        const Image colour = makeImage(64, 64, ImageFormat::RGBA8Srgb, 1.0f, patternTexel);
        Image level = colour;
        level.mips.resize(1);
        level.data.resize(level.mips[0].size);
        struct Ratio
        {
            TextureCompression compression;
            u64 ratio;
        };
        for (const Ratio expected : {Ratio{TextureCompression::BC7, 4}, Ratio{TextureCompression::BC5, 4},
                                     Ratio{TextureCompression::BC4, 8}})
        {
            const TextureCompression compression = expected.compression;
            const u64 ratio = expected.ratio;
            CAPTURE(static_cast<int>(compression));
            const std::optional<Image> compressed = compressImage(level, compression);
            REQUIRE(compressed.has_value());
            const TextureHandle texture = resources.createTexture(*compressed);
            const RenderResourceStatistics with = resources.statistics();
            const u64 bytes = with.textureBytes - before.textureBytes;
            const u64 uncompressed = with.textureBytesUncompressed - before.textureBytesUncompressed;
            CHECK(bytes == compressed->data.size());
            CHECK(uncompressed == 64u * 64u * 4u);
            CHECK(uncompressed == bytes * ratio);
            CHECK(with.textures == before.textures + 1);
            resources.releaseTexture(texture);
            settle(*device, resources);
            CHECK(resources.statistics().textureBytes == before.textureBytes);
            CHECK(resources.statistics().textureBytesUncompressed == before.textureBytesUncompressed);
        }

        // A full chain: levels below 4x4 still take a whole block. Float images compare with RGBA32F.
        const Image hdr = makeImage(64, 64, ImageFormat::RGBA32F, 4.0f, patternTexel);
        for (const Image* source : {&colour, &hdr})
        {
            const std::optional<Image> compressed = compressImage(*source, TextureCompression::BC7);
            REQUIRE(compressed.has_value());
            const TextureHandle texture = resources.createTexture(*compressed);
            const RenderResourceStatistics with = resources.statistics();
            CHECK(with.textureBytes - before.textureBytes == compressed->data.size());
            CHECK(with.textureBytesUncompressed - before.textureBytesUncompressed == source->data.size());
            resources.releaseTexture(texture);
        }
        settle(*device, resources);
        CHECK(resources.statistics().textureBytes == before.textureBytes);
        CHECK(resources.statistics().textureBytesUncompressed == before.textureBytesUncompressed);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("without BC support compressed textures are decoded on the CPU and draw the same")
{
    const Image source = makeImage(37, 23, ImageFormat::RGBA8Srgb, 1.0f, patternTexel);
    const std::optional<Image> compressed = compressImage(source, TextureCompression::BC7);
    REQUIRE(compressed.has_value());

    // The GPU's BC decoder.
    std::vector<u8> sampled;
    {
        auto device = RenderDevice::create(headless());
        REQUIRE(device != nullptr);
        {
            RenderResources resources(*device);
            auto renderer = makeRenderer(*device, resources);
            QuadScene scene{*device, resources, *renderer, resources.createMesh(makePlane(1.0f))};
            sampled = scene.draw(*compressed, Slot::BaseColor);
        }
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }

    auto device = RenderDevice::create(headless(false));
    REQUIRE(device != nullptr);
    {
        CHECK_FALSE(device->gpu().textureCompressionBC);
        RenderResources resources(*device);
        auto renderer = makeRenderer(*device, resources);
        QuadScene scene{*device, resources, *renderer, resources.createMesh(makePlane(1.0f))};
        const std::optional<Image> decoded = decompressImage(*compressed);
        REQUIRE(decoded.has_value());
        const std::vector<u8> fallback = scene.draw(*compressed, Slot::BaseColor);
        CHECK(difference(fallback, scene.draw(*decoded, Slot::BaseColor)).max == 0);
        CHECK(difference(fallback, sampled).max <= 1);

        // The fallback takes the uncompressed amount of memory.
        settle(*device, resources); // the textures of the draws above count until they are freed
        const RenderResourceStatistics before = resources.statistics();
        const TextureHandle texture = resources.createTexture(*compressed);
        const RenderResourceStatistics with = resources.statistics();
        CHECK(with.textureBytes - before.textureBytes == decoded->data.size());
        CHECK(with.textureBytesUncompressed - before.textureBytesUncompressed == decoded->data.size());
        resources.releaseTexture(texture);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a BC6H environment draws like its half-float bake and takes an eighth of the memory")
{
    for (const bool supported : {true, false})
    {
        CAPTURE(supported);
        auto device = RenderDevice::create(headless(supported));
        REQUIRE(device != nullptr);
        {
            RenderResources resources(*device);
            auto renderer = makeRenderer(*device, resources);
            REQUIRE(renderer != nullptr);
            const Image sky = makeSkyImage(64, SkyDesc{.sunRadiance = Vec3(20.0f), .sunAngularRadius = 0.1f});
            const EnvironmentHandle bakedHandle =
                resources.createEnvironment(sky, EnvironmentDesc{32, 16, 8});
            BakedEnvironment baked;
            REQUIRE(resources.readEnvironment(resources.requestEnvironmentReadback(bakedHandle), baked,
                                              true) == ReadbackStatus::Ready);
            resources.releaseEnvironment(bakedHandle);
            settle(*device, resources);
            const std::optional<BakedEnvironment> compressed = compressEnvironment(baked);
            REQUIRE(compressed.has_value());

            const RenderResourceStatistics before = resources.statistics();
            const EnvironmentHandle half = resources.createEnvironment(baked, "Half");
            const RenderResourceStatistics withHalf = resources.statistics();
            const EnvironmentHandle bc6h = resources.createEnvironment(*compressed, "BC6H");
            const RenderResourceStatistics withBoth = resources.statistics();
            const u64 halfBytes =
                (cubeTexelCount(32, baked.backgroundMips) + cubeTexelCount(16, baked.specularMips)) *
                sizeof(u64);
            CHECK(withHalf.environmentBytes - before.environmentBytes == halfBytes);
            CHECK(withHalf.environmentBytesUncompressed - before.environmentBytesUncompressed == halfBytes);
            const u64 blockBytes =
                cubeBlockBytes(32, baked.backgroundMips) + cubeBlockBytes(16, baked.specularMips);
            CHECK(withBoth.environmentBytes - withHalf.environmentBytes ==
                  (supported ? blockBytes : halfBytes));
            CHECK(withBoth.environmentBytesUncompressed - withHalf.environmentBytesUncompressed == halfBytes);
            // Levels of at least 4x4 take an eighth; the smallest levels are one block each.
            CHECK(static_cast<f64>(halfBytes) / static_cast<f64>(blockBytes) > 7.0);

            if (supported)
            {
                // Block formats are not read back: whoever uploaded the blocks has them.
                CHECK(resources.requestEnvironmentReadback(bc6h) == 0);
            }

            QuadScene scene{*device, resources, *renderer, resources.createMesh(makeSphere(0.4f))};
            const auto draw = [&](EnvironmentHandle environment) {
                World world;
                MaterialDesc chrome;
                chrome.parameters.metallicFactor = 1.0f;
                chrome.parameters.roughnessFactor = 0.3f;
                world.create(Transform{}, RenderMesh{scene.plane, 0, resources.createMaterial(chrome)});
                world.create(Transform{}, SkyLight{}, RenderSky{environment});
                return scene.render(world);
            };
            const std::vector<u8> expected = draw(half);
            const Difference error = difference(draw(bc6h), expected);
            MESSAGE("BC6H environment (supported: ", supported, "): max ", error.max, ", mean ", error.mean);
            CHECK(brightness(expected) > 20.0);
            CHECK(error.max <= 12);
            CHECK(error.mean <= 1.5);

            resources.releaseEnvironment(half);
            resources.releaseEnvironment(bc6h);
            settle(*device, resources);
            CHECK(resources.statistics().environmentBytes == before.environmentBytes);
            CHECK(resources.statistics().environmentBytesUncompressed == before.environmentBytesUncompressed);
        }
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }
}
