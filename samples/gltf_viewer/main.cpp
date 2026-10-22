// gltf_viewer: renders a glTF model (or a PBR test scene) with an orbit camera.
//
// Usage: illusion_gltf_viewer [path/to/model.gltf|.glb] [--environment sky.hdr] [--taa] [--frames N]
//                             [--screenshot out.png]
//   Without a model, a grid of spheres with varying metallic (x) and roughness (y) is shown.
//   Orbit: left mouse drag or right stick. Zoom: mouse wheel or triggers. Escape quits.
//   --environment lights the scene with an equirectangular HDR image (for example from polyhaven.com);
//   without it, the built-in procedural sky (builtin:/sky/procedural) is used. The model and the image are
//   loaded through asset references (MeshRenderer, SkyLight) by a RenderAssetResolver. --taa turns on
//   temporal anti-aliasing.
//   --screenshot renders the final view offscreen at 1280x720 and saves it as PNG.

#include "common/Screenshot.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
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
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>
#include <illusion/render/Swapchain.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

// Mounts the directory of the file @p path as @p mount and returns the file's virtual path.
std::optional<std::string> mountFile(VirtualFileSystem& files, std::string_view mount, std::string_view path)
{
    const std::filesystem::path file = utf8Path(path);
    std::error_code code;
    if (files.mount(mount, std::filesystem::absolute(file, code).parent_path()) != FileError::None)
    {
        ILL_LOG_ERROR("Sample", "cannot open the directory of {}", path);
        return std::nullopt;
    }
    const std::u8string name = file.filename().u8string();
    return std::format("{}:/{}", mount,
                       std::string_view(reinterpret_cast<const char*>(name.data()), name.size()));
}

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
            world.create(transform, RenderMesh{sphere, 0, resources.createMaterial(material)});
        }
    }
    MaterialDesc ground;
    ground.parameters.baseColorFactor = Vec4(0.5f, 0.5f, 0.5f, 1.0f);
    ground.parameters.metallicFactor = 0.0f;
    ground.parameters.roughnessFactor = 0.9f;
    world.create(Transform{}, RenderMesh{plane, 0, resources.createMaterial(ground)});

    Transform blueLight;
    blueLight.position = Vec3(-3.0f, 2.0f, 2.0f);
    world.create(blueLight, PointLight{Vec3(0.3f, 0.5f, 1.0f), 20.0f, 8.0f});
}

} // namespace

int main(int argc, char** argv)
{
    std::string modelPath;
    std::string screenshotPath;
    std::string environmentPath;
    bool temporalAntiAliasing = false;
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
        else if (arg == "--taa")
        {
            temporalAntiAliasing = true;
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
    rendererDesc.post.temporalAntiAliasing.enabled = temporalAntiAliasing;
    auto renderer = SceneRenderer::create(*device, resources, rendererDesc);
    if (!renderer)
    {
        return 1;
    }

    // The model and the environment are referenced by virtual path; the resolver loads them (ADR 0035).
    VirtualFileSystem files;
    AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""});
    RenderAssetCache assetCache(resources, streamer);
    RenderAssetResolver assets(assetCache);

    World world;
    Vec3 target(0.0f, 2.0f, 0.0f);
    f32 distance = 7.0f;
    if (modelPath.empty())
    {
        buildTestScene(world, resources);
    }
    else
    {
        const std::optional<std::string> virtualPath = mountFile(files, "model", modelPath);
        if (!virtualPath)
        {
            return 1;
        }
        // The model is read once here for its bounds; the resolver loads it again through the streamer.
        std::string error;
        const auto model = loadGltf(files, *virtualPath, &error);
        if (!model)
        {
            ILL_LOG_ERROR("Sample", "{}", error);
            return 1;
        }
        const Aabb bounds = model->bounds();
        // Scale the model to about 4 units and stand it on the ground.
        const f32 size = std::max(glm::length(bounds.max - bounds.min), 1e-3f);
        const f32 scale = 4.0f / size;
        const Mat4 root =
            glm::scale(Mat4(1.0f), Vec3(scale)) *
            glm::translate(Mat4(1.0f), -Vec3(bounds.center().x, bounds.min.y, bounds.center().z));
        const auto entities = spawnModel(world, *model, *virtualPath, root);
        target = Vec3(0.0f, (bounds.max.y - bounds.min.y) * scale * 0.5f, 0.0f);
        ILL_LOG_INFO("Sample", "loaded {} ({} entities)", modelPath, entities.size());
    }
    Transform sun;
    sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.4f, -1.0f, -0.6f)), Vec3(0.0f, 1.0f, 0.0f));
    world.create(sun, DirectionalLight{Vec3(1.0f, 0.95f, 0.9f), 3.0f});

    if (environmentPath.empty())
    {
        world.create(Transform{}, SkyLight{.environment = AssetRef("builtin:/sky/procedural")},
                     ProceduralSky{.resolution = 2048});
    }
    else
    {
        const std::optional<std::string> virtualPath = mountFile(files, "environment", environmentPath);
        if (!virtualPath)
        {
            return 1;
        }
        world.create(Transform{}, SkyLight{.environment = AssetRef(*virtualPath)});
    }
    Stopwatch loadTimer;
    assets.waitIdle(world, &jobs);
    if (assets.statistics().failures > 0)
    {
        return 1; // the cache logged why
    }
    device->waitIdle();
    ILL_LOG_INFO("Sample", "assets loaded and the environment baked in {:.1f} ms",
                 loadTimer.elapsedSeconds() * 1000.0);

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
            streamer.update();
            assetCache.update();
            assets.update(world, &jobs);
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
    if (!screenshotPath.empty() &&
        !sample::saveScreenshot(*device, resources, world, camera, jobs, screenshotPath))
    {
        ILL_LOG_ERROR("Sample", "cannot save {}", screenshotPath);
        return 1;
    }
    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    ILL_LOG_INFO("Sample", "rendered {} frames, {} validation error(s)", frames, errors);
    return errors == 0 ? 0 : 2;
}
