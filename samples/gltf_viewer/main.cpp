// gltf_viewer: renders a glTF model (or a PBR test scene) with an orbit camera.
//
// Usage: illusion_gltf_viewer [path/to/model.gltf|.glb] [--environment sky.hdr] [--frames N]
//                             [--screenshot out.png]
//   Without a model, a grid of spheres with varying metallic (x) and roughness (y) is shown.
//   Orbit: left mouse drag or right stick. Zoom: mouse wheel or triggers. Escape quits.
//   --environment lights the scene with an equirectangular HDR image (for example from polyhaven.com);
//   without it, a procedural sky is used.
//   --screenshot renders the final view offscreen at 1280x720 and saves it as PNG.

#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>
#include <illusion/render/Swapchain.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

void buildTestScene(World& world, RenderResources& resources)
{
    const MeshHandle sphere = resources.createMesh(makeSphere(0.4f, 48, 24), "Sphere");
    const MeshHandle plane = resources.createMesh(makePlane(12.0f), "Ground");
    constexpr int kGrid = 5;
    for (int y = 0; y < kGrid; ++y)
    {
        for (int x = 0; x < kGrid; ++x)
        {
            MaterialDesc material;
            material.parameters.baseColorFactor = Vec4(0.9f, 0.15f, 0.1f, 1.0f);
            material.parameters.metallicFactor = static_cast<f32>(x) / (kGrid - 1);
            material.parameters.roughnessFactor = std::max(static_cast<f32>(y) / (kGrid - 1), 0.05f);
            Transform transform;
            transform.position =
                Vec3((static_cast<f32>(x) - 2.0f) * 1.0f, static_cast<f32>(y) * 1.0f + 0.5f, 0.0f);
            world.create(transform, MeshRenderer{sphere, 0, resources.createMaterial(material)});
        }
    }
    MaterialDesc ground;
    ground.parameters.baseColorFactor = Vec4(0.5f, 0.5f, 0.5f, 1.0f);
    ground.parameters.metallicFactor = 0.0f;
    ground.parameters.roughnessFactor = 0.9f;
    world.create(Transform{}, MeshRenderer{plane, 0, resources.createMaterial(ground)});

    Transform blueLight;
    blueLight.position = Vec3(-3.0f, 2.0f, 2.0f);
    world.create(blueLight, PointLight{Vec3(0.3f, 0.5f, 1.0f), 20.0f, 8.0f});
}

bool saveScreenshot(RenderDevice& device, RenderResources& resources, World& world, const Transform& camera,
                    JobSystem& jobs, const std::string& path)
{
    constexpr u32 kWidth = 1280;
    constexpr u32 kHeight = 720;
    nvrhi::IDevice* gpu = device.device();
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM; // the shader encodes sRGB for UNORM targets
    auto renderer = SceneRenderer::create(device, resources, desc);
    if (!renderer)
    {
        return false;
    }
    nvrhi::TextureDesc targetDesc;
    targetDesc.width = kWidth;
    targetDesc.height = kHeight;
    targetDesc.format = desc.colorFormat;
    targetDesc.isRenderTarget = true;
    targetDesc.debugName = "Screenshot";
    targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
    nvrhi::TextureHandle target = gpu->createTexture(targetDesc);
    nvrhi::TextureDesc readbackDesc = targetDesc;
    readbackDesc.isRenderTarget = false;
    nvrhi::StagingTextureHandle readback =
        gpu->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    renderer->render(commands, target, world, RenderView::fromCamera(camera, Camera{}, 1280.0f / 720.0f),
                     &jobs);
    commands->copyTexture(readback, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
    commands->close();
    gpu->executeCommandList(commands);
    gpu->waitForIdle();

    Image image = makeSolidImage(kWidth, kHeight, Vec4(0.0f), ImageFormat::RGBA8Srgb);
    size_t rowPitch = 0;
    const auto* bytes = static_cast<const std::byte*>(
        gpu->mapStagingTexture(readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
    for (u32 y = 0; y < kHeight; ++y)
    {
        std::memcpy(image.data.data() + static_cast<usize>(y) * kWidth * 4, bytes + y * rowPitch, kWidth * 4);
    }
    gpu->unmapStagingTexture(readback);

    const std::vector<std::byte> png = encodePng(image);
    std::ofstream file(utf8Path(path), std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    ILL_LOG_INFO("Sample", "saved {}", path);
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char** argv)
{
    std::string modelPath;
    std::string screenshotPath;
    std::string environmentPath;
    u64 frameLimit = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc)
        {
            const std::string_view value = argv[++i];
            std::from_chars(value.data(), value.data() + value.size(), frameLimit);
        }
        else if (arg == "--screenshot" && i + 1 < argc)
        {
            screenshotPath = argv[++i];
        }
        else if (arg == "--environment" && i + 1 < argc)
        {
            environmentPath = argv[++i];
        }
        else
        {
            modelPath = arg;
        }
    }

    EventBus events;
    auto platform = Platform::create();
    if (!platform)
    {
        return 1;
    }
    WindowDesc windowDesc;
    windowDesc.title = "illusion - glTF viewer";
    auto window = Window::create(*platform, windowDesc, &events);
    auto gamepads = GamepadSystem::create(&events);
    auto device = RenderDevice::create();
    if (!window || !device)
    {
        return 1;
    }
    auto swapchain = Swapchain::create(*device, *window);
    if (!swapchain)
    {
        return 1;
    }
    JobSystem jobs;
    RenderResources resources(*device);
    SceneRendererDesc rendererDesc;
    rendererDesc.colorFormat = swapchain->format();
    auto renderer = SceneRenderer::create(*device, resources, rendererDesc);
    if (!renderer)
    {
        return 1;
    }

    World world;
    Vec3 target(0.0f, 2.0f, 0.0f);
    f32 distance = 7.0f;
    if (modelPath.empty())
    {
        buildTestScene(world, resources);
    }
    else
    {
        const std::filesystem::path path = utf8Path(modelPath);
        VirtualFileSystem files;
        std::error_code code;
        const std::filesystem::path directory = std::filesystem::absolute(path, code).parent_path();
        if (files.mount("model", directory) != FileError::None)
        {
            ILL_LOG_ERROR("Sample", "cannot open directory of {}", modelPath);
            return 1;
        }
        const std::u8string fileName = path.filename().u8string();
        std::string error;
        const auto model = loadGltf(
            files, "model:/" + std::string(reinterpret_cast<const char*>(fileName.data()), fileName.size()),
            &error);
        if (!model)
        {
            ILL_LOG_ERROR("Sample", "{}", error);
            return 1;
        }
        const ModelHandles handles = resources.uploadModel(*model);
        const Aabb bounds = model->bounds();
        // Scale the model to about 4 units and stand it on the ground.
        const f32 size = std::max(glm::length(bounds.max - bounds.min), 1e-3f);
        const f32 scale = 4.0f / size;
        const Mat4 root =
            glm::scale(Mat4(1.0f), Vec3(scale)) *
            glm::translate(Mat4(1.0f), -Vec3(bounds.center().x, bounds.min.y, bounds.center().z));
        const auto entities = spawnModel(world, *model, handles, root);
        target = Vec3(0.0f, (bounds.max.y - bounds.min.y) * scale * 0.5f, 0.0f);
        ILL_LOG_INFO("Sample", "loaded {} ({} entities)", modelPath, entities.size());
    }
    Transform sun;
    sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.4f, -1.0f, -0.6f)), Vec3(0.0f, 1.0f, 0.0f));
    world.create(sun, DirectionalLight{Vec3(1.0f, 0.95f, 0.9f), 3.0f});

    Image environmentImage = makeSkyImage(2048);
    if (!environmentPath.empty())
    {
        std::ifstream file(utf8Path(environmentPath), std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        std::string error;
        auto decoded = decodeImage(std::as_bytes(std::span(bytes)), false, &error);
        if (!decoded)
        {
            ILL_LOG_ERROR("Sample", "cannot load {}: {}", environmentPath, error);
            return 1;
        }
        environmentImage = std::move(*decoded);
    }
    Stopwatch bakeTimer;
    const EnvironmentHandle environment = resources.createEnvironment(environmentImage);
    device->waitIdle();
    ILL_LOG_INFO("Sample", "environment {}x{} baked in {:.1f} ms", environmentImage.width(),
                 environmentImage.height(), bakeTimer.elapsedSeconds() * 1000.0);
    world.create(Transform{}, SkyLight{environment, 1.0f, true});

    InputActions actions;
    const ActionId orbit = actions.addAction("Orbit", ActionType::Vector2);
    const ActionId zoom = actions.addAction("Zoom", ActionType::Axis);
    actions.addBinding(orbit, InputBinding::gamepadStick(GamepadStick::Right));
    actions.addBinding(zoom, InputBinding::gamepadAxis(GamepadAxis::RightTrigger, -1.0f));
    actions.addBinding(zoom, InputBinding::gamepadAxis(GamepadAxis::LeftTrigger, 1.0f));

    nvrhi::CommandListHandle commands = device->device()->createCommandList();
    FrameClock clock;
    Stopwatch titleTimer;
    f32 yaw = 0.0f;
    f32 pitch = -0.25f;
    Transform camera;
    u64 frames = 0;
    u64 framesSinceTitle = 0;
    std::vector<const GamepadState*> pads;
    while (!window->shouldClose() && (frameLimit == 0 || frames < frameLimit))
    {
        platform->pollEvents();
        if (gamepads)
        {
            gamepads->update(window->isFocused());
        }
        events.dispatchQueued();
        const FrameTime& frame = clock.tick(monotonicSeconds());
        const auto dt = static_cast<f32>(frame.deltaSeconds);

        pads.clear();
        if (gamepads)
        {
            for (Gamepad* pad : gamepads->gamepads())
            {
                pads.push_back(&pad->state());
            }
        }
        const InputState& input = window->input();
        actions.update(&input, pads);
        if (input.wasPressed(Key::Escape))
        {
            window->setShouldClose(true);
        }
        if (input.isDown(MouseButton::Left))
        {
            yaw -= input.mouseDelta().x * 0.005f;
            pitch -= input.mouseDelta().y * 0.005f;
        }
        const Vec2 stick = actions.vector(orbit);
        yaw -= stick.x * 2.0f * dt;
        pitch += stick.y * 2.0f * dt;
        pitch = std::clamp(pitch, -1.5f, 1.5f);
        distance *= std::exp(-input.scrollDelta().y * 0.1f + actions.value(zoom) * dt);
        distance = std::clamp(distance, 0.5f, 100.0f);

        camera.position = target + Vec3(std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                                        std::cos(yaw) * std::cos(pitch)) *
                                       distance;
        camera.rotation = glm::quatLookAt(glm::normalize(target - camera.position), Vec3(0.0f, 1.0f, 0.0f));

        if (swapchain->beginFrame())
        {
            const UVec2 size = swapchain->size();
            const RenderView view =
                RenderView::fromCamera(camera, Camera{}, static_cast<f32>(size.x) / static_cast<f32>(size.y));
            commands->open();
            renderer->render(commands, swapchain->currentBackBuffer(), world, view, &jobs);
            commands->close();
            device->device()->executeCommandList(commands);
            swapchain->present();
        }
        ++frames;
        ++framesSinceTitle;
        ILL_PROFILE_FRAME();

        if (titleTimer.elapsedSeconds() >= 0.5)
        {
            const f64 fps = static_cast<f64>(framesSinceTitle) / titleTimer.restart();
            framesSinceTitle = 0;
            const RenderStats& stats = renderer->stats();
            window->setTitle(
                std::format("illusion - glTF viewer - {:.0f} FPS - {}/{} visible, {} draws, {} tris", fps,
                            stats.visible, stats.candidates, stats.drawCalls, stats.triangles));
        }
    }
    device->waitIdle();
    if (!screenshotPath.empty() && !saveScreenshot(*device, resources, world, camera, jobs, screenshotPath))
    {
        ILL_LOG_ERROR("Sample", "cannot save {}", screenshotPath);
        return 1;
    }
    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    ILL_LOG_INFO("Sample", "rendered {} frames, {} validation error(s)", frames, errors);
    return errors == 0 ? 0 : 2;
}
