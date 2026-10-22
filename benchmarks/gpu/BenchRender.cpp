// Frame-time benchmarks of the SceneRenderer on the local GPU (CPU recording plus GPU execution, with
// a wait for completion). Run in Release on real hardware; CI's software rasterizer does not run them.

#include "Budget.hpp"

#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/World.hpp>
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

ILL_BENCHMARK("render/10k-instances", 2'000.0)
{
    Scene scene;
    scene.addCubes(10'000, 50);
    bench.batch(10'000).unit("instance").run("full frame, 10k cubes, 50 materials, 1280x720", [&] {
        scene.frame();
    });
}
