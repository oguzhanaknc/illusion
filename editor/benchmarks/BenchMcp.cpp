#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/mcp/McpServer.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>

#include <optional>
#include <string>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::mcp;

namespace
{

// The editor's real catalogue; every call answers with a typical entity.create result at once.
class ImmediateProvider final : public ToolProvider
{
public:
    explicit ImmediateProvider(Catalog catalog)
        : m_catalog(std::move(catalog))
    {
    }

    const Catalog& catalog() override { return m_catalog; }

    u64 catalogRevision() const noexcept override { return 1; }

    CallResult call(std::string_view, std::string_view, CallControl&) override
    {
        return CallResult{
            true,
            R"({"entity":{"id":"9f2c41aa0b7d3e15","name":"Board","path":"/Game/Board"},"_editor":{"revision":42,"transaction":17,"dryRun":false,"operations":3,"warnings":[]}})",
            {}};
    }

private:
    Catalog m_catalog;
};

class CountingTransport final : public McpTransport
{
public:
    void send(std::string_view line) override { bytes += line.size(); }

    std::optional<std::string> poll() override { return std::nullopt; }

    usize bytes = 0;
};

} // namespace

// One tools/call through the MCP server, per message: parsing the request, finding the tool among the
// editor's ~80, the call, and writing the result with its text summary and structuredContent. Budget about
// 6x the 1.6 us measured on a desktop (Release, MSVC); see Budget.hpp.
ILL_BENCHMARK("mcp/translate", 10'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 1, .crashJournal = false});
    rpc::RpcServer server(editor);
    Expected<Catalog, std::string> catalog = catalogFromOpenRpc(server.discoverDocument());
    ILL_CHECK(catalog.hasValue(), "cannot read the editor's catalogue");
    ImmediateProvider provider(std::move(catalog).value());
    CountingTransport transport;
    McpServer mcp(provider, transport);
    mcp.handle(
        R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"bench","version":"1"}}})");
    const std::string request =
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"entity_create","arguments":{"name":"Board","parent":"/Game","components":{"illusion.Transform":{"position":[4.5,9.5,0]}}},"_meta":{"progressToken":7}}})";
    bench.unit("message").run("tools/call entity_create", [&] { mcp.handle(request); });
    ankerl::nanobench::doNotOptimizeAway(transport.bytes);
}
