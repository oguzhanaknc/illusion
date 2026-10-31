#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
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

#include <cstring>
#include <memory>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kSize = 32;
constexpr std::string_view kQuad = "game:/binary.glb#mesh/0";
constexpr std::string_view kModel = "game:/binary.glb";

constexpr std::string_view kRedPaint = R"({
  "format": "illusion.material",
  "version": 1,
  "material": {"baseColorFactor": [1, 0, 0, 1], "metallicFactor": 0, "roughnessFactor": 1},
  "textures": {"baseColor": "game:/textures/checker.png"}
})";

constexpr std::string_view kGreenPaint = R"({
  "format": "illusion.material",
  "version": 1,
  "material": {"baseColorFactor": [0, 1, 0, 1], "metallicFactor": 0, "roughnessFactor": 1}
})";

RenderDeviceDesc headless()
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

void reflectAll()
{
    reflectCoreComponents();
    reflectAssetStructs();
    reflectRenderComponents();
}

// Renders @p world from (0, 0, 5) looking down -z with full ambient light and no lights, and reads it back.
std::vector<u8> renderScene(RenderDevice& device, SceneRenderer& renderer, World& world)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::TextureDesc targetDesc;
    targetDesc.width = kSize;
    targetDesc.height = kSize;
    targetDesc.format = nvrhi::Format::RGBA8_UNORM;
    targetDesc.isRenderTarget = true;
    targetDesc.debugName = "Lifetime test target";
    targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
    nvrhi::TextureHandle target = gpu->createTexture(targetDesc);
    nvrhi::TextureDesc readbackDesc = targetDesc;
    readbackDesc.isRenderTarget = false;
    nvrhi::StagingTextureHandle readback =
        gpu->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    Transform cameraTransform;
    cameraTransform.position = Vec3(0.0f, 0.0f, 5.0f);
    const RenderView view = RenderView::fromCamera(cameraTransform, Camera{}, 1.0f);
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

std::unique_ptr<SceneRenderer> ambientRenderer(RenderDevice& device, RenderResources& resources)
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    desc.ambient = Vec3(1.0f);
    desc.post.bloom.enabled = false;
    desc.post.ambientOcclusion.enabled = false;
    desc.occlusionCulling = false;
    return SceneRenderer::create(device, resources, desc);
}

MaterialDesc colored(Vec4 color)
{
    MaterialDesc desc;
    desc.parameters.baseColorFactor = color;
    desc.parameters.metallicFactor = 0.0f;
    desc.parameters.roughnessFactor = 1.0f;
    return desc;
}

// Waits until the GPU finished everything and lets @p resources free what was released.
u32 settle(RenderDevice& device, RenderResources& resources)
{
    device.waitIdle();
    return resources.collect();
}

bool sameStatistics(const RenderResourceStatistics& a, const RenderResourceStatistics& b)
{
    return a.vertices == b.vertices && a.vertexCapacity == b.vertexCapacity && a.indices == b.indices &&
           a.indexCapacity == b.indexCapacity && a.retiring == b.retiring &&
           a.materialCapacity == b.materialCapacity;
}

} // namespace

TEST_CASE("released resources are freed once the GPU is done, and 100 cycles keep memory flat")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        const u32 meshes = resources.meshCount();
        const u32 textures = resources.textureCount();
        const u32 materials = resources.materialCount();
        const u32 environments = resources.environmentCount();
        const MeshData cube = makeCube();
        const MeshData sphere = makeSphere(0.5f, 16, 8);
        const Image image = makeSolidImage(8, 8, Vec4(1.0f, 0.5f, 0.0f, 1.0f), ImageFormat::RGBA8);
        const Image sky = makeSolidImage(16, 8, Vec4(0.2f, 0.3f, 0.4f, 1.0f), ImageFormat::RGBA32F);
        const MeshHandle pinned = resources.createMesh(sphere); // keeps a range below the churn

        RenderResourceStatistics steady;
        MeshHandle firstMesh;
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            const MeshHandle mesh = resources.createMesh(cube);
            const TextureHandle texture = resources.createTexture(image);
            MaterialDesc desc = colored(Vec4(1.0f));
            desc.baseColor = texture;
            const MaterialHandle material = resources.createMaterial(desc);
            const EnvironmentHandle environment = resources.createEnvironment(sky, EnvironmentDesc{16, 8, 4});
            CHECK(resources.isValid(mesh));
            CHECK(resources.isValid(texture));
            CHECK(resources.isValid(material));
            CHECK(resources.isValid(environment));

            resources.releaseMaterial(material);
            resources.releaseTexture(texture);
            resources.releaseMesh(mesh);
            resources.releaseEnvironment(environment);
            CHECK_FALSE(resources.isValid(mesh)); // stale at once
            CHECK(resources.submeshCount(mesh) == 0);
            CHECK(resources.meshCount() == meshes + 1);
            CHECK(resources.statistics().retiring == 4);
            CHECK(settle(*device, resources) == 4);
            CHECK(resources.statistics().retiring == 0);
            if (cycle == 0)
            {
                firstMesh = mesh;
                steady = resources.statistics();
            }
            else if (cycle == 1)
            {
                CHECK(mesh.index == firstMesh.index); // the slot is reused with a new generation
                CHECK(mesh.generation == firstMesh.generation + 1);
                CHECK_FALSE(resources.isValid(firstMesh));
            }
        }
        CHECK(sameStatistics(resources.statistics(), steady));
        CHECK(resources.statistics().vertices == sphere.vertices.size());
        CHECK(resources.statistics().freeRanges == 0);
        CHECK(resources.meshCount() == meshes + 1);
        CHECK(resources.textureCount() == textures);
        CHECK(resources.materialCount() == materials);
        CHECK(resources.environmentCount() == environments);
        CHECK(resources.submeshCount(pinned) == 1);

        // Releasing an empty handle does nothing.
        resources.releaseMesh(MeshHandle{});
        resources.releaseModel(ModelHandles{});
        CHECK(resources.statistics().retiring == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("stale and built-in handles assert when released or updated")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        const MeshHandle mesh = resources.createMesh(makeCube());
        const TextureHandle texture =
            resources.createTexture(makeSolidImage(2, 2, Vec4(1.0f), ImageFormat::RGBA8));
        const MaterialHandle material = resources.createMaterial(colored(Vec4(1.0f)));
        resources.releaseMesh(mesh);
        resources.releaseTexture(texture);
        resources.releaseMaterial(material);

        test::ScopedAssertCatcher catcher;
        CHECK_THROWS_AS(resources.releaseMesh(mesh), test::AssertionFailure);
        CHECK_THROWS_AS(resources.releaseTexture(texture), test::AssertionFailure);
        CHECK_THROWS_AS(resources.releaseMaterial(material), test::AssertionFailure);
        CHECK_THROWS_AS(resources.updateMaterial(material, colored(Vec4(0.5f))), test::AssertionFailure);
        CHECK_THROWS_AS(resources.releaseMaterial(resources.defaultMaterial()), test::AssertionFailure);
        CHECK_THROWS_AS(resources.releaseEnvironment(EnvironmentHandle{7, 0}), test::AssertionFailure);
        CHECK(resources.statistics().retiring == 3); // the failed calls changed nothing
        MaterialDesc usesReleased = colored(Vec4(1.0f));
        usesReleased.baseColor = texture;
        CHECK_THROWS_AS(static_cast<void>(resources.createMaterial(usesReleased)), test::AssertionFailure);
        CHECK(resources.materialCount() == 1); // only the default material
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
#endif

TEST_CASE("updateMaterial changes a material in place and renders like a new material")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = ambientRenderer(*device, resources);
        REQUIRE(renderer != nullptr);
        const MeshHandle plane = resources.createMesh(makePlane(4.0f));
        Transform facing; // the plane's normal (+y) towards the camera (+z)
        facing.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));

        const MaterialHandle material = resources.createMaterial(colored(Vec4(1.0f, 0.0f, 0.0f, 1.0f)));
        World world;
        const Entity entity = world.create(facing, RenderMesh{plane, 0, material});
        const std::vector<u8> red = renderScene(*device, *renderer, world);

        // The "golden" image: the same plane with a material created green.
        const MaterialHandle green = resources.createMaterial(colored(Vec4(0.0f, 1.0f, 0.0f, 1.0f)));
        world.get<RenderMesh>(entity).material = green;
        const std::vector<u8> expected = renderScene(*device, *renderer, world);
        CHECK(expected != red);

        // Factors only: the binding set stays.
        world.get<RenderMesh>(entity).material = material;
        nvrhi::IBindingSet* const bindings = resources.materialBindingSet(material);
        resources.updateMaterial(material, colored(Vec4(0.0f, 1.0f, 0.0f, 1.0f)));
        CHECK(resources.materialBindingSet(material) == bindings);
        CHECK(renderScene(*device, *renderer, world) == expected);

        // A new texture makes the binding set again; the handle stays the same.
        MaterialDesc textured = colored(Vec4(1.0f));
        textured.baseColor = resources.createTexture(
            makeSolidImage(2, 2, Vec4(0.0f, 1.0f, 0.0f, 1.0f), ImageFormat::RGBA8Srgb));
        resources.updateMaterial(material, textured);
        CHECK(resources.materialBindingSet(material) != bindings);
        CHECK(resources.isValid(material));
        CHECK(renderScene(*device, *renderer, world) == expected);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("reloading a material file changes it in place and keeps it when the new file is broken")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kRedPaint)) == FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer);
        RenderAssetResolver resolver(cache);
        const u32 texturesBefore = resources.textureCount();

        World world;
        const Entity entity =
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"),
                                                   .material = AssetRef("game:/paint.material")});
        resolver.waitIdle(world);
        const MaterialHandle paint = world.get<RenderMesh>(entity).material;
        REQUIRE(resources.isValid(paint));
        CHECK(resources.textureCount() == texturesBefore + 1); // the checker

        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kGreenPaint)) == FileError::None);
        CHECK(streamer.reload("game:/paint.material"));
        cache.waitIdle();
        resolver.update(world);
        CHECK(world.get<RenderMesh>(entity).material == paint); // updated in place
        CHECK(cache.statistics().reloads == 1);
        CHECK(resolver.statistics().resolved == 0); // nothing to resolve again

        // The checker is no longer used by any material: eviction frees it.
        CHECK(cache.evictUnused() == 1);
        settle(*device, resources);
        CHECK(resources.textureCount() == texturesBefore);
        CHECK(cache.statistics().textures == 0);

        // A broken file keeps the previous version and reports why.
        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf("{ not json")) == FileError::None);
        CHECK(streamer.reload("game:/paint.material"));
        cache.waitIdle();
        resolver.update(world);
        CHECK(world.get<RenderMesh>(entity).material == paint);
        CHECK(resources.isValid(paint));
        CHECK_FALSE(cache.error(AssetId::fromPath("game:/paint.material")).empty());
        CHECK(cache.statistics().failed == 0);

        // Fixing it clears the error.
        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kRedPaint)) == FileError::None);
        CHECK(streamer.reload("game:/paint.material"));
        cache.waitIdle();
        CHECK(cache.error(AssetId::fromPath("game:/paint.material")).empty());
        CHECK(resources.isValid(paint));
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("reimporting a model 100 times re-resolves renderers and keeps resource counts flat")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer);
        RenderAssetResolver resolver(cache);
        World world;
        const Entity entity = world.create(Transform{}, MeshRenderer{.mesh = AssetRef(kQuad)});
        resolver.waitIdle(world, &jobs);
        const u32 meshes = resources.meshCount();
        const u32 textures = resources.textureCount();
        const u32 materials = resources.materialCount();

        RenderResourceStatistics steady;
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            const RenderMesh before = world.get<RenderMesh>(entity);
            CHECK(streamer.reload(kModel));
            cache.waitIdle();
            resolver.update(world, &jobs);
            const RenderMesh after = world.get<RenderMesh>(entity);
            CHECK(resources.isValid(after.mesh));
            CHECK(resources.isValid(after.material));
            CHECK_FALSE(resources.isValid(before.mesh)); // released when the new model was on the GPU
            CHECK(after.mesh != before.mesh);
            settle(*device, resources);
            if (cycle == 0)
            {
                steady = resources.statistics();
            }
        }
        CHECK(sameStatistics(resources.statistics(), steady));
        CHECK(resources.meshCount() == meshes);
        CHECK(resources.textureCount() == textures);
        CHECK(resources.materialCount() == materials);
        CHECK(cache.statistics().reloads == 100);
        CHECK(cache.statistics().models == 1);
        CHECK(resolver.statistics().reresolves >= 100);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a reload while the file is loading runs once more, and failed assets load when the file appears")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer);
        RenderAssetResolver resolver(cache);
        World world;
        const Entity late =
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"),
                                                   .material = AssetRef("game:/late.material")});
        resolver.waitIdle(world);
        CHECK(world.get<RenderMesh>(late).material == resources.defaultMaterial());
        CHECK_FALSE(cache.error(AssetId::fromPath("game:/late.material")).empty());

        REQUIRE(project.files.writeFile("game:/late.material", bytesOf(kGreenPaint)) == FileError::None);
        CHECK(streamer.reload("game:/late.material"));
        cache.update();                                // starts the reload
        CHECK(streamer.reload("game:/late.material")); // changed again while it loads
        cache.waitIdle();
        resolver.update(world);
        const MaterialHandle material = world.get<RenderMesh>(late).material;
        CHECK(material != resources.defaultMaterial());
        CHECK(resources.isValid(material));
        CHECK(cache.error(AssetId::fromPath("game:/late.material")).empty());
        CHECK(cache.statistics().reloads == 2);
        CHECK(cache.statistics().failed == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("reloading an environment image gives sky lights a new environment")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        const auto writeSky = [&](Vec4 color) {
            const std::vector<std::byte> png =
                encodePng(makeSolidImage(16, 8, color, ImageFormat::RGBA8Srgb));
            REQUIRE(project.files.writeFile("game:/sky.png", png) == FileError::None);
        };
        writeSky(Vec4(0.2f, 0.4f, 0.8f, 1.0f));
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCacheDesc desc;
        desc.environment = EnvironmentDesc{16, 8, 4};
        RenderAssetCache cache(resources, streamer, desc);
        RenderAssetResolver resolver(cache);
        World world;
        const Entity sky = world.create(Transform{}, SkyLight{.environment = AssetRef("game:/sky.png")});
        resolver.waitIdle(world);
        const EnvironmentHandle first = world.get<RenderSky>(sky).environment;
        REQUIRE(resources.isValid(first));
        const f32 blue = resources.irradiance(first).evaluate(Vec3(0.0f, 1.0f, 0.0f)).b;

        writeSky(Vec4(0.8f, 0.4f, 0.2f, 1.0f));
        CHECK(streamer.reload("game:/sky.png"));
        cache.waitIdle();
        resolver.update(world);
        const EnvironmentHandle second = world.get<RenderSky>(sky).environment;
        CHECK(resources.isValid(second));
        CHECK_FALSE(resources.isValid(first));
        CHECK(resources.irradiance(second).evaluate(Vec3(0.0f, 1.0f, 0.0f)).b < blue);
        CHECK(resources.environmentCount() == 1);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("evictUnused frees assets that no resolver uses")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kRedPaint)) == FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer);
        const u32 meshes = resources.meshCount();
        const u32 textures = resources.textureCount();
        const u32 materials = resources.materialCount();

        World world;
        const Entity entity = world.create(
            Transform{}, MeshRenderer{.mesh = AssetRef(kQuad), .material = AssetRef("game:/paint.material")});
        {
            RenderAssetResolver resolver(cache);
            resolver.waitIdle(world);
            CHECK(cache.evictUnused() == 0); // everything is in use
            CHECK(resources.meshCount() > meshes);

            // A renderer that switches assets keeps the old ones until releaseUnused() finds them unused.
            world.get<MeshRenderer>(entity) = MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube")};
            resolver.update(world);
            CHECK(cache.evictUnused() == 0);
            CHECK(resolver.releaseUnused(world) == 2); // the quad and the paint
            CHECK(cache.references(AssetId::fromPath(kQuad)) == 0);
            CHECK(cache.evictUnused() == 4); // quad mesh, paint, the model and the checker
            settle(*device, resources);
            CHECK(resources.meshCount() == meshes + 1); // the cube
            CHECK(resources.textureCount() == textures);
            CHECK(resources.materialCount() == materials);
            CHECK(world.get<RenderMesh>(entity).mesh.index != 0);
        }
        CHECK(cache.evictUnused() == 1); // the cube, once the resolver is gone
        settle(*device, resources);
        CHECK(resources.meshCount() == meshes);
        CHECK(cache.statistics().evicted == 5);

        // An evicted asset loads again when it is requested.
        RenderAssetResolver resolver(cache);
        resolver.waitIdle(world);
        CHECK(resources.isValid(world.get<RenderMesh>(entity).mesh));
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
