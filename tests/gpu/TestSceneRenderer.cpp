#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numbers>

using namespace illusion;

namespace
{

constexpr u32 kSize = 64;

struct Rgb
{
    int r;
    int g;
    int b;
};

/// Renders @p world from a camera at (0, 0, 5) looking down -z into a 64x64 RGBA8 target and reads it back.
std::vector<u8> renderScene(RenderDevice& device, SceneRenderer& renderer, World& world, JobSystem* jobs)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::TextureDesc targetDesc;
    targetDesc.width = kSize;
    targetDesc.height = kSize;
    targetDesc.format = nvrhi::Format::RGBA8_UNORM;
    targetDesc.isRenderTarget = true;
    targetDesc.debugName = "Scene test target";
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
    renderer.render(commands, target, world, view, jobs);
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

Rgb pixelAt(const std::vector<u8>& pixels, u32 x, u32 y)
{
    const u8* p = pixels.data() + (static_cast<usize>(y) * kSize + x) * 4;
    return Rgb{p[0], p[1], p[2]};
}

RenderDeviceDesc headless()
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

} // namespace

TEST_CASE("the scene renderer lights, culls and instances meshes")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc;
        desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
        desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        desc.ambient = Vec3(0.0f);
        desc.post.bloom.enabled = false; // exact clear-colour checks below
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);

        const MeshHandle sphere = resources.createMesh(makeSphere(1.0f, 32, 16), "Sphere");
        MaterialDesc red;
        red.parameters.baseColorFactor = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        red.parameters.metallicFactor = 0.0f;
        red.parameters.roughnessFactor = 0.6f;
        const MaterialHandle redMaterial = resources.createMaterial(red);

        World world;
        world.create(Transform{}, RenderMesh{sphere, 0, redMaterial});
        Transform behind;
        behind.position = Vec3(10.0f, 0.0f, 20.0f); // behind the camera, off the light's path (no shadow)
        world.create(behind, RenderMesh{sphere, 0, redMaterial});
        world.create(Transform{}, DirectionalLight{Vec3(1.0f), 3.0f}); // shines along -z, towards the sphere

        JobSystem jobs(JobSystemDesc{2, 256});
        const std::vector<u8> pixels = renderScene(*device, *renderer, world, &jobs);
        CHECK(renderer->stats().candidates == 2);
        CHECK(renderer->stats().visible == 1);
        CHECK(renderer->stats().drawCalls == 4); // 2 culling phases x (depth prepass + shading)

        // Red diffuse plus a neutral (white) specular highlight: the light comes from the camera's
        // direction, so at the center GGX gives ~0.074 linear specular, ~79/255 after ACES and sRGB.
        const Rgb center = pixelAt(pixels, 32, 32);
        CHECK(center.r > 200);
        CHECK(center.r > center.g + 100);
        CHECK(std::abs(center.g - center.b) <= 2);
        const Rgb rim = pixelAt(pixels, 32, 23); // near the top edge: little specular, darker diffuse
        CHECK(rim.r > 20);
        CHECK(rim.r < center.r);
        CHECK(rim.g < 20);
        const Rgb corner = pixelAt(pixels, 1, 1);
        CHECK(corner.r == 0);
        CHECK(corner.g == 0);

        // Three more spheres with the same mesh and material are drawn with one instanced call.
        for (const f32 x : {-2.5f, 2.5f, 0.0f})
        {
            Transform t;
            t.position = Vec3(x, x == 0.0f ? 2.5f : 0.0f, -2.0f);
            world.create(t, RenderMesh{sphere, 0, redMaterial});
        }
        renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->stats().visible == 4);
        CHECK(renderer->stats().drawCalls == 4); // one indirect draw per phase and pass
        CHECK(renderer->stats().triangles == 4ull * resources.submeshCount(sphere) * 32 * 16 * 2);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("one-sided surfaces are visible from the front only")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc;
        desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
        desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        desc.ambient = Vec3(1.0f); // unlit check: ambient makes any visible surface bright
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);
        const MeshHandle plane = resources.createMesh(makePlane(4.0f), "Plane");

        World world;
        Transform facing; // rotate +y (the plane normal) to +z, towards the camera
        facing.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        const Entity entity = world.create(facing, RenderMesh{plane, 0, MaterialHandle{}});
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r > 100);

        world.get<Transform>(entity).rotation =
            glm::angleAxis(-std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f)); // normal now faces away
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("disabled entities and their children are not drawn")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc;
        desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
        desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        desc.ambient = Vec3(1.0f);
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);
        const MeshHandle sphere = resources.createMesh(makeSphere(1.0f, 16, 8), "Sphere");

        World world;
        HierarchyIndex hierarchy;
        const Entity parent = world.create(Transform{}, RenderMesh{sphere, 0, MaterialHandle{}});
        const Entity child = world.create(Transform{}, RenderMesh{sphere, 0, MaterialHandle{}});
        REQUIRE(setParent(world, child, parent));
        updateTransformHierarchy(world, hierarchy);
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r > 100);
        CHECK(renderer->stats().candidates == 2);

        setEnabled(world, parent, false);
        updateTransformHierarchy(world, hierarchy);
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r == 0);
        CHECK(renderer->stats().candidates == 0);
        CHECK(renderer->stats().visible == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("uploading a model skips meshes without triangles")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        // Regression: a glTF primitive with fewer than three indices leaves a mesh with vertices but no
        // indices, which used to reach createMesh() and abort.
        ModelData model;
        MeshData degenerate;
        degenerate.vertices.resize(2);
        degenerate.submeshes.push_back(Submesh{});
        model.meshes.push_back(degenerate);
        model.meshes.push_back(makeCube());
        const ModelHandles handles = resources.uploadModel(model);
        REQUIRE(handles.meshes.size() == 2);
        CHECK(handles.meshes[0].index == 0);
        CHECK(resources.submeshCount(handles.meshes[0]) == 0);
        CHECK(resources.submeshCount(handles.meshes[1]) == 1);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

namespace
{

// Equirectangular RGBA32F environment with radiance given per direction.
template <class Radiance>
Image makeEnvironment(u32 width, Radiance radiance)
{
    Image image = makeSolidImage(width, width / 2, Vec4(0.0f), ImageFormat::RGBA32F);
    auto* pixels = reinterpret_cast<f32*>(image.data.data());
    for (u32 y = 0; y < width / 2; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const Vec3 d =
                equirectDirection(Vec2((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width),
                                       (static_cast<f32>(y) + 0.5f) / static_cast<f32>(width / 2)));
            const Vec3 value = radiance(d);
            std::memcpy(pixels + (static_cast<usize>(y) * width + x) * 4, &value.x, 12);
        }
    }
    return image;
}

EnvironmentDesc smallEnvironment()
{
    EnvironmentDesc desc;
    desc.cubeSize = 32;
    desc.specularSize = 16;
    desc.sampleCount = 64;
    return desc;
}

SceneRendererDesc unormTarget()
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return desc;
}

} // namespace

TEST_CASE("a uniform environment neither adds nor loses energy (white furnace)")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = SceneRenderer::create(*device, resources, unormTarget());
        REQUIRE(renderer != nullptr);
        const EnvironmentHandle environment = resources.createEnvironment(
            makeEnvironment(64, [](Vec3) { return Vec3(1.0f); }), smallEnvironment());
        CHECK(resources.environmentCount() == 1);
        // Degenerate settings still produce a usable environment.
        EnvironmentDesc tiny;
        tiny.cubeSize = 0;
        tiny.specularSize = 3;
        tiny.sampleCount = 0;
        const EnvironmentHandle degenerate =
            resources.createEnvironment(makeSolidImage(1, 1, Vec4(1.0f), ImageFormat::RGBA8), tiny);
        CHECK(resources.environmentCount() == 2);
        CHECK(resources.irradiance(degenerate).evaluate(Vec3(0, 0, -1)).r > 0.0f); // a 1x1 image is coarse
        CHECK(resources.irradiance(EnvironmentHandle{}).evaluate(Vec3(0, 0, 1)).r == 0.0f);
        CHECK(resources.irradiance(environment).evaluate(Vec3(0, 1, 0)).r ==
              doctest::Approx(std::numbers::pi).epsilon(0.02));
        const MeshHandle sphere = resources.createMesh(makeSphere(1.5f, 48, 24), "Sphere");

        World world;
        world.create(Transform{}, SkyLight{}, RenderSky{environment});
        const Entity entity = world.create(Transform{}, RenderMesh{sphere, 0, MaterialHandle{}});

        // White surfaces in a uniform environment of radiance 1 reflect radiance 1 at every angle, the
        // same as the background: ACES(1) = 0.804 -> sRGB 231.
        struct Case
        {
            f32 metallic;
            f32 roughness;
        };
        for (const Case c : {Case{1.0f, 0.2f}, Case{1.0f, 0.7f}, Case{0.0f, 0.5f}, Case{0.0f, 1.0f}})
        {
            CAPTURE(c.metallic);
            CAPTURE(c.roughness);
            MaterialDesc material;
            material.parameters.metallicFactor = c.metallic;
            material.parameters.roughnessFactor = c.roughness;
            world.get<RenderMesh>(entity).material = resources.createMaterial(material);
            const std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
            const Rgb background = pixelAt(pixels, 1, 1);
            CHECK(std::abs(background.r - 231) <= 2);
            for (const u32 x : {32u, 40u, 23u})
            {
                CAPTURE(x);
                const Rgb surface = pixelAt(pixels, x, 32);
                CHECK(std::abs(surface.r - background.r) <= 12);
                CHECK(std::abs(surface.g - surface.b) <= 2);
            }
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the environment background and lighting follow the SkyLight orientation")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc = unormTarget();
        desc.post.bloom.enabled = false; // exact clear-colour check at the end
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);
        // Red towards -x (west), blue towards +x (east).
        const EnvironmentHandle environment = resources.createEnvironment(
            makeEnvironment(
                64, [](Vec3 d) { return d.x < 0.0f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f); }),
            smallEnvironment());
        World world;
        const Entity sky = world.create(Transform{}, SkyLight{}, RenderSky{environment});

        std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 2, 32).r > 150);
        CHECK(pixelAt(pixels, 2, 32).b < 50);
        CHECK(pixelAt(pixels, 61, 32).b > 150);
        CHECK(pixelAt(pixels, 61, 32).r < 50);

        // Turning the environment half way round swaps the sides.
        world.get<Transform>(sky).rotation = glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
        pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 2, 32).b > 150);
        CHECK(pixelAt(pixels, 61, 32).r > 150);

        // A rough white sphere is lit red on the side facing the red half (here +x after the rotation).
        MaterialDesc white;
        white.parameters.metallicFactor = 0.0f;
        world.create(Transform{}, RenderMesh{resources.createMesh(makeSphere(1.5f, 48, 24), "Sphere"), 0,
                                             resources.createMaterial(white)});
        world.get<SkyLight>(sky).background = false;
        pixels = renderScene(*device, *renderer, world, nullptr);
        const Rgb left = pixelAt(pixels, 20, 32);
        const Rgb right = pixelAt(pixels, 44, 32);
        CHECK(right.r > right.b + 40);
        CHECK(left.b > left.r + 40);
        CHECK(pixelAt(pixels, 1, 1).r == 0); // background disabled: clear colour
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a directional light casts soft shadows from casters outside the view")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc = unormTarget();
        desc.ambient = Vec3(0.05f);
        desc.shadows.resolution = 512;
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);

        World world;
        Transform ground;
        ground.position = Vec3(0.0f, -1.0f, 0.0f);
        world.create(ground,
                     RenderMesh{resources.createMesh(makePlane(40.0f, 1), "Ground"), 0, MaterialHandle{}});
        // A 2 m wide roof high above the view: invisible, but it shades the ground for x in [-1, 1].
        Transform roof;
        roof.position = Vec3(0.0f, 10.0f, 0.0f);
        world.create(roof, RenderMesh{resources.createMesh(makePlane(2.0f, 1), "Roof"), 0, MaterialHandle{}});
        Transform sun; // pointing straight down
        sun.rotation = glm::angleAxis(-std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        const Entity light = world.create(sun, DirectionalLight{Vec3(1.0f), 3.0f});

        // Ground points (0, -1, 0), (0.7, -1, 0) (0.3 m inside the shadow edge) and (1.8, -1, 0) seen
        // from the test camera.
        constexpr u32 kShadowX = 32;
        constexpr u32 kNearEdgeX = 40;
        constexpr u32 kLitX = 54;
        constexpr u32 kGroundY = 43;
        std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->stats().visible == 1);
        CHECK(renderer->stats().shadowCasters >= 2);
        CHECK(renderer->stats().shadowDrawCalls >= 1);
        CHECK(renderer->shadowCascades().count == 4);
        const int shadowed = pixelAt(pixels, kShadowX, kGroundY).r;
        const int lit = pixelAt(pixels, kLitX, kGroundY).r;
        CHECK(lit > 150);
        CHECK(shadowed < lit / 3);
        const int nearEdge = pixelAt(pixels, kNearEdgeX, kGroundY).r;
        CHECK(nearEdge < lit / 3);

        // A light with a large angular size gives a wide penumbra that reaches into the shadow.
        world.get<DirectionalLight>(light).angularDiameter = 0.4f;
        pixels = renderScene(*device, *renderer, world, nullptr);
        const int soft = pixelAt(pixels, kNearEdgeX, kGroundY).r;
        CHECK(soft > nearEdge + 20);
        CHECK(soft < lit);

        // Turning shadows off, per light or for the renderer, lights the ground evenly.
        world.get<DirectionalLight>(light).castShadows = false;
        pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(std::abs(pixelAt(pixels, kShadowX, kGroundY).r - lit) <= 3);
        CHECK(renderer->shadowCascades().count == 0);
        world.get<DirectionalLight>(light).castShadows = true;
        ShadowSettings off = renderer->shadowSettings();
        off.enabled = false;
        renderer->setShadowSettings(off);
        pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(std::abs(pixelAt(pixels, kShadowX, kGroundY).r - lit) <= 3);
        CHECK(renderer->stats().shadowCasters == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("ambient occlusion darkens indirect light in creases")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc = unormTarget();
        desc.post.bloom.enabled = false;
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);
        const EnvironmentHandle environment = resources.createEnvironment(
            makeEnvironment(64, [](Vec3) { return Vec3(0.6f); }), smallEnvironment());

        // A sphere resting on the ground, lit only by a uniform sky.
        World world;
        world.create(Transform{}, SkyLight{}, RenderSky{environment});
        world.create(Transform{}, RenderMesh{resources.createMesh(makeSphere(1.0f, 48, 24), "Sphere"), 0,
                                             MaterialHandle{}});
        Transform ground;
        ground.position = Vec3(0.0f, -1.0f, 0.0f);
        world.create(ground,
                     RenderMesh{resources.createMesh(makePlane(20.0f, 1), "Ground"), 0, MaterialHandle{}});

        // Row 44 is the ground just in front of the contact point; x = 32 is next to the sphere, x = 55
        // far from it.
        constexpr u32 kGroundY = 44;
        std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        const int crease = pixelAt(pixels, 32, kGroundY).r;
        const int open = pixelAt(pixels, 55, kGroundY).r;
        CHECK(crease < open - 20);

        PostProcessSettings settings = renderer->postProcessSettings();
        settings.ambientOcclusion.enabled = false;
        renderer->setPostProcessSettings(settings);
        pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(std::abs(pixelAt(pixels, 32, kGroundY).r - pixelAt(pixels, 55, kGroundY).r) <= 3);
        CHECK(std::abs(pixelAt(pixels, 55, kGroundY).r - open) <= 3);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("bloom spreads light from bright pixels")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = SceneRenderer::create(*device, resources, unormTarget());
        REQUIRE(renderer != nullptr);
        MaterialDesc glowing;
        glowing.parameters.baseColorFactor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        glowing.parameters.emissiveFactor = Vec3(40.0f);
        World world;
        world.create(Transform{}, RenderMesh{resources.createMesh(makeSphere(0.5f, 32, 16), "Sphere"), 0,
                                             resources.createMaterial(glowing)});

        // The sphere covers about 5.5 pixels around the centre; x = 42 is outside it.
        std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 32, 32).r == 255);
        CHECK(pixelAt(pixels, 42, 32).r > 5);
        CHECK(pixelAt(pixels, 42, 32).r < 200);

        PostProcessSettings settings = renderer->postProcessSettings();
        settings.bloom.enabled = false;
        renderer->setPostProcessSettings(settings);
        pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 42, 32).r == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("temporal anti-aliasing smooths edges and keeps flat areas")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc = unormTarget();
        desc.post.bloom.enabled = false;
        desc.post.ambientOcclusion.enabled = false;
        desc.ambient = Vec3(0.0f);
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);
        // A flat (emissive only) disc-like sphere: every covered pixel has the same value.
        MaterialDesc flat;
        flat.parameters.baseColorFactor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        flat.parameters.emissiveFactor = Vec3(0.5f);
        World world;
        world.create(Transform{}, RenderMesh{resources.createMesh(makeSphere(1.5f, 64, 32), "Sphere"), 0,
                                             resources.createMaterial(flat)});

        const std::vector<u8> aliased = renderScene(*device, *renderer, world, nullptr);
        PostProcessSettings settings = renderer->postProcessSettings();
        settings.temporalAntiAliasing.enabled = true;
        renderer->setPostProcessSettings(settings);
        std::vector<u8> smooth;
        for (int frame = 0; frame < 24; ++frame)
        {
            smooth = renderScene(*device, *renderer, world, nullptr);
        }
        // Inside the sphere both images agree.
        CHECK(std::abs(pixelAt(smooth, 32, 32).r - pixelAt(aliased, 32, 32).r) <= 4);
        // Along a row crossing the silhouette, the aliased image jumps from surface to background while
        // the anti-aliased one has partially covered pixels in between.
        const int inside = pixelAt(aliased, 32, 32).r;
        bool aliasedHasPartial = false;
        bool smoothHasPartial = false;
        for (u32 x = 40; x < 64; ++x)
        {
            const int a = pixelAt(aliased, x, 36).r;
            const int s = pixelAt(smooth, x, 36).r;
            aliasedHasPartial |= a > 8 && a < inside - 8;
            smoothHasPartial |= s > 8 && s < inside - 8;
        }
        CHECK_FALSE(aliasedHasPartial);
        CHECK(smoothHasPartial);
        renderer->resetTemporalHistory();
        renderScene(*device, *renderer, world, nullptr);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

namespace
{

// Reads back the RG16F motion vectors of the last render() (UV units).
std::vector<Vec2> readMotion(RenderDevice& device, nvrhi::ITexture* motion)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::TextureDesc desc = motion->getDesc();
    desc.isRenderTarget = false;
    desc.debugName = "Motion readback";
    nvrhi::StagingTextureHandle staging = gpu->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    commands->copyTexture(staging, nvrhi::TextureSlice(), motion, nvrhi::TextureSlice());
    commands->close();
    gpu->executeCommandList(commands);
    gpu->waitForIdle();
    size_t rowPitch = 0;
    const auto* bytes = static_cast<const std::byte*>(
        gpu->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
    std::vector<Vec2> result(static_cast<usize>(desc.width) * desc.height);
    for (u32 y = 0; y < desc.height; ++y)
    {
        for (u32 x = 0; x < desc.width; ++x)
        {
            u32 packed = 0;
            std::memcpy(&packed, bytes + y * rowPitch + x * 4, 4);
            result[static_cast<usize>(y) * desc.width + x] = glm::unpackHalf2x16(packed);
        }
    }
    gpu->unmapStagingTexture(staging);
    return result;
}

} // namespace

TEST_CASE("the prepass writes motion vectors for moving objects")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = SceneRenderer::create(*device, resources, unormTarget());
        REQUIRE(renderer != nullptr);
        World world;
        Transform plane; // facing the camera
        plane.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        const Entity entity = world.create(
            plane, RenderMesh{resources.createMesh(makePlane(2.0f, 1), "Plane"), 0, MaterialHandle{}});

        const u64 structure = world.structuralVersion();
        renderScene(*device, *renderer, world, nullptr);
        std::vector<Vec2> motion = readMotion(*device, renderer->motionVectors());
        CHECK(world.structuralVersion() == structure);
        CHECK(glm::length(motion[32 * kSize + 32]) < 1e-4f);

        // Moving 0.2 units right at distance 5 with a 60 degree field of view shifts the image by
        // 0.2 / (5 tan 30) / 2 = 0.0346 in UV units.
        world.get<Transform>(entity).position.x = 0.2f;
        renderScene(*device, *renderer, world, nullptr);
        motion = readMotion(*device, renderer->motionVectors());
        CHECK(motion[32 * kSize + 32].x == doctest::Approx(0.0346).epsilon(0.02));
        CHECK(std::abs(motion[32 * kSize + 32].y) < 1e-3f);
        CHECK(glm::length(motion[1 * kSize + 1]) == 0.0f); // background

        // Standing still again: no motion.
        renderScene(*device, *renderer, world, nullptr);
        motion = readMotion(*device, renderer->motionVectors());
        CHECK(glm::length(motion[32 * kSize + 32]) < 1e-4f);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("materials that sway bend with the wind in every pass, the others stay rigid")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        for (const bool bindless : {true, false})
        {
            CAPTURE(bindless);
            SceneRendererDesc desc = unormTarget();
            desc.ambient = Vec3(1.0f);
            desc.post.bloom.enabled = false;
            desc.bindless = bindless;
            auto renderer = SceneRenderer::create(*device, resources, desc);
            REQUIRE(renderer != nullptr);

            // A 2 m square facing the camera with its origin at its centre, (0, -1, 0): the top half bends.
            World world;
            Transform plane;
            plane.position = Vec3(0.0f, -1.0f, 0.0f);
            plane.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
            MaterialDesc grass;
            grass.parameters.metallicFactor = 0.0f;
            grass.parameters.windSway = 0.1f;
            const MaterialHandle swaying = resources.createMaterial(grass);
            // Subdivided, so the lower half has vertices of its own that stay still.
            const Entity entity = world.create(
                plane, RenderMesh{resources.createMesh(makePlane(2.0f, 8), "Plane"), 0, swaying});
            world.create(Transform{},
                         DirectionalLight{Vec3(1.0f), 1.0f}); // casts shadows: the wind caster path

            // The right edge (x = 1) is at column 43; near the top (y = -0.1) it moves right in the wind.
            constexpr u32 kTopY = 33;
            constexpr u32 kBottomY = 52;
            const auto covered = [](const std::vector<u8>& pixels, u32 x, u32 y) {
                return pixelAt(pixels, x, y).r > 40;
            };
            std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
            CHECK(covered(pixels, 42, kTopY));
            CHECK_FALSE(covered(pixels, 45, kTopY)); // no Wind: calm air
            CHECK_FALSE(covered(pixels, 45, kBottomY));

            const Entity wind =
                world.create(Wind{.direction = Vec3(1.0f, 0.0f, 0.0f), .speed = 4.0f, .gustStrength = 0.0f});
            pixels = renderScene(*device, *renderer, world, nullptr);
            CHECK(covered(pixels, 45, kTopY));          // about 0.33 m (3.6 pixels) to the right at the top
            CHECK_FALSE(covered(pixels, 45, kBottomY)); // the lower half stays put
            CHECK(renderer->stats().shadowDrawCalls >= 1);

            // A steady wind leaves no motion; a moving one gives the top motion vectors.
            std::vector<Vec2> motion = readMotion(*device, renderer->motionVectors());
            CHECK(glm::length(motion[kTopY * kSize + 40]) < 1e-4f);
            advanceWind(world, 0.5f);
            renderScene(*device, *renderer, world, nullptr);
            motion = readMotion(*device, renderer->motionVectors());
            CHECK(glm::length(motion[kTopY * kSize + 40]) > 1e-3f);
            CHECK(glm::length(motion[kBottomY * kSize + 32]) < 1e-3f);

            // A rigid material ignores the wind.
            world.get<RenderMesh>(entity).material = MaterialHandle{};
            pixels = renderScene(*device, *renderer, world, nullptr);
            CHECK_FALSE(covered(pixels, 45, kTopY));
            world.destroy(wind);
            resources.releaseMaterial(swaying);
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("occlusion culling skips hidden objects without popping when they appear")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc = unormTarget();
        desc.post.bloom.enabled = false;
        desc.post.ambientOcclusion.enabled = false;
        desc.ambient = Vec3(1.0f);
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);

        World world;
        Transform wall; // facing the camera, covering the whole view
        wall.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        const Entity wallEntity = world.create(
            wall, RenderMesh{resources.createMesh(makePlane(20.0f, 1), "Wall"), 0, MaterialHandle{}});
        MaterialDesc red;
        red.parameters.baseColorFactor = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        const MaterialHandle redMaterial = resources.createMaterial(red);
        const MeshHandle cube = resources.createMesh(makeCube(0.2f), "Cube");
        for (int y = 0; y < 10; ++y)
        {
            for (int x = 0; x < 10; ++x)
            {
                Transform t; // behind the wall
                t.position =
                    Vec3(-1.8f + 0.4f * static_cast<f32>(x), -1.8f + 0.4f * static_cast<f32>(y), -3.0f);
                world.create(t, RenderMesh{cube, 0, redMaterial});
            }
        }

        // Without history everything is drawn; afterwards the wall hides the cubes.
        renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->stats().visible == 101); // all inside the frustum
        CHECK(renderer->readGpuDrawnInstances() == 101);
        std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->readGpuDrawnInstances() == 1);
        CHECK(pixelAt(pixels, 32, 32).g > 100); // the white wall

        // The wall moves away: the cubes appear in the same frame (phase 2 catches them).
        world.get<Transform>(wallEntity).position = Vec3(0.0f, 0.0f, 50.0f); // behind the camera
        pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->readGpuDrawnInstances() == 100);
        int redPixels = 0;
        for (u32 y = 0; y < kSize; ++y)
        {
            for (u32 x = 0; x < kSize; ++x)
            {
                const Rgb pixel = pixelAt(pixels, x, y);
                redPixels += pixel.r > 100 && pixel.g < 40 ? 1 : 0;
            }
        }
        CHECK(redPixels > 100); // a hundred small red cubes

        // Turned off, every candidate is drawn.
        world.get<Transform>(wallEntity).position = Vec3(0.0f);
        renderer->setOcclusionCulling(false);
        renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->readGpuDrawnInstances() == 101);
        renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->readGpuDrawnInstances() == 101);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("bindless multi-draw rendering matches per-material rendering")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        SceneRendererDesc desc = unormTarget();
        desc.post.bloom.enabled = false;
        desc.post.ambientOcclusion.enabled = false;
        desc.ambient = Vec3(0.5f);
        auto bindlessRenderer = SceneRenderer::create(*device, resources, desc);
        desc.bindless = false;
        auto classicRenderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(bindlessRenderer != nullptr);
        REQUIRE(classicRenderer != nullptr);

        // Several materials on two meshes: colours, a texture, alpha masking and double-sided faces.
        Image stripes = makeSolidImage(4, 4, Vec4(0.0f), ImageFormat::RGBA8Srgb);
        for (u32 i = 0; i < 16; ++i)
        {
            const bool on = (i % 4) % 2 == 0;
            std::memset(stripes.data.data() + i * 4, on ? 255 : 40, 3);
            stripes.data[i * 4 + 3] = std::byte{static_cast<unsigned char>(on ? 255 : 0)};
        }
        const TextureHandle texture = resources.createTexture(stripes, "Stripes");
        std::vector<MaterialHandle> materials;
        for (int m = 0; m < 6; ++m)
        {
            MaterialDesc material;
            material.parameters.baseColorFactor =
                Vec4(0.2f + 0.15f * static_cast<f32>(m), 0.8f - 0.1f * static_cast<f32>(m), 0.3f, 1.0f);
            material.parameters.metallicFactor = 0.0f;
            if (m == 3)
            {
                material.baseColor = texture;
            }
            if (m == 4)
            {
                material.baseColor = texture;
                material.parameters.alphaMode = AlphaMode::Mask;
            }
            material.parameters.doubleSided = m == 5;
            materials.push_back(resources.createMaterial(material));
        }
        const MeshHandle cube = resources.createMesh(makeCube(0.5f), "Cube");
        const MeshHandle sphere = resources.createMesh(makeSphere(0.3f, 24, 12), "Sphere");
        World world;
        world.create(Transform{}, DirectionalLight{});
        for (int i = 0; i < 24; ++i)
        {
            Transform t;
            t.position =
                Vec3(-2.0f + 0.8f * static_cast<f32>(i % 6), -1.2f + 0.8f * static_cast<f32>(i / 6), -1.0f);
            t.rotation = glm::angleAxis(0.3f * static_cast<f32>(i), glm::normalize(Vec3(1.0f, 1.0f, 0.0f)));
            world.create(t, RenderMesh{i % 2 == 0 ? cube : sphere, 0,
                                       materials[static_cast<usize>(i) % materials.size()]});
        }

        const std::vector<u8> bindless = renderScene(*device, *bindlessRenderer, world, nullptr);
        const u32 bindlessDraws = bindlessRenderer->stats().drawCalls;
        const std::vector<u8> classic = renderScene(*device, *classicRenderer, world, nullptr);
        const u32 classicDraws = classicRenderer->stats().drawCalls;
        CHECK_FALSE(classicRenderer->usesBindless());
        const GpuInfo& gpu = device->gpu();
        CHECK(bindlessRenderer->usesBindless() == (gpu.bindless && gpu.multiDrawIndirect));
        if (bindlessRenderer->usesBindless())
        {
            CHECK(bindlessDraws < classicDraws);
        }
        int maxDifference = 0;
        int covered = 0;
        for (usize i = 0; i < bindless.size(); ++i)
        {
            maxDifference = std::max(maxDifference, std::abs(int(bindless[i]) - int(classic[i])));
            covered += i % 4 == 0 && bindless[i] > 0 ? 1 : 0;
        }
        CHECK(maxDifference <= 2);
        CHECK(covered > 500); // the scene is actually drawn
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

namespace
{

// A renderer without bloom or temporal anti-aliasing, so a pixel shows exactly what was drawn there.
std::unique_ptr<SceneRenderer> plainRenderer(RenderDevice& device, RenderResources& resources)
{
    auto renderer = SceneRenderer::create(device, resources, unormTarget());
    REQUIRE(renderer != nullptr);
    PostProcessSettings settings = renderer->postProcessSettings();
    settings.bloom.enabled = false;
    settings.temporalAntiAliasing.enabled = false;
    settings.ambientOcclusion.enabled = false;
    renderer->setPostProcessSettings(settings);
    return renderer;
}

// @p count particles of @p color and width @p size at @p position, alive for a long time and not moving.
Entity particlesAt(World& world, ParticleWorld& particles, Vec3 position, Vec4 color, f32 size, u32 count,
                   ParticleBlend blend)
{
    ParticleEmitter emitter;
    emitter.rate = 0.0f;
    emitter.burst = count;
    emitter.speedMin = 0.0f;
    emitter.speedMax = 0.0f;
    emitter.lifetimeMin = 100.0f;
    emitter.lifetimeMax = 100.0f;
    emitter.sizeStart = size;
    emitter.sizeEnd = size;
    emitter.colorStart = color;
    emitter.colorEnd = color;
    emitter.blend = blend;
    Transform transform;
    transform.position = position;
    const Entity entity = world.create(transform, emitter);
    particles.update(1.0f / 60.0f);
    return entity;
}

} // namespace

TEST_CASE("particles are drawn as camera-facing quads with a soft round edge and counted")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = plainRenderer(*device, resources);
        World world;
        ParticleWorld particles(world);
        particlesAt(world, particles, Vec3(0.0f), Vec4(4.0f, 0.0f, 0.0f, 1.0f), 2.0f, 1,
                    ParticleBlend::Additive);
        const std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 32, 32).r > 150); // the middle of the dot
        CHECK(pixelAt(pixels, 32, 32).g == 0);
        CHECK(pixelAt(pixels, 2, 2).r == 0); // far outside it
        CHECK(renderer->stats().particles == 1);
        // The dot fades towards the edge of its quad.
        CHECK(pixelAt(pixels, 40, 32).r + 20 < pixelAt(pixels, 32, 32).r);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("particles behind a surface are hidden and alpha particles cover what is behind them")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = plainRenderer(*device, resources);
        MaterialDesc black;
        black.parameters.baseColorFactor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const MeshHandle cube = resources.createMesh(makeCube(), "Cube");
        World world;
        ParticleWorld particles(world);
        // A black wall between the camera and red particles.
        Transform wall;
        wall.position = Vec3(0.0f, 0.0f, 1.0f);
        wall.scale = Vec3(4.0f, 4.0f, 0.1f);
        const Entity blocker = world.create(wall, RenderMesh{cube, 0, resources.createMaterial(black)});
        particlesAt(world, particles, Vec3(0.0f), Vec4(8.0f, 0.0f, 0.0f, 1.0f), 4.0f, 1,
                    ParticleBlend::Additive);
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r == 0);

        // Without the wall, an opaque black alpha particle in front darkens the red ones behind.
        world.destroy(blocker);
        particlesAt(world, particles, Vec3(0.0f, 0.0f, 2.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.5f, 1,
                    ParticleBlend::Alpha);
        const std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 32, 32).r < 40);
        CHECK(pixelAt(pixels, 32, 24).r > 100); // beside the small black one, the red shows
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a sprite texture colours the particles that use it")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = plainRenderer(*device, resources);
        World world;
        ParticleWorld particles(world);
        const Entity entity = particlesAt(world, particles, Vec3(0.0f), Vec4(8.0f, 8.0f, 8.0f, 1.0f), 2.0f, 1,
                                          ParticleBlend::Additive);
        const TextureHandle blue = resources.createTexture(
            makeSolidImage(4, 4, Vec4(0.0f, 0.0f, 1.0f, 1.0f), ImageFormat::RGBA8Srgb));
        world.emplace<RenderParticles>(entity, RenderParticles{blue, 0});
        const std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 32, 32).b > 200);
        CHECK(pixelAt(pixels, 32, 32).r == 0);
        // The sprite covers the whole quad: no round falloff.
        CHECK(pixelAt(pixels, 40, 32).b > 200);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("GPU emitters simulate in compute shaders, draw indirectly and report their live count")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto renderer = plainRenderer(*device, resources);
        World world;
        ParticleWorld particles(world);
        ParticleEmitter emitter;
        emitter.simulation = ParticleSimulation::Gpu;
        emitter.rate = 0.0f;
        emitter.burst = 200;
        emitter.maxParticles = 50; // the burst does not fit
        emitter.speedMin = 0.0f;
        emitter.speedMax = 0.0f;
        emitter.lifetimeMin = 100.0f;
        emitter.lifetimeMax = 100.0f;
        emitter.sizeStart = 2.0f;
        emitter.sizeEnd = 2.0f;
        emitter.colorStart = Vec4(0.0f, 0.05f, 0.0f, 1.0f); // 50 additive layers add up
        emitter.colorEnd = emitter.colorStart;
        const Entity entity = world.create(Transform{}, emitter);
        particles.update(1.0f / 60.0f);
        std::vector<u8> pixels = renderScene(*device, *renderer, world, nullptr);
        CHECK(pixelAt(pixels, 32, 32).g > 150);
        CHECK(pixelAt(pixels, 32, 32).r == 0);
        CHECK(pixelAt(pixels, 2, 2).g == 0);

        // The live count comes back a few frames later; the particles stay where they are.
        for (int frame = 0; frame < 4; ++frame)
        {
            particles.update(1.0f / 60.0f);
            pixels = renderScene(*device, *renderer, world, nullptr);
        }
        CHECK(particles.aliveCount(entity) == 50);
        CHECK(pixelAt(pixels, 32, 32).g > 150);

        // clear() starts the GPU particles afresh.
        CHECK(particles.clear(entity));
        for (int frame = 0; frame < 4; ++frame)
        {
            particles.update(1.0f / 60.0f);
            pixels = renderScene(*device, *renderer, world, nullptr);
        }
        CHECK(pixelAt(pixels, 32, 32).g == 0);
        CHECK(particles.aliveCount(entity) == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
