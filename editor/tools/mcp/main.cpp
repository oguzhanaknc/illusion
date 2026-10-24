// illusion_mcp: the MCP bridge of the editor (ADR 0039). It speaks the Model Context Protocol on stdio (both
// eras) and runs every tool as an editor command over the editor protocol.
//
//     illusion_mcp [--project <dir>] [--auto | --attach | --launch] [--headless] [--no-gpu]
//                  [--deterministic] [--seed <n>] [--permissions read-only|ask|auto]
//                  [--editor <illusion_editor>] [--editor-arg <arg>]... [--tools all|core|<domain>,...]
//                  [--record <file.jsonl>] [--page-size <n>] [--log-level trace|debug|info|warn|error|off]
//
// stdout carries only protocol messages; logs (its own and a launched editor's) go to stderr. It exits when
// stdin ends. Exit codes: 0 stdin ended, 1 the editor could not be reached at start, 2 usage error.

#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Version.hpp>
#include <illusion/editor/client/EditorToolProvider.hpp>
#include <illusion/editor/mcp/McpServer.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <charconv>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr std::string_view kUsage =
    "usage: illusion_mcp [--project <dir>] [--auto | --attach | --launch] [--headless] [--no-gpu]\n"
    "                    [--deterministic] [--seed <n>] [--permissions read-only|ask|auto]\n"
    "                    [--editor <illusion_editor>] [--editor-arg <arg>]... [--tools "
    "all|core|<domain>,...]\n"
    "                    [--record <file.jsonl>] [--page-size <n>] [--log-level <level>]\n";

int usage(std::string_view error)
{
    std::fprintf(stderr, "illusion_mcp: %.*s\n%.*s", static_cast<int>(error.size()), error.data(),
                 static_cast<int>(kUsage.size()), kUsage.data());
    return 2;
}

std::optional<LogLevel> parseLevel(std::string_view name)
{
    constexpr std::pair<std::string_view, LogLevel> kLevels[] = {
        {"trace", LogLevel::Trace}, {"debug", LogLevel::Debug}, {"info", LogLevel::Info},
        {"warn", LogLevel::Warn},   {"error", LogLevel::Error}, {"off", LogLevel::Off}};
    for (const auto& [text, level] : kLevels)
    {
        if (text == name)
        {
            return level;
        }
    }
    return std::nullopt;
}

std::FILE* openForWriting(const std::filesystem::path& path)
{
#if defined(_WIN32)
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), L"wb") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

// Writes `{"t": ms, "dir": "in"|"out", "line": "<message>"}` per message (--record).
class Recorder
{
public:
    explicit Recorder(std::FILE* file) noexcept
        : m_file(file)
        , m_start(std::chrono::steady_clock::now())
    {
    }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    ~Recorder()
    {
        if (m_file != nullptr)
        {
            std::fclose(m_file);
        }
    }

    void write(std::string_view direction, std::string_view line)
    {
        if (m_file == nullptr)
        {
            return;
        }
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start)
                .count();
        JsonMutDocument doc;
        JsonMutValue entry = doc.makeObject();
        entry.add("t", doc.makeI64(static_cast<i64>(ms)));
        entry.add("dir", doc.makeString(direction));
        entry.add("line", doc.makeString(line));
        doc.setRoot(entry);
        std::string text;
        if (!doc.write(text, JsonWriteOptions{.pretty = false}))
        {
            return;
        }
        text += '\n';
        const std::lock_guard lock(m_mutex);
        std::fwrite(text.data(), 1, text.size(), m_file);
        std::fflush(m_file);
    }

private:
    std::FILE* m_file;
    std::chrono::steady_clock::time_point m_start;
    std::mutex m_mutex;
};

// MCP over this process's stdin and stdout: a thread reads and frames lines, the main thread handles them.
class StdioTransport final : public mcp::McpTransport
{
public:
    explicit StdioTransport(Recorder& recorder)
        : m_stream(rpc::makeStdioStream())
        , m_recorder(recorder)
    {
        m_reader = std::thread([this] { readLoop(); });
    }
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;
    ~StdioTransport() override
    {
        m_stream->close();
        if (m_reader.joinable())
        {
            m_reader.join();
        }
    }

    void send(std::string_view line) override
    {
        m_recorder.write("out", line);
        std::string text(line);
        text += '\n';
        (void)m_stream->write(text);
    }

    std::optional<std::string> poll() override
    {
        const std::lock_guard lock(m_mutex);
        return take();
    }

    // The next line, waiting up to @p timeout; nothing on a timeout or once stdin ended and all lines are
    // taken.
    std::optional<std::string> wait(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        m_ready.wait_for(lock, timeout, [&] { return !m_lines.empty() || m_ended; });
        return take();
    }

    [[nodiscard]] bool finished()
    {
        const std::lock_guard lock(m_mutex);
        return m_ended && m_lines.empty();
    }

private:
    std::optional<std::string> take()
    {
        if (m_lines.empty())
        {
            return std::nullopt;
        }
        std::string line = std::move(m_lines.front());
        m_lines.pop_front();
        return line;
    }

    void readLoop()
    {
        rpc::LineFramer framer;
        std::vector<char> buffer(64 * 1024);
        for (;;)
        {
            usize count = 0;
            const rpc::ReadStatus status = m_stream->read(buffer, count, std::chrono::milliseconds::max());
            if (status == rpc::ReadStatus::Closed)
            {
                break;
            }
            if (status != rpc::ReadStatus::Data)
            {
                continue;
            }
            const bool ok = framer.feed(std::string_view(buffer.data(), count), [&](std::string_view line) {
                m_recorder.write("in", line);
                const std::lock_guard lock(m_mutex);
                m_lines.emplace_back(line);
                m_ready.notify_one();
            });
            if (!ok)
            {
                ILL_LOG_ERROR("mcp", "a message exceeds {} bytes; closing", rpc::kMaxMessageSize);
                break;
            }
        }
        const std::lock_guard lock(m_mutex);
        m_ended = true;
        m_ready.notify_one();
    }

    std::unique_ptr<rpc::RpcStream> m_stream;
    Recorder& m_recorder;
    std::thread m_reader;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::string> m_lines;
    bool m_ended = false;
};

int run(const std::vector<std::string>& args)
{
    client::EditorToolProviderOptions provider;
    mcp::McpServerOptions server;
    server.version = engineVersion().toString();
    provider.launch.editor = executableDirectory() / ("illusion_editor" ILLUSION_EXECUTABLE_SUFFIX);
    std::filesystem::path record;
    LogLevel level = LogLevel::Info;
    for (usize i = 0; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        const bool hasValue = i + 1 < args.size();
        if (arg == "--project" && hasValue)
        {
            provider.project = utf8Path(args[++i]);
        }
        else if (arg == "--auto" || arg == "--attach" || arg == "--launch")
        {
            provider.mode = arg == "--auto"     ? client::BridgeMode::Auto
                            : arg == "--attach" ? client::BridgeMode::Attach
                                                : client::BridgeMode::Launch;
        }
        else if (arg == "--headless")
        {
            // The editor has no window yet: a launched editor is always headless.
        }
        else if (arg == "--no-gpu" || arg == "--deterministic")
        {
            provider.launch.arguments.emplace_back(arg);
        }
        else if ((arg == "--seed" || arg == "--permissions") && hasValue)
        {
            provider.launch.arguments.emplace_back(arg);
            provider.launch.arguments.push_back(args[++i]);
        }
        else if (arg == "--editor" && hasValue)
        {
            provider.launch.editor = utf8Path(args[++i]);
        }
        else if (arg == "--editor-arg" && hasValue)
        {
            provider.launch.arguments.push_back(args[++i]);
        }
        else if (arg == "--tools" && hasValue)
        {
            Expected<mcp::ToolSelection, std::string> tools = mcp::ToolSelection::parse(args[++i]);
            if (!tools)
            {
                return usage(tools.error());
            }
            server.tools = std::move(tools).value();
        }
        else if (arg == "--record" && hasValue)
        {
            record = utf8Path(args[++i]);
        }
        else if (arg == "--page-size" && hasValue)
        {
            const std::string& text = args[++i];
            usize size = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), size);
            if (error != std::errc() || end != text.data() + text.size() || size == 0)
            {
                return usage("--page-size takes a positive number");
            }
            server.pageSize = size;
        }
        else if (arg == "--log-level" && hasValue)
        {
            const std::optional<LogLevel> parsed = parseLevel(args[++i]);
            if (!parsed)
            {
                return usage("--log-level takes trace, debug, info, warn, error or off");
            }
            level = *parsed;
        }
        else
        {
            return usage("unknown argument '" + std::string(arg) + "'");
        }
    }

    // stdout carries the protocol: every log line goes to stderr.
    Logger& logger = Logger::instance();
    logger.clearSinks();
    logger.addSink(std::make_shared<ConsoleSink>(ConsoleStreams::StderrOnly));
    logger.setLevel(level);
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif
    provider.log = [](std::string_view line) {
        std::fprintf(stderr, "[editor] %.*s\n", static_cast<int>(line.size()), line.data());
    };

    std::FILE* recordFile = nullptr;
    if (!record.empty())
    {
        recordFile = openForWriting(record);
        if (recordFile == nullptr)
        {
            return usage("cannot write " +
                         std::string(reinterpret_cast<const char*>(record.u8string().c_str())));
        }
    }
    Recorder recorder(recordFile);

    client::EditorToolProvider editor(std::move(provider));
    if (Expected<void, std::string> connected = editor.connect(); !connected)
    {
        ILL_LOG_ERROR("mcp", "{}", connected.error());
        return 1;
    }
    server.clientChanged = [&editor](const mcp::McpClientInfo& client) {
        ILL_LOG_INFO("mcp", "client '{}' {} speaks MCP {}", client.name, client.version,
                     client.protocolVersion);
        editor.setAgent(client);
    };
    StdioTransport transport(recorder);
    mcp::McpServer mcp(editor, transport, std::move(server));
    mcp.refresh();
    ILL_LOG_INFO("mcp", "serving {} tools on stdio", mcp.tools().size());
    for (;;)
    {
        if (std::optional<std::string> line = transport.wait(std::chrono::milliseconds(20)))
        {
            mcp.handle(*line);
            continue;
        }
        if (transport.finished())
        {
            break;
        }
        editor.poll();
        mcp.refresh();
    }
    mcp.close();
    const std::optional<i32> exit = editor.close();
    ILL_LOG_INFO("mcp", "stdin ended; the editor exited with code {}", exit.value_or(0));
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
