#include <illusion/core/Json.hpp>
#include <illusion/editor/client/McpClient.hpp>
#include <illusion/editor/mcp/McpCatalog.hpp>
#include <illusion/editor/mcp/McpServer.hpp>

namespace illusion::editor::client
{
namespace
{

constexpr std::chrono::milliseconds kHandshakeTimeout{60000};

std::string jsonString(std::string_view text)
{
    JsonMutDocument doc;
    doc.setRoot(doc.makeString(text));
    std::string out;
    (void)doc.write(out, JsonWriteOptions{.pretty = false});
    return out;
}

// Adds the members @p members to the compact JSON object @p object.
std::string withMembers(std::string_view object, std::string_view members)
{
    std::string text = object.empty() ? std::string("{}") : std::string(object);
    text.pop_back();
    if (text.back() != '{')
    {
        text += ',';
    }
    text += members;
    text += '}';
    return text;
}

} // namespace

McpClient::~McpClient()
{
    close(std::chrono::milliseconds(30000));
}

Expected<McpClient, std::string> McpClient::launch(const McpLaunchOptions& options)
{
    LaunchOptions launch;
    launch.isEditor = false;
    launch.arguments = options.arguments;
    launch.workingDirectory = options.workingDirectory;
    launch.editor = options.bridge;
    Expected<RpcClient, std::string> started = RpcClient::launch(launch);
    if (!started)
    {
        return unexpected("cannot start illusion_mcp: " + started.error());
    }
    McpClient client;
    client.m_rpc = std::move(started).value();
    client.m_modern = options.modern;
    client.m_clientName = options.clientName;
    if (options.modern)
    {
        client.m_version = std::string(mcp::kModernVersion);
        Expected<std::string, RpcError> discovered =
            client.request("server/discover", "{}", kHandshakeTimeout);
        if (!discovered)
        {
            return unexpected("server/discover failed: " + discovered.error().message);
        }
        client.m_handshake = std::move(discovered).value();
        return client;
    }
    const std::string params = R"({"protocolVersion":)" + jsonString(options.legacyVersion) +
                               R"(,"capabilities":{},"clientInfo":{"name":)" +
                               jsonString(options.clientName) + R"(,"version":"1.0.0"}})";
    Expected<std::string, RpcError> initialized = client.m_rpc.call("initialize", params, kHandshakeTimeout);
    if (!initialized)
    {
        return unexpected("initialize failed: " + initialized.error().message);
    }
    const Expected<JsonDocument, JsonError> result = JsonDocument::parse(*initialized);
    client.m_version =
        result ? std::string(result->root().find("protocolVersion").asString().value_or("")) : "";
    client.m_handshake = std::move(initialized).value();
    client.m_rpc.notify("notifications/initialized", "{}");
    return client;
}

Expected<std::string, RpcError> McpClient::request(std::string_view method, std::string_view params,
                                                   std::chrono::milliseconds timeout)
{
    if (!m_modern)
    {
        return m_rpc.call(method, params, timeout);
    }
    const std::string meta = R"("_meta":{")" + std::string(mcp::kProtocolVersionKey) + R"(":)" +
                             jsonString(mcp::kModernVersion) +
                             R"(,"io.modelcontextprotocol/clientInfo":{"name":)" + jsonString(m_clientName) +
                             R"(,"version":"1.0.0"},"io.modelcontextprotocol/clientCapabilities":{}})";
    return m_rpc.call(method, withMembers(params, meta), timeout);
}

Expected<std::string, RpcError> McpClient::call(std::string_view method, std::string_view params,
                                                std::chrono::milliseconds timeout)
{
    const std::string request = R"({"name":)" + jsonString(mcp::toolName(method)) + R"(,"arguments":)" +
                                std::string(params.empty() ? std::string_view("{}") : params) + "}";
    Expected<std::string, RpcError> reply = this->request("tools/call", request, timeout);
    if (!reply)
    {
        return reply;
    }
    const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(*reply);
    if (!parsed)
    {
        return unexpected(RpcError{0, "illusion_mcp sent a result that is not JSON"});
    }
    const JsonValue structured = parsed->root().find("structuredContent");
    if (!parsed->root().find("isError").asBool().value_or(false))
    {
        return structured.dump();
    }
    // The flat MCP error back in the editor protocol's shape: {code, message, data}.
    JsonMutDocument doc;
    JsonMutValue error = doc.makeObject();
    error.add("code", doc.makeI64(structured.find("code").asI64().value_or(0)));
    error.add("message", doc.makeString(structured.find("message").asString().value_or("")));
    JsonMutValue data = error.addObject("data");
    for (const JsonMember member : structured.members())
    {
        if (member.key != "code" && member.key != "message" && member.key != "path" && member.key != "hint")
        {
            data.add(member.key, doc.copy(member.value));
        }
    }
    doc.setRoot(error);
    RpcError result;
    result.code = static_cast<i32>(structured.find("code").asI64().value_or(0));
    result.message = std::string(structured.find("message").asString().value_or(""));
    result.data = data.dump();
    result.json = error.dump();
    return unexpected(std::move(result));
}

std::optional<i32> McpClient::close(std::chrono::milliseconds timeout)
{
    return m_rpc.close(timeout);
}

} // namespace illusion::editor::client
