#include "mcp/McpTestSupport.hpp"
#include "rpc/RpcTestSupport.hpp"

#include <illusion/core/Version.hpp>
#include <illusion/editor/client/EditorToolProvider.hpp>
#include <illusion/editor/client/McpClient.hpp>
#include <illusion/editor/core/Project.hpp>
#include <illusion/editor/mcp/McpServer.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::client;

namespace
{

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<std::string> lines(std::string_view text)
{
    std::vector<std::string> out;
    while (!text.empty())
    {
        const usize end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        if (!line.empty())
        {
            out.emplace_back(line);
        }
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    }
    return out;
}

// A project "game" in @p workspace, made from the empty template next to the test executable.
std::filesystem::path makeProject(const test::TempDirectory& workspace)
{
    const std::filesystem::path root = workspace.path / "game";
    Expected<Project, std::string> project =
        Project::create(root, "Game", executableDirectory() / "editor-data" / "templates" / "empty");
    REQUIRE_MESSAGE(project.hasValue(), (project ? std::string() : project.error()));
    return root;
}

EditorToolProviderOptions launchOptions(const test::TempDirectory& workspace,
                                        const std::filesystem::path& project)
{
    EditorToolProviderOptions options;
    options.mode = BridgeMode::Launch;
    options.project = project;
    options.launch.editor = utf8Path(ILLUSION_EDITOR_EXECUTABLE);
    options.launch.arguments = {"--deterministic", "--workspace", utf8(workspace.path)};
    return options;
}

// What a transcript did, without the details that depend on the editor: per message its id and whether it
// succeeded, failed with a code, or was a tool error with a code; notifications by method. Progress
// notifications are left out (their number depends on timing).
std::vector<std::string> outcomes(const std::vector<std::string>& sent)
{
    std::vector<std::string> out;
    for (const std::string& line : sent)
    {
        const JsonDocument message = test::parseJson(line);
        const JsonValue root = message.root();
        if (const std::optional<std::string_view> method = root.find("method").asString())
        {
            if (*method != "notifications/progress")
            {
                out.push_back("notification " + std::string(*method));
            }
            continue;
        }
        const std::string id = root.find("id").dump();
        if (const JsonValue error = root.find("error"); !error.isAbsent())
        {
            out.push_back(id + " error " + error.find("code").dump());
        }
        else if (root.find("result").find("isError").asBool().value_or(false))
        {
            out.push_back(id + " tool error " +
                          root.find("result").find("structuredContent").find("code").dump());
        }
        else
        {
            out.push_back(id + " result");
        }
    }
    return out;
}

std::vector<std::string> runTranscript(mcp::ToolProvider& provider, const std::vector<std::string>& input)
{
    test::MemoryTransport transport;
    transport.inbox.assign(input.begin(), input.end());
    mcp::McpServerOptions options;
    options.pageSize = 4;
    mcp::McpServer server(provider, transport, options);
    while (std::optional<std::string> line = transport.poll())
    {
        server.handle(*line);
    }
    server.close();
    return transport.sent;
}

JsonDocument handleOne(mcp::McpServer& server, test::MemoryTransport& transport, std::string_view line)
{
    transport.sent.clear();
    server.handle(line);
    REQUIRE_MESSAGE(!transport.sent.empty(), "no reply to " << line);
    return test::parseJson(transport.sent.back());
}

constexpr std::string_view kInitialize =
    R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})";

class NoControl final : public mcp::CallControl
{
public:
    bool cancelled() const noexcept override { return false; }
    void progress(f64, f64, std::string_view) override {}
    void service() override {}
};

// Starts illusion_mcp, sends @p input, closes its stdin and returns its stdout and stderr once it exits.
std::pair<std::string, std::string> runBridge(const std::vector<std::string>& arguments,
                                              const std::vector<std::string>& input)
{
    ProcessOptions options;
    options.executable = utf8Path(ILLUSION_MCP_EXECUTABLE);
    options.arguments = arguments;
    options.pipeInput = true;
    options.captureOutput = true;
    options.captureError = true;
    Expected<Process, std::string> started = Process::spawn(options);
    REQUIRE_MESSAGE(started.hasValue(), (started ? std::string() : started.error()));
    Process& process = *started;
    for (const std::string& line : input)
    {
        REQUIRE(process.writeInput(line + "\n"));
    }
    process.closeInput();
    std::string out;
    std::string err;
    const auto deadline = std::chrono::steady_clock::now() + test::kRpcPatience;
    std::optional<i32> code;
    while (!code && std::chrono::steady_clock::now() < deadline)
    {
        process.read(ProcessStream::Output, out);
        process.read(ProcessStream::Error, err);
        code = process.wait(std::chrono::milliseconds(10));
    }
    REQUIRE_MESSAGE(code.has_value(), "illusion_mcp did not exit");
    process.read(ProcessStream::Output, out);
    process.read(ProcessStream::Error, err);
    CHECK(*code == 0);
    return {out, err};
}

} // namespace

TEST_CASE("the golden transcripts have the same outcomes against a real headless editor")
{
    const std::filesystem::path golden = utf8Path(ILLUSION_EDITOR_TEST_DATA) / "mcp" / "golden";
    usize transcripts = 0;
    for (const std::string_view era : {"2025-06-18", "2025-11-25", "2026-07-28"})
    {
        for (const auto& entry : std::filesystem::directory_iterator(golden / std::string(era)))
        {
            const std::string name = entry.path().filename().string();
            if (!name.ends_with(".in.jsonl"))
            {
                continue;
            }
            CAPTURE(era);
            CAPTURE(name);
            const std::vector<std::string> input = lines(readText(entry.path()));
            test::FakeProvider fake;
            const std::vector<std::string> expected = outcomes(runTranscript(fake, input));

            test::TempDirectory workspace;
            EditorToolProvider editor(launchOptions(workspace, makeProject(workspace)));
            Expected<void, std::string> connected = editor.connect();
            REQUIRE_MESSAGE(connected.hasValue(), (connected ? std::string() : connected.error()));
            CHECK(outcomes(runTranscript(editor, input)) == expected);
            CHECK(editor.close() == 0);
            ++transcripts;
        }
    }
    CHECK(transcripts == 19);
}

TEST_CASE("a killed editor yields editor_crashed with its log; the next call relaunches it and recovers")
{
    test::TempDirectory workspace;
    EditorToolProvider editor(launchOptions(workspace, makeProject(workspace)));
    REQUIRE(editor.connect().hasValue());
    CHECK_FALSE(editor.attached());
    test::MemoryTransport transport;
    mcp::McpServerOptions options;
    options.clientChanged = [&](const mcp::McpClientInfo& client) {
        editor.setAgent(client);
    };
    mcp::McpServer server(editor, transport, options);
    handleOne(server, transport, kInitialize);

    const JsonDocument created = handleOne(
        server, transport,
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"entity_create","arguments":{"name":"Board"}}})");
    REQUIRE(created.root().find("result").find("isError").asBool() == false);
    // A second call makes sure the frame that journaled the first one has ended.
    handleOne(
        server, transport,
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene_info","arguments":{}}})");

    Process* process = editor.editorProcess();
    REQUIRE(process != nullptr);
    REQUIRE(process->terminate());
    REQUIRE(process->wait(test::kRpcPatience).has_value());

    const JsonDocument crashed = handleOne(
        server, transport,
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene_info","arguments":{}}})");
    const JsonValue failure = crashed.root().find("result");
    REQUIRE(failure.find("isError").asBool() == true);
    CHECK(failure.find("structuredContent").find("kind").asString() == "editor_crashed");
    CHECK(failure.find("structuredContent").find("retryable").asBool() == true);
    CHECK(failure.find("structuredContent").find("log").size() > 0);
    CHECK(failure.find("content")[0].find("text").asString().value_or("").find("Last editor log lines:") !=
          std::string_view::npos);

    const JsonDocument recovered = handleOne(
        server, transport,
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"entity_get","arguments":{"entity":"name:Board"}}})");
    const JsonValue result = recovered.root().find("result");
    REQUIRE_MESSAGE(result.find("isError").asBool() == false, recovered.root().dump());
    CHECK(result.find("structuredContent").find("entity").find("name").asString() == "Board");
    const JsonValue content = result.find("content");
    REQUIRE(content.size() == 2);
    const std::string_view note = content[1].find("text").asString().value_or("");
    CHECK(note.find("project.recover replayed ") != std::string_view::npos);
    CHECK(note.find("replayed 0 operations") == std::string_view::npos);
    CHECK(editor.close() == 0);
}

TEST_CASE("auto mode attaches to the editor of the project's endpoint file")
{
    test::RpcHarness rpc;
    test::TempDirectory project;
    std::filesystem::create_directories(project.path / ".illusion" / "run");
    Expected<rpc::SocketListener, std::string> listener =
        rpc::SocketListener::listen(rpc::defaultSocketPath(project.path));
    REQUIRE_MESSAGE(listener.hasValue(), (listener ? std::string() : listener.error()));
    const std::string address = utf8(listener->path());
    rpc.server->listen(std::move(listener).value());
    REQUIRE(rpc::writeEndpointFile(rpc::endpointFilePath(project.path),
                                   rpc::EndpointInfo{currentProcessId(), std::string(rpc::kProtocolVersion),
                                                     engineVersion().toString(), "unix", address,
                                                     rpc.server->token()})
                .hasValue());

    EditorToolProviderOptions options;
    options.mode = BridgeMode::Auto;
    options.project = project.path;
    options.launch.editor = utf8Path(ILLUSION_EDITOR_EXECUTABLE);
    EditorToolProvider editor(options);
    // The provider blocks on its calls, so it runs on a thread while this one pumps the editor's requests.
    std::atomic<bool> done{false};
    Expected<void, std::string> connected = unexpected(std::string("not run"));
    mcp::CallResult created;
    std::thread worker([&] {
        connected = editor.connect();
        if (connected)
        {
            NoControl control;
            created = editor.call("entity.create", R"({"name":"Board"})", control);
        }
        done = true;
    });
    const auto deadline = std::chrono::steady_clock::now() + test::kRpcPatience;
    while (!done && std::chrono::steady_clock::now() < deadline)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    worker.join();
    REQUIRE_MESSAGE(connected.hasValue(), (connected ? std::string() : connected.error()));
    CHECK(editor.attached());
    CHECK(editor.editorProcess() == nullptr);
    CHECK_MESSAGE(created.ok, created.json);
    REQUIRE(rpc.fixture.editor->history().undoStep() != nullptr);
    CHECK(rpc.fixture.editor->history().undoStep()->origin == "rpc:illusion_mcp");
    CHECK(editor.catalog().find("entity.create") != nullptr);
    editor.close();
}

TEST_CASE("illusion_mcp writes only protocol messages to stdout at every log level")
{
    test::TempDirectory workspace;
    const std::vector<std::string> input = {
        std::string(kInitialize),
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"entity_create","arguments":{"name":"Board"}}})",
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"entity_get","arguments":{"entity":"/Nope"}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"editor_wait","arguments":{"frames":2},"_meta":{"progressToken":7}}})",
        R"({"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"illusion://log/recent"}})",
        "not json"};
    for (const std::string_view level : {"trace", "info", "error"})
    {
        CAPTURE(level);
        const auto [out, err] = runBridge({"--launch", "--editor", ILLUSION_EDITOR_EXECUTABLE, "--editor-arg",
                                           "--deterministic", "--editor-arg", "--workspace", "--editor-arg",
                                           utf8(workspace.path), "--log-level", std::string(level)},
                                          input);
        const std::vector<std::string> replies = lines(out);
        CHECK(replies.size() >= 6);
        usize responses = 0;
        for (const std::string& line : replies)
        {
            const Expected<JsonDocument, JsonError> message = JsonDocument::parse(line);
            REQUIRE_MESSAGE(message.hasValue(), "not a protocol message on stdout: " << line);
            CHECK(message->root().find("jsonrpc").asString() == "2.0");
            responses += message->root().find("id").isAbsent() ? 0 : 1;
        }
        CHECK(responses == 6);
        if (level == "trace")
        {
            CHECK(err.find("[mcp]") != std::string::npos);
            CHECK(err.find("[editor]") != std::string::npos);
        }
    }
}

TEST_CASE("McpClient runs editor methods as tools in both eras, and project.create writes .mcp.json")
{
    for (const bool modern : {false, true})
    {
        CAPTURE(modern);
        test::TempDirectory workspace;
        McpLaunchOptions options;
        options.bridge = utf8Path(ILLUSION_MCP_EXECUTABLE);
        options.modern = modern;
        options.arguments = {"--launch",           "--editor",    ILLUSION_EDITOR_EXECUTABLE,
                             "--editor-arg",       "--workspace", "--editor-arg",
                             utf8(workspace.path), "--tools",     "core"};
        Expected<McpClient, std::string> client = McpClient::launch(options);
        REQUIRE_MESSAGE(client.hasValue(), (client ? std::string() : client.error()));
        CHECK(client->protocolVersion() == (modern ? "2026-07-28" : "2025-06-18"));

        Expected<std::string, RpcError> created =
            client->call("project.create", R"({"directory":"game","name":"Game"})", kDefaultCallTimeout);
        REQUIRE_MESSAGE(created.hasValue(), (created ? std::string() : created.error().message));
        const JsonDocument config = test::parseJson(readText(workspace.path / "game" / ".mcp.json"));
        const JsonValue server = config.root().find("mcpServers").find("illusion");
        CHECK(server.find("type").asString() == "stdio");
        CHECK(utf8Path(server.find("command").asString().value_or("")).filename() ==
              utf8Path(ILLUSION_MCP_EXECUTABLE).filename());
        CHECK(server.find("args").dump() == R"(["--project","."])");

        Expected<std::string, RpcError> missing =
            client->call("entity.get", R"({"entity":"/Nope"})", kDefaultCallTimeout);
        REQUIRE_FALSE(missing.hasValue());
        CHECK(missing.error().code == 1200);
        CHECK(test::parseJson(missing.error().data).root().find("kind").asString() == "not_found");
        CHECK(test::parseJson(missing.error().json).root().find("code").asI64() == 1200);

        // --tools core leaves asset tools out; illusion_call still reaches them.
        Expected<std::string, RpcError> filtered = client->call("asset.list", "{}", kDefaultCallTimeout);
        REQUIRE_FALSE(filtered.hasValue());
        CHECK(filtered.error().code == -32602);
        Expected<std::string, RpcError> escaped =
            client->call("illusion.call", R"({"method":"asset.list","params":{}})", kDefaultCallTimeout);
        CHECK_MESSAGE(escaped.hasValue(), (escaped ? std::string() : escaped.error().message));
        CHECK(client->close(test::kRpcPatience) == 0);
    }
}
