// illusion_editor: the editor process. It opens the editor window and serves the editor protocol (ADR 0037);
// with --headless it runs without a window, rendering the UI offscreen for editor.screenshot when a GPU is
// available:
//
//     illusion_editor [--headless] [--project <dir>] [--workspace <dir>] [--rpc-stdio] [--rpc-listen
//     [<socket>]]
//                     [--no-gpu] [--deterministic] [--seed <n>] [--permissions read-only|ask|auto]
//                     [--frames <n>] [--ui-size <w>x<h>] [--user-dir <dir>]

#include <illusion/assets/Image.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/core/Version.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>
#include <illusion/editor/shell/EditorShell.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Process.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/Swapchain.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#endif

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr std::string_view kUsage =
    "usage: illusion_editor [--headless] [--project <dir>] [--workspace <dir>] [--rpc-stdio]\n"
    "                       [--rpc-listen [<socket>]] [--no-gpu] [--deterministic] [--seed <n>]\n"
    "                       [--permissions read-only|ask|auto] [--frames <n>] [--ui-size <w>x<h>]\n"
    "                       [--user-dir <dir>]\n";

constexpr f64 kFixedStep = 1.0 / 60.0;

struct Arguments
{
    bool headless = false;
    bool noGpu = false;
    bool rpcStdio = false;
    bool rpcListen = false;
    bool deterministic = false;
    std::filesystem::path project;
    std::filesystem::path workspace;
    std::filesystem::path socket;
    std::filesystem::path userDirectory;
    u64 seed = 0;
    u64 frames = 0;
    UVec2 uiSize{1920, 1080};
    std::optional<rpc::PermissionMode> permissions;
};

int fail(std::string_view message)
{
    std::fprintf(stderr, "illusion_editor: %.*s\n%.*s", static_cast<int>(message.size()), message.data(),
                 static_cast<int>(kUsage.size()), kUsage.data());
    return 2;
}

std::optional<u64> parseNumber(std::string_view text)
{
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<Arguments> parse(const std::vector<std::string>& args, std::string& error)
{
    Arguments out;
    for (usize i = 0; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= args.size())
            {
                error = std::string(arg) + " needs a value";
                return std::nullopt;
            }
            return args[++i];
        };
        if (arg == "--headless")
        {
            out.headless = true;
        }
        else if (arg == "--no-gpu")
        {
            out.noGpu = true;
        }
        else if (arg == "--rpc-stdio")
        {
            out.rpcStdio = true;
        }
        else if (arg == "--rpc-listen")
        {
            out.rpcListen = true;
            if (i + 1 < args.size() && !args[i + 1].starts_with("--"))
            {
                out.socket = utf8Path(args[++i]);
            }
        }
        else if (arg == "--deterministic")
        {
            out.deterministic = true;
        }
        else if (arg == "--project" || arg == "--workspace" || arg == "--user-dir")
        {
            const std::optional<std::string> path = value();
            if (!path)
            {
                return std::nullopt;
            }
            (arg == "--project"     ? out.project
             : arg == "--workspace" ? out.workspace
                                    : out.userDirectory) = utf8Path(*path);
        }
        else if (arg == "--seed" || arg == "--frames")
        {
            const std::optional<std::string> text = value();
            const std::optional<u64> number = text ? parseNumber(*text) : std::nullopt;
            if (!number)
            {
                error = error.empty() ? std::string(arg) + " needs a whole number" : error;
                return std::nullopt;
            }
            (arg == "--seed" ? out.seed : out.frames) = *number;
        }
        else if (arg == "--ui-size")
        {
            const std::optional<std::string> text = value();
            const usize x = text ? text->find('x') : std::string::npos;
            const std::optional<u64> width =
                x != std::string::npos ? parseNumber(text->substr(0, x)) : std::nullopt;
            const std::optional<u64> height =
                x != std::string::npos ? parseNumber(text->substr(x + 1)) : std::nullopt;
            if (!width || !height || *width < 64 || *height < 64 || *width > 7680 || *height > 4320)
            {
                error = error.empty() ? "--ui-size takes WIDTHxHEIGHT from 64x64 to 7680x4320" : error;
                return std::nullopt;
            }
            out.uiSize = UVec2(static_cast<u32>(*width), static_cast<u32>(*height));
        }
        else if (arg == "--permissions")
        {
            const std::optional<std::string> text = value();
            const std::optional<rpc::PermissionMode> mode =
                text ? rpc::parsePermissionMode(*text) : std::nullopt;
            if (!mode)
            {
                error = error.empty() ? "--permissions takes read-only, ask or auto" : error;
                return std::nullopt;
            }
            out.permissions = *mode;
        }
        else
        {
            error = "unknown argument '" + std::string(arg) + "'";
            return std::nullopt;
        }
    }
    return out;
}

std::filesystem::path homeDirectory()
{
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    std::error_code code;
    return home != nullptr && home[0] != '\0' ? utf8Path(home) : std::filesystem::current_path(code);
}

// The UI of the editor: fonts, theme and the shell, in a UiContext that is current on this thread.
struct EditorUi
{
    VirtualFileSystem data;
    ui::UiContext context;
    std::unique_ptr<ui::FontManager> fonts;
    std::unique_ptr<shell::EditorShell> shell;

    bool create(EditorContext& editor, const std::filesystem::path& userDirectory, bool windowed)
    {
        if (!ui::mountEditorData(data))
        {
            return false;
        }
        auto loaded = ui::FontManager::load(context, data, "editor:/fonts");
        if (!loaded)
        {
            ILL_LOG_ERROR("editor", "cannot load the editor fonts: {}", loaded.error());
            return false;
        }
        fonts = std::move(loaded).value();
        shell::ShellOptions options;
        options.editorData = &data;
        options.fonts = fonts.get();
        options.userDirectory = userDirectory;
        options.canCapture = true;
        options.agentPermissions = windowed;
        options.version = engineVersion().toString();
        shell = std::make_unique<shell::EditorShell>(editor, context, std::move(options));
        return true;
    }

    ~EditorUi()
    {
        // The shell first (it saves the arrangement), the fonts before the context.
        shell.reset();
        fonts.reset();
    }
};

UiScreenshot screenshotOf(const Image& image)
{
    const std::vector<std::byte> png = encodePng(image);
    return UiScreenshot{UVec2(image.mips.front().width, image.mips.front().height),
                        std::string(reinterpret_cast<const char*>(png.data()), png.size())};
}

// The frame loop without a window: run queued requests, draw the UI offscreen when there is one (rendering
// it only for screenshots), then end the frame (events, journal, autosave).
int runHeadless(const Arguments& arguments, EditorContext& editor, PlaySession& play, rpc::RpcServer& server,
                u32 stdio)
{
    std::unique_ptr<RenderDevice> device;
    std::optional<EditorUi> ui;
    std::unique_ptr<ui::OffscreenUi> offscreen;
    std::unique_ptr<viewport::ViewportService> viewports;
    if (!arguments.noGpu)
    {
        RenderDeviceDesc desc;
        desc.headless = true;
        device = RenderDevice::create(desc);
        if (device)
        {
            ui.emplace();
            offscreen = ui->create(editor, arguments.userDirectory, false)
                            ? ui::OffscreenUi::create(*device, ui->context, arguments.uiSize)
                            : nullptr;
            if (!offscreen)
            {
                ui.reset();
            }
            else
            {
                ui->shell->setAgents(&server);
                viewports = viewport::ViewportService::create(*device, editor, offscreen->renderer());
            }
        }
        if (!ui)
        {
            ILL_LOG_INFO("editor", "running without a GPU: the UI and editor.screenshot are unavailable");
        }
    }
    else
    {
        ILL_LOG_INFO("editor", "running without a GPU");
    }
    if (arguments.permissions)
    {
        server.setPermissions(*arguments.permissions);
    }

    auto last = std::chrono::steady_clock::now();
    f64 time = 0.0;
    for (u64 frame = 0; !server.quitRequested() && !(ui && ui->shell->quitRequested()); ++frame)
    {
        if (arguments.frames != 0 && frame >= arguments.frames)
        {
            break;
        }
        if (stdio != 0 && !server.isOpen(stdio))
        {
            ILL_LOG_INFO("editor", "the stdio client closed the connection");
            break;
        }
        server.waitForWork(std::chrono::milliseconds(16));
        server.pump();
        const auto now = std::chrono::steady_clock::now();
        const f64 elapsed =
            arguments.deterministic ? kFixedStep : std::chrono::duration<f64>(now - last).count();
        last = now;
        time += elapsed;
        if (ui)
        {
            const std::optional<shell::ScreenshotRequest> request = ui->shell->pendingScreenshot();
            const UVec2 size = request && request->size.x != 0 ? request->size : arguments.uiSize;
            ui->context.makeCurrent();
            ui->shell->beginFrame(1.0f);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<f32>(size.x), static_cast<f32>(size.y));
            io.DeltaTime = static_cast<f32>(elapsed > 0.0 ? elapsed : kFixedStep);
            ImGui::NewFrame();
            ui->shell->draw(time, elapsed);
            ImGui::Render();
            // The viewports render for screenshots and for picks; the UI is not drawn otherwise.
            if (viewports)
            {
                viewports->render(request.has_value());
            }
            if (request)
            {
                std::optional<Image> image;
                if (offscreen->resize(size))
                {
                    image = offscreen->render(ImGui::GetDrawData(), ui->shell->clearColor());
                }
                ui->shell->completeScreenshot(
                    request->ticket, image
                                         ? Expected<UiScreenshot, CommandError>(screenshotOf(*image))
                                         : Expected<UiScreenshot, CommandError>(unexpected(CommandError::make(
                                               ErrorKind::Internal, "the frame could not be read back"))));
            }
        }
        play.update(elapsed);
        editor.endFrame(elapsed);
    }
    if (device)
    {
        device->waitIdle();
    }
    return 0;
}

// The windowed editor: events, requests (8 ms per frame), the UI, then present.
int runWindowed(const Arguments& arguments, EditorContext& editor, PlaySession& play, rpc::RpcServer& server,
                Platform& platform, EventBus& events, Window& window)
{
    auto device = RenderDevice::create();
    auto swapchain = device ? Swapchain::create(*device, window) : nullptr;
    if (!swapchain)
    {
        return 1;
    }
    EditorUi ui;
    if (!ui.create(editor, arguments.userDirectory, true))
    {
        return 1;
    }
    ui::UiPlatformBackend backend(ui.context, window, events);
    auto offscreen = ui::OffscreenUi::create(*device, ui.context, swapchain->size());
    auto viewports =
        offscreen ? viewport::ViewportService::create(*device, editor, offscreen->renderer()) : nullptr;
    if (!viewports)
    {
        return 1;
    }
    ui.shell->setAgents(&server);
    if (arguments.permissions)
    {
        server.setPermissions(*arguments.permissions);
    }
    FrameClock clock;
    u64 frames = 0;
    while (!window.shouldClose() && !ui.shell->quitRequested() && !server.quitRequested())
    {
        if (arguments.frames != 0 && frames >= arguments.frames)
        {
            break;
        }
        // An unfocused editor sleeps until input or a request arrives (the server wakes it).
        if (window.isFocused())
        {
            platform.pollEvents();
        }
        else
        {
            platform.waitEvents(0.25);
        }
        events.dispatchQueued();
        server.pump(std::chrono::milliseconds(8));
        const FrameTime& time = clock.tick(monotonicSeconds());
        const f64 delta = arguments.deterministic ? kFixedStep : time.deltaSeconds;
        ui.context.makeCurrent();
        ui.shell->beginFrame(backend.contentScale().x);
        (void)backend.consumeContentScaleChange();
        backend.newFrame(delta);
        ImGui::NewFrame();
        ui.shell->draw(time.totalSeconds, delta);
        ImGui::Render();
        const bool presenting = swapchain->beginFrame();
        viewports->render(presenting);
        if (presenting)
        {
            if (offscreen->resize(swapchain->size()))
            {
                offscreen->renderTo(ImGui::GetDrawData(), ui.shell->clearColor(),
                                    swapchain->currentFramebuffer());
                if (const std::optional<shell::ScreenshotRequest> request = ui.shell->pendingScreenshot())
                {
                    // A window captures itself at its own size.
                    std::optional<Image> image = offscreen->capture();
                    ui.shell->completeScreenshot(
                        request->ticket,
                        image ? Expected<UiScreenshot, CommandError>(screenshotOf(*image))
                              : Expected<UiScreenshot, CommandError>(unexpected(CommandError::make(
                                    ErrorKind::Internal, "the frame could not be read back"))));
                }
            }
            swapchain->present();
        }
        play.update(delta);
        editor.endFrame(delta);
        ++frames;
    }
    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    if (errors != 0)
    {
        ILL_LOG_ERROR("editor", "{} Vulkan validation error(s)", errors);
    }
    return errors == 0 ? 0 : 3;
}

int run(const std::vector<std::string>& args)
{
    std::string error;
    std::optional<Arguments> parsed = parse(args, error);
    if (!parsed)
    {
        return fail(error);
    }
    Arguments& arguments = *parsed;
    if (arguments.rpcListen && arguments.socket.empty() && arguments.project.empty())
    {
        return fail("--rpc-listen needs a socket path or --project");
    }
    if (!arguments.headless && arguments.rpcStdio)
    {
        return fail("--rpc-stdio needs --headless (the window's stdout is free for logs)");
    }

    // stdout carries the protocol in --rpc-stdio mode: every log line goes to stderr.
    Logger& logger = Logger::instance();
    logger.clearSinks();
    logger.addSink(std::make_shared<ConsoleSink>(ConsoleStreams::StderrOnly));
#if !defined(_WIN32)
    // A client that disconnects must not end the editor through SIGPIPE.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    reflectRenderComponents();
    reflectPhysicsComponents();
    reflectAudioComponents();

    // The window needs the windowing system before the editor starts (the server wakes its event loop).
    EventBus events;
    std::unique_ptr<Platform> platform;
    std::unique_ptr<Window> window;
    if (!arguments.headless)
    {
        platform = Platform::create();
        if (!platform)
        {
            return fail("no display is available; pass --headless");
        }
        WindowDesc desc;
        desc.title = "illusion";
        desc.width = 1600;
        desc.height = 900;
        desc.maximized = true;
        window = Window::create(*platform, desc, &events);
        if (!window)
        {
            return 1;
        }
        // People keep their preferences and layouts; headless and deterministic runs start clean.
        if (arguments.userDirectory.empty() && !arguments.deterministic)
        {
            arguments.userDirectory = userDataDirectory("illusion", "editor");
        }
    }

    std::error_code code;
    EditorContextOptions options;
    options.seed = arguments.deterministic && arguments.seed == 0 ? 1 : arguments.seed;
    options.workspace = !arguments.workspace.empty() ? arguments.workspace
                        : !arguments.project.empty()
                            ? std::filesystem::absolute(arguments.project, code).parent_path()
                        : arguments.headless ? std::filesystem::current_path(code)
                                             : homeDirectory();
    options.workspace = std::filesystem::absolute(options.workspace, code);
    options.mcpServer = executableDirectory() / ("illusion_mcp" ILLUSION_EXECUTABLE_SUFFIX);
    options.userDirectory = arguments.userDirectory;
#if defined(ILLUSION_MODULE_SDK)
    // The module SDK of the build tree this editor was built in (ADR 0043); M8 installs one beside it.
    options.moduleSdk = std::filesystem::path(std::u8string_view(u8"" ILLUSION_MODULE_SDK));
#endif
    EditorContext editor(options);
    // The game runtime of Play mode (ADR 0042); a headless editor plays without sound.
    JobSystem jobs;
    PlaySession play(
        editor, {.jobs = &jobs, .audioOutput = arguments.headless ? AudioOutput::Null : AudioOutput::Device});
    if (!arguments.project.empty())
    {
        if (Expected<void, CommandError> opened = editor.openProject(arguments.project); !opened)
        {
            ILL_LOG_ERROR("editor", "cannot open the project: {}", opened.error().message);
            return 1;
        }
    }

    rpc::RpcServerOptions serverOptions;
    serverOptions.permissions = arguments.permissions.value_or(rpc::PermissionMode::Auto);
    serverOptions.editorVersion = engineVersion().toString();
    serverOptions.interactive = !arguments.headless;
    if (platform)
    {
        Platform* waker = platform.get();
        serverOptions.wake = [waker] {
            waker->wake();
        };
    }
    rpc::RpcServer server(editor, std::move(serverOptions));

    u32 stdio = 0;
    if (arguments.rpcStdio)
    {
        stdio = server.addConnection(rpc::makeStdioStream(), true, "stdio");
    }
    std::filesystem::path endpoint;
    if (arguments.rpcListen)
    {
        if (editor.project() != nullptr)
        {
            const std::filesystem::path runDirectory = editor.project()->stateDirectory() / "run";
            std::filesystem::create_directories(runDirectory, code);
            if (!rpc::restrictToOwner(runDirectory))
            {
                ILL_LOG_ERROR("editor", "cannot restrict the run directory to the current user");
                return 1;
            }
        }
        const std::filesystem::path socket =
            arguments.socket.empty() ? rpc::defaultSocketPath(editor.project()->root()) : arguments.socket;
        Expected<rpc::SocketListener, std::string> listener = rpc::SocketListener::listen(socket);
        if (!listener)
        {
            ILL_LOG_ERROR("editor", "cannot listen: {}", listener.error());
            return 1;
        }
        server.listen(std::move(listener).value());
        {
            // Without a project the endpoint file sits next to the socket.
            const std::u8string address = socket.u8string();
            endpoint =
                editor.project() != nullptr
                    ? rpc::endpointFilePath(editor.project()->root())
                    : utf8Path(std::string(reinterpret_cast<const char*>(address.data()), address.size()) +
                               ".endpoint.json");
            const rpc::EndpointInfo info{
                currentProcessId(),
                std::string(rpc::kProtocolVersion),
                engineVersion().toString(),
                "unix",
                std::string(reinterpret_cast<const char*>(address.data()), address.size()),
                server.token()};
            if (Expected<void, std::string> written = rpc::writeEndpointFile(endpoint, info); !written)
            {
                ILL_LOG_ERROR("editor", "cannot write the endpoint file: {}", written.error());
                return 1;
            }
        }
        ILL_LOG_INFO("editor", "listening on {}", reinterpret_cast<const char*>(socket.u8string().c_str()));
    }
    if (arguments.headless && !arguments.rpcStdio && !arguments.rpcListen && arguments.frames == 0)
    {
        return fail("nothing to do: pass --rpc-stdio, --rpc-listen or --frames");
    }

    const int result = arguments.headless
                           ? runHeadless(arguments, editor, play, server, stdio)
                           : runWindowed(arguments, editor, play, server, *platform, events, *window);
    // Send the reply to editor.quit before the process ends.
    server.stop();
    if (!endpoint.empty())
    {
        std::filesystem::remove(endpoint, code);
    }
    return result;
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        const std::u8string text = std::filesystem::path(argv[i]).u8string();
        args.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return run(args);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
