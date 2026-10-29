#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace illusion;

namespace
{
constexpr u32 kSize = 64;

SceneRendererDesc description(bool bindless = false)
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.entityIds = true;
    desc.bindless = bindless;
    desc.gpuTimers = true;
    desc.clearColor = Vec4(0, 0, 0, 1);
    desc.shadows.resolution = 128;
    desc.post.temporalAntiAliasing.enabled = false;
    desc.post.bloom.enabled = false;
    desc.post.ambientOcclusion.enabled = false;
    return desc;
}

nvrhi::TextureHandle targetFor(RenderDevice& device, u32 size = kSize)
{
    nvrhi::TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.isRenderTarget = true;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
    return device.device()->createTexture(desc);
}

RenderView cameraView(bool orthographic = false)
{
    Camera camera;
    camera.projection = orthographic ? CameraProjection::Orthographic : CameraProjection::Perspective;
    camera.orthoHeight = 4;
    camera.farPlane = 30;
    Transform transform;
    transform.position = Vec3(0, 0, 5);
    return RenderView::fromCamera(transform, camera, 1);
}

void render(RenderDevice& device, SceneRenderer& renderer, nvrhi::ITexture* target, World& world,
            const RenderView& view)
{
    auto commands = device.device()->createCommandList();
    commands->open();
    renderer.render(commands, target, world, view);
    commands->close();
    device.device()->executeCommandList(commands);
}

template <class T>
T readPixel(ReadbackRing& ring, nvrhi::ITexture* texture, u32 x = 32, u32 y = 32)
{
    T value{};
    const auto ticket = ring.request(texture, UVec2(x, y), UVec2(1));
    REQUIRE(ticket.has_value());
    REQUIRE(ring.read(*ticket, std::as_writable_bytes(std::span(&value, 1)), true) == ReadbackStatus::Ready);
    return value;
}

std::vector<std::byte> pixels(ReadbackRing& ring, nvrhi::ITexture* texture)
{
    std::vector<std::byte> result(static_cast<usize>(texture->getDesc().width) * texture->getDesc().height *
                                  4);
    const auto ticket = ring.request(texture);
    REQUIRE(ticket.has_value());
    REQUIRE(ring.read(*ticket, result, true) == ReadbackStatus::Ready);
    return result;
}

// Small images allow a bounded number of edge pixels to differ across Vulkan drivers. An RGB channel
// delta above 12 is a differing pixel; at most 1% of the image may differ. Captures are always retained.
void golden(std::string_view name, const std::vector<std::byte>& data)
{
    Image image;
    image.mips.push_back({kSize, kSize, 0, data.size()});
    image.data = data;
    const auto encoded = encodePng(image);
    const auto write = [&](const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        REQUIRE(stream.good());
    };
    const auto filename = std::string(name) + ".png";
    write(std::filesystem::path("render-captures") / filename);
    const auto reference = utf8Path(ILLUSION_TEST_DATA_DIR) / "render" / filename;
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    if (update && std::string_view(update) == "1")
    {
        write(reference);
    }
    std::ifstream stream(reference, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), reference.string());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto expected = decodeImage(std::as_bytes(std::span(bytes)), false);
    REQUIRE(expected.has_value());
    REQUIRE(expected->data.size() == data.size());
    usize different = 0;
    for (usize i = 0; i < data.size(); i += 4)
    {
        bool differs = false;
        for (usize channel = 0; channel < 3; ++channel)
        {
            differs |= std::abs(std::to_integer<int>(data[i + channel]) -
                                std::to_integer<int>(expected->data[i + channel])) > 12;
        }
        different += differs ? 1u : 0u;
    }
    CHECK(different <= kSize * kSize / 100);
}
} // namespace

TEST_CASE("entity IDs cover instancing masks and transparency in both material paths")
{
    auto device = RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    REQUIRE(device);
    {
        RenderResources resources(*device);
        auto ring = ReadbackRing::create(*device);
        REQUIRE(ring);
        auto target = targetFor(*device);
        const auto mesh = resources.createMesh(makeCube(), "ID cube");
        for (bool bindless : {false, true})
        {
            CAPTURE(bindless);
            auto renderer = SceneRenderer::create(*device, resources, description(bindless));
            REQUIRE(renderer);
            World world;
            const Entity back = world.create(Transform{}, RenderMesh{mesh});
            Transform offset;
            offset.position.x = 1.5f;
            const Entity instance = world.create(offset, RenderMesh{mesh});
            const RenderView view = cameraView(true);
            render(*device, *renderer, target, world, view);
            auto ids = renderer->viewTargets().ids;
            CHECK(readPixel<UVec2>(*ring, ids) == UVec2(back.index, back.generation));
            CHECK(readPixel<UVec2>(*ring, ids, 56, 32) == UVec2(instance.index, instance.generation));
            CHECK(readPixel<UVec2>(*ring, ids, 0, 0) == UVec2(~0u));
            const f32 opaqueDepth = readPixel<f32>(*ring, renderer->viewTargets().depth);

            MaterialDesc material;
            material.parameters.alphaMode = AlphaMode::Mask;
            material.parameters.baseColorFactor.a = 0;
            auto masked = resources.createMaterial(material);
            offset.position = Vec3(0, 0, 1);
            const Entity front = world.create(offset, RenderMesh{mesh, 0, masked});
            render(*device, *renderer, target, world, view);
            CHECK(readPixel<UVec2>(*ring, ids) == UVec2(back.index, back.generation));
            material.parameters.baseColorFactor.a = 1;
            world.get<RenderMesh>(front).material = resources.createMaterial(material);
            render(*device, *renderer, target, world, view);
            CHECK(readPixel<UVec2>(*ring, ids) == UVec2(front.index, front.generation));

            material.parameters.alphaMode = AlphaMode::Blend;
            material.parameters.baseColorFactor.a = 0.5f;
            world.get<RenderMesh>(front).material = resources.createMaterial(material);
            render(*device, *renderer, target, world, view);
            CHECK(readPixel<UVec2>(*ring, ids) == UVec2(front.index, front.generation));
            CHECK(readPixel<f32>(*ring, renderer->viewTargets().depth) == opaqueDepth);
            material.parameters.baseColorFactor.a = 0;
            world.get<RenderMesh>(front).material = resources.createMaterial(material);
            render(*device, *renderer, target, world, view);
            CHECK(readPixel<UVec2>(*ring, ids) == UVec2(back.index, back.generation));
            CHECK(renderer->usesBindless() ==
                  (bindless && device->gpu().bindless && device->gpu().multiDrawIndirect));
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("separate views keep independent object motion without changing World structure")
{
    auto device = RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    REQUIRE(device);
    {
        RenderResources resources(*device);
        auto ring = ReadbackRing::create(*device);
        auto target = targetFor(*device);
        auto a = SceneRenderer::create(*device, resources, description());
        auto b = SceneRenderer::create(*device, resources, description());
        REQUIRE(ring);
        REQUIRE(a);
        REQUIRE(b);
        World world;
        const auto mesh = resources.createMesh(makeCube(), "Motion cube");
        Entity entity = world.create(Transform{}, RenderMesh{mesh});
        const auto version = world.structuralVersion();
        const RenderView view = cameraView(true);
        render(*device, *a, target, world, view);
        render(*device, *b, target, world, view);
        world.get<Transform>(entity).position.x = 0.1f;
        render(*device, *a, target, world, view);
        const Vec2 motionA = glm::unpackHalf2x16(readPixel<u32>(*ring, a->viewTargets().motion));
        render(*device, *b, target, world, view);
        const Vec2 motionB = glm::unpackHalf2x16(readPixel<u32>(*ring, b->viewTargets().motion));
        CHECK(motionA.x == doctest::Approx(0.025).epsilon(0.01));
        CHECK(motionB == motionA);
        CHECK(world.structuralVersion() == version);
        world.destroy(entity);
        entity = world.create(Transform{}, RenderMesh{mesh});
        render(*device, *a, target, world, view);
        CHECK(glm::unpackHalf2x16(readPixel<u32>(*ring, a->viewTargets().motion)) == Vec2(0));
        world.get<Transform>(entity).position.x = -0.1f;
        a->resetTemporalHistory();
        render(*device, *a, target, world, view);
        CHECK(glm::unpackHalf2x16(readPixel<u32>(*ring, a->viewTargets().motion)) == Vec2(0));
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("readback tickets bound outstanding copies and validate regions without consuming valid tickets")
{
    auto device = RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    REQUIRE(device);
    {
        CHECK_FALSE(ReadbackRing::create(*device, 0));
        auto ring = ReadbackRing::create(*device, 1);
        REQUIRE(ring);
        auto target = targetFor(*device);
        auto commands = device->device()->createCommandList();
        commands->open();
        commands->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(1, 0, 0, 1));
        commands->close();
        device->device()->executeCommandList(commands);
        CHECK_FALSE(ring->request(nullptr));
        CHECK_FALSE(ring->request(target, UVec2(kSize)));
        CHECK_FALSE(ring->request(target, UVec2(0), UVec2(1, 0)));
        const auto ticket = ring->request(target, UVec2(3, 4), UVec2(3, 2));
        REQUIRE(ticket);
        CHECK_FALSE(ring->request(target));
        std::array<std::byte, 24> data{};
        CHECK(ring->read(*ticket, std::span(data).first(1)) == ReadbackStatus::Invalid);
        const auto polled = ring->read(*ticket, data);
        CHECK((polled == ReadbackStatus::Pending || polled == ReadbackStatus::Ready));
        if (polled == ReadbackStatus::Pending)
        {
            CHECK(ring->read(*ticket, data, true) == ReadbackStatus::Ready);
        }
        for (usize i = 0; i < data.size(); i += 4)
        {
            CHECK(data[i] == std::byte{255});
            CHECK(data[i + 1] == std::byte{0});
        }
        CHECK(ring->read(*ticket, data) == ReadbackStatus::Invalid);
        const auto reused = ring->request(target, UVec2(0), UVec2(1));
        REQUIRE(reused);
        CHECK(*reused != *ticket);
        CHECK(ring->read(*reused, data, true) == ReadbackStatus::Ready);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("view filters shadows diagnostics and GPU timings share the renderer extension targets")
{
    auto device = RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    REQUIRE(device);
    {
        RenderResources resources(*device);
        auto ring = ReadbackRing::create(*device);
        auto target = targetFor(*device);
        auto renderer = SceneRenderer::create(*device, resources, description());
        REQUIRE(ring);
        REQUIRE(renderer);
        World world;
        const auto mesh = resources.createMesh(makeCube(), "Filter cube");
        const auto entity = world.create(Transform{}, RenderMesh{mesh});
        Transform sunlight;
        sunlight.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.4f, -1, -0.6f)), Vec3(0, 1, 0));
        world.create(sunlight, DirectionalLight{});
        RenderView view = cameraView(true);
        render(*device, *renderer, target, world, view);
        CHECK(renderer->stats().visible == 1);
        CHECK(renderer->stats().shadowCasters > 0);
        Transform floorTransform;
        floorTransform.position.y = -0.5f;
        const Entity floor =
            world.create(floorTransform, RenderMesh{resources.createMesh(makePlane(6), "Shadow receiver")});
        RenderView shadowView = view;
        shadowView.position = Vec3(4, 3, 6);
        shadowView.view = glm::lookAt(shadowView.position, Vec3(0), Vec3(0, 1, 0));
        render(*device, *renderer, target, world, shadowView);
        golden("orthographic-shadows", pixels(*ring, target));
        world.destroy(floor);
        const auto& timings = renderer->gpuTimings();
        CHECK(timings.available == device->gpu().timestamps);
        if (timings.available)
        {
            CHECK(timings.valid);
            CHECK(timings.depth > 0);
            CHECK(timings.shading > 0);
            CHECK(timings.post > 0);
        }
        world.get<RenderMesh>(entity).flags = RenderMesh::kVisible;
        render(*device, *renderer, target, world, view);
        CHECK(renderer->stats().visible == 1);
        CHECK(renderer->stats().shadowCasters == 0);
        world.get<RenderMesh>(entity).layers = 2;
        view.layerMask = 1;
        render(*device, *renderer, target, world, view);
        CHECK(renderer->stats().visible == 0);
        view.layerMask = 2;
        std::array<u64, 1> hidden{u64{1} << entity.index};
        view.hiddenEntities = hidden;
        render(*device, *renderer, target, world, view);
        CHECK(renderer->stats().visible == 0);
        view.hiddenEntities = {};
        world.get<RenderMesh>(entity).flags = 0;
        render(*device, *renderer, target, world, view);
        CHECK(renderer->stats().visible == 0);
        world.get<RenderMesh>(entity).flags = RenderMesh::kVisible;
        renderer->setDebugView(RenderDebugView::Normals);
        render(*device, *renderer, target, world, view);
        golden("normals", pixels(*ring, target));
        CHECK(renderer->viewTargets().renderSize == UVec2(kSize));
        CHECK(renderer->viewTargets().outputSize == UVec2(kSize));
        CHECK(renderer->viewTargets().unjitteredViewProjection == view.projection * view.view);
        for (u32 i = 0; i < SceneRenderer::kMaxPointLights + 2; ++i)
        {
            world.create(Transform{}, PointLight{});
        }
        for (u32 i = 0; i < SceneRenderer::kMaxDirectionalLights; ++i)
        {
            world.create(Transform{}, DirectionalLight{});
        }
        for (auto mode : {RenderDebugView::Unlit, RenderDebugView::Albedo, RenderDebugView::Roughness,
                          RenderDebugView::Metallic, RenderDebugView::AmbientOcclusion,
                          RenderDebugView::ShadowCascades, RenderDebugView::MotionVectors})
        {
            renderer->setDebugView(mode);
            render(*device, *renderer, target, world, view);
            CHECK(renderer->debugView() == mode);
        }
        CHECK(renderer->stats().lightsDropped == 3);
        renderer->setClearColor(Vec4(1, 0, 0, 1));
        render(*device, *renderer, target, world, view);
        const auto background = readPixel<std::array<u8, 4>>(*ring, target, 0, 0);
        CHECK(background[0] == 255);
        CHECK(background[1] == 0);
        CHECK(renderer->setWireframeOverlay(true) == device->gpu().wireframe);
        render(*device, *renderer, target, world, view);
        (void)pixels(*ring, target);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("debug line quads sample scene depth at independent output resolution")
{
    auto device = RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    REQUIRE(device);
    {
        RenderResources resources(*device);
        auto ring = ReadbackRing::create(*device);
        auto target = targetFor(*device);
        auto small = targetFor(*device, 32);
        auto renderer = SceneRenderer::create(*device, resources, description());
        auto overlay = DebugDrawRenderer::create(*device, nvrhi::Format::RGBA8_UNORM);
        REQUIRE(ring);
        REQUIRE(renderer);
        REQUIRE(overlay);
        World world;
        world.create(Transform{}, RenderMesh{resources.createMesh(makeCube(2), "Occluder")});
        renderer->setDebugView(RenderDebugView::Albedo);
        render(*device, *renderer, small, world, cameraView(true));
        auto view = renderer->viewTargets();
        view.outputSize = UVec2(kSize);
        DebugDraw draw;
        REQUIRE(draw.line(Vec3(-1.8f, 0, -1), Vec3(1.8f, 0, -1), Vec4(1, 0, 0, 1), 4));
        REQUIRE(
            draw.line(Vec3(-1.8f, 0.5f, -1), Vec3(1.8f, 0.5f, -1), Vec4(0, 1, 0, 1), 4, DebugDepth::OnTop));
        REQUIRE(draw.line(Vec3(-1.8f, -0.5f, 2), Vec3(1.8f, -0.5f, 2), Vec4(0, 0, 1, 1), 4));
        auto commands = device->device()->createCommandList();
        commands->open();
        commands->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 1));
        REQUIRE(overlay->render(commands, target, view, draw));
        commands->close();
        device->device()->executeCommandList(commands);
        golden("debug-draw", pixels(*ring, target));
        const auto centre = readPixel<std::array<u8, 4>>(*ring, target);
        CHECK(centre[0] == 0); // hidden red line
        const auto outside = readPixel<std::array<u8, 4>>(*ring, target, 6, 32);
        CHECK(outside[0] == 255);
        const auto green = readPixel<std::array<u8, 4>>(*ring, target, 32, 24);
        CHECK(green[1] == 255);
        const auto blue = readPixel<std::array<u8, 4>>(*ring, target, 32, 40);
        CHECK(blue[2] == 255);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("resizing between frames keeps the replaced targets alive until the GPU has finished with them")
{
    // Regression: a resize released the Entity ID and motion targets while the previous frame still ran on
    // the GPU. Without geometry only clears use them, and NVRHI's clears do not keep textures alive (the
    // validation layer reported vkDestroyImage on an image in use). Correct code passes whatever the timing.
    auto device = RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    REQUIRE(device);
    {
        RenderResources resources(*device);
        auto renderer = SceneRenderer::create(*device, resources, description());
        REQUIRE(renderer);
        World world;
        // No waits between the frames: each one replaces the previous one's targets.
        for (const u32 size : {1024u, 1536u, 1024u, 2048u, 1280u, 1024u})
        {
            auto target = targetFor(*device, size);
            render(*device, *renderer, target, world, cameraView());
            CHECK(renderer->viewTargets().renderSize == UVec2(size));
        }
        // The last frame's targets belong to the renderer: let it finish before the renderer goes.
        device->waitIdle();
    }
    CHECK(device->validationErrorCount() == 0);
}
