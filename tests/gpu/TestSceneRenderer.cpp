#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
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
        auto renderer = SceneRenderer::create(*device, resources, desc);
        REQUIRE(renderer != nullptr);

        const MeshHandle sphere = resources.createMesh(makeSphere(1.0f, 32, 16), "Sphere");
        MaterialDesc red;
        red.parameters.baseColorFactor = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        red.parameters.metallicFactor = 0.0f;
        red.parameters.roughnessFactor = 0.6f;
        const MaterialHandle redMaterial = resources.createMaterial(red);

        World world;
        world.create(Transform{}, MeshRenderer{sphere, 0, redMaterial});
        Transform behind;
        behind.position = Vec3(10.0f, 0.0f, 20.0f); // behind the camera, off the light's path (no shadow)
        world.create(behind, MeshRenderer{sphere, 0, redMaterial});
        world.create(Transform{}, DirectionalLight{Vec3(1.0f), 3.0f}); // shines along -z, towards the sphere

        JobSystem jobs(JobSystemDesc{2, 256});
        const std::vector<u8> pixels = renderScene(*device, *renderer, world, &jobs);
        CHECK(renderer->stats().candidates == 2);
        CHECK(renderer->stats().visible == 1);
        CHECK(renderer->stats().drawCalls == 1);

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
            world.create(t, MeshRenderer{sphere, 0, redMaterial});
        }
        renderScene(*device, *renderer, world, nullptr);
        CHECK(renderer->stats().visible == 4);
        CHECK(renderer->stats().drawCalls == 1);
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
        const Entity entity = world.create(facing, MeshRenderer{plane, 0, MaterialHandle{}});
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r > 100);

        world.get<Transform>(entity).rotation =
            glm::angleAxis(-std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f)); // normal now faces away
        CHECK(pixelAt(renderScene(*device, *renderer, world, nullptr), 32, 32).r == 0);
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
        world.create(Transform{}, SkyLight{environment, 1.0f, true});
        const Entity entity = world.create(Transform{}, MeshRenderer{sphere, 0, MaterialHandle{}});

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
            world.get<MeshRenderer>(entity).material = resources.createMaterial(material);
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
        auto renderer = SceneRenderer::create(*device, resources, unormTarget());
        REQUIRE(renderer != nullptr);
        // Red towards -x (west), blue towards +x (east).
        const EnvironmentHandle environment = resources.createEnvironment(
            makeEnvironment(
                64, [](Vec3 d) { return d.x < 0.0f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f); }),
            smallEnvironment());
        World world;
        const Entity sky = world.create(Transform{}, SkyLight{environment, 1.0f, true});

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
        world.create(Transform{}, MeshRenderer{resources.createMesh(makeSphere(1.5f, 48, 24), "Sphere"), 0,
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
                     MeshRenderer{resources.createMesh(makePlane(40.0f, 1), "Ground"), 0, MaterialHandle{}});
        // A 2 m wide roof high above the view: invisible, but it shades the ground for x in [-1, 1].
        Transform roof;
        roof.position = Vec3(0.0f, 10.0f, 0.0f);
        world.create(roof,
                     MeshRenderer{resources.createMesh(makePlane(2.0f, 1), "Roof"), 0, MaterialHandle{}});
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
