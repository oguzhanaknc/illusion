// Frame-time benchmarks of the SceneRenderer on the local GPU (CPU recording plus GPU execution, with
// a wait for completion). Run in Release on real hardware; CI's software rasterizer does not run them.

#include "Budget.hpp"

#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <memory>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kWidth = 1280;
constexpr u32 kHeight = 720;

struct Scene
{
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<RenderResources> resources;
    std::unique_ptr<SceneRenderer> renderer;
    nvrhi::TextureHandle target;
    nvrhi::CommandListHandle commands;
    World world;
    JobSystem jobs;
    RenderView view;

    Scene()
    {
        RenderDeviceDesc desc;
        desc.headless = true;
        desc.validation = ValidationMode::Disabled;
        device = RenderDevice::create(desc);
        ILL_CHECK(device != nullptr, "the GPU benchmarks need a Vulkan 1.3 device");
        resources = std::make_unique<RenderResources>(*device);
        SceneRendererDesc rendererDesc;
        rendererDesc.colorFormat = nvrhi::Format::RGBA8_UNORM;
        renderer = SceneRenderer::create(*device, *resources, rendererDesc);
        nvrhi::TextureDesc targetDesc;
        targetDesc.width = kWidth;
        targetDesc.height = kHeight;
        targetDesc.format = nvrhi::Format::RGBA8_UNORM;
        targetDesc.isRenderTarget = true;
        targetDesc.debugName = "Benchmark target";
        targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        target = device->device()->createTexture(targetDesc);
        commands = device->device()->createCommandList();

        Transform camera;
        camera.position = Vec3(0.0f, 8.0f, 30.0f);
        camera.rotation = glm::quatLookAt(glm::normalize(Vec3(0.0f, -8.0f, -30.0f)), Vec3(0.0f, 1.0f, 0.0f));
        view = RenderView::fromCamera(camera, Camera{}, static_cast<f32>(kWidth) / static_cast<f32>(kHeight));
        Transform sun;
        sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.4f, -1.0f, -0.6f)), Vec3(0.0f, 1.0f, 0.0f));
        world.create(sun, DirectionalLight{});
    }

    // A grid of `count` cubes using `materials` different materials.
    void addCubes(u32 count, u32 materials)
    {
        const MeshHandle cube = resources->createMesh(makeCube(0.5f), "Cube");
        std::vector<MaterialHandle> handles;
        for (u32 m = 0; m < materials; ++m)
        {
            MaterialDesc material;
            material.parameters.baseColorFactor =
                Vec4(static_cast<f32>(m) / static_cast<f32>(materials), 0.5f, 0.5f, 1.0f);
            handles.push_back(resources->createMaterial(material));
        }
        for (u32 i = 0; i < count; ++i)
        {
            Transform t;
            t.position = Vec3(static_cast<f32>(i % 100) - 50.0f, 0.0f, -static_cast<f32>(i / 100));
            world.create(t, RenderMesh{cube, 0, handles[i % materials]});
        }
    }

    void frame()
    {
        commands->open();
        renderer->render(commands, target, world, view, &jobs);
        commands->close();
        device->device()->executeCommandList(commands);
        device->device()->waitForIdle();
        device->device()->runGarbageCollection();
    }
};

} // namespace

// Budgets are roughly 5-10x an RTX 4070 SUPER result (see Budget.hpp).

ILL_BENCHMARK("render/empty-frame", 3'000'000.0)
{
    Scene scene;
    bench.batch(1).unit("frame").run("shadows, culling, GTAO, bloom, tonemap at 1280x720, no meshes",
                                     [&] { scene.frame(); });
}

ILL_BENCHMARK("render/volumetric-fog", 3'000'000.0)
{
    Scene scene;
    scene.world.create(VolumetricFog{});
    for (u32 i = 0; i < SceneRenderer::kMaxPointLights; ++i)
    {
        Transform at;
        at.position = Vec3(static_cast<f32>(i % 4) * 6.0f - 9.0f, 1.0f, -static_cast<f32>(i / 4) * 6.0f);
        scene.world.create(at, PointLight{});
    }
    bench.batch(1).unit("frame").run("empty frame plus shadowed fog with 16 point lights at 1280x720",
                                     [&] { scene.frame(); });
}

ILL_BENCHMARK("render/point-light-shadows", 6'000'000.0)
{
    Scene scene;
    scene.addCubes(2'000, 10);
    for (u32 i = 0; i < kMaxShadowedPointLights; ++i)
    {
        Transform at;
        at.position = Vec3(static_cast<f32>(i) * 12.0f - 18.0f, 2.0f, -5.0f);
        PointLight light{Vec3(1.0f), 40.0f, 12.0f};
        light.castShadows = true;
        scene.world.create(at, light);
    }
    bench.batch(1).unit("frame").run("2k cubes lit by 4 point lights with 512x512 cube shadow maps, 1280x720",
                                     [&] { scene.frame(); });
}

ILL_BENCHMARK("render/10k-instances", 2'000.0)
{
    Scene scene;
    scene.addCubes(10'000, 50);
    bench.batch(10'000).unit("instance").run("full frame, 10k cubes, 50 materials, 1280x720", [&] {
        scene.frame();
    });
}

ILL_BENCHMARK("render/100k-particles", 200.0)
{
    Scene scene;
    ParticleWorld particles(scene.world);
    for (u32 i = 0; i < 50; ++i)
    {
        ParticleEmitter emitter;
        emitter.maxParticles = 2000;
        emitter.rate = 0.0f;
        emitter.burst = 2000;
        emitter.speedMin = 0.5f;
        emitter.speedMax = 2.0f;
        emitter.lifetimeMin = 1000.0f;
        emitter.lifetimeMax = 1000.0f;
        emitter.sizeStart = 0.2f;
        emitter.sizeEnd = 0.2f;
        emitter.colorStart = Vec4(2.0f, 1.0f, 0.5f, 0.5f);
        emitter.colorEnd = emitter.colorStart;
        emitter.blend = i % 2 == 0 ? ParticleBlend::Additive : ParticleBlend::Alpha; // half are sorted
        emitter.seed = i + 1;
        Transform at;
        at.position = Vec3(static_cast<f32>(i % 10) * 4.0f - 20.0f, 2.0f, -static_cast<f32>(i / 10) * 4.0f);
        scene.world.create(at, emitter);
    }
    particles.update(0.5f, &scene.jobs);
    bench.batch(100'000).unit("particle").run("full frame, 100k particles in 50 emitters, 1280x720", [&] {
        scene.frame();
    });
}

ILL_BENCHMARK("render/1m-gpu-particles", 10.0)
{
    Scene scene;
    ParticleWorld particles(scene.world);
    for (u32 i = 0; i < 16; ++i)
    {
        ParticleEmitter emitter;
        emitter.simulation = ParticleSimulation::Gpu;
        emitter.maxParticles = 65536;
        emitter.rate = 65536.0f; // a full emitter after the first second, refilled as particles die
        emitter.lifetimeMin = 2.0f;
        emitter.lifetimeMax = 4.0f;
        emitter.speedMin = 0.5f;
        emitter.speedMax = 2.0f;
        emitter.gravityFactor = 0.2f;
        emitter.sizeStart = 0.05f;
        emitter.sizeEnd = 0.02f;
        emitter.colorStart = Vec4(2.0f, 1.0f, 0.5f, 1.0f);
        emitter.colorEnd = Vec4(0.5f, 0.2f, 0.1f, 0.0f);
        emitter.seed = i + 1;
        Transform at;
        at.position = Vec3(static_cast<f32>(i % 4) * 8.0f - 12.0f, 2.0f, -static_cast<f32>(i / 4) * 8.0f);
        scene.world.create(at, emitter);
    }
    for (int i = 0; i < 90; ++i)
    {
        particles.update(1.0f / 60.0f);
        scene.frame();
    }
    bench.batch(1'048'576)
        .unit("particle")
        .run("full frame, 16 GPU emitters of 65536 particles, 1280x720", [&] {
            particles.update(1.0f / 60.0f);
            scene.frame();
        });
}
