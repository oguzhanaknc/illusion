// illusion_editor: the editor process. Until the windowed shell arrives (M7l) it runs headless and serves the
// editor protocol (ADR 0037):
//
//     illusion_editor --headless [--project <dir>] [--workspace <dir>] [--rpc-stdio] [--rpc-listen
//     [<socket>]]
//                     [--no-gpu] [--deterministic] [--seed <n>] [--permissions read-only|ask|auto]
//                     [--frames <n>]

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Version.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>
#include <illusion/render/Components.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
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
    "usage: illusion_editor --headless [--project <dir>] [--workspace <dir>] [--rpc-stdio]\n"
    "                       [--rpc-listen [<socket>]] [--no-gpu] [--deterministic] [--seed <n>]\n"
    "                       [--permissions read-only|ask|auto] [--frames <n>]\n";

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
    u64 seed = 0;
    u64 frames = 0;
    rpc::PermissionMode permissions = rpc::PermissionMode::Auto;
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
        else if (arg == "--project" || arg == "--workspace")
        {
            const std::optional<std::string> path = value();
            if (!path)
            {
                return std::nullopt;
            }
            (arg == "--project" ? out.project : out.workspace) = utf8Path(*path);
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

int run(const std::vector<std::string>& args)
{
    std::string error;
    const std::optional<Arguments> parsed = parse(args, error);
    if (!parsed)
    {
        return fail(error);
    }
    const Arguments& arguments = *parsed;
    if (!arguments.headless)
    {
        return fail("the windowed editor is not available yet; pass --headless");
    }
    if (arguments.rpcListen && arguments.socket.empty() && arguments.project.empty())
    {
        return fail("--rpc-listen needs a socket path or --project");
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

    std::error_code code;
    EditorContextOptions options;
    options.seed = arguments.deterministic && arguments.seed == 0 ? 1 : arguments.seed;
    options.workspace = !arguments.workspace.empty() ? arguments.workspace
                        : !arguments.project.empty()
                            ? std::filesystem::absolute(arguments.project, code).parent_path()
                            : std::filesystem::current_path(code);
    options.workspace = std::filesystem::absolute(options.workspace, code);
    options.mcpServer = executableDirectory() / ("illusion_mcp" ILLUSION_EXECUTABLE_SUFFIX);
    EditorContext editor(options);
    if (!arguments.project.empty())
    {
        if (Expected<void, CommandError> opened = editor.openProject(arguments.project); !opened)
        {
            ILL_LOG_ERROR("editor", "cannot open the project: {}", opened.error().message);
            return 1;
        }
    }
    if (arguments.noGpu)
    {
        ILL_LOG_INFO("editor", "running without a GPU");
    }

    rpc::RpcServerOptions serverOptions;
    serverOptions.permissions = arguments.permissions;
    serverOptions.editorVersion = engineVersion().toString();
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
            const std::filesystem::path run = editor.project()->stateDirectory() / "run";
            std::filesystem::create_directories(run, code);
            if (!rpc::restrictToOwner(run))
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
    if (!arguments.rpcStdio && !arguments.rpcListen && arguments.frames == 0)
    {
        return fail("nothing to do: pass --rpc-stdio, --rpc-listen or --frames");
    }

    // The headless frame loop: run queued requests, then end the frame (events, journal, autosave).
    constexpr f64 kFixedStep = 1.0 / 60.0;
    auto last = std::chrono::steady_clock::now();
    for (u64 frame = 0; !server.quitRequested(); ++frame)
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
        const f64 elapsed = std::chrono::duration<f64>(now - last).count();
        last = now;
        editor.endFrame(arguments.deterministic ? kFixedStep : elapsed);
    }
    // Send the reply to editor.quit before the process ends.
    server.stop();
    if (!endpoint.empty())
    {
        std::filesystem::remove(endpoint, code);
    }
    return 0;
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
