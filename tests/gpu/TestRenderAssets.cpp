#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/assets/ShaderLoader.hpp>
#include <illusion/assets/TextureCompression.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kSize = 64;
constexpr std::string_view kQuad = "game:/binary.glb#mesh/0";
constexpr std::string_view kPaint = R"({
  "format": "illusion.material",
  "version": 1,
  "material": {"baseColorFactor": [0.9, 0.3, 0.2, 1], "metallicFactor": 0, "roughnessFactor": 0.6},
  "textures": {"baseColor": "game:/textures/checker.png"}
})";

RenderDeviceDesc headless()
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

// Small bakes keep the tests fast; uncompressed ones read back exactly as they were baked.
RenderAssetCacheDesc smallBakes(bool compress = false)
{
    RenderAssetCacheDesc desc;
    desc.environment = EnvironmentDesc{32, 16, 8, compress};
    return desc;
}

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

// Encodes mip 0 of an RGBA32F image as an uncompressed Radiance HDR (RGBE) file.
std::vector<std::byte> encodeRadiance(const Image& image)
{
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(image.height()) +
                               " +X " + std::to_string(image.width()) + "\n";
    std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(header.data()),
                                 reinterpret_cast<const std::byte*>(header.data()) + header.size());
    for (u32 y = 0; y < image.height(); ++y)
    {
        for (u32 x = 0; x < image.width(); ++x)
        {
            const Vec4 pixel = readPixel(image, x, y);
            const f32 largest = std::max({pixel.r, pixel.g, pixel.b});
            u8 rgbe[4] = {0, 0, 0, 0};
            if (largest > 1e-32f)
            {
                int exponent = 0;
                const f32 scale = std::frexp(largest, &exponent) * 256.0f / largest;
                rgbe[0] = static_cast<u8>(pixel.r * scale);
                rgbe[1] = static_cast<u8>(pixel.g * scale);
                rgbe[2] = static_cast<u8>(pixel.b * scale);
                rgbe[3] = static_cast<u8>(exponent + 128);
            }
            for (const u8 value : rgbe)
            {
                bytes.push_back(std::byte{value});
            }
        }
    }
    return bytes;
}

// Renders @p world from (0, 0, 5) looking down -z into a 64x64 RGBA8 target and reads it back.
std::vector<u8> renderScene(RenderDevice& device, SceneRenderer& renderer, World& world)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::TextureDesc targetDesc;
    targetDesc.width = kSize;
    targetDesc.height = kSize;
    targetDesc.format = nvrhi::Format::RGBA8_UNORM;
    targetDesc.isRenderTarget = true;
    targetDesc.debugName = "Render asset test target";
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

Transform placed(Vec3 position, f32 yaw = 0.0f)
{
    Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    return transform;
}

void reflectAll()
{
    reflectCoreComponents();
    reflectAssetStructs();
    reflectRenderComponents();
    reflectParticleComponents();
}

constexpr std::string_view kTintShader = R"([ShadingModel("Unlit")]
struct Parameters
{
    [Color(1, 0, 0, 1)] float3 tint;
    [Default(1)] float gain;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = p.tint * p.gain;
    return output;
}
)";

// The same parameters with the channels swapped, so that an edit shows in the picture.
constexpr std::string_view kSwappedShader = R"([ShadingModel("Unlit")]
struct Parameters
{
    [Color(1, 0, 0, 1)] float3 tint;
    [Default(1)] float gain;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = p.tint.bgr * p.gain;
    return output;
}
)";

constexpr std::string_view kBrokenShader = "struct Parameters {};\nnonsense";

ShaderLoaderDesc compilingLoader()
{
    ShaderLoaderDesc desc;
    desc.compiler = ShaderCompilerDesc{.slangc = utf8Path(ILLUSION_TEST_SLANGC),
                                       .library = utf8Path(ILLUSION_TEST_SHADER_LIBRARY),
                                       .workDirectory = {},
                                       .timeout = std::chrono::minutes(2),
                                       .maxProcesses = 0};
    return desc;
}

// The colour at the middle of a rendered picture.
Vec3 centerOf(const std::vector<u8>& pixels)
{
    const usize index = (static_cast<usize>(kSize / 2) * kSize + kSize / 2) * 4;
    return Vec3(pixels[index], pixels[index + 1], pixels[index + 2]);
}

std::string shaderMaterial(std::string_view shader, std::string_view parameters)
{
    return R"({"format": "illusion.material", "version": 2, "shader": ")" + std::string(shader) +
           R"(", "parameters": )" + std::string(parameters) + "}";
}

} // namespace

TEST_CASE("material files name a shader with parameters, and edits of the shader swap in")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        const auto write = [&](std::string_view path, std::string_view text) {
            REQUIRE(project.files.writeFile(std::string(path), bytesOf(text)) == FileError::None);
        };
        write("game:/tint.shader", kTintShader);
        write("game:/red.material", shaderMaterial("game:/tint.shader", R"({"gain": 2})"));
        write("game:/blue.material", shaderMaterial("game:/tint.shader", R"({"tint": [0, 0, 1]})"));
        write("game:/wrong.material", shaderMaterial("game:/tint.shader", R"({"glow": 2})"));
        write("game:/broken.material", shaderMaterial("game:/broken.shader", "{}"));
        write("game:/broken.shader", kBrokenShader);
        write("game:/standard.material", shaderMaterial("builtin:/shader/standard", "{}"));
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        ShaderLoader loader(project.files, compilingLoader());
        RenderResources resources(*device);
        SceneRendererDesc rendererDesc;
        rendererDesc.colorFormat = nvrhi::Format::RGBA8_UNORM;
        rendererDesc.post.bloom.enabled = false;
        rendererDesc.post.ambientOcclusion.enabled = false;
        rendererDesc.occlusionCulling = false;
        auto renderer = SceneRenderer::create(*device, resources, rendererDesc);
        REQUIRE(renderer != nullptr);
        RenderAssetCacheDesc cacheDesc = smallBakes();
        cacheDesc.shaders = &loader;
        RenderAssetCache cache(resources, streamer, cacheDesc);
        RenderAssetResolver resolver(cache);

        World world;
        const Entity cube =
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"),
                                                   .material = AssetRef("game:/red.material")});
        resolver.waitIdle(world, &jobs);
        CHECK(cache.statistics().failed == 0);
        CHECK(cache.statistics().shaders == 1);
        const MaterialHandle red = world.get<RenderMesh>(cube).material;
        CHECK(red != resources.defaultMaterial());
        const ShaderHandle tint = resources.materialShader(red);
        CHECK(tint.index > 3); // not one of the built-in shaders
        Vec3 pixel = centerOf(renderScene(*device, *renderer, world));
        CHECK(pixel.r > 200.0f);
        CHECK(pixel.g < 40.0f);
        CHECK(pixel.b < 40.0f);

        // A material sets the parameters it names and leaves the others at the shader's defaults.
        world.get<MeshRenderer>(cube).material = AssetRef("game:/blue.material");
        resolver.waitIdle(world, &jobs);
        pixel = centerOf(renderScene(*device, *renderer, world));
        CHECK(pixel.b > 100.0f);
        CHECK(pixel.r < 40.0f);
        CHECK(resources.materialShader(world.get<RenderMesh>(cube).material) == tint); // one shader for both
        CHECK(cache.statistics().shaders == 1);

        // A name the shader does not declare fails the material, which then draws with the default one.
        world.get<MeshRenderer>(cube).material = AssetRef("game:/wrong.material");
        resolver.waitIdle(world, &jobs);
        CHECK(world.get<RenderMesh>(cube).material == resources.defaultMaterial());
        const std::string_view wrong = cache.error(AssetId::fromPath("game:/wrong.material"));
        CHECK(wrong.starts_with(
            "/parameters/glow: the shader 'game:/tint.shader' declares no parameter 'glow'"));
        CHECK(wrong.find("(it declares: tint, gain)") != std::string_view::npos);

        // A shader that does not compile draws its materials with the error shader and keeps the message.
        world.get<MeshRenderer>(cube).material = AssetRef("game:/broken.material");
        resolver.waitIdle(world, &jobs);
        const MaterialHandle broken = world.get<RenderMesh>(cube).material;
        CHECK(broken != resources.defaultMaterial());
        CHECK(resources.materialShader(broken) == resources.errorShader());
        CHECK_FALSE(cache.error(AssetId::fromPath("game:/broken.material")).empty());
        CHECK_FALSE(cache.error(AssetId::fromPath("game:/broken.shader")).empty());

        // The built-in shaders can be named too.
        world.get<MeshRenderer>(cube).material = AssetRef("game:/standard.material");
        resolver.waitIdle(world, &jobs);
        CHECK(resources.materialShader(world.get<RenderMesh>(cube).material) == resources.standardShader());

        // Editing the shader builds it on the loader's thread and swaps it in: the same handles, new
        // colours, the parameters of the material kept by name.
        world.get<MeshRenderer>(cube).material = AssetRef("game:/blue.material");
        resolver.waitIdle(world, &jobs);
        const MaterialHandle blue = world.get<RenderMesh>(cube).material;
        write("game:/tint.shader", kSwappedShader);
        REQUIRE(streamer.reload("game:/tint.shader"));
        cache.waitIdle();
        resolver.waitIdle(world, &jobs);
        CHECK(world.get<RenderMesh>(cube).material == blue);
        CHECK(resources.materialShader(blue) == tint);
        pixel = centerOf(renderScene(*device, *renderer, world));
        CHECK(pixel.r > 100.0f); // the tint (0, 0, 1) now reads as (1, 0, 0)
        CHECK(pixel.b < 40.0f);
        CHECK(cache.statistics().reloads > 0);

        // A shader that stops compiling keeps the last version that did.
        write("game:/tint.shader", kBrokenShader);
        REQUIRE(streamer.reload("game:/tint.shader"));
        cache.waitIdle();
        resolver.waitIdle(world, &jobs);
        CHECK_FALSE(cache.error(AssetId::fromPath("game:/tint.shader")).empty());
        pixel = centerOf(renderScene(*device, *renderer, world));
        CHECK(pixel.r > 100.0f);

        // Without a loader a material that names a shader fails.
        RenderAssetCache withoutLoader(resources, streamer, smallBakes());
        RenderAssetResolver plainResolver(withoutLoader);
        World other;
        other.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"),
                                               .material = AssetRef("game:/blue.material")});
        plainResolver.waitIdle(other, &jobs);
        CHECK_FALSE(withoutLoader.error(AssetId::fromPath("game:/tint.shader")).empty());
        CHECK(withoutLoader.statistics().shaders == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a scene of asset references renders like the same scene built from handles")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kPaint)) == FileError::None);
        REQUIRE(project.files.writeFile("game:/sky.hdr", encodeRadiance(makeSkyImage(64))) ==
                FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        SceneRendererDesc rendererDesc;
        rendererDesc.colorFormat = nvrhi::Format::RGBA8_UNORM;
        rendererDesc.post.bloom.enabled = false;
        rendererDesc.post.ambientOcclusion.enabled = false;
        rendererDesc.occlusionCulling = false;
        auto renderer = SceneRenderer::create(*device, resources, rendererDesc);
        REQUIRE(renderer != nullptr);

        const Transform quadPlace = placed(Vec3(-0.9f, 0.4f, 0.0f), 0.3f);
        const Transform cubePlace = placed(Vec3(1.0f, -0.6f, 0.0f), 0.7f);
        const Transform spherePlace = placed(Vec3(0.2f, -1.2f, 1.0f));
        const Transform skyPlace = placed(Vec3(0.0f), 1.0f);
        const RenderAssetCacheDesc cacheDesc = smallBakes();

        // Reference: the same scene built in code from GPU handles.
        std::vector<u8> expected;
        {
            const AssetHandle<ModelData> model = streamer.loadModel("game:/binary.glb");
            // Material files ask for their base colour in BC7 (materialTextureUses()).
            const AssetHandle<Image> checker = streamer.loadImage(
                "game:/textures/checker.png", {.srgb = true, .compression = TextureCompression::BC7});
            const AssetHandle<Image> sky =
                streamer.loadImage("game:/sky.hdr", {.srgb = true, .generateMips = false});
            streamer.waitIdle();
            REQUIRE(model.ready());
            REQUIRE(checker.ready());
            REQUIRE(sky.ready());
            const ModelHandles quad = resources.uploadModel(model.value());
            MaterialDesc paint;
            paint.parameters = parseMaterialAsset(kPaint).value().material;
            paint.baseColor = resources.createTexture(checker.value());
            World world;
            world.create(quadPlace, RenderMesh{quad.meshes[0], 0, quad.materials[0]});
            world.create(cubePlace,
                         RenderMesh{resources.createMesh(makeCube()), 0, resources.createMaterial(paint)});
            world.create(spherePlace,
                         RenderMesh{resources.createMesh(makeSphere()), 0, resources.defaultMaterial()});
            world.create(skyPlace, SkyLight{.intensity = 1.5f},
                         RenderSky{resources.createEnvironment(sky.value(), cacheDesc.environment)});
            world.create(placed(Vec3(0.0f), 2.0f), DirectionalLight{});
            expected = renderScene(*device, *renderer, world);
        }

        // The asset scene: saved, cleared, loaded and resolved.
        std::string json;
        {
            World world;
            world.create(quadPlace, MeshRenderer{.mesh = AssetRef(kQuad)});
            world.create(cubePlace, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"),
                                                 .material = AssetRef("game:/paint.material")});
            world.create(spherePlace, MeshRenderer{.mesh = AssetRef("builtin:/mesh/sphere")});
            world.create(skyPlace, SkyLight{.environment = AssetRef("game:/sky.hdr"), .intensity = 1.5f});
            world.create(placed(Vec3(0.0f), 2.0f), DirectionalLight{});
            assignMissingGuids(world);
            const SceneSaveResult saved = saveScene(world);
            REQUIRE(saved.ok());
            json = saved.json;
        }
        World world;
        SceneLoadOptions options;
        options.assetExists = [&](std::string_view path) {
            return project.files.exists(path);
        };
        const SceneLoadResult loaded = loadScene(world, json, options);
        REQUIRE(loaded.ok());
        CHECK(loaded.missingAssets.empty());
        RenderAssetCache cache(resources, streamer, cacheDesc);
        RenderAssetResolver resolver(cache);
        resolver.waitIdle(world, &jobs);
        CHECK(resolver.statistics().failures == 0);
        CHECK(cache.statistics().failed == 0);
        const std::vector<u8> actual = renderScene(*device, *renderer, world);

        int maxDifference = 0;
        int lit = 0;
        for (usize i = 0; i < actual.size(); ++i)
        {
            maxDifference = std::max(maxDifference, std::abs(int(actual[i]) - int(expected[i])));
            lit += i % 4 == 0 && actual[i] > 8 ? 1 : 0;
        }
        CHECK(maxDifference <= 1);
        CHECK(lit > static_cast<int>(kSize * kSize / 2)); // the sky fills the background
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the resolver shares assets between renderers and Worlds")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer, smallBakes());
        const u32 meshesBefore = resources.meshCount();

        World world;
        for (int i = 0; i < 100; ++i)
        {
            world.create(placed(Vec3(static_cast<f32>(i), 0.0f, 0.0f)),
                         MeshRenderer{.mesh = AssetRef(kQuad)});
        }
        {
            RenderAssetResolver resolver(cache);
            resolver.update(world, &jobs);
            CHECK(resolver.statistics().pending == 100);
            CHECK(cache.loading() > 0);
            resolver.waitIdle(world, &jobs);
            CHECK(resolver.statistics().pending == 0);
            CHECK(resources.meshCount() == meshesBefore + 1); // one upload for 100 renderers
            CHECK(cache.statistics().models == 1);
            CHECK(cache.statistics().meshes == 1);
            std::vector<MeshHandle> meshes;
            world.query<const RenderMesh>().each(
                [&](const RenderMesh& renderMesh) { meshes.push_back(renderMesh.mesh); });
            REQUIRE(meshes.size() == 100);
            CHECK(meshes.front().index != 0);
            CHECK(std::all_of(meshes.begin(), meshes.end(),
                              [&](MeshHandle mesh) { return mesh == meshes.front(); }));
            CHECK(cache.references(AssetId::fromPath(kQuad)) == 1);

            World other;
            other.create(Transform{}, MeshRenderer{.mesh = AssetRef(kQuad)});
            RenderAssetResolver otherResolver(cache);
            otherResolver.update(other);
            CHECK(otherResolver.statistics().resolved == 1); // already loaded: resolved at once
            CHECK(cache.references(AssetId::fromPath(kQuad)) == 2);
        }
        CHECK(cache.references(AssetId::fromPath(kQuad)) == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the resolver follows changes, removals and failures")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/paint.material", bytesOf(kPaint)) == FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer, smallBakes());
        RenderAssetResolver resolver(cache);

        World world;
        const Entity entity = world.create(Transform{}, MeshRenderer{.mesh = AssetRef(kQuad)});
        const Entity procedural = world.create(Transform{}, RenderMesh{resources.createMesh(makeCube())});
        resolver.waitIdle(world);
        const RenderMesh first = world.get<RenderMesh>(entity);
        CHECK(first.mesh.index != 0);
        CHECK(first.material != resources.defaultMaterial()); // the model's material
        CHECK(first.flags == (RenderMesh::kVisible | RenderMesh::kCastShadows));
        CHECK(first.sourceKey == renderSourceKey(world.get<MeshRenderer>(entity)));
        CHECK(first.lodBias == 1.0f);

        // The level-of-detail bias is copied (negative values clamp to 0).
        world.get<MeshRenderer>(entity).lodBias = 4.0f;
        resolver.update(world);
        CHECK(world.get<RenderMesh>(entity).lodBias == 4.0f);
        world.get<MeshRenderer>(entity).lodBias = -1.0f;
        resolver.update(world);
        CHECK(world.get<RenderMesh>(entity).lodBias == 0.0f);
        world.get<MeshRenderer>(entity).lodBias = 1.0f;
        resolver.update(world);

        // A material of the loaded model is picked up on the next update.
        world.get<MeshRenderer>(entity).material = AssetRef("builtin:/material/default");
        world.get<MeshRenderer>(entity).castShadows = false;
        resolver.update(world);
        CHECK(world.get<RenderMesh>(entity).material == resources.defaultMaterial());
        CHECK(world.get<RenderMesh>(entity).flags == RenderMesh::kVisible);

        // A material file loads in the background; the old material stays until then.
        world.get<MeshRenderer>(entity).material = AssetRef("game:/paint.material");
        resolver.update(world);
        CHECK(resolver.statistics().pending == 1);
        CHECK(world.get<RenderMesh>(entity).material == resources.defaultMaterial());
        resolver.waitIdle(world);
        const MaterialHandle paint = world.get<RenderMesh>(entity).material;
        CHECK(paint != resources.defaultMaterial());
        CHECK(cache.statistics().textures == 1);

        // Failures draw nothing (meshes) or the default material, and are reported once.
        const u32 failuresBefore = resolver.statistics().failures;
        const Entity missingMesh =
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef("game:/missing.glb#mesh/0")});
        const Entity missingMaterial =
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube"),
                                                   .material = AssetRef("game:/none.material")});
        const Entity unknownBuiltin =
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/torus")});
        resolver.waitIdle(world);
        CHECK(resolver.statistics().failures == failuresBefore + 3);
        CHECK(world.get<RenderMesh>(missingMesh).mesh.index == 0);
        CHECK(world.get<RenderMesh>(missingMaterial).mesh.index != 0);
        CHECK(world.get<RenderMesh>(missingMaterial).material == resources.defaultMaterial());
        CHECK(world.get<RenderMesh>(unknownBuiltin).mesh.index == 0);
        CHECK_FALSE(cache.error(AssetId::fromPath("game:/none.material")).empty());
        resolver.update(world);
        CHECK(resolver.statistics().resolved == 0); // failed renderers are not retried every frame
        CHECK(resolver.statistics().failures == failuresBefore + 3);

        // Removing the MeshRenderer removes its RenderMesh; RenderMeshes made by code stay.
        world.remove<MeshRenderer>(entity);
        resolver.update(world);
        CHECK_FALSE(world.has<RenderMesh>(entity));
        CHECK(world.has<RenderMesh>(procedural));
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("sky lights resolve procedural and file environments")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/sky.hdr", encodeRadiance(makeSkyImage(32))) ==
                FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer, smallBakes());
        RenderAssetResolver resolver(cache);

        World world;
        ProceduralSky parameters;
        parameters.resolution = 32;
        const Entity procedural = world.create(
            Transform{}, SkyLight{.environment = AssetRef("builtin:/sky/procedural")}, parameters);
        const Entity file = world.create(Transform{}, SkyLight{.environment = AssetRef("game:/sky.hdr")});
        const EnvironmentHandle baked =
            resources.createEnvironment(makeSkyImage(16), smallBakes().environment);
        const Entity coded = world.create(Transform{}, SkyLight{}, RenderSky{baked});
        resolver.update(world);
        CHECK(world.get<RenderSky>(procedural).environment.index != 0); // built in place
        resolver.waitIdle(world);
        CHECK(world.get<RenderSky>(file).environment.index != 0);
        CHECK(world.get<RenderSky>(coded).environment == baked);
        const u32 environments = resources.environmentCount();

        // New parameters bake a new environment; clearing the reference removes the RenderSky.
        world.get<ProceduralSky>(procedural).zenith = Vec3(1.0f, 0.0f, 0.0f);
        resolver.update(world);
        CHECK(resources.environmentCount() == environments + 1);
        world.get<SkyLight>(file).environment = AssetRef();
        resolver.update(world);
        CHECK_FALSE(world.has<RenderSky>(file));
        CHECK(world.has<RenderSky>(coded));
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("particle emitters resolve their sprite texture and release it when it is gone")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer, smallBakes());
        RenderAssetResolver resolver(cache);
        World world;
        ParticleEmitter sprite;
        sprite.texture = AssetRef("game:/textures/checker.png");
        const Entity textured = world.create(Transform{}, sprite);
        const Entity plain = world.create(Transform{}, ParticleEmitter{});
        resolver.waitIdle(world);
        REQUIRE(world.has<RenderParticles>(textured));
        CHECK(resources.isValid(world.get<RenderParticles>(textured).texture));
        CHECK_FALSE(world.has<RenderParticles>(plain)); // no texture: the round dot

        // The texture is in use: eviction keeps it. Once no emitter uses it, it can go.
        CHECK(cache.evictUnused() == 0);
        world.get<ParticleEmitter>(textured).texture = AssetRef();
        resolver.update(world);
        CHECK_FALSE(world.has<RenderParticles>(textured));
        CHECK(resolver.releaseUnused(world) == 1);
        CHECK(cache.evictUnused() == 1); // the texture
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a resolved World is checked without allocating")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer, smallBakes());
        RenderAssetResolver resolver(cache);
        World world;
        for (int i = 0; i < 1000; ++i)
        {
            world.create(Transform{},
                         MeshRenderer{.mesh = AssetRef(i % 2 == 0 ? "builtin:/mesh/cube" : kQuad)});
        }
        world.create(Transform{}, SkyLight{.environment = AssetRef("builtin:/sky/procedural")},
                     ProceduralSky{.resolution = 16});
        resolver.waitIdle(world, &jobs);
        resolver.update(world, &jobs); // warm up the Job System's queues
        // Resources released while loading are freed when the GPU reaches them: wait for that before
        // counting.
        device->waitIdle();
        static_cast<void>(resources.collect());
        static_cast<void>(resources.collect());
        REQUIRE(resources.statistics().retiring == 0);
        usize allocations = 0;
        {
            const test::ScopedAllocationCounter counter;
            resolver.update(world, nullptr);
            resolver.update(world, &jobs);
            cache.update();
            allocations = counter.count();
        }
        CHECK(allocations == 0);
        CHECK(resolver.statistics().resolved == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("baked environments read back and upload again without baking")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        const Image sky = makeSkyImage(32, SkyDesc{.sunRadiance = Vec3(20.0f)});
        const EnvironmentHandle baked = resources.createEnvironment(sky, EnvironmentDesc{16, 8, 4});
        CHECK(resources.requestEnvironmentReadback(EnvironmentHandle{}) == 0);
        BakedEnvironment none;
        CHECK(resources.readEnvironment(12345, none) == ReadbackStatus::Invalid);

        const u64 ticket = resources.requestEnvironmentReadback(baked);
        REQUIRE(ticket != 0);
        BakedEnvironment first;
        REQUIRE(resources.readEnvironment(ticket, first, true) == ReadbackStatus::Ready);
        CHECK(resources.readEnvironment(ticket, first) == ReadbackStatus::Invalid); // consumed
        REQUIRE(first.isValid());
        CHECK(first.backgroundSize == 16);
        CHECK(first.backgroundMips == 5);
        CHECK(first.specularSize == 8);
        CHECK(first.specularMips == 4);
        CHECK(first.irradiance.coefficients == resources.irradiance(baked).coefficients);
        CHECK(
            std::ranges::any_of(first.specular, [](u64 texel) { return (texel & 0xFFFFFFFFFFFFull) != 0; }));

        // Uploading the readback gives the same texels back.
        const EnvironmentHandle uploaded = resources.createEnvironment(first, "Uploaded");
        CHECK(uploaded != baked);
        BakedEnvironment second;
        REQUIRE(resources.readEnvironment(resources.requestEnvironmentReadback(uploaded), second, true) ==
                ReadbackStatus::Ready);
        CHECK(second.background == first.background);
        CHECK(second.specular == first.specular);
        CHECK(resources.irradiance(uploaded).coefficients == first.irradiance.coefficients);

        // A released environment cannot be read back.
        resources.releaseEnvironment(uploaded);
        CHECK(resources.requestEnvironmentReadback(uploaded) == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the environment bake cache hits after a restart and bakes again when the image changes")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/sky.hdr", encodeRadiance(makeSkyImage(32))) ==
                FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});

        // One run of the renderer: returns the statistics and the read back environment of the sky light.
        const auto run = [&](const RenderAssetCacheDesc& desc, BakedEnvironment& environment,
                             const auto& afterLoad) {
            AssetStreamer streamer(project.files, jobs);
            RenderResources resources(*device);
            RenderAssetCache cache(resources, streamer, desc);
            RenderAssetResolver resolver(cache);
            World world;
            const Entity sky = world.create(Transform{}, SkyLight{.environment = AssetRef("game:/sky.hdr")});
            resolver.waitIdle(world);
            afterLoad(streamer, cache, resolver, world);
            cache.waitIdle();
            resolver.update(world); // takes the handles of reloads
            const EnvironmentHandle handle = world.get<RenderSky>(sky).environment;
            CHECK(resources.readEnvironment(resources.requestEnvironmentReadback(handle), environment,
                                            true) == ReadbackStatus::Ready);
            CHECK(cache.statistics().failed == 0);
            return cache.statistics();
        };
        const auto nothing = [](AssetStreamer&, RenderAssetCache&, RenderAssetResolver&, World&) {
        };

        BakedEnvironment baked;
        const RenderAssetCacheStatistics first = run(smallBakes(), baked, nothing);
        CHECK(first.bakes == 1);
        CHECK(first.bakeCacheHits == 0);

        // A restart uploads the stored bake without baking.
        BakedEnvironment cached;
        const RenderAssetCacheStatistics second = run(smallBakes(), cached, nothing);
        CHECK(second.bakes == 0);
        CHECK(second.bakeCacheHits == 1);
        CHECK(cached.background == baked.background);
        CHECK(cached.specular == baked.specular);
        CHECK(cached.irradiance.coefficients == baked.irradiance.coefficients);

        // Another bake resolution is another entry.
        RenderAssetCacheDesc larger = smallBakes();
        larger.environment.cubeSize = 64;
        BakedEnvironment large;
        CHECK(run(larger, large, nothing).bakes == 1);
        CHECK(large.backgroundSize == 64);
        CHECK(run(larger, large, nothing).bakeCacheHits == 1);

        // Reimport bakes again although nothing changed; a changed image bakes again and replaces the entry.
        const auto reimport = [](AssetStreamer& streamer, RenderAssetCache&, RenderAssetResolver& resolver,
                                 World& world) {
            CHECK(streamer.reload("game:/sky.hdr", AssetReloadMode::Reimport));
            resolver.waitIdle(world);
        };
        const RenderAssetCacheStatistics reimported = run(smallBakes(), cached, reimport);
        CHECK(reimported.bakeCacheHits == 1);
        CHECK(reimported.bakes == 1);
        CHECK(reimported.reloads == 1);

        const auto change = [&](AssetStreamer& streamer, RenderAssetCache&, RenderAssetResolver& resolver,
                                World& world) {
            REQUIRE(project.files.writeFile(
                        "game:/sky.hdr",
                        encodeRadiance(makeSkyImage(32, SkyDesc{.zenith = Vec3(2.0f, 0.1f, 0.1f)}))) ==
                    FileError::None);
            CHECK(streamer.reload("game:/sky.hdr"));
            resolver.waitIdle(world);
        };
        BakedEnvironment changed;
        const RenderAssetCacheStatistics edited = run(smallBakes(), changed, change);
        CHECK(edited.bakes == 1);
        CHECK(changed.background != baked.background);
        BakedEnvironment again;
        const RenderAssetCacheStatistics restarted = run(smallBakes(), again, nothing);
        CHECK(restarted.bakes == 0);
        CHECK(restarted.bakeCacheHits == 1);
        CHECK(again.background == changed.background);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the bake cache stores bakes compressed to BC6H and later sessions upload them")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/sky.hdr", encodeRadiance(makeSkyImage(32))) ==
                FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});

        // One session: whether the sky light's environment can be read back (only a half-float one can), and
        // the memory it takes.
        const auto run = [&](bool& halfFloat, RenderResourceStatistics& memory) {
            AssetStreamer streamer(project.files, jobs);
            RenderResources resources(*device);
            RenderAssetCache cache(resources, streamer, smallBakes(true));
            RenderAssetResolver resolver(cache);
            World world;
            const Entity sky = world.create(Transform{}, SkyLight{.environment = AssetRef("game:/sky.hdr")});
            resolver.waitIdle(world);
            cache.waitIdle();
            const EnvironmentHandle handle = world.get<RenderSky>(sky).environment;
            BakedEnvironment environment;
            halfFloat = resources.readEnvironment(resources.requestEnvironmentReadback(handle), environment,
                                                  true) == ReadbackStatus::Ready;
            CHECK(cache.statistics().failed == 0);
            memory = resources.statistics();
            return cache.statistics();
        };

        bool bakedHalfFloat = false;
        RenderResourceStatistics bakedMemory;
        const RenderAssetCacheStatistics first = run(bakedHalfFloat, bakedMemory);
        CHECK(first.bakes == 1);
        CHECK(bakedHalfFloat); // the session that bakes keeps drawing the half-float bake
        CHECK(bakedMemory.environmentBytesUncompressed == bakedMemory.environmentBytes);

        bool cachedHalfFloat = true;
        RenderResourceStatistics cachedMemory;
        const RenderAssetCacheStatistics second = run(cachedHalfFloat, cachedMemory);
        CHECK(second.bakes == 0);
        CHECK(second.bakeCacheHits == 1);
        CHECK_FALSE(cachedHalfFloat);
        CHECK(cachedMemory.environmentBytesUncompressed == bakedMemory.environmentBytes);
        CHECK(cachedMemory.environmentBytes * 7 < bakedMemory.environmentBytes);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("without an import cache environments are baked every time")
{
    reflectAll();
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        REQUIRE(project.files.writeFile("game:/sky.hdr", encodeRadiance(makeSkyImage(32))) ==
                FileError::None);
        JobSystem jobs(JobSystemDesc{2, 256});
        for (int runs = 0; runs < 2; ++runs)
        {
            AssetStreamer streamer(project.files, jobs, AssetStreamerDesc{.cacheMount = ""});
            RenderResources resources(*device);
            RenderAssetCache cache(resources, streamer, smallBakes());
            RenderAssetResolver resolver(cache);
            World world;
            world.create(Transform{}, SkyLight{.environment = AssetRef("game:/sky.hdr")});
            resolver.waitIdle(world);
            cache.waitIdle();
            CHECK(cache.statistics().bakes == 1);
            CHECK(cache.statistics().bakeCacheHits == 0);
            CHECK(streamer.statistics().cacheWrites == 0);
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
