#include "PlayerSetup.hpp"
#include "PresentPass.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/platform/Dialog.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/player/Player.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>
#include <illusion/render/Swapchain.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
// shellapi.h needs windows.h first.
#include <shellapi.h>
#endif

namespace illusion
{
namespace
{

using detail::PlayerFailure;
using detail::PlayerOptions;
using detail::PlayerProblem;

// The 3D view is rendered into this format, as the editor's views are, then presented (PresentPass).
constexpr nvrhi::Format kViewFormat = nvrhi::Format::RGBA8_UNORM;

std::FILE* openForWriting(const std::filesystem::path& path)
{
#if defined(ILL_PLATFORM_WINDOWS)
    return _wfopen(path.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

// The arguments after the program name as UTF-8. Windows passes argv in the ANSI code page, which is not
// UTF-8 and cannot hold every path, so the UTF-16 command line is read instead.
std::vector<std::string> commandLineArguments(int argc, char** argv)
{
    std::vector<std::string> arguments;
#if defined(ILL_PLATFORM_WINDOWS)
    static_cast<void>(argc);
    static_cast<void>(argv);
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide != nullptr && i < count; ++i)
    {
        arguments.push_back(detail::pathText(std::filesystem::path(wide[i])));
    }
    LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
#endif
    return arguments;
}

// Copies every message to `user:/player.log` while it lives.
class LogFile : NonCopyable
{
public:
    explicit LogFile(const std::filesystem::path& path)
        : m_file(openForWriting(path))
    {
        if (m_file != nullptr)
        {
            m_sink = std::make_shared<ConsoleSink>(ConsoleStreams::StderrOnly, nullptr, m_file);
            Logger::instance().addSink(m_sink);
        }
    }

    ~LogFile()
    {
        if (m_file != nullptr)
        {
            Logger::instance().removeSink(m_sink.get());
            m_sink.reset();
            std::fclose(m_file);
        }
    }

    [[nodiscard]] bool isOpen() const noexcept { return m_file != nullptr; }

private:
    std::FILE* m_file = nullptr;
    std::shared_ptr<ConsoleSink> m_sink;
};

// Logs @p failure and shows it in a message box (unless --no-dialogs); returns its exit code.
int report(const PlayerFailure& failure, std::string_view name, bool dialogs)
{
    ILL_LOG_ERROR("Player", "{}", failure.message);
    Logger::instance().flush();
    if (dialogs)
    {
        (void)showErrorDialog(name, detail::failureText(failure));
    }
    return static_cast<int>(failure.exit);
}

void reflectEngineTypes()
{
    reflectCoreComponents();
    reflectAssetStructs();
    reflectPhysicsComponents();
    reflectAudioComponents();
    reflectParticleComponents();
    reflectRenderComponents();
    reflectRuntimeStructs();
}

// Loads the startup scene into @p world, with its EntityGuids, as Play mode copies the edit World.
Expected<void, std::string> loadStartupScene(World& world, const VirtualFileSystem& files,
                                             std::string_view path)
{
    std::string text;
    if (const FileError error = files.readText(path, text); error != FileError::None)
    {
        return unexpected(std::format("cannot read the startup scene {} ({})", path, fileErrorName(error)));
    }
    SceneLoadOptions options;
    options.prefabs = [&files](std::string_view prefab) -> std::optional<std::string> {
        std::string json;
        return files.readText(prefab, json) == FileError::None ? std::optional<std::string>(std::move(json))
                                                               : std::nullopt;
    };
    const SceneLoadResult loaded = loadScene(world, text, options);
    if (!loaded.ok())
    {
        return unexpected(std::format("cannot load the startup scene {}: {}", path, loaded.error));
    }
    ILL_LOG_INFO("Player", "loaded {} ({} entities)", path, loaded.entities.size());
    return {};
}

// The 3D view at the render scale of the back buffer's size, recreated when that size changes.
struct ViewTarget
{
    nvrhi::TextureHandle texture;
    UVec2 size{0};

    bool ensure(nvrhi::IDevice* device, UVec2 wanted)
    {
        if (texture && size == wanted)
        {
            return true;
        }
        nvrhi::TextureDesc desc;
        desc.width = wanted.x;
        desc.height = wanted.y;
        desc.format = kViewFormat;
        desc.isRenderTarget = true;
        desc.debugName = "Player view";
        desc.setClearValue(nvrhi::Color(0.0f));
        desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
        texture = device->createTexture(desc);
        size = texture ? wanted : UVec2(0);
        return texture != nullptr;
    }
};

UVec2 scaledSize(UVec2 size, f32 renderScale) noexcept
{
    const f32 scale = std::clamp(std::isfinite(renderScale) ? renderScale : 1.0f, 0.25f, 2.0f);
    return glm::max(UVec2(Vec2(size) * scale + 0.5f), UVec2(1));
}

// Reads @p texture back and writes it to @p path as PNG; fails saying why.
Expected<void, std::string> writeScreenshot(RenderDevice& device, nvrhi::ITexture* texture, UVec2 size,
                                            const std::filesystem::path& path)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::TextureDesc readbackDesc = texture->getDesc();
    readbackDesc.isRenderTarget = false;
    readbackDesc.debugName = "Player screenshot";
    readbackDesc.initialState = nvrhi::ResourceStates::CopyDest;
    nvrhi::StagingTextureHandle readback =
        gpu->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);
    if (!readback)
    {
        return unexpected(std::string("cannot create the readback texture"));
    }
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    commands->copyTexture(readback, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
    commands->close();
    gpu->executeCommandList(commands);
    gpu->waitForIdle();

    Image image = makeSolidImage(size.x, size.y, Vec4(0.0f), ImageFormat::RGBA8Srgb);
    usize rowPitch = 0;
    const auto* bytes = static_cast<const std::byte*>(
        gpu->mapStagingTexture(readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
    if (bytes == nullptr)
    {
        return unexpected(std::string("cannot map the readback texture"));
    }
    const usize row = static_cast<usize>(size.x) * 4;
    for (u32 y = 0; y < size.y; ++y)
    {
        std::memcpy(image.data.data() + y * row, bytes + y * rowPitch, row);
    }
    gpu->unmapStagingTexture(readback);

    const std::vector<std::byte> png = encodePng(image);
    if (png.empty())
    {
        return unexpected(std::string("cannot encode the PNG"));
    }
    std::error_code code;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), code);
        if (code)
        {
            return unexpected(std::format("cannot create its directory ({})", code.message()));
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return unexpected(std::string("cannot open the file for writing"));
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file.good())
    {
        return unexpected(std::string("cannot write the file"));
    }
    return {};
}

// Everything runPlayer() owns after the command line is read, in construction order.
int runGame(const PlayerDesc& desc, const PlayerOptions& options, VirtualFileSystem& files)
{
    const bool dialogs = !options.noDialogs;
    const std::filesystem::path dataDirectory =
        detail::playerDataDirectory(desc, options, executableDirectory());
    if (Expected<void, PlayerFailure> mounted = detail::mountPlayerData(files, dataDirectory); !mounted)
    {
        return report(mounted.error(), desc.name, dialogs);
    }

    reflectEngineTypes();
    ProjectSettings settings;
    if (Expected<void, std::string> loaded = loadProjectSettings(files, settings); !loaded)
    {
        return report(
            PlayerFailure{PlayerExit::StartFailed, loaded.error(), PlayerProblem::GameFailed, loaded.error()},
            desc.name, dialogs);
    }

    EventBus events;
    std::unique_ptr<Platform> platform = Platform::create();
    if (!platform)
    {
        return report(
            PlayerFailure{
                PlayerExit::StartFailed, "no window system is available", PlayerProblem::NoWindowSystem, {}},
            desc.name, dialogs);
    }
    std::unique_ptr<Window> window =
        Window::create(*platform, detail::playerWindowDesc(desc, settings.window, options), &events);
    if (!window)
    {
        return report(
            PlayerFailure{
                PlayerExit::StartFailed, "cannot open the game's window", PlayerProblem::WindowFailed, {}},
            desc.name, dialogs);
    }
    RenderDeviceDesc deviceDesc;
    deviceDesc.applicationName = std::string(desc.name);
    std::unique_ptr<RenderDevice> device = RenderDevice::create(deviceDesc);
    if (!device)
    {
        return report(PlayerFailure{PlayerExit::StartFailed,
                                    "no Vulkan 1.3 device was found: update the graphics driver",
                                    PlayerProblem::NoGraphics,
                                    {}},
                      desc.name, dialogs);
    }
    SwapchainDesc swapchainDesc;
    swapchainDesc.vsync = settings.render.vsync;
    std::unique_ptr<Swapchain> swapchain = Swapchain::create(*device, *window, swapchainDesc);
    if (!swapchain)
    {
        return report(PlayerFailure{PlayerExit::StartFailed,
                                    "cannot present to the game's window",
                                    PlayerProblem::GraphicsFailed,
                                    {}},
                      desc.name, dialogs);
    }

    JobSystem jobs;
    // The cache archive holds the cooked assets; their sources are not shipped, so they are not verified.
    AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = "cache", .verifySources = false});
    RenderResources resources(*device);
    RenderAssetCache renderAssets(resources, streamer);
    const std::unique_ptr<GamepadSystem> gamepads = GamepadSystem::create(&events);
    SceneRendererDesc rendererDesc;
    rendererDesc.colorFormat = kViewFormat;
    std::unique_ptr<SceneRenderer> renderer = SceneRenderer::create(*device, resources, rendererDesc);
    if (!renderer)
    {
        return report(PlayerFailure{PlayerExit::StartFailed,
                                    "cannot create the scene renderer",
                                    PlayerProblem::GraphicsFailed,
                                    {}},
                      desc.name, dialogs);
    }

    ModuleHost modules(jobs);
    for (const PlayerModuleEntry entry : desc.modules)
    {
        ILL_ASSERT(entry != nullptr, "PlayerDesc::modules holds a null entry");
        if (std::optional<std::string> error = modules.add(*entry()))
        {
            return report(PlayerFailure{PlayerExit::StartFailed,
                                        std::format("a module failed to load: {}", *error),
                                        PlayerProblem::ModuleFailed, *error},
                          desc.name, dialogs);
        }
    }

    GameSessionDesc sessionDesc;
    sessionDesc.jobs = &jobs;
    sessionDesc.files = &files;
    sessionDesc.settings = settings;
    sessionDesc.modules = &modules;
    sessionDesc.streamer = &streamer;
    sessionDesc.renderAssets = &renderAssets;
    sessionDesc.gamepads = gamepads.get();
    sessionDesc.fixedStep = options.fixedStep;
    Expected<std::unique_ptr<GameSession>, std::string> created = GameSession::create(sessionDesc);
    if (!created)
    {
        return report(PlayerFailure{PlayerExit::StartFailed, created.error(), PlayerProblem::GameFailed,
                                    created.error()},
                      desc.name, dialogs);
    }
    GameSession& session = **created;
    if (Expected<void, std::string> loaded = loadStartupScene(session.world(), files, desc.startupScene);
        !loaded)
    {
        return report(
            PlayerFailure{PlayerExit::StartFailed, loaded.error(), PlayerProblem::GameFailed, loaded.error()},
            desc.name, dialogs);
    }
    session.start();

    nvrhi::IDevice* gpu = device->device();
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    std::unique_ptr<detail::PresentPass> present;
    ViewTarget view;
    bool rendered = false;
    FrameClock clock;
    f64 nextFrame = monotonicSeconds();
    const f64 frameSeconds = settings.render.maxFramesPerSecond > 0
                                 ? 1.0 / static_cast<f64>(settings.render.maxFramesPerSecond)
                                 : 0.0;
    u64 frames = 0;
    while (!window->shouldClose() && (options.frames == 0 || frames < options.frames))
    {
        platform->pollEvents();
        events.dispatchQueued();
        const bool focused = window->isFocused();
        if (gamepads)
        {
            gamepads->update(focused);
        }
        const f64 delta = clock.tick(monotonicSeconds()).deltaSeconds;

        // As Play mode does: background loads are published before the game advances, and fixed steps wait
        // for them so that every run is the same.
        if (options.fixedStep)
        {
            streamer.waitIdle();
        }
        else
        {
            (void)streamer.update();
        }
        session.setInputFocus(focused);
        session.keyboardMouse() = window->input();
        session.update(delta);
        if (options.fixedStep)
        {
            renderAssets.waitIdle();
        }
        else
        {
            renderAssets.update();
        }

        if (swapchain->beginFrame())
        {
            if (!present)
            {
                present = detail::PresentPass::create(*device, swapchain->currentFramebuffer());
                if (!present)
                {
                    return report(PlayerFailure{PlayerExit::StartFailed,
                                                "cannot present to the game's window",
                                                PlayerProblem::GraphicsFailed,
                                                {}},
                                  desc.name, dialogs);
                }
            }
            commands->open();
            World& world = session.world();
            const Entity camera = findMainCamera(world);
            if (!camera.isNull() &&
                view.ensure(gpu, scaledSize(swapchain->size(), settings.render.renderScale)))
            {
                RenderView renderView =
                    RenderView::fromCamera(world.get<Transform>(camera), world.get<Camera>(camera),
                                           static_cast<f32>(view.size.x) / static_cast<f32>(view.size.y));
                renderView.deltaSeconds =
                    static_cast<f32>(options.fixedStep ? settings.physics.stepSeconds : delta);
                renderer->render(commands, view.texture, world, renderView, &jobs);
                present->draw(commands, swapchain->currentFramebuffer(), view.texture);
                rendered = true;
            }
            else
            {
                commands->clearTextureFloat(swapchain->currentBackBuffer(), nvrhi::AllSubresources,
                                            nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
                rendered = false;
            }
            commands->close();
            gpu->executeCommandList(commands);
            swapchain->present();
        }
        ++frames;
        ILL_PROFILE_FRAME();

        if (frameSeconds > 0.0)
        {
            nextFrame = std::max(nextFrame + frameSeconds, monotonicSeconds() - frameSeconds);
            const f64 wait = nextFrame - monotonicSeconds();
            if (wait > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<f64>(wait));
            }
        }
    }
    session.stop();
    device->waitIdle();

    int exit = static_cast<int>(PlayerExit::Ok);
    if (!options.screenshot.empty())
    {
        const Expected<void, std::string> written =
            rendered
                ? writeScreenshot(*device, view.texture, view.size, options.screenshot)
                : Expected<void, std::string>(unexpected(std::string("no frame was rendered (no Camera)")));
        if (written)
        {
            ILL_LOG_INFO("Player", "saved the screenshot {}", detail::pathText(options.screenshot));
        }
        else
        {
            ILL_LOG_ERROR("Player", "cannot save the screenshot {}: {}", detail::pathText(options.screenshot),
                          written.error());
            exit = static_cast<int>(PlayerExit::StartFailed);
        }
    }
    if (const u32 errors = device->validationErrorCount(); errors != 0)
    {
        ILL_LOG_WARN("Player", "{} Vulkan validation error(s)", errors);
    }
    ILL_LOG_INFO("Player", "ended after {} frames", frames);
    return exit;
}

} // namespace

int runPlayer(const PlayerDesc& desc, int argc, char** argv)
{
    ILL_ASSERT(!desc.name.empty() && !desc.organization.empty() && !desc.startupScene.empty(),
               "PlayerDesc needs a name, an organization and a startup scene");
    const std::vector<std::string> utf8Arguments = commandLineArguments(argc, argv);
    const std::vector<std::string_view> arguments(utf8Arguments.begin(), utf8Arguments.end());
    Expected<PlayerOptions, PlayerFailure> options = detail::parsePlayerArguments(arguments);
    if (!options)
    {
        return report(options.error(), desc.name, !detail::wantsNoDialogs(arguments));
    }
    const bool dialogs = !options->noDialogs;

    VirtualFileSystem files;
    if (Expected<void, PlayerFailure> user = detail::mountUserData(
            files, options->userDirectory ? *options->userDirectory
                                          : userDataDirectory(desc.organization, desc.name));
        !user)
    {
        return report(user.error(), desc.name, dialogs);
    }
    std::filesystem::path logPath;
    (void)files.resolve("user:/player.log", logPath);
    const LogFile log(logPath);
    if (!log.isOpen())
    {
        ILL_LOG_WARN("Player", "cannot write the log file {}", detail::pathText(logPath));
    }
    ILL_LOG_INFO("Player", "{} {} (illusion player)", desc.name, desc.version);
    return runGame(desc, *options, files);
}

} // namespace illusion
