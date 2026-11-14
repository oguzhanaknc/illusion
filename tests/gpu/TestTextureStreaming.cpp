#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/TextureCompression.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

std::unique_ptr<SceneRenderer> makeRenderer(RenderDevice& device, RenderResources& resources, bool bindless)
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    desc.ambient = Vec3(1.0f);
    desc.post.bloom.enabled = false;
    desc.post.ambientOcclusion.enabled = false;
    desc.post.temporalAntiAliasing.enabled = false;
    desc.occlusionCulling = false;
    desc.bindless = bindless;
    return SceneRenderer::create(device, resources, desc);
}

// A 64x64 sRGB pattern with an 8-pixel checker, so that every level of its chain looks different.
Image makePattern()
{
    Image image = makeSolidImage(kSize, kSize, Vec4(0.0f), ImageFormat::RGBA8Srgb);
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            const bool dark = (x / 8 + y / 8) % 2 == 0;
            std::byte* texel = image.data.data() + (static_cast<usize>(y) * kSize + x) * 4;
            texel[0] = static_cast<std::byte>(dark ? 40 : 230);
            texel[1] = static_cast<std::byte>(x * 4);
            texel[2] = static_cast<std::byte>(y * 4);
            texel[3] = std::byte{255};
        }
    }
    generateMips(image);
    return image;
}

// Draws a quad filling a 64x64 view with a material and reads the colour back.
struct QuadScene
{
    RenderDevice& device;
    RenderResources& resources;
    SceneRenderer& renderer;
    MeshHandle plane;

    std::vector<u8> draw(MaterialHandle material)
    {
        World world;
        Transform quad;
        quad.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f)); // faces +z
        world.create(quad, RenderMesh{plane, 0, material});

        nvrhi::IDevice* gpu = device.device();
        nvrhi::TextureDesc targetDesc;
        targetDesc.width = kSize;
        targetDesc.height = kSize;
        targetDesc.format = nvrhi::Format::RGBA8_UNORM;
        targetDesc.isRenderTarget = true;
        targetDesc.debugName = "Texture streaming test target";
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

    // Draws @p image created at @p firstMip, as a reference.
    std::vector<u8> drawCreated(const Image& image, u32 firstMip)
    {
        const TextureHandle texture = resources.createTexture(image, firstMip, "Reference");
        MaterialDesc desc;
        desc.baseColor = texture;
        const MaterialHandle material = resources.createMaterial(desc);
        std::vector<u8> pixels = draw(material);
        resources.releaseMaterial(material);
        resources.releaseTexture(texture);
        return pixels;
    }
};

int maxDifference(const std::vector<u8>& a, const std::vector<u8>& b)
{
    REQUIRE(a.size() == b.size());
    int result = 0;
    for (usize i = 0; i < a.size(); ++i)
    {
        result = std::max(result, std::abs(int(a[i]) - int(b[i])));
    }
    return result;
}

// Lets the GPU finish: pending texture swaps are applied, then what they replaced is freed.
void settle(RenderDevice& device, RenderResources& resources)
{
    device.waitIdle();
    static_cast<void>(resources.collect());
    static_cast<void>(resources.collect());
}

// True if @p pixels are exactly one of @p a and @p b: a complete image of either range.
bool isOneOf(const std::vector<u8>& pixels, const std::vector<u8>& a, const std::vector<u8>& b)
{
    return maxDifference(pixels, a) == 0 || maxDifference(pixels, b) == 0;
}

// GPU bytes of the levels [first, end) of a 64x64 chain in @p format.
u64 levelBytes(ImageFormat format, u32 first, u32 end)
{
    u64 bytes = 0;
    for (u32 level = first; level < end; ++level)
    {
        bytes += imageLevelSize(format, std::max(kSize >> level, 1u), std::max(kSize >> level, 1u));
    }
    return bytes;
}

} // namespace

TEST_CASE("raising and dropping texture levels keeps the handle and draws like a texture made at that level")
{
    const Image pattern = makePattern();
    const std::optional<Image> compressed = compressImage(pattern, TextureCompression::BC7);
    REQUIRE(compressed.has_value());
    REQUIRE(pattern.mips.size() == 7);

    for (const bool supported : {true, false})
    {
        auto device = RenderDevice::create(headless(supported));
        REQUIRE(device != nullptr);
        {
            RenderResources resources(*device);
            for (const bool bindless : {true, false})
            {
                if (bindless && !(device->gpu().bindless && device->gpu().multiDrawIndirect))
                {
                    continue;
                }
                auto renderer = makeRenderer(*device, resources, bindless);
                REQUIRE(renderer != nullptr);
                QuadScene scene{*device, resources, *renderer, resources.createMesh(makePlane(1.0f))};
                for (const Image* image : {&pattern, &*compressed})
                {
                    CAPTURE(supported);
                    CAPTURE(bindless);
                    CAPTURE(static_cast<int>(image->format));
                    const std::vector<u8> full = scene.drawCreated(*image, 0);
                    std::array<std::vector<u8>, 4> expected;
                    for (u32 first = 1; first < expected.size(); ++first)
                    {
                        expected[first] = scene.drawCreated(*image, first);
                        CHECK(maxDifference(expected[first], full) > 8); // each level looks different
                    }

                    // The material is made before the levels change and is never updated.
                    const TextureHandle texture = resources.createTexture(*image, "Streamed");
                    MaterialDesc desc;
                    desc.baseColor = texture;
                    const MaterialHandle material = resources.createMaterial(desc);
                    CHECK(resources.textureFirstMip(texture) == 0);
                    CHECK(resources.textureMipCount(texture) == 7);
                    CHECK(maxDifference(scene.draw(material), full) == 0);
                    CHECK(renderer->usesBindless() == bindless);

                    // Right after the call the previous range is still bound; the new one once the GPU
                    // wrote it. Either way a complete image.
                    CHECK(resources.setTextureMips(texture, 3)); // drop
                    CHECK(resources.isValid(texture));
                    CHECK(resources.textureFirstMip(texture) == 3);
                    CHECK(isOneOf(scene.draw(material), full, expected[3]));
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(material), expected[3]) == 0);

                    CHECK(resources.setTextureMips(texture, 1, image)); // raise: copies 3.., uploads 1 and 2
                    CHECK(resources.textureFirstMip(texture) == 1);
                    CHECK(isOneOf(scene.draw(material), expected[3], expected[1]));
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(material), expected[1]) == 0);

                    CHECK(resources.setTextureMips(texture, 2)); // drop one level
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(material), expected[2]) == 0);

                    CHECK(resources.setTextureMips(texture, 0, image));
                    CHECK(resources.isValid(texture));
                    CHECK(resources.textureFirstMip(texture) == 0);
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(material), full) == 0);

                    // Two changes before the GPU finished the first: the second copies from the first one's
                    // texture, and only the last one is bound.
                    CHECK(resources.setTextureMips(texture, 3));
                    CHECK(resources.setTextureMips(texture, 1, image));
                    CHECK(resources.textureFirstMip(texture) == 1);
                    CHECK(isOneOf(scene.draw(material), full, expected[1]));
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(material), expected[1]) == 0);
                    CHECK(resources.setTextureMips(texture, 2));
                    CHECK(resources.setTextureMips(texture, 0, image));
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(material), full) == 0);

                    // Down to the last level (clamped) and back up from a texture created small.
                    CHECK(resources.setTextureMips(texture, 100));
                    CHECK(resources.textureFirstMip(texture) == 6);
                    resources.releaseMaterial(material);
                    resources.releaseTexture(texture);

                    const TextureHandle small = resources.createTexture(*image, 5, "Small");
                    CHECK(resources.textureFirstMip(small) == 5);
                    MaterialDesc smallDesc;
                    smallDesc.baseColor = small;
                    const MaterialHandle smallMaterial = resources.createMaterial(smallDesc);
                    CHECK(resources.setTextureMips(small, 0, image));
                    settle(*device, resources);
                    CHECK(maxDifference(scene.draw(smallMaterial), full) == 0);
                    resources.releaseMaterial(smallMaterial);
                    resources.releaseTexture(small);
                    settle(*device, resources);
                }
            }
        }
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }
}

TEST_CASE("texture statistics count the resident levels and the bytes streaming leaves out")
{
    const Image pattern = makePattern();
    const std::optional<Image> compressed = compressImage(pattern, TextureCompression::BC7);
    REQUIRE(compressed.has_value());
    for (const bool supported : {true, false})
    {
        CAPTURE(supported);
        auto device = RenderDevice::create(headless(supported));
        REQUIRE(device != nullptr);
        {
            RenderResources resources(*device);
            const RenderResourceStatistics before = resources.statistics();
            CHECK(before.streamingSavedBytes == 0);
            for (const Image* image : {&pattern, &*compressed})
            {
                CAPTURE(static_cast<int>(image->format));
                const ImageFormat gpu =
                    supported || image == &pattern ? image->format : ImageFormat::RGBA8Srgb;
                const TextureHandle texture = resources.createTexture(*image, 2);
                RenderResourceStatistics with = resources.statistics();
                CHECK(with.textureBytes - before.textureBytes == levelBytes(gpu, 2, 7));
                CHECK(with.textureBytesUncompressed - before.textureBytesUncompressed ==
                      levelBytes(ImageFormat::RGBA8Srgb, 2, 7));
                CHECK(with.streamingSavedBytes == levelBytes(gpu, 0, 2));

                // The replaced texture counts until the GPU is done with it.
                REQUIRE(resources.setTextureMips(texture, 0, image));
                with = resources.statistics();
                CHECK(with.textureBytes - before.textureBytes ==
                      levelBytes(gpu, 2, 7) + levelBytes(gpu, 0, 7));
                CHECK(with.streamingSavedBytes == 0);
                settle(*device, resources);
                with = resources.statistics();
                CHECK(with.textureBytes - before.textureBytes == levelBytes(gpu, 0, 7));
                CHECK(with.textureBytesUncompressed - before.textureBytesUncompressed ==
                      levelBytes(ImageFormat::RGBA8Srgb, 0, 7));

                REQUIRE(resources.setTextureMips(texture, 4));
                settle(*device, resources);
                with = resources.statistics();
                CHECK(with.textureBytes - before.textureBytes == levelBytes(gpu, 4, 7));
                CHECK(with.streamingSavedBytes == levelBytes(gpu, 0, 4));
                CHECK(with.textures == before.textures + 1);

                resources.releaseTexture(texture);
                settle(*device, resources);
                with = resources.statistics();
                CHECK(with.textureBytes == before.textureBytes);
                CHECK(with.textureBytesUncompressed == before.textureBytesUncompressed);
                CHECK(with.streamingSavedBytes == 0);
                CHECK(with.retiring == 0);
            }
        }
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }
}

TEST_CASE(
    "changing texture levels fails without changes for a wrong image, a raise without one or a bad handle")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        const Image pattern = makePattern();
        const TextureHandle texture = resources.createTexture(pattern, 2);
        const RenderResourceStatistics before = resources.statistics();

        CHECK_FALSE(resources.setTextureMips(texture, 0)); // a raise needs the image
        Image smaller = makeSolidImage(32, 32, Vec4(1.0f), ImageFormat::RGBA8Srgb);
        generateMips(smaller);
        CHECK_FALSE(resources.setTextureMips(texture, 0, &smaller));
        CHECK_FALSE(resources.setTextureMips(texture, 4, &smaller)); // also when dropping
        Image linear = pattern;
        linear.format = ImageFormat::RGBA8;
        CHECK_FALSE(resources.setTextureMips(texture, 0, &linear));
        Image fewer = pattern;
        fewer.mips.resize(3);
        CHECK_FALSE(resources.setTextureMips(texture, 0, &fewer));
        CHECK(resources.textureFirstMip(texture) == 2);
        CHECK(resources.statistics().textureBytes == before.textureBytes);
        CHECK(resources.statistics().retiring == 0);

        CHECK(resources.setTextureMips(texture, 2)); // nothing to change
        CHECK(resources.statistics().retiring == 0);

        CHECK_FALSE(resources.setTextureMips(TextureHandle{}, 0, &pattern));
        CHECK_FALSE(
            resources.setTextureMips(TextureHandle{texture.index, texture.generation + 1}, 0, &pattern));
        CHECK(resources.textureFirstMip(TextureHandle{}) == 0);
        CHECK(resources.textureMipCount(TextureHandle{}) == 0);
        resources.releaseTexture(texture);
        CHECK_FALSE(resources.setTextureMips(texture, 0, &pattern));
        CHECK(resources.textureMipCount(texture) == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("releasing a texture with a level change pending cancels it, and its slot serves a new texture")
{
    const Image pattern = makePattern();
    Image plain = makeSolidImage(kSize, kSize, Vec4(0.2f, 0.7f, 0.3f, 1.0f), ImageFormat::RGBA8Srgb);
    generateMips(plain);
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = makeRenderer(*device, resources, true);
        REQUIRE(renderer != nullptr);
        QuadScene scene{*device, resources, *renderer, resources.createMesh(makePlane(1.0f))};
        const std::vector<u8> plainImage = scene.drawCreated(plain, 0);
        settle(*device, resources);
        const RenderResourceStatistics before = resources.statistics();

        // Released with one and with two changes pending: everything returns to the baseline.
        for (const int changes : {1, 2})
        {
            CAPTURE(changes);
            const TextureHandle texture = resources.createTexture(pattern, "Released");
            MaterialDesc desc;
            desc.baseColor = texture;
            const MaterialHandle material = resources.createMaterial(desc);
            CHECK(resources.setTextureMips(texture, 2));
            if (changes == 2)
            {
                CHECK(resources.setTextureMips(texture, 0, &pattern));
            }
            resources.releaseMaterial(material);
            resources.releaseTexture(texture);
            CHECK_FALSE(resources.isValid(texture));
            CHECK(resources.textureFirstMip(texture) == 0);
            settle(*device, resources);
            const RenderResourceStatistics after = resources.statistics();
            CHECK(after.textureBytes == before.textureBytes);
            CHECK(after.textureBytesUncompressed == before.textureBytesUncompressed);
            CHECK(after.streamingSavedBytes == 0);
            CHECK(after.textures == before.textures);
            CHECK(after.retiring == 0);
        }

        // A change pending on a released texture never reaches the new texture that reuses its slot.
        const TextureHandle old = resources.createTexture(pattern, "Old");
        CHECK(resources.setTextureMips(old, 4));
        resources.releaseTexture(old);
        settle(*device, resources);
        const TextureHandle reused = resources.createTexture(plain, "Reused");
        CHECK(reused.index == old.index);
        CHECK(reused.generation != old.generation);
        CHECK(resources.textureFirstMip(reused) == 0);
        MaterialDesc desc;
        desc.baseColor = reused;
        const MaterialHandle material = resources.createMaterial(desc);
        settle(*device, resources);
        CHECK(maxDifference(scene.draw(material), plainImage) == 0);
        resources.releaseMaterial(material);
        resources.releaseTexture(reused);
        settle(*device, resources);
        CHECK(resources.statistics().textureBytes == before.textureBytes);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

namespace
{

constexpr u32 kTextureSide = 256;

std::string materialFile(std::string_view texture)
{
    return std::string(
               R"({"format": "illusion.material", "version": 1, "material": {"baseColorFactor": [1, 1, 1, 1]},
"textures": {"baseColor": ")") +
           std::string(texture) + "\"}}";
}

// Bytes a 256x256 BC7 texture saves with levels below @p first left out.
u64 savedBc7(u32 first)
{
    u64 bytes = 0;
    for (u32 level = 0; level < first; ++level)
    {
        bytes += imageLevelSize(ImageFormat::BC7Srgb, kTextureSide >> level, kTextureSide >> level);
    }
    return bytes;
}

// Bytes of the levels of a 256x256 BC7 texture from @p first on.
u64 residentBc7(u32 first)
{
    u64 bytes = 0;
    for (u32 level = first; level < 9; ++level)
    {
        bytes += imageLevelSize(ImageFormat::BC7Srgb, std::max(kTextureSide >> level, 1u),
                                std::max(kTextureSide >> level, 1u));
    }
    return bytes;
}

} // namespace

TEST_CASE("the render asset cache streams material textures within its budget by distance")
{
    reflectCoreComponents();
    reflectAssetStructs();
    reflectRenderComponents();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        for (const char* name : {"a", "b"})
        {
            const Image image =
                makeSolidImage(kTextureSide, kTextureSide,
                               name[0] == 'a' ? Vec4(0.8f, 0.2f, 0.2f, 1.0f) : Vec4(0.2f, 0.2f, 0.8f, 1.0f),
                               ImageFormat::RGBA8Srgb);
            const std::string texture = std::string("game:/textures/") + name + ".png";
            REQUIRE(project.files.writeFile(texture, encodePng(image)) == FileError::None);
            REQUIRE(project.files.writeText(std::string("game:/") + name + ".material",
                                            materialFile(texture)) == FileError::None);
        }
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);

        // Two cubes 100 units apart; a view 2 units from one sees the other at about 18 pixels.
        const Vec3 nearA(-48.0f, 0.0f, 0.0f);
        const Vec3 nearB(48.0f, 0.0f, 0.0f);
        World world;
        world.create(
            Transform{.position = Vec3(-50.0f, 0.0f, 0.0f)},
            MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"), .material = AssetRef("game:/a.material")});
        world.create(
            Transform{.position = Vec3(50.0f, 0.0f, 0.0f)},
            MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"), .material = AssetRef("game:/b.material")});

        SUBCASE("without a budget every level stays resident")
        {
            RenderAssetCache cache(resources, streamer);
            RenderAssetResolver resolver(cache);
            resolver.waitIdle(world, &jobs);
            const std::array<TextureStreamingView, 1> views{TextureStreamingView{nearA, 1000.0f}};
            for (int frame = 0; frame < 5; ++frame)
            {
                resolver.update(world, &jobs, views);
                cache.update();
            }
            CHECK(cache.statistics().textures == 2);
            CHECK(cache.statistics().streamedTextures == 0);
            CHECK(cache.statistics().textureRaises == 0);
            CHECK(cache.statistics().textureDrops == 0);
            CHECK(resources.statistics().streamingSavedBytes == 0);
        }

        SUBCASE("near textures are raised at once and far ones dropped after the cooldown")
        {
            constexpr u32 kCooldown = 4;
            RenderAssetCacheDesc desc;
            // Both at level 0 do not fit, one at 0 and one at its lowest level (2: 64 pixels) do.
            desc.textureBudgetBytes = residentBc7(0) + residentBc7(2) + 1000;
            desc.textureStreamingCooldown = kCooldown;
            RenderAssetCache cache(resources, streamer, desc);
            RenderAssetResolver resolver(cache);
            resolver.waitIdle(world, &jobs);
            CHECK(cache.statistics().textures == 2);
            // First loaded without demand: created at their lowest level.
            CHECK(cache.statistics().streamedTextures == 2);
            CHECK(resources.statistics().streamingSavedBytes == 2 * savedBc7(2));

            std::vector<TextureStreamingView> views;
            const auto frame = [&] {
                resolver.update(world, &jobs, views);
                cache.update();
                streamer.waitIdle();
                return resources.statistics().streamingSavedBytes;
            };

            // Both near: over budget at level 0, so both get a bias of one level.
            views = {TextureStreamingView{nearA, 1000.0f}, TextureStreamingView{nearB, 1000.0f}};
            for (int i = 0; i < 4 && resources.statistics().streamingSavedBytes != 2 * savedBc7(1); ++i)
            {
                frame();
            }
            CHECK(resources.statistics().streamingSavedBytes == 2 * savedBc7(1));
            CHECK(cache.statistics().textureRaises == 2);
            CHECK(cache.statistics().textureDrops == 0);

            // Only B near: B is raised as soon as its image is loaded again, A drops after exactly the
            // cooldown.
            views = {TextureStreamingView{nearB, 1000.0f}};
            CHECK(frame() == 2 * savedBc7(1)); // B's image is loading
            CHECK(frame() == savedBc7(1));     // B raised
            CHECK(frame() == savedBc7(1));
            CHECK(frame() == savedBc7(2)); // A dropped on the fourth update
            CHECK(cache.statistics().textureRaises == 3);
            CHECK(cache.statistics().textureDrops == 1);
            CHECK(cache.statistics().streamedTextures == 1);

            // The view moves to A: they swap.
            views = {TextureStreamingView{nearA, 1000.0f}};
            CHECK(frame() == savedBc7(2));
            CHECK(frame() == 0); // A raised, B cooling down
            CHECK(frame() == 0);
            CHECK(frame() == savedBc7(2)); // B dropped
            CHECK(cache.statistics().textureRaises == 4);
            CHECK(cache.statistics().textureDrops == 2);
            CHECK(cache.statistics().streamedTextures == 1);

            // Nothing changes from here on, and an update that changes nothing does not allocate.
            for (int i = 0; i < 2 * static_cast<int>(kCooldown); ++i)
            {
                CHECK(frame() == savedBc7(2));
            }
            usize allocations = 0;
            {
                const test::ScopedAllocationCounter counter;
                resolver.update(world, &jobs, views);
                cache.update();
                resolver.update(world, nullptr, views);
                cache.update();
                allocations = counter.count();
            }
            CHECK(allocations == 0);
            CHECK(cache.statistics().textureRaises == 4);
            CHECK(cache.statistics().textureDrops == 2);

            // Without views no material is seen: after the cooldown both are at their lowest level.
            for (u32 i = 0; i < kCooldown; ++i)
            {
                resolver.update(world, &jobs);
                cache.update();
            }
            CHECK(resources.statistics().streamingSavedBytes == 2 * savedBc7(2));
            CHECK(cache.statistics().textureDrops == 3);
        }
        settle(*device, resources);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
