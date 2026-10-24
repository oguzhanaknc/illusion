#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/mcp/McpServer.hpp>

#include <algorithm>
#include <array>
#include <charconv>

namespace illusion::editor::mcp
{
namespace
{

// JSON-RPC and MCP error codes.
constexpr i32 kInvalidRequest = -32600;
constexpr i32 kMethodNotFound = -32601;
constexpr i32 kInvalidParams = -32602;
constexpr i32 kLegacyResourceNotFound = -32002;
constexpr i32 kCommandNotFound = 1200;

constexpr std::string_view kSubscriptionKey = "io.modelcontextprotocol/subscriptionId";
constexpr std::string_view kServerInfoKey = "io.modelcontextprotocol/serverInfo";
constexpr std::string_view kClientInfoKey = "io.modelcontextprotocol/clientInfo";

constexpr std::string_view kInstructions =
    "This server controls the illusion game engine editor. Every tool is an editor command.\n"
    "Workflow: discover (editor_list_commands, editor_describe, scene_tree, component_types), then create "
    "(entity_create with Components, component_set, hierarchy_set_parent, transform_set; editor_batch runs "
    "several steps as one undo step), save (scene_save), and verify (scene_tree, entity_get, scene_info).\n"
    "Units: metres, seconds, kilograms. Angles are radians or {\"deg\": n}; transform tools also take "
    "{\"euler\": [x, y, z]} in degrees. Quaternions are [x, y, z, w]; colours are linear [r, g, b] or "
    "\"#rrggbb\"; 64-bit integers are decimal strings; asset fields are paths such as "
    "\"builtin:/mesh/cube\".\n"
    "Entity references: a 16-hex id, a path of names (\"/Game/Board\"), \"name:Board\" or \"$selection\". "
    "Results name entities as {id, name, path}.\n"
    "Mutating tools take dryRun to validate without applying. A failed call returns isError with a code, a "
    "kind, the JSON Pointer of the bad parameter, a hint and suggestions. history_undo with "
    "{\"scope\": \"mine\"} undoes only your own steps.\n"
    "Read illusion://guide for details and illusion://commands for every command.";

constexpr std::string_view kGuideTail =
    "\n\n## Transactions and undo\n\n"
    "Every mutating command is one undo step. transaction_begin, transaction_commit and transaction_cancel "
    "group several commands; editor_batch does the same in one call and can refer to earlier results with "
    "\"$name.path.to.field\". history_undo {\"scope\": \"mine\"} skips steps of other clients and refuses "
    "when a later step touched the same Component.\n\n"
    "## Permissions\n\n"
    "The editor runs in a permission mode: read-only (only read tools), ask (file, build, destructive and "
    "system tools need a person's approval; denied without a window) or auto. A denied call fails with kind "
    "permission_denied.\n\n"
    "## Limits\n\n"
    "Text summaries of results are cut at about 4 KB; the full result is always in structuredContent. Ask "
    "for less with limit and cursor parameters where a tool has them. Images are at most 1024 pixels on the "
    "long side.\n\n"
    "## Crashes\n\n"
    "If the editor process ends, the failing call returns kind editor_crashed with the editor's last log "
    "lines. The next call starts the editor again and replays its crash journal (project_recover).";

std::string compact(const JsonMutDocument& doc)
{
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return "{}";
    }
    return text;
}

void appendJsonString(std::string& out, std::string_view text)
{
    constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20)
            {
                out += "\\u00";
                out += kHex[byte >> 4];
                out += kHex[byte & 0xF];
            }
            else
            {
                out += c;
            }
        }
    }
    out += '"';
}

std::string jsonString(std::string_view text)
{
    std::string out;
    appendJsonString(out, text);
    return out;
}

// Appends members (`"a":1,"b":2`) to the compact JSON object @p object.
void appendMembers(std::string& object, std::string_view members)
{
    if (object.size() < 2 || object.back() != '}')
    {
        object = "{" + std::string(members) + "}";
        return;
    }
    object.pop_back();
    if (object.back() != '{')
    {
        object += ',';
    }
    object += members;
    object += '}';
}

// The longest prefix of @p text with at most @p limit bytes that does not split a UTF-8 sequence.
std::string_view utf8Prefix(std::string_view text, usize limit) noexcept
{
    if (text.size() <= limit)
    {
        return text;
    }
    usize end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
    {
        --end;
    }
    return text.substr(0, end);
}

// Width and height of a base64 PNG, from its IHDR chunk.
std::optional<std::pair<u32, u32>> pngSize(std::string_view base64)
{
    constexpr usize kHeaderChars = 32; // 24 bytes: signature, IHDR length and type, width, height
    if (base64.size() < kHeaderChars)
    {
        return std::nullopt;
    }
    std::array<u8, 24> bytes{};
    for (usize group = 0; group < kHeaderChars / 4; ++group)
    {
        u32 value = 0;
        for (usize i = 0; i < 4; ++i)
        {
            const char c = base64[group * 4 + i];
            u32 digit = 0;
            if (c >= 'A' && c <= 'Z')
            {
                digit = static_cast<u32>(c - 'A');
            }
            else if (c >= 'a' && c <= 'z')
            {
                digit = static_cast<u32>(c - 'a' + 26);
            }
            else if (c >= '0' && c <= '9')
            {
                digit = static_cast<u32>(c - '0' + 52);
            }
            else if (c == '+')
            {
                digit = 62;
            }
            else if (c == '/')
            {
                digit = 63;
            }
            else
            {
                return std::nullopt;
            }
            value = (value << 6) | digit;
        }
        bytes[group * 3] = static_cast<u8>(value >> 16);
        bytes[group * 3 + 1] = static_cast<u8>(value >> 8);
        bytes[group * 3 + 2] = static_cast<u8>(value);
    }
    constexpr std::array<u8, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!std::equal(kSignature.begin(), kSignature.end(), bytes.begin()) || bytes[12] != 'I' ||
        bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R')
    {
        return std::nullopt;
    }
    const auto readU32 = [&](usize at) {
        return (u32{bytes[at]} << 24) | (u32{bytes[at + 1]} << 16) | (u32{bytes[at + 2]} << 8) |
               u32{bytes[at + 3]};
    };
    return std::pair<u32, u32>{readU32(16), readU32(20)};
}

std::string hintFor(std::string_view kind, const std::vector<std::string>& suggestions)
{
    if (!suggestions.empty())
    {
        std::string hint = "Did you mean ";
        for (usize i = 0; i < suggestions.size(); ++i)
        {
            hint += i == 0 ? "" : i + 1 == suggestions.size() ? " or " : ", ";
            hint += "'" + suggestions[i] + "'";
        }
        return hint + "?";
    }
    if (kind == "validation_failed")
    {
        return "Check the parameters against the tool's input schema; editor_describe returns it in full.";
    }
    if (kind == "not_found")
    {
        return "Look up existing names with scene_tree, entity_find, component_types or asset_list.";
    }
    if (kind == "conflict")
    {
        return "The scene changed since you read it: read it again, then retry.";
    }
    if (kind == "permission_denied")
    {
        return "The editor's permission mode does not allow this command.";
    }
    if (kind == "precondition_failed")
    {
        return "The current state does not allow this; save or discard changes first, or check the mode.";
    }
    if (kind == "editor_crashed")
    {
        return "Call the tool again: the editor is started again and recovers its crash journal.";
    }
    if (kind == "editor_unavailable")
    {
        return "Start the editor (or check --project), then call the tool again.";
    }
    return {};
}

struct ResourceEntry
{
    std::string_view uri;
    std::string_view name;
    std::string_view title;
    std::string_view description;
    std::string_view mimeType;
    std::string_view method; // command that reads it ("" for text the server writes)
    std::string_view params;
};

constexpr std::array kResources = {
    ResourceEntry{
        "illusion://guide", "guide", "Guide",
        "How to work with the editor: workflow, units, entity references, undo, permissions, limits.",
        "text/markdown", "", ""},
    ResourceEntry{"illusion://commands", "commands", "Commands",
                  "Every editor command with its tool name and summary.", "text/markdown",
                  "editor.list_commands", ""},
    ResourceEntry{"illusion://project", "project", "Project", "The open project: name, root, mounts.",
                  "application/json", "project.info", "{}"},
    ResourceEntry{"illusion://scene/current", "scene", "Current scene",
                  "The entity tree of the active scene with ids, names, paths and Components.",
                  "application/json", "scene.tree", "{}"},
    ResourceEntry{"illusion://scene/summary", "scene-summary", "Scene summary",
                  "The active scene's path, revision, entity count, content hash and unsaved state.",
                  "application/json", "scene.info", "{}"},
    ResourceEntry{"illusion://log/recent", "log", "Recent log", "The last 50 editor log messages.",
                  "application/json", "log.read", R"({"limit":50})"},
};

constexpr std::string_view kComponentsPrefix = "illusion://components/";

struct PromptArgument
{
    std::string_view name;
    std::string_view description;
    bool required;
};

struct PromptEntry
{
    std::string_view name;
    std::string_view title;
    std::string_view description;
    std::array<PromptArgument, 2> arguments;
};

constexpr std::array kPrompts = {
    PromptEntry{"new_game",
                "New game",
                "Start a new game: create a project, build its scene through the editor, save and verify it.",
                {PromptArgument{"name", "Name of the game, e.g. Tetris.", true},
                 PromptArgument{"idea", "What the game is about, in a sentence or two.", false}}},
    PromptEntry{
        "add_component_type",
        "Add a Component type",
        "Add a reflected Component type to the game module and use it in the scene.",
        {PromptArgument{"name", "Stable name of the type, e.g. tetris.Board.", true},
         PromptArgument{"fields", "Its fields and their types, e.g. \"width: u32, height: u32\".", false}}},
};

std::string promptText(std::string_view name, JsonValue arguments)
{
    const auto argument = [&](std::string_view key) {
        return std::string(arguments.find(key).asString().value_or(""));
    };
    if (name == "new_game")
    {
        const std::string idea = argument("idea");
        return "Create a new game called \"" + argument("name") + "\" with the illusion editor" +
               (idea.empty() ? std::string(".") : ": " + idea) +
               "\n\n1. Read illusion://guide for units and entity references.\n"
               "2. Create the project with project_create (template \"3d-starter\"), or open one with "
               "project_open.\n"
               "3. Build the scene with entity_create (with Components), hierarchy_set_parent and "
               "transform_set; "
               "group related steps with editor_batch.\n"
               "4. Save it with scene_save.\n"
               "5. Verify it with scene_tree, entity_get and scene_info, and fix what differs.";
    }
    const std::string fields = argument("fields");
    return "Add a Component type named \"" + argument("name") + "\"" +
           (fields.empty() ? std::string() : " with the fields " + fields) +
           ".\n\n1. List the existing types with component_types and look at one with component_schema.\n"
           "2. Component types are plain C++ structs in the game module, reflected with "
           "reflect<T>(\"<stable name>\") and field metadata (units, ranges). Write the struct with "
           "project_file_write in the module's source directory.\n"
           "3. Once the module is built and loaded, add the Component with component_add and set its "
           "values with component_set, then save the scene.";
}

} // namespace

std::string_view serverInstructions() noexcept
{
    return kInstructions;
}

struct McpServer::Message
{
    JsonDocument document;
    std::string id; // compact JSON; empty for a notification
    std::string method;
    JsonValue params;
    std::string progressToken; // compact JSON; empty if none
    bool modern = false;

    [[nodiscard]] bool notification() const noexcept { return id.empty(); }
};

class McpServer::Control final : public CallControl
{
public:
    Control(McpServer& server, const Message& message) noexcept
        : m_server(server)
        , m_message(message)
    {
    }

    [[nodiscard]] bool cancelled() const noexcept override { return m_cancelled; }

    void progress(f64 done, f64 total, std::string_view message) override
    {
        if (m_message.progressToken.empty())
        {
            return;
        }
        JsonMutDocument doc;
        JsonMutValue params = doc.makeObject();
        params.add("progress", doc.makeF64(done));
        if (total > 0.0)
        {
            params.add("total", doc.makeF64(total));
        }
        if (!message.empty())
        {
            params.add("message", doc.makeString(message));
        }
        doc.setRoot(params);
        std::string line =
            R"({"jsonrpc":"2.0","method":"notifications/progress","params":{"progressToken":)" +
            m_message.progressToken + ",";
        const std::string members = compact(doc);
        line.append(members, 1, std::string::npos);
        line += '}';
        m_server.m_transport.send(line);
    }

    void service() override
    {
        while (std::optional<std::string> line = m_server.m_transport.poll())
        {
            m_server.serviceLine(*line);
        }
    }

    [[nodiscard]] const std::string& id() const noexcept { return m_message.id; }

    void cancel() noexcept { m_cancelled = true; }

private:
    McpServer& m_server;
    const Message& m_message;
    bool m_cancelled = false;
};

McpServer::McpServer(ToolProvider& provider, McpTransport& transport, McpServerOptions options)
    : m_provider(provider)
    , m_transport(transport)
    , m_options(std::move(options))
{
}

McpServer::~McpServer() = default;

void McpServer::handle(std::string_view line)
{
    ILL_PROFILE_SCOPE();
    refresh();
    process(line);
    runDeferred();
}

void McpServer::runDeferred()
{
    while (!m_deferred.empty())
    {
        const std::string line = std::move(m_deferred.front());
        m_deferred.pop_front();
        process(line);
    }
    m_cancelledIds.clear();
}

void McpServer::process(std::string_view line)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(line);
    if (!parsed)
    {
        m_transport.send(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":)" +
                         jsonString("parse error: " + parsed.error().message) + "}}");
        return;
    }
    Message message;
    message.document = std::move(parsed).value();
    const JsonValue root = message.document.root();
    const JsonValue method = root.find("method");
    const JsonValue id = root.find("id");
    if (root.isObject() && method.isAbsent() &&
        (!root.find("result").isAbsent() || !root.find("error").isAbsent()))
    {
        return; // a response: this server sends no requests
    }
    message.id = id.isString() || id.isInteger() ? id.dump() : std::string("null");
    if (!root.isObject() || root.find("jsonrpc").asString() != "2.0" || !method.isString() ||
        (!id.isAbsent() && message.id == "null"))
    {
        fail(message, kInvalidRequest, "not a JSON-RPC 2.0 request (batch arrays are not supported)");
        return;
    }
    if (id.isAbsent())
    {
        message.id.clear();
    }
    message.method = std::string(*method.asString());
    message.params = root.find("params");
    if (!message.params.isAbsent() && !message.params.isObject())
    {
        fail(message, kInvalidParams, "params must be an object");
        return;
    }
    const JsonValue meta = message.params.find("_meta");
    if (const JsonValue token = meta.find("progressToken"); token.isString() || token.isInteger())
    {
        message.progressToken = token.dump();
    }
    dispatch(message);
}

void McpServer::serviceLine(std::string_view line)
{
    const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(line);
    if (!parsed || !parsed->root().isObject())
    {
        process(line); // answers the parse error now
        return;
    }
    const JsonValue root = parsed->root();
    const std::optional<std::string_view> method = root.find("method").asString();
    if (method == "notifications/cancelled")
    {
        const JsonValue requestId = root.find("params").find("requestId");
        if (m_active != nullptr && !requestId.isAbsent() && requestId.dump() == m_active->id())
        {
            m_active->cancel();
            return;
        }
        // A request that waits behind the call is dropped when its turn comes (dispatch()); a subscription
        // ends when the notification itself is handled.
        if (!requestId.isAbsent())
        {
            m_cancelledIds.push_back(requestId.dump());
        }
    }
    else if (method == "ping" && root.find("params").find("_meta").find(kProtocolVersionKey).isAbsent())
    {
        process(line); // legacy ping: answered at once
        return;
    }
    else if (!method)
    {
        return; // a response
    }
    m_deferred.emplace_back(line);
}

void McpServer::dispatch(Message& message)
{
    if (message.notification())
    {
        if (message.method == "notifications/cancelled")
        {
            cancel(message);
        }
        return; // notifications/initialized and others need nothing
    }
    if (const auto cancelled = std::find(m_cancelledIds.begin(), m_cancelledIds.end(), message.id);
        cancelled != m_cancelledIds.end())
    {
        m_cancelledIds.erase(cancelled);
        return;
    }
    const JsonValue version = message.params.find("_meta").find(kProtocolVersionKey);
    if (!version.isAbsent())
    {
        message.modern = true;
        handleModern(message, version.asString().value_or(""));
        return;
    }
    handleLegacy(message);
}

void McpServer::handleModern(Message& message, std::string_view version)
{
    if (version != kModernVersion)
    {
        JsonMutDocument doc;
        JsonMutValue data = doc.makeObject();
        JsonMutValue supported = data.addArray("supported");
        supported.push(doc.makeString(kModernVersion));
        for (const std::string_view legacy : kLegacyVersions)
        {
            supported.push(doc.makeString(legacy));
        }
        data.add("requested", doc.makeString(version));
        doc.setRoot(data);
        fail(message, kUnsupportedProtocolVersion, "Unsupported protocol version", compact(doc));
        return;
    }
    updateClient(message);
    const std::string_view method = message.method;
    if (method == "server/discover")
    {
        JsonMutDocument doc;
        JsonMutValue result = doc.makeObject();
        JsonMutValue versions = result.addArray("supportedVersions");
        versions.push(doc.makeString(kModernVersion));
        for (const std::string_view legacy : kLegacyVersions)
        {
            versions.push(doc.makeString(legacy));
        }
        JsonMutValue capabilities = result.addObject("capabilities");
        capabilities.addObject("tools").add("listChanged", doc.makeBool(true));
        capabilities.addObject("resources");
        capabilities.addObject("prompts");
        result.add("instructions", doc.makeString(kInstructions));
        doc.setRoot(result);
        reply(message, compact(doc), true, m_options.listTtlMs);
    }
    else if (method == "tools/list")
    {
        listTools(message);
    }
    else if (method == "tools/call")
    {
        callTool(message);
    }
    else if (method == "resources/list")
    {
        listResources(message);
    }
    else if (method == "resources/templates/list")
    {
        listResourceTemplates(message);
    }
    else if (method == "resources/read")
    {
        readResource(message);
    }
    else if (method == "prompts/list")
    {
        listPrompts(message);
    }
    else if (method == "prompts/get")
    {
        getPrompt(message);
    }
    else if (method == "subscriptions/listen")
    {
        listen(message);
    }
    else
    {
        fail(message, kMethodNotFound, "method not found: " + message.method);
    }
}

void McpServer::handleLegacy(Message& message)
{
    const std::string_view method = message.method;
    if (method == "initialize")
    {
        initialize(message);
        return;
    }
    if (method == "ping")
    {
        reply(message, "{}");
        return;
    }
    if (!m_legacyReady)
    {
        fail(message, kInvalidRequest,
             "send initialize first, or send requests with _meta[\"" + std::string(kProtocolVersionKey) +
                 "\"] = \"" + std::string(kModernVersion) + "\"");
        return;
    }
    if (method == "tools/list")
    {
        listTools(message);
    }
    else if (method == "tools/call")
    {
        callTool(message);
    }
    else if (method == "resources/list")
    {
        listResources(message);
    }
    else if (method == "resources/templates/list")
    {
        listResourceTemplates(message);
    }
    else if (method == "resources/read")
    {
        readResource(message);
    }
    else if (method == "prompts/list")
    {
        listPrompts(message);
    }
    else if (method == "prompts/get")
    {
        getPrompt(message);
    }
    else
    {
        fail(message, kMethodNotFound, "method not found: " + message.method);
    }
}

void McpServer::initialize(Message& message)
{
    const std::optional<std::string_view> requested = message.params.find("protocolVersion").asString();
    if (!requested)
    {
        fail(message, kInvalidParams, "initialize needs protocolVersion");
        return;
    }
    // A version this server does not speak gets the newest legacy one; the client decides whether to go on.
    std::string_view version = kLegacyVersions[0];
    for (const std::string_view legacy : kLegacyVersions)
    {
        if (legacy == *requested)
        {
            version = legacy;
        }
    }
    const JsonValue clientInfo = message.params.find("clientInfo");
    m_client.name = std::string(clientInfo.find("name").asString().value_or(""));
    m_client.version = std::string(clientInfo.find("version").asString().value_or(""));
    m_client.protocolVersion = std::string(version);
    m_client.era = McpEra::Legacy;
    m_legacyReady = true;

    JsonMutDocument doc;
    JsonMutValue result = doc.makeObject();
    result.add("protocolVersion", doc.makeString(version));
    JsonMutValue capabilities = result.addObject("capabilities");
    capabilities.addObject("tools").add("listChanged", doc.makeBool(true));
    capabilities.addObject("resources");
    capabilities.addObject("prompts");
    JsonMutValue serverInfo = result.addObject("serverInfo");
    serverInfo.add("name", doc.makeString(m_options.name));
    serverInfo.add("version", doc.makeString(m_options.version));
    result.add("instructions", doc.makeString(kInstructions));
    doc.setRoot(result);
    reply(message, compact(doc));
    if (m_options.clientChanged)
    {
        m_options.clientChanged(m_client);
    }
}

void McpServer::updateClient(const Message& message)
{
    const JsonValue info = message.params.find("_meta").find(kClientInfoKey);
    const std::string name(info.find("name").asString().value_or(m_client.name));
    const std::string version(info.find("version").asString().value_or(m_client.version));
    if (m_client.era == McpEra::Modern && name == m_client.name && version == m_client.version)
    {
        return;
    }
    m_client.name = name;
    m_client.version = version;
    m_client.protocolVersion = std::string(kModernVersion);
    m_client.era = McpEra::Modern;
    if (m_options.clientChanged)
    {
        m_options.clientChanged(m_client);
    }
}

void McpServer::refresh()
{
    if (m_built && m_provider.catalogRevision() == m_catalogRevision)
    {
        return;
    }
    const bool notify = m_built;
    std::vector<ToolDefinition> previous = std::move(m_tools);
    rebuild();
    const bool changed =
        previous.size() != m_tools.size() ||
        !std::equal(previous.begin(), previous.end(), m_tools.begin(),
                    [](const ToolDefinition& a, const ToolDefinition& b) { return a.json == b.json; });
    if (!notify || !changed)
    {
        return;
    }
    if (m_legacyReady)
    {
        m_transport.send(R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})");
    }
    for (const Subscription& subscription : m_subscriptions)
    {
        if (subscription.tools)
        {
            m_transport.send(
                R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed","params":{"_meta":{")" +
                std::string(kSubscriptionKey) + "\":" + subscription.id + "}}}");
        }
    }
}

void McpServer::rebuild()
{
    m_catalogRevision = m_provider.catalogRevision();
    m_tools = generateTools(m_provider.catalog(), m_options.tools);
    m_built = true;
}

void McpServer::reply(const Message& message, std::string result, bool cacheable, u32 ttlMs)
{
    if (message.notification())
    {
        return;
    }
    if (message.modern)
    {
        std::string members = R"("resultType":"complete","_meta":{")" + std::string(kServerInfoKey) +
                              R"(":{"name":)" + jsonString(m_options.name) + R"(,"version":)" +
                              jsonString(m_options.version) + "}}";
        if (cacheable)
        {
            members += R"(,"ttlMs":)" + std::to_string(ttlMs) + R"(,"cacheScope":"private")";
        }
        appendMembers(result, members);
    }
    m_transport.send(R"({"jsonrpc":"2.0","id":)" + message.id + R"(,"result":)" + result + "}");
}

void McpServer::fail(const Message& message, i32 code, std::string_view text, std::string_view data)
{
    if (message.notification())
    {
        return;
    }
    std::string line = R"({"jsonrpc":"2.0","id":)" + message.id + R"(,"error":{"code":)" +
                       std::to_string(code) + R"(,"message":)" + jsonString(text);
    if (!data.empty())
    {
        line += R"(,"data":)";
        line += data;
    }
    line += "}}";
    m_transport.send(line);
}

namespace
{

// The page offset of @p cursor ("" -> 0); nothing if it is not a cursor this server wrote.
std::optional<usize> pageOffset(JsonValue cursor)
{
    if (cursor.isAbsent() || cursor.isNull())
    {
        return usize{0};
    }
    const std::optional<std::string_view> text = cursor.asString();
    if (!text || text->empty())
    {
        return std::nullopt;
    }
    usize offset = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), offset);
    if (error != std::errc() || end != text->data() + text->size())
    {
        return std::nullopt;
    }
    return offset;
}

} // namespace

void McpServer::listTools(Message& message)
{
    const std::optional<usize> offset = pageOffset(message.params.find("cursor"));
    if (!offset || *offset > m_tools.size())
    {
        fail(message, kInvalidParams, "invalid cursor");
        return;
    }
    const usize pageSize = std::max<usize>(m_options.pageSize, 1);
    const usize end = std::min(m_tools.size(), *offset + pageSize);
    std::string result = R"({"tools":[)";
    for (usize i = *offset; i < end; ++i)
    {
        result += i == *offset ? "" : ",";
        result += m_tools[i].json;
    }
    result += ']';
    if (end < m_tools.size())
    {
        result += R"(,"nextCursor":")" + std::to_string(end) + "\"";
    }
    result += '}';
    reply(message, std::move(result), true, m_options.listTtlMs);
}

void McpServer::callTool(Message& message)
{
    ILL_PROFILE_SCOPE();
    const std::optional<std::string_view> name = message.params.find("name").asString();
    if (!name)
    {
        fail(message, kInvalidParams, "tools/call needs a tool name");
        return;
    }
    const ToolDefinition* tool = findTool(m_tools, *name);
    if (tool == nullptr)
    {
        fail(message, kInvalidParams, "Unknown tool: " + std::string(*name));
        return;
    }
    const JsonValue arguments = message.params.find("arguments");
    if (!arguments.isAbsent() && !arguments.isObject())
    {
        fail(message, kInvalidParams, "arguments must be an object");
        return;
    }
    std::string method = tool->method;
    std::string params;
    if (tool->name == kCallTool)
    {
        const std::optional<std::string_view> target = arguments.find("method").asString();
        const JsonValue targetParams = arguments.find("params");
        if (!target || (!targetParams.isAbsent() && !targetParams.isObject()))
        {
            fail(message, kInvalidParams, "illusion_call takes {\"method\": string, \"params\": object}");
            return;
        }
        method = std::string(*target);
        params = targetParams.isAbsent() ? std::string("{}") : targetParams.dump();
    }
    else
    {
        params = arguments.isAbsent() ? std::string("{}") : arguments.dump();
    }

    CallResult outcome;
    if (m_provider.catalog().find(method) == nullptr)
    {
        // illusion_call reaches commands only, never the server methods that act on the bridge's connection.
        outcome.ok = false;
        outcome.json =
            R"({"code":-32601,"message":)" +
            jsonString("'" + method + "' is not an editor command; illusion://commands lists them") +
            R"(,"data":{"kind":"not_found","errors":[{"path":"/method","message":"is not an editor command"}],"suggestions":[],"retryable":false}})";
    }
    else
    {
        Control control(*this, message);
        m_active = &control;
        outcome = m_provider.call(method, params, control);
        m_active = nullptr;
        if (control.cancelled())
        {
            return; // a cancelled request gets no response
        }
    }

    std::string structured;
    std::string summary;
    std::string extra; // further content blocks, each with a leading comma
    if (outcome.ok)
    {
        structured = std::move(outcome.json);
        if (structured.find("\"_images\"") != std::string::npos)
        {
            // Images move from the result into image content blocks.
            const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(structured);
            if (parsed && parsed->root().isObject())
            {
                JsonMutDocument doc;
                JsonMutValue object = doc.makeObject();
                for (const JsonMember member : parsed->root().members())
                {
                    if (member.key != "_images")
                    {
                        object.add(member.key, doc.copy(member.value));
                        continue;
                    }
                    for (const JsonValue image : member.value.elements())
                    {
                        const std::string_view mimeType =
                            image.find("mimeType").asString().value_or("image/png");
                        const std::string_view data = image.find("data").asString().value_or("");
                        const std::optional<std::pair<u32, u32>> size =
                            mimeType == "image/png" ? pngSize(data) : std::nullopt;
                        if (!size || std::max(size->first, size->second) > m_options.maxImageSize)
                        {
                            const std::string note = size ? "image omitted: " + std::to_string(size->first) +
                                                                "x" + std::to_string(size->second) +
                                                                " exceeds " +
                                                                std::to_string(m_options.maxImageSize) +
                                                                " pixels; ask for a smaller one"
                                                          : std::string("image omitted: not a valid PNG");
                            extra += R"(,{"type":"text","text":)" + jsonString(note) + "}";
                            continue;
                        }
                        extra += R"(,{"type":"image","data":)" + jsonString(data) + R"(,"mimeType":)" +
                                 jsonString(mimeType) + "}";
                    }
                }
                doc.setRoot(object);
                structured = compact(doc);
            }
        }
        summary = std::string(utf8Prefix(structured, m_options.maxText));
        if (summary.size() < structured.size())
        {
            summary += "\n... truncated: " + std::to_string(summary.size()) + " of " +
                       std::to_string(structured.size()) +
                       " bytes shown; the full result is in structuredContent. Ask for less with limit or "
                       "cursor parameters where the tool has them.";
        }
    }
    else
    {
        // {code, message, data: {kind, errors, suggestions, retryable, ...}} -> a flat error for the agent.
        const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(outcome.json);
        const JsonValue error = parsed ? parsed->root() : JsonValue();
        const JsonValue data = error.find("data");
        const i64 code = error.find("code").asI64().value_or(-32603);
        const std::string kind(
            data.find("kind").asString().value_or(code == kMethodNotFound ? "not_found" : "error"));
        const std::string text(error.find("message").asString().value_or("the command failed"));
        std::vector<std::string> suggestions;
        for (const JsonValue suggestion : data.find("suggestions").elements())
        {
            suggestions.emplace_back(suggestion.asString().value_or(""));
        }
        const JsonValue errors = data.find("errors");
        const std::string path(errors[0].find("path").asString().value_or(""));
        const std::string hint = hintFor(kind, suggestions);

        JsonMutDocument doc;
        JsonMutValue object = doc.makeObject();
        object.add("code", doc.makeI64(code));
        object.add("kind", doc.makeString(kind));
        object.add("message", doc.makeString(text));
        object.add("path", doc.makeString(path));
        object.add("hint", doc.makeString(hint));
        object.add("errors", errors.isArray() ? doc.copy(errors) : doc.makeArray());
        JsonMutValue list = object.addArray("suggestions");
        for (const std::string& suggestion : suggestions)
        {
            list.push(doc.makeString(suggestion));
        }
        object.add("retryable", doc.makeBool(data.find("retryable").asBool().value_or(false)));
        for (const JsonMember member : data.members())
        {
            if (member.key != "kind" && member.key != "errors" && member.key != "suggestions" &&
                member.key != "retryable")
            {
                object.add(member.key, doc.copy(member.value));
            }
        }
        doc.setRoot(object);
        structured = compact(doc);
        summary = "Error " + std::to_string(code) + " (" + kind + "): " + text;
        for (const JsonValue problem : errors.elements())
        {
            summary += "\n- " + std::string(problem.find("path").asString().value_or("")) + ": " +
                       std::string(problem.find("message").asString().value_or(""));
        }
        if (!hint.empty())
        {
            summary += "\nHint: " + hint;
        }
        if (const JsonValue log = data.find("log"); log.isArray() && log.size() != 0)
        {
            summary += "\nLast editor log lines:";
            for (const JsonValue entry : log.elements())
            {
                summary += "\n" + std::string(entry.asString().value_or(""));
            }
        }
        summary = std::string(utf8Prefix(summary, m_options.maxText));
    }
    for (const std::string& note : outcome.notes)
    {
        extra += R"(,{"type":"text","text":)" + jsonString(note) + "}";
    }
    std::string result = R"({"content":[{"type":"text","text":)" + jsonString(summary) + "}" + extra +
                         R"(],"structuredContent":)" + structured + R"(,"isError":)" +
                         (outcome.ok ? "false" : "true") + "}";
    reply(message, std::move(result));
}

void McpServer::listResources(Message& message)
{
    const Catalog& catalog = m_provider.catalog();
    JsonMutDocument doc;
    JsonMutValue result = doc.makeObject();
    JsonMutValue list = result.addArray("resources");
    for (const ResourceEntry& entry : kResources)
    {
        if (!entry.method.empty() && catalog.find(entry.method) == nullptr)
        {
            continue;
        }
        JsonMutValue resource = list.pushObject();
        resource.add("uri", doc.makeString(entry.uri));
        resource.add("name", doc.makeString(entry.name));
        resource.add("title", doc.makeString(entry.title));
        resource.add("description", doc.makeString(entry.description));
        resource.add("mimeType", doc.makeString(entry.mimeType));
    }
    doc.setRoot(result);
    reply(message, compact(doc), true, m_options.listTtlMs);
}

void McpServer::listResourceTemplates(Message& message)
{
    JsonMutDocument doc;
    JsonMutValue result = doc.makeObject();
    JsonMutValue list = result.addArray("resourceTemplates");
    if (m_provider.catalog().find("component.schema") != nullptr)
    {
        JsonMutValue entry = list.pushObject();
        entry.add("uriTemplate", doc.makeString("illusion://components/{stableName}"));
        entry.add("name", doc.makeString("component"));
        entry.add("title", doc.makeString("Component type"));
        entry.add(
            "description",
            doc.makeString("The JSON Schema of a Component type's value (fields, types, units as ranges), "
                           "e.g. illusion://components/illusion.Transform."));
        entry.add("mimeType", doc.makeString("application/json"));
    }
    doc.setRoot(result);
    reply(message, compact(doc), true, m_options.listTtlMs);
}

void McpServer::readResource(Message& message)
{
    const std::optional<std::string_view> uri = message.params.find("uri").asString();
    if (!uri)
    {
        fail(message, kInvalidParams, "resources/read needs a uri");
        return;
    }
    const i32 notFound = message.modern ? kInvalidParams : kLegacyResourceNotFound;
    const std::string notFoundData = R"({"uri":)" + jsonString(*uri) + "}";
    const Catalog& catalog = m_provider.catalog();
    std::string mimeType = "application/json";
    std::string text;
    u32 ttlMs = 0;
    std::string_view method;
    std::string params;
    if (uri->starts_with(kComponentsPrefix) && catalog.find("component.schema") != nullptr)
    {
        const std::string_view stableName = uri->substr(kComponentsPrefix.size());
        if (stableName.empty())
        {
            fail(message, notFound, "Resource not found", notFoundData);
            return;
        }
        method = "component.schema";
        params = R"({"type":)" + jsonString(stableName) + "}";
    }
    else
    {
        const auto entry = std::find_if(kResources.begin(), kResources.end(),
                                        [&](const ResourceEntry& resource) { return resource.uri == *uri; });
        if (entry == kResources.end() || (!entry->method.empty() && catalog.find(entry->method) == nullptr))
        {
            fail(message, notFound, "Resource not found", notFoundData);
            return;
        }
        mimeType = std::string(entry->mimeType);
        if (entry->uri == "illusion://guide")
        {
            text = "# illusion editor guide\n\n" + std::string(kInstructions) + std::string(kGuideTail);
            ttlMs = m_options.listTtlMs;
        }
        else if (entry->uri == "illusion://commands")
        {
            text = "# Editor commands\n\nEach command is the tool named after it with `.` replaced by `_`.\n";
            std::string category;
            for (const CommandSpec& command : catalog.commands)
            {
                if (command.category != category)
                {
                    category = command.category;
                    text += "\n## " + category + "\n\n";
                }
                const std::string_view summary = command.description;
                const usize sentence = summary.find(". ");
                text += "- `" + toolName(command.name) + "` (`" + command.name + "`, " + command.permission +
                        "): " +
                        std::string(sentence == std::string_view::npos ? summary
                                                                       : summary.substr(0, sentence + 1)) +
                        "\n";
            }
            ttlMs = m_options.listTtlMs;
        }
        else
        {
            method = entry->method;
            params = std::string(entry->params);
        }
    }
    if (!method.empty())
    {
        Control control(*this, message);
        m_active = &control;
        CallResult outcome = m_provider.call(method, params, control);
        m_active = nullptr;
        if (control.cancelled())
        {
            return;
        }
        if (!outcome.ok)
        {
            const Expected<JsonDocument, JsonError> error = JsonDocument::parse(outcome.json);
            const i64 code = error ? error->root().find("code").asI64().value_or(-32603) : -32603;
            const std::string reason(error ? error->root().find("message").asString().value_or("") : "");
            if (code == kCommandNotFound)
            {
                fail(message, notFound, "Resource not found: " + reason, notFoundData);
            }
            else
            {
                fail(message, static_cast<i32>(code), reason, error ? error->root().find("data").dump() : "");
            }
            return;
        }
        text = std::move(outcome.json);
    }
    std::string result = R"({"contents":[{"uri":)" + jsonString(*uri) + R"(,"mimeType":)" +
                         jsonString(mimeType) + R"(,"text":)" + jsonString(text) + "}]}";
    reply(message, std::move(result), true, ttlMs);
}

void McpServer::listPrompts(Message& message)
{
    JsonMutDocument doc;
    JsonMutValue result = doc.makeObject();
    JsonMutValue list = result.addArray("prompts");
    for (const PromptEntry& entry : kPrompts)
    {
        JsonMutValue prompt = list.pushObject();
        prompt.add("name", doc.makeString(entry.name));
        prompt.add("title", doc.makeString(entry.title));
        prompt.add("description", doc.makeString(entry.description));
        JsonMutValue arguments = prompt.addArray("arguments");
        for (const PromptArgument& argument : entry.arguments)
        {
            JsonMutValue item = arguments.pushObject();
            item.add("name", doc.makeString(argument.name));
            item.add("description", doc.makeString(argument.description));
            item.add("required", doc.makeBool(argument.required));
        }
    }
    doc.setRoot(result);
    reply(message, compact(doc), true, m_options.listTtlMs);
}

void McpServer::getPrompt(Message& message)
{
    const std::optional<std::string_view> name = message.params.find("name").asString();
    const auto entry = std::find_if(kPrompts.begin(), kPrompts.end(),
                                    [&](const PromptEntry& prompt) { return name && prompt.name == *name; });
    if (entry == kPrompts.end())
    {
        fail(message, kInvalidParams, "unknown prompt: " + std::string(name.value_or("")));
        return;
    }
    const JsonValue arguments = message.params.find("arguments");
    for (const PromptArgument& argument : entry->arguments)
    {
        const JsonValue value = arguments.find(argument.name);
        if ((argument.required && !value.isString()) || (!value.isAbsent() && !value.isString()))
        {
            fail(message, kInvalidParams,
                 "prompt " + std::string(entry->name) + " needs the string argument '" +
                     std::string(argument.name) + "'");
            return;
        }
    }
    JsonMutDocument doc;
    JsonMutValue result = doc.makeObject();
    result.add("description", doc.makeString(entry->description));
    JsonMutValue prompt = result.addArray("messages").pushObject();
    prompt.add("role", doc.makeString("user"));
    JsonMutValue content = prompt.addObject("content");
    content.add("type", doc.makeString("text"));
    content.add("text", doc.makeString(promptText(entry->name, arguments)));
    doc.setRoot(result);
    reply(message, compact(doc));
}

void McpServer::listen(Message& message)
{
    const JsonValue requested = message.params.find("notifications");
    Subscription subscription;
    subscription.id = message.id;
    subscription.tools = requested.find("toolsListChanged").asBool().value_or(false);
    m_subscriptions.push_back(subscription);
    // Only tool list changes happen here; other types are left out of the acknowledgement.
    m_transport.send(
        R"({"jsonrpc":"2.0","method":"notifications/subscriptions/acknowledged","params":{"_meta":{")" +
        std::string(kSubscriptionKey) + "\":" + message.id + R"(},"notifications":{)" +
        (subscription.tools ? R"("toolsListChanged":true)" : "") + "}}}");
}

void McpServer::cancel(const Message& message)
{
    const JsonValue requestId = message.params.find("requestId");
    if (requestId.isAbsent())
    {
        return;
    }
    const std::string id = requestId.dump();
    const auto subscription = std::find_if(m_subscriptions.begin(), m_subscriptions.end(),
                                           [&](const Subscription& s) { return s.id == id; });
    if (subscription != m_subscriptions.end())
    {
        m_subscriptions.erase(subscription);
    }
}

void McpServer::close()
{
    for (const Subscription& subscription : m_subscriptions)
    {
        m_transport.send(R"({"jsonrpc":"2.0","id":)" + subscription.id +
                         R"(,"result":{"resultType":"complete","_meta":{")" + std::string(kSubscriptionKey) +
                         "\":" + subscription.id + "}}}");
    }
    m_subscriptions.clear();
}

} // namespace illusion::editor::mcp
