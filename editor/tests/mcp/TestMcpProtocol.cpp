#include "mcp/McpTestSupport.hpp"
#include "rpc/RpcTestSupport.hpp"

#include <illusion/editor/mcp/McpCatalog.hpp>
#include <illusion/editor/mcp/McpServer.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::mcp;

namespace
{

std::filesystem::path goldenDirectory()
{
    return utf8Path(ILLUSION_EDITOR_TEST_DATA) / "mcp" / "golden";
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
        const std::string_view line = text.substr(0, end);
        if (!line.empty())
        {
            out.emplace_back(line);
        }
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    }
    return out;
}

McpServerOptions goldenOptions()
{
    McpServerOptions options;
    options.version = "1.0.0";
    options.pageSize = 4;
    return options;
}

// Queues every input line, lets the server take them one at a time, then closes it.
std::vector<std::string> runTranscript(ToolProvider& provider, const std::vector<std::string>& input,
                                       McpServerOptions options = goldenOptions())
{
    test::MemoryTransport transport;
    transport.inbox.assign(input.begin(), input.end());
    McpServer server(provider, transport, std::move(options));
    while (std::optional<std::string> line = transport.poll())
    {
        server.handle(*line);
    }
    server.close();
    return transport.sent;
}

JsonDocument handleOne(McpServer& server, test::MemoryTransport& transport, std::string_view line)
{
    transport.sent.clear();
    server.handle(line);
    REQUIRE_MESSAGE(transport.sent.size() == 1, "expected one reply to " << line);
    return test::parseJson(transport.sent.back());
}

constexpr std::string_view kInitialize =
    R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})";

} // namespace

TEST_CASE("tool names replace every character outside [a-z0-9_] with an underscore")
{
    CHECK(toolName("entity.create") == "entity_create");
    CHECK(toolName("project.file_read") == "project_file_read");
    CHECK(toolName("Asset.Info-2") == "asset_info_2");
    CHECK(toolName(std::string(80, 'a')).size() == 64);
    CHECK(isValidToolName("entity_create"));
    CHECK_FALSE(isValidToolName(""));
    CHECK_FALSE(isValidToolName("entity.create"));
    CHECK_FALSE(isValidToolName("Entity"));
    CHECK_FALSE(isValidToolName(std::string(65, 'a')));
}

TEST_CASE("the editor's catalogue becomes unique, valid tools in catalogue order with the escape hatch last")
{
    test::RpcHarness rpc;
    Expected<Catalog, std::string> catalog = catalogFromOpenRpc(rpc.server->discoverDocument());
    REQUIRE_MESSAGE(catalog.hasValue(), (catalog ? std::string() : catalog.error()));
    // Server methods (editor.subscribe, editor.quit, ...) act on the connection and are not tools.
    CHECK(catalog->find("editor.quit") == nullptr);
    CHECK(catalog->find("editor.set_client") == nullptr);
    REQUIRE(catalog->find("entity.create") != nullptr);
    CHECK(catalog->commands.size() == rpc.fixture.editor->commands().commands().size());

    const std::vector<ToolDefinition> tools = generateTools(*catalog, ToolSelection{});
    REQUIRE(tools.size() == catalog->commands.size() + 1);
    std::set<std::string> names;
    for (usize i = 0; i < tools.size(); ++i)
    {
        CAPTURE(tools[i].name);
        CHECK(isValidToolName(tools[i].name));
        CHECK(names.insert(tools[i].name).second);
        const JsonDocument tool = test::parseJson(tools[i].json);
        CHECK(tool.root().find("name").asString() == tools[i].name);
        CHECK(tool.root().find("inputSchema").find("type").asString() == "object");
        CHECK(tool.root().find("annotations").find("openWorldHint").asBool() == false);
        if (i + 1 < tools.size())
        {
            CHECK(tools[i].method == catalog->commands[i].name);
            CHECK(tool.root().find("outputSchema").find("type").asString() == "object");
        }
    }
    CHECK(tools.back().name == kCallTool);

    const ToolDefinition* get = findTool(tools, "entity_get");
    REQUIRE(get != nullptr);
    const JsonDocument tool = test::parseJson(get->json);
    CHECK(tool.root().find("annotations").find("readOnlyHint").asBool() == true);
    CHECK(tool.root().find("annotations").find("idempotentHint").asBool() == true);
    const std::string_view description = tool.root().find("description").asString().value_or("");
    CHECK(description.find("Example:") != std::string_view::npos);
    CHECK(description.find("Parameters:\n- entity (string, required)") != std::string_view::npos);
    CHECK(description.find("Permission: read.") != std::string_view::npos);
}

TEST_CASE("tool descriptions document types, ranges, enums and optional values")
{
    CommandSpec command;
    command.name = "test.describe";
    command.description = "Does a thing. Example: {}.";
    command.permission = "edit";
    command.async = true;
    command.inputSchema =
        R"({"type":"object","properties":{"frames":{"type":"integer","minimum":1,"maximum":10},"mode":{"enum":["push","pull"]},"axis":{"anyOf":[{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},{"type":"null"}]},"id":{"type":"string","pattern":"^[0-9]+$","description":"An id."},"dryRun":{"type":"boolean"}},"required":["frames"]})";
    const std::string text = toolDescription(command);
    CHECK(text.find("- frames (integer in [1, 10], required)") != std::string::npos);
    CHECK(text.find(R"(- mode ("push" | "pull", optional))") != std::string::npos);
    CHECK(text.find("- axis (number[3], optional)") != std::string::npos);
    CHECK(text.find("- id (string matching ^[0-9]+$, optional): An id.") != std::string::npos);
    CHECK(text.find("dryRun (validate only)") != std::string::npos);
    CHECK(text.find("- dryRun") == std::string::npos);
    CHECK(text.find("Long-running: reports progress and can be cancelled.") != std::string::npos);
}

TEST_CASE(
    "--tools selects domains; core is a fixed set; illusion_batch stands in for an unselected editor.batch")
{
    CHECK(ToolSelection::parse("all")->all());
    Expected<ToolSelection, std::string> core = ToolSelection::parse("core");
    REQUIRE(core.hasValue());
    CHECK(core->includes("entity.create"));
    CHECK(core->includes("editor.batch"));
    CHECK_FALSE(core->includes("asset.move"));
    Expected<ToolSelection, std::string> picked = ToolSelection::parse("entity,scene");
    REQUIRE(picked.hasValue());
    CHECK(picked->includes("scene.info"));
    CHECK_FALSE(picked->includes("editor.wait"));
    CHECK_FALSE(ToolSelection::parse("").hasValue());
    CHECK_FALSE(ToolSelection::parse("entity,").hasValue());
    CHECK_FALSE(ToolSelection::parse("Entity").hasValue());
    CHECK_FALSE(ToolSelection::parse("all,entity").hasValue());

    test::FakeProvider provider;
    const std::vector<ToolDefinition> tools = generateTools(provider.catalog(), *picked);
    std::vector<std::string> names;
    for (const ToolDefinition& tool : tools)
    {
        names.push_back(tool.name);
    }
    CHECK(names == std::vector<std::string>{"entity_create", "entity_get", "scene_info", "scene_tree",
                                            std::string(kCallTool), std::string(kBatchTool)});
    CHECK(findTool(tools, kBatchTool)->method == "editor.batch");
    CHECK(generateTools(Catalog{}, ToolSelection{}).empty());
}

TEST_CASE("golden transcripts of every protocol era against a fake provider")
{
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    const bool rewrite = update != nullptr && std::string_view(update) == "1";
    usize transcripts = 0;
    for (const std::string_view era : {"2025-06-18", "2025-11-25", "2026-07-28"})
    {
        const std::filesystem::path directory = goldenDirectory() / std::string(era);
        REQUIRE(std::filesystem::is_directory(directory));
        std::vector<std::filesystem::path> inputs;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.path().filename().string().ends_with(".in.jsonl"))
            {
                inputs.push_back(entry.path());
            }
        }
        std::sort(inputs.begin(), inputs.end());
        for (const std::filesystem::path& input : inputs)
        {
            const std::string name = input.filename().string();
            CAPTURE(era);
            CAPTURE(name);
            test::FakeProvider provider;
            std::string output;
            for (const std::string& line : runTranscript(provider, lines(readText(input))))
            {
                CHECK(test::parseJson(line).root().find("jsonrpc").asString() == "2.0");
                output += line + "\n";
            }
            const std::filesystem::path golden = directory / (name.substr(0, name.size() - 9) + ".out.jsonl");
            if (rewrite)
            {
                std::ofstream(golden, std::ios::binary) << output;
            }
            REQUIRE_MESSAGE(std::filesystem::exists(golden),
                            "missing golden file; run with ILLUSION_UPDATE_GOLDENS=1");
            CHECK_MESSAGE(readText(golden) == output,
                          "the transcript changed; run with ILLUSION_UPDATE_GOLDENS=1 and review");
            ++transcripts;
        }
    }
    CHECK(transcripts == 19);
}

TEST_CASE("a cancelled running call gets no response and the requests behind it run in order")
{
    test::FakeProvider provider;
    const std::vector<std::string> sent = runTranscript(
        provider,
        {std::string(kInitialize),
         R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor_wait","arguments":{"frames":100000}}})",
         R"({"jsonrpc":"2.0","id":2,"method":"ping"})",
         R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})",
         R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene_info"}})"});
    REQUIRE(sent.size() == 3);
    // The ping is answered while the call runs; the call itself never is.
    CHECK(test::parseJson(sent[1]).root().find("id").asI64() == 2);
    CHECK(test::parseJson(sent[2]).root().find("id").asI64() == 3);
    CHECK(provider.calls == std::vector<std::string>{"editor.wait", "scene.info"});
}

TEST_CASE("illusion_call runs catalogue commands only, never the server methods of the bridge's connection")
{
    test::FakeProvider provider;
    test::MemoryTransport transport;
    McpServer server(provider, transport);
    handleOne(server, transport, kInitialize);
    for (const std::string_view method : {"editor.quit", "editor.subscribe", "rpc.cancel"})
    {
        CAPTURE(method);
        const JsonDocument reply = handleOne(
            server, transport,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"illusion_call","arguments":{"method":")" +
                std::string(method) + R"("}}})");
        const JsonValue result = reply.root().find("result");
        CHECK(result.find("isError").asBool() == true);
        CHECK(result.find("structuredContent").find("code").asI64() == -32601);
        CHECK(result.find("structuredContent").find("path").asString() == "/method");
    }
    CHECK(provider.calls.empty());
}

TEST_CASE(
    "results are summarized in at most maxText bytes without splitting UTF-8; structuredContent stays whole")
{
    test::FakeProvider provider;
    provider.catalogValue.commands.push_back(CommandSpec{.name = "big.result", .description = "Big."});
    std::string text;
    for (int i = 0; i < 600; ++i)
    {
        text += "\xc3\xbc"; // ü
    }
    provider.result = R"({"text":")" + text + R"("})";
    McpServerOptions options;
    options.maxText = 101;
    test::MemoryTransport transport;
    McpServer server(provider, transport, options);
    handleOne(server, transport, kInitialize);
    const JsonDocument reply =
        handleOne(server, transport,
                  R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"big_result"}})");
    const JsonValue result = reply.root().find("result");
    CHECK(result.find("structuredContent").find("text").asString() == text);
    const std::string_view summary = result.find("content")[0].find("text").asString().value_or("");
    const usize cut = summary.find("\n... truncated: ");
    REQUIRE(cut != std::string_view::npos);
    CHECK(cut <= 101);
    CHECK(cut >= 100);
    CHECK(summary.find("full result is in structuredContent") != std::string_view::npos);
    CHECK(result.find("isError").asBool() == false);
}

TEST_CASE("PNG images become image content up to the size cap; larger ones are replaced by a note")
{
    // 1x1 and 2048x16 PNG headers (signature, IHDR) in base64; the pixel data does not matter here.
    const auto png = [](u32 width, u32 height) {
        const u8 header[24] = {0x89,
                               'P',
                               'N',
                               'G',
                               '\r',
                               '\n',
                               0x1A,
                               '\n',
                               0,
                               0,
                               0,
                               13,
                               'I',
                               'H',
                               'D',
                               'R',
                               static_cast<u8>(width >> 24),
                               static_cast<u8>(width >> 16),
                               static_cast<u8>(width >> 8),
                               static_cast<u8>(width),
                               static_cast<u8>(height >> 24),
                               static_cast<u8>(height >> 16),
                               static_cast<u8>(height >> 8),
                               static_cast<u8>(height)};
        constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (usize i = 0; i < 24; i += 3)
        {
            const u32 value = (u32{header[i]} << 16) | (u32{header[i + 1]} << 8) | u32{header[i + 2]};
            out += kAlphabet[(value >> 18) & 63];
            out += kAlphabet[(value >> 12) & 63];
            out += kAlphabet[(value >> 6) & 63];
            out += kAlphabet[value & 63];
        }
        return out;
    };
    test::FakeProvider provider;
    provider.catalogValue.commands.push_back(CommandSpec{.name = "big.result", .description = "Images."});
    provider.result = R"({"ok":true,"_images":[{"mimeType":"image/png","data":")" + png(1, 1) +
                      R"("},{"mimeType":"image/png","data":")" + png(2048, 16) +
                      R"("},{"mimeType":"image/png","data":"bm90IGEgcG5n"}]})";
    test::MemoryTransport transport;
    McpServer server(provider, transport);
    handleOne(server, transport, kInitialize);
    const JsonDocument reply =
        handleOne(server, transport,
                  R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"big_result"}})");
    const JsonValue result = reply.root().find("result");
    CHECK(result.find("structuredContent").find("_images").isAbsent());
    CHECK(result.find("structuredContent").find("ok").asBool() == true);
    const JsonValue content = result.find("content");
    REQUIRE(content.size() == 4);
    CHECK(content[1].find("type").asString() == "image");
    CHECK(content[1].find("data").asString() == png(1, 1));
    CHECK(content[2].find("text").asString().value_or("").find("2048x16 exceeds 1024") !=
          std::string_view::npos);
    CHECK(content[3].find("text").asString() == "image omitted: not a valid PNG");
}

TEST_CASE("a changed catalogue notifies legacy clients and modern subscribers of new tools")
{
    test::FakeProvider provider;
    test::MemoryTransport transport;
    McpServer server(provider, transport);
    handleOne(server, transport, kInitialize);
    transport.sent.clear();
    server.handle(
        R"({"jsonrpc":"2.0","id":"sub","method":"subscriptions/listen","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"},"notifications":{"toolsListChanged":true}}})");
    REQUIRE(transport.sent.size() == 1);
    CHECK(transport.sent[0].find("notifications/subscriptions/acknowledged") != std::string::npos);

    // The same catalogue under a new revision changes nothing.
    ++provider.revision;
    transport.sent.clear();
    server.refresh();
    CHECK(transport.sent.empty());

    provider.catalogValue.commands.push_back(CommandSpec{.name = "test.added", .description = "New."});
    ++provider.revision;
    transport.sent.clear();
    server.refresh();
    REQUIRE(transport.sent.size() == 2);
    CHECK(transport.sent[0] == R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})");
    CHECK(
        transport.sent[1] ==
        R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed","params":{"_meta":{"io.modelcontextprotocol/subscriptionId":"sub"}}})");
    CHECK(findTool(server.tools(), "test_added") != nullptr);

    transport.sent.clear();
    server.close();
    REQUIRE(transport.sent.size() == 1);
    CHECK(test::parseJson(transport.sent[0]).root().find("id").asString() == "sub");
}

TEST_CASE("clients that identify themselves are reported once per change")
{
    test::FakeProvider provider;
    test::MemoryTransport transport;
    std::vector<McpClientInfo> seen;
    McpServerOptions options;
    options.clientChanged = [&](const McpClientInfo& client) {
        seen.push_back(client);
    };
    McpServer server(provider, transport, options);
    constexpr std::string_view kModern =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"claude-code","version":"3.0"}}}})";
    server.handle(kModern);
    server.handle(kModern);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].name == "claude-code");
    CHECK(seen[0].era == McpEra::Modern);
    CHECK(seen[0].protocolVersion == "2026-07-28");
    server.handle(kInitialize);
    REQUIRE(seen.size() == 2);
    CHECK(seen[1].era == McpEra::Legacy);
    CHECK(server.client().name == "t");
}

TEST_CASE("the editor reports connected clients and an activity ring of their requests")
{
    test::RpcHarness rpc;
    JsonDocument named = rpc.call(
        "editor.set_client",
        R"({"client":"illusion_mcp","agent":"claude-code","agentVersion":"3.0","era":"2025-06-18"})");
    CHECK(named.root().find("result").find("origin").asString() == "rpc:illusion_mcp");
    CHECK(rpc.call("editor.set_client", R"({"agent":7})").root().find("error").find("code").asI64() == 1202);
    CHECK(rpc.call("editor.set_client", R"({"era":")" + std::string(65, 'x') + R"("})")
              .root()
              .find("error")
              .find("code")
              .asI64() == 1202);
    rpc.call("entity.create", R"({"name":"Board"})");
    rpc.call("entity.get", R"({"entity":"/Nope"})");

    const JsonDocument clients = rpc.call("editor.clients");
    const JsonValue list = clients.root().find("result").find("clients");
    REQUIRE(list.size() == 1);
    CHECK(list[0].find("client").asString() == "illusion_mcp");
    CHECK(list[0].find("agent").asString() == "claude-code");
    CHECK(list[0].find("era").asString() == "2025-06-18");
    CHECK(list[0].find("requests").asU64() == 5);

    const JsonDocument activity = rpc.call("editor.activity", R"({"since":0})");
    const JsonValue entries = activity.root().find("result").find("entries");
    REQUIRE(entries.size() == 5);
    CHECK(entries[3].find("method").asString() == "entity.create");
    CHECK(entries[3].find("code").asI64() == 0);
    CHECK(entries[3].find("client").asString() == "illusion_mcp");
    CHECK(entries[4].find("method").asString() == "entity.get");
    CHECK(entries[4].find("code").asI64() == 1200);
    const u64 last = entries[4].find("seq").asU64().value_or(0);
    CHECK(rpc.call("editor.activity", R"({"since":)" + std::to_string(last) + "}")
              .root()
              .find("result")
              .find("entries")
              .size() == 0);
    CHECK(rpc.call("editor.activity", R"({"limit":0})").root().find("error").find("code").asI64() == 1202);
    // The entity created over RPC carries the connection's origin.
    REQUIRE(rpc.fixture.editor->history().undoStep() != nullptr);
    CHECK(rpc.fixture.editor->history().undoStep()->origin == "rpc:illusion_mcp");
}
