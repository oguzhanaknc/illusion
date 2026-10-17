#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>

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
        behind.position = Vec3(0.0f, 0.0f, 20.0f); // behind the camera
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
