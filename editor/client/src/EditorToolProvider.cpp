#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/client/EditorToolProvider.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>
#include <illusion/platform/FileSystem.hpp>

namespace illusion::editor::client
{
namespace
{

constexpr std::chrono::milliseconds kSetupTimeout{30000};
constexpr i32 kInternalError = 1500;

std::string jsonText(const JsonMutDocument& doc)
{
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return "{}";
    }
    return text;
}

std::string pathText(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace

EditorToolProvider::EditorToolProvider(EditorToolProviderOptions options)
    : m_options(std::move(options))
{
}

EditorToolProvider::~EditorToolProvider()
{
    close();
}

Expected<void, std::string> EditorToolProvider::connect()
{
    if (m_options.mode == BridgeMode::Launch)
    {
        return open(true);
    }
    if (m_options.mode == BridgeMode::Attach)
    {
        return open(false);
    }
    // Auto: attach to a live editor of the project, otherwise launch one.
    if (!m_options.project.empty())
    {
        if (Expected<void, std::string> attached = open(false); attached)
        {
            return attached;
        }
    }
    return open(true);
}

Expected<void, std::string> EditorToolProvider::open(bool launch)
{
    m_client.close();
    m_attached = false;
    if (launch)
    {
        LaunchOptions options = m_options.launch;
        options.captureLog = true;
        options.isEditor = true;
        const std::filesystem::path project = m_projectRoot.empty() ? m_options.project : m_projectRoot;
        if (!project.empty())
        {
            options.arguments.emplace_back("--project");
            options.arguments.push_back(pathText(project));
        }
        Expected<RpcClient, std::string> started = RpcClient::launch(options);
        if (!started)
        {
            return unexpected("cannot start the editor: " + started.error());
        }
        m_client = std::move(started).value();
        JsonMutDocument doc;
        JsonMutValue params = doc.makeObject();
        params.add("client", doc.makeString(m_options.client));
        doc.setRoot(params);
        if (Expected<std::string, RpcError> named =
                m_client.call("editor.set_client", jsonText(doc), kSetupTimeout);
            !named)
        {
            drainLog();
            m_client.close();
            return unexpected("the editor did not start: " + named.error().message);
        }
    }
    else
    {
        if (m_options.project.empty())
        {
            return unexpected(std::string("attaching needs --project (its endpoint file names the editor)"));
        }
        Expected<RpcClient, std::string> connected =
            RpcClient::connectEndpoint(rpc::endpointFilePath(m_options.project), m_options.client);
        if (!connected)
        {
            return unexpected("cannot attach to the editor: " + connected.error());
        }
        m_client = std::move(connected).value();
        m_attached = true;
    }
    if (Expected<void, std::string> loaded = loadCatalog(); !loaded)
    {
        m_client.close();
        return loaded;
    }
    (void)m_client.call("editor.subscribe", R"({"topics":["commands.changed"],"mode":"push"})",
                        kSetupTimeout);
    m_everConnected = true;
    sendAgent();
    ILL_LOG_INFO("mcp", "{} the editor ({} commands)", m_attached ? "attached to" : "launched",
                 m_catalog.commands.size());
    return {};
}

Expected<void, std::string> EditorToolProvider::loadCatalog()
{
    Expected<std::string, RpcError> document = m_client.call("rpc.discover", "{}", kSetupTimeout);
    if (!document)
    {
        return unexpected("cannot read the command catalogue: " + document.error().message);
    }
    if (*document == m_catalogText)
    {
        return {};
    }
    Expected<mcp::Catalog, std::string> catalog = mcp::catalogFromOpenRpc(*document);
    if (!catalog)
    {
        return unexpected(std::move(catalog).error());
    }
    m_catalog = std::move(catalog).value();
    m_catalogText = std::move(document).value();
    ++m_revision;
    return {};
}

void EditorToolProvider::setAgent(const mcp::McpClientInfo& agent)
{
    m_agent = agent;
    sendAgent();
}

void EditorToolProvider::sendAgent()
{
    if (!m_client.connected() || m_agent.era == mcp::McpEra::None)
    {
        return;
    }
    JsonMutDocument doc;
    JsonMutValue params = doc.makeObject();
    params.add("client", doc.makeString(m_options.client));
    params.add("agent", doc.makeString(m_agent.name.substr(0, 64)));
    params.add("agentVersion", doc.makeString(m_agent.version.substr(0, 64)));
    params.add("era", doc.makeString(m_agent.protocolVersion));
    doc.setRoot(params);
    (void)m_client.call("editor.set_client", jsonText(doc), kSetupTimeout);
}

void EditorToolProvider::drainLog()
{
    while (std::optional<std::string> line = m_client.readLog())
    {
        if (m_options.log)
        {
            m_options.log(*line);
        }
        if (m_options.logLines == 0)
        {
            continue;
        }
        if (m_log.size() == m_options.logLines)
        {
            m_log.pop_front();
        }
        m_log.push_back(std::move(*line));
    }
}

void EditorToolProvider::handleNotifications()
{
    while (std::optional<std::string> message = m_client.takeNotification())
    {
        const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(*message);
        if (parsed && parsed->root().find("method").asString() == "event" &&
            parsed->root().find("params").find("topic").asString() == "commands.changed")
        {
            m_catalogDirty = true;
        }
    }
}

void EditorToolProvider::poll()
{
    ILL_PROFILE_SCOPE();
    if (!m_client.connected())
    {
        return;
    }
    drainLog();
    while (std::optional<std::string> message = m_client.nextNotification(std::chrono::milliseconds(0)))
    {
        const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(*message);
        if (parsed && parsed->root().find("params").find("topic").asString() == "commands.changed")
        {
            m_catalogDirty = true;
        }
    }
    Process* process = m_client.process();
    if (!m_client.connected() || (process != nullptr && process->poll().has_value()))
    {
        // The editor ended between calls; the next call starts it again.
        drainLog();
        m_lastExit = process != nullptr ? process->poll() : std::nullopt;
        m_endedIdle = true;
        ILL_LOG_WARN("mcp", "the editor ended (exit code {}); it starts again with the next call",
                     m_lastExit.value_or(-1));
        m_client.close(std::chrono::milliseconds(0));
        return;
    }
    if (m_catalogDirty)
    {
        m_catalogDirty = false;
        if (Expected<void, std::string> loaded = loadCatalog(); !loaded)
        {
            ILL_LOG_WARN("mcp", "{}", loaded.error());
        }
    }
}

bool EditorToolProvider::ensureConnected(std::vector<std::string>& notes, mcp::CallResult& failure)
{
    if (m_client.connected())
    {
        return true;
    }
    const bool relaunch = m_everConnected && !m_attached;
    Expected<void, std::string> opened = m_everConnected ? open(!m_attached) : connect();
    if (!opened)
    {
        JsonMutDocument doc;
        JsonMutValue error = doc.makeObject();
        error.add("code", doc.makeI64(kInternalError));
        error.add("message", doc.makeString(opened.error()));
        JsonMutValue data = error.addObject("data");
        data.add("kind", doc.makeString("editor_unavailable"));
        data.add("errors", doc.makeArray());
        data.add("suggestions", doc.makeArray());
        data.add("retryable", doc.makeBool(true));
        doc.setRoot(error);
        failure = mcp::CallResult{false, jsonText(doc), {}};
        return false;
    }
    if (!relaunch)
    {
        return true;
    }
    std::string note = m_endedIdle ? "The editor had ended (exit code " +
                                         std::to_string(m_lastExit.value_or(-1)) + ") and was started again"
                                   : std::string("The editor was started again after it ended");
    m_endedIdle = false;
    Expected<std::string, RpcError> recovered = m_client.call("project.recover", "{}", kSetupTimeout);
    if (recovered)
    {
        const Expected<JsonDocument, JsonError> result = JsonDocument::parse(*recovered);
        const u64 operations = result ? result->root().find("operations").asU64().value_or(0) : 0;
        note +=
            "; project.recover replayed " + std::to_string(operations) + " operations of its crash journal.";
    }
    else
    {
        note += "; nothing was recovered (" + recovered.error().message + ").";
    }
    ILL_LOG_INFO("mcp", "{}", note);
    notes.push_back(std::move(note));
    return true;
}

mcp::CallResult EditorToolProvider::lost(std::string_view method)
{
    Process* process = m_client.process();
    std::optional<i32> exitCode;
    if (process != nullptr)
    {
        exitCode = process->wait(std::chrono::milliseconds(2000));
    }
    drainLog();
    const bool crashed = !m_attached;
    m_client.close(std::chrono::milliseconds(0));

    JsonMutDocument doc;
    JsonMutValue error = doc.makeObject();
    error.add("code", doc.makeI64(kInternalError));
    error.add(
        "message",
        doc.makeString(crashed ? "the editor ended unexpectedly while running " + std::string(method) +
                                     (exitCode ? " (exit code " + std::to_string(*exitCode) + ")" : "")
                               : "the connection to the editor closed while running " + std::string(method)));
    JsonMutValue data = error.addObject("data");
    data.add("kind", doc.makeString(crashed ? "editor_crashed" : "editor_unavailable"));
    data.add("errors", doc.makeArray());
    data.add("suggestions", doc.makeArray());
    data.add("retryable", doc.makeBool(true));
    if (exitCode)
    {
        data.add("exitCode", doc.makeI64(*exitCode));
    }
    JsonMutValue log = data.addArray("log");
    for (const std::string& line : m_log)
    {
        log.push(doc.makeString(line));
    }
    doc.setRoot(error);
    ILL_LOG_ERROR("mcp", "{}", error.find("message").asString().value_or(""));
    return mcp::CallResult{false, jsonText(doc), {}};
}

mcp::CallResult EditorToolProvider::call(std::string_view method, std::string_view params,
                                         mcp::CallControl& control)
{
    ILL_PROFILE_SCOPE();
    std::vector<std::string> notes;
    mcp::CallResult failure;
    if (!ensureConnected(notes, failure))
    {
        return failure;
    }
    const std::optional<u64> id = m_client.start(method, params);
    if (!id)
    {
        return lost(method);
    }
    bool cancelSent = false;
    for (;;)
    {
        std::optional<Expected<std::string, RpcError>> reply = m_client.poll(*id, m_options.pollInterval);
        while (std::optional<std::string> message = m_client.takeNotification())
        {
            const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(*message);
            if (!parsed)
            {
                continue;
            }
            const JsonValue root = parsed->root();
            const std::optional<std::string_view> name = root.find("method").asString();
            const JsonValue notification = root.find("params");
            if (name == "rpc.progress" && notification.find("id").asU64() == *id)
            {
                control.progress(notification.find("progress").asF64().value_or(0.0),
                                 notification.find("total").asF64().value_or(0.0),
                                 notification.find("message").asString().value_or(""));
            }
            else if (name == "event" && notification.find("topic").asString() == "commands.changed")
            {
                m_catalogDirty = true;
            }
        }
        drainLog();
        if (reply)
        {
            if (!*reply && reply->error().code == 0)
            {
                return lost(method);
            }
            if (!*reply)
            {
                return mcp::CallResult{false, reply->error().json, std::move(notes)};
            }
            if (method == "project.create" || method == "project.open")
            {
                const Expected<JsonDocument, JsonError> result = JsonDocument::parse(**reply);
                if (const std::optional<std::string_view> root =
                        result ? result->root().find("root").asString() : std::nullopt)
                {
                    m_projectRoot = utf8Path(*root);
                }
            }
            return mcp::CallResult{true, std::move(**reply), std::move(notes)};
        }
        if (!m_client.connected())
        {
            return lost(method);
        }
        control.service();
        if (control.cancelled() && !cancelSent)
        {
            cancelSent = m_client.notify("rpc.cancel", R"({"id":)" + std::to_string(*id) + "}");
        }
    }
}

std::optional<i32> EditorToolProvider::close()
{
    return m_client.close();
}

} // namespace illusion::editor::client
