#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/JsonSchema.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace illusion::editor::rpc
{
namespace
{

// A client that lets this much output pile up is not reading; it is disconnected.
constexpr usize kMaxOutbox = 64u * 1024u * 1024u;
constexpr usize kReadBuffer = 64u * 1024u;
constexpr usize kMaxClientName = 64;

struct MethodEntry
{
    PermissionClass permission = PermissionClass::Read;
    bool readOnly = true;
};

// Methods the server implements itself (they need the connection), with their OpenRPC descriptions.
struct ServerMethod
{
    std::string_view name;
    PermissionClass permission;
    bool readOnly;
    std::string_view summary;
    std::string_view params; // OpenRPC content descriptors (JSON array)
    std::string_view result; // JSON Schema of the result
};

constexpr std::array kServerMethods = {
    ServerMethod{
        "editor.subscribe", PermissionClass::Read, true,
        "Subscribes this connection to editor events. Push mode sends {\"method\":\"event\",\"params\":"
        "{topic, payload}} notifications; pull mode keeps them for editor.poll_events. Example: "
        "{\"topics\":[\"scene.changed\",\"history.changed\"],\"mode\":\"pull\"}.",
        R"([{"name":"topics","schema":{"type":"array","items":{"type":"string"}}},{"name":"mode","schema":{"enum":["push","pull"]}}])",
        R"({"type":"object","properties":{"topics":{"type":"array","items":{"type":"string"}},"mode":{"type":"string"}}})"},
    ServerMethod{"editor.unsubscribe", PermissionClass::Read, true,
                 "Ends this connection's event subscription.", "[]", R"({"type":"object"})"},
    ServerMethod{
        "editor.poll_events", PermissionClass::Read, true,
        "Returns and removes the events kept for a pull subscription, oldest first. overflow is true if "
        "events were dropped; then read the scene again (scene.info, scene.tree). Example: {\"limit\":100}.",
        R"([{"name":"limit","schema":{"type":"integer","minimum":1}}])",
        R"({"type":"object","properties":{"events":{"type":"array"},"overflow":{"type":"boolean"}}})"},
    ServerMethod{"editor.quit", PermissionClass::System, false, "Ends the editor process after this reply.",
                 "[]", R"({"type":"object"})"},
};

std::string compact(const JsonMutDocument& doc)
{
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return "{}";
    }
    return text;
}

std::string jsonString(std::string_view text)
{
    JsonMutDocument doc;
    doc.setRoot(doc.makeString(text));
    return compact(doc);
}

// "Does X. Example: ..." -> "Does X."
std::string_view firstSentence(std::string_view text)
{
    const usize end = text.find(". ");
    return end == std::string_view::npos ? text : text.substr(0, end + 1);
}

std::string sanitizeClient(std::string_view name, u32 id)
{
    std::string out;
    for (const char c : name.substr(0, kMaxClientName))
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.')
        {
            out += c;
        }
    }
    return out.empty() ? "client" + std::to_string(id) : out;
}

bool topicMatches(const std::vector<std::string>& topics, std::string_view topic)
{
    return topics.empty() || std::any_of(topics.begin(), topics.end(),
                                         [&](const std::string& t) { return t == "*" || t == topic; });
}

// The OpenRPC 1.3 document of every command and server method.
std::string buildDiscover(EditorContext& editor)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("openrpc", doc.makeString("1.3.2"));
    JsonMutValue info = root.addObject("info");
    info.add("title", doc.makeString("illusion editor"));
    info.add("version", doc.makeString(kProtocolVersion));
    JsonMutValue methods = root.addArray("methods");
    for (const CommandDescriptor* command : editor.commands().commands())
    {
        JsonMutValue method = methods.pushObject();
        method.add("name", doc.makeString(command->info.name));
        method.add("summary", doc.makeString(firstSentence(command->info.agentSummary)));
        method.add("description", doc.makeString(command->info.agentSummary));
        method.add("paramStructure", doc.makeString("by-name"));

        // Each property of the params schema becomes one content descriptor.
        JsonMutDocument schemaDoc;
        schemaDoc.setRoot(writeCommandParamsSchema(schemaDoc, *command));
        const Expected<JsonDocument, JsonError> schema = JsonDocument::parse(compact(schemaDoc));
        JsonMutValue params = method.addArray("params");
        if (schema)
        {
            const JsonValue required = schema->root().find("required");
            for (const JsonMember member : schema->root().find("properties").members())
            {
                JsonMutValue param = params.pushObject();
                param.add("name", doc.makeString(member.key));
                bool isRequired = false;
                for (const JsonValue name : required.elements())
                {
                    isRequired = isRequired || name.asString() == member.key;
                }
                param.add("required", doc.makeBool(isRequired));
                param.add("schema", doc.copy(member.value));
            }
        }
        JsonMutValue result = method.addObject("result");
        result.add("name", doc.makeString("result"));
        result.add("schema", writeCommandResultSchema(doc, *command));

        JsonMutValue extension = method.addObject("x-illusion");
        extension.add("version", doc.makeU64(command->info.version));
        extension.add("category", doc.makeString(command->info.category));
        extension.add("permission", doc.makeString(permissionClassName(command->info.permission)));
        extension.add("mode", doc.makeString(commandModeName(command->info.mode)));
        JsonMutValue flags = extension.addObject("flags");
        flags.add("readOnly", doc.makeBool(command->info.flags.readOnly));
        flags.add("destructive", doc.makeBool(command->info.flags.destructive));
        flags.add("idempotent", doc.makeBool(command->info.flags.idempotent));
        flags.add("undoable", doc.makeBool(command->info.flags.undoable));
        flags.add("needsGpu", doc.makeBool(command->info.flags.needsGpu));
    }
    for (const ServerMethod& server : kServerMethods)
    {
        JsonMutValue method = methods.pushObject();
        method.add("name", doc.makeString(server.name));
        method.add("summary", doc.makeString(firstSentence(server.summary)));
        method.add("description", doc.makeString(server.summary));
        method.add("paramStructure", doc.makeString("by-name"));
        const Expected<JsonDocument, JsonError> params = JsonDocument::parse(server.params);
        const Expected<JsonDocument, JsonError> resultSchema = JsonDocument::parse(server.result);
        ILL_ASSERT(params && resultSchema, "invalid OpenRPC text of {}", server.name);
        method.add("params", doc.copy(params->root()));
        JsonMutValue result = method.addObject("result");
        result.add("name", doc.makeString("result"));
        result.add("schema", doc.copy(resultSchema->root()));
        JsonMutValue extension = method.addObject("x-illusion");
        extension.add("permission", doc.makeString(permissionClassName(server.permission)));
        extension.add("server", doc.makeBool(true));
    }
    doc.setRoot(root);
    return compact(doc);
}

} // namespace

std::string_view permissionModeName(PermissionMode mode) noexcept
{
    switch (mode)
    {
    case PermissionMode::ReadOnly:
        return "read-only";
    case PermissionMode::Ask:
        return "ask";
    case PermissionMode::Auto:
        return "auto";
    }
    return "auto";
}

std::optional<PermissionMode> parsePermissionMode(std::string_view name) noexcept
{
    for (const PermissionMode mode : {PermissionMode::ReadOnly, PermissionMode::Ask, PermissionMode::Auto})
    {
        if (permissionModeName(mode) == name)
        {
            return mode;
        }
    }
    return std::nullopt;
}

bool permits(PermissionMode mode, PermissionClass permission, bool readOnly, bool interactive) noexcept
{
    switch (mode)
    {
    case PermissionMode::ReadOnly:
        return readOnly;
    case PermissionMode::Ask:
        return permission == PermissionClass::Read || permission == PermissionClass::Edit || interactive;
    case PermissionMode::Auto:
        return true;
    }
    return false;
}

struct RpcServer::Connection
{
    u32 id = 0;
    std::unique_ptr<RpcStream> stream;
    std::string client;
    std::string origin;
    std::atomic<bool> authenticated{false};
    std::atomic<bool> open{true};
    std::atomic<bool> stopReading{false};
    std::atomic<u32> inFlight{0};

    std::mutex outMutex;
    std::condition_variable outReady;
    std::string outbox;
    bool outClosed = false;

    std::thread reader;
    std::thread writer;

    // Main thread only: the event subscription.
    bool subscribed = false;
    bool push = true;
    bool overflow = false;
    std::vector<std::string> topics;
    std::deque<std::string> events;

    // Queues @p line (one message, without the line break) for the writer.
    void send(std::string_view line)
    {
        bool tooMuch = false;
        {
            const std::lock_guard lock(outMutex);
            if (outClosed)
            {
                return;
            }
            tooMuch = outbox.size() + line.size() > kMaxOutbox;
            if (!tooMuch)
            {
                outbox.append(line);
                outbox += '\n';
                outReady.notify_all();
            }
        }
        if (tooMuch)
        {
            ILL_LOG_WARN("rpc", "closing connection {} ({}): it does not read its replies", id, client);
            abort();
        }
    }

    // No more output: the writer sends what is queued, then closes the stream.
    void finish()
    {
        stopReading.store(true);
        const std::lock_guard lock(outMutex);
        outClosed = true;
        outReady.notify_all();
    }

    // Closes at once, dropping queued output.
    void abort()
    {
        open.store(false);
        stopReading.store(true);
        stream->close();
        const std::lock_guard lock(outMutex);
        outbox.clear();
        outClosed = true;
        outReady.notify_all();
    }

    void writeLoop()
    {
        std::string batch;
        for (;;)
        {
            {
                std::unique_lock lock(outMutex);
                outReady.wait(lock, [&] { return !outbox.empty() || outClosed; });
                if (outbox.empty())
                {
                    break;
                }
                batch.swap(outbox);
                outbox.clear();
            }
            if (!stream->write(batch))
            {
                break;
            }
            batch.clear();
        }
        open.store(false);
        stream->close();
    }
};

struct RpcServer::Shared
{
    struct Work
    {
        std::shared_ptr<Connection> connection;
        Request request;
    };

    struct TokenCache
    {
        std::unordered_map<std::string, std::pair<std::string, std::string>>
            entries; // token -> method, result
        std::deque<std::string> order;
    };

    RpcServerOptions options;
    std::string token;
    std::string discover;
    std::map<std::string, MethodEntry, std::less<>> methods;
    std::vector<std::string> names;

    std::mutex queueMutex;
    std::condition_variable queueReady;
    std::deque<Work> queue;
    bool stopping = false;

    mutable std::mutex connectionsMutex;
    std::vector<std::shared_ptr<Connection>> connections;
    u32 nextId = 1;

    SocketListener listener;
    std::thread acceptor;

    // Main thread only.
    std::map<std::string, TokenCache, std::less<>> tokens;

    u32 add(std::unique_ptr<RpcStream> stream, bool trusted, std::string_view client)
    {
        std::shared_ptr<Connection> connection;
        {
            const std::lock_guard lock(connectionsMutex);
            const usize open = static_cast<usize>(std::count_if(
                connections.begin(), connections.end(), [](const auto& c) { return c->open.load(); }));
            if (stopping || open >= options.maxConnections)
            {
                const CommandError error = CommandError::make(
                    ErrorKind::LimitExceeded,
                    "the editor accepts at most " + std::to_string(options.maxConnections) + " connections");
                stream->write(errorResponse({}, error) + "\n");
                stream->close();
                return 0;
            }
            connection = std::make_shared<Connection>();
            connection->id = nextId++;
            connection->stream = std::move(stream);
            connection->authenticated.store(trusted);
            connection->client = sanitizeClient(client, connection->id);
            connection->origin = "rpc:" + connection->client;
            connections.push_back(connection);
        }
        Connection* raw = connection.get();
        raw->writer = std::thread([raw] { raw->writeLoop(); });
        raw->reader = std::thread([this, connection] { readLoop(connection); });
        return raw->id;
    }

    void readLoop(const std::shared_ptr<Connection>& connection)
    {
        Connection& c = *connection;
        LineFramer framer;
        std::vector<char> buffer(kReadBuffer);
        const auto start = std::chrono::steady_clock::now();
        while (!c.stopReading.load())
        {
            std::chrono::milliseconds timeout = std::chrono::milliseconds::max();
            if (!c.authenticated.load())
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                timeout = elapsed >= options.authenticationTimeout ? std::chrono::milliseconds(0)
                                                                   : options.authenticationTimeout - elapsed;
            }
            usize count = 0;
            const ReadStatus status = c.stream->read(buffer, count, timeout);
            if (status == ReadStatus::Closed)
            {
                break;
            }
            if (status == ReadStatus::Timeout)
            {
                if (!c.authenticated.load())
                {
                    reject(c, "authentication timed out: send rpc.authenticate first");
                    break;
                }
                continue;
            }
            const bool framed = framer.feed(std::string_view(buffer.data(), count),
                                            [&](std::string_view line) { handleLine(connection, line); });
            if (!framed)
            {
                c.send(errorResponse({}, static_cast<i32>(ProtocolError::InvalidRequest),
                                     "message exceeds the limit of " + std::to_string(kMaxMessageSize) +
                                         " bytes"));
                break;
            }
        }
        c.finish();
    }

    void reject(Connection& c, std::string message)
    {
        c.send(errorResponse({}, CommandError::make(ErrorKind::Unauthenticated, std::move(message))));
        c.finish();
    }

    void handleLine(const std::shared_ptr<Connection>& connection, std::string_view line)
    {
        Connection& c = *connection;
        if (c.stopReading.load())
        {
            return;
        }
        Expected<Request, std::string> parsed = parseRequest(line);
        if (!parsed)
        {
            if (!parsed.error().empty())
            {
                c.send(parsed.error());
            }
            if (!c.authenticated.load())
            {
                c.finish();
            }
            return;
        }
        Request& request = *parsed;
        const auto reply = [&](std::string_view response) {
            if (!request.notification())
            {
                c.send(response);
            }
        };
        if (request.method == "rpc.authenticate")
        {
            authenticate(c, request);
            return;
        }
        if (!c.authenticated.load())
        {
            ILL_LOG_WARN("rpc", "connection {} sent '{}' before rpc.authenticate; closing it", c.id,
                         request.method);
            reject(c, "authenticate first: the first message must be rpc.authenticate");
            return;
        }
        if (request.method == "rpc.discover")
        {
            reply(resultResponse(request.id, discover));
            return;
        }
        const auto method = methods.find(request.method);
        if (method == methods.end())
        {
            std::vector<std::string_view> candidates(names.begin(), names.end());
            CommandError error = CommandError::make(ErrorKind::NotFound,
                                                    "unknown method '" + std::string(request.method) + "'");
            error.suggestions = closestMatches(request.method, candidates);
            JsonMutDocument doc;
            JsonMutValue data = writeCommandError(doc, error).find("data");
            doc.setRoot(data);
            reply(errorResponse(request.id, static_cast<i32>(ProtocolError::MethodNotFound), error.message,
                                compact(doc)));
            return;
        }
        if (!permits(options.permissions, method->second.permission, method->second.readOnly))
        {
            reply(errorResponse(
                request.id,
                CommandError::make(ErrorKind::PermissionDenied,
                                   std::string(request.method) + " needs the '" +
                                       std::string(permissionClassName(method->second.permission)) +
                                       "' permission, which the permission mode '" +
                                       std::string(permissionModeName(options.permissions)) +
                                       "' does not grant")));
            return;
        }
        if (c.inFlight.load() >= options.maxInFlight)
        {
            CommandError error = CommandError::make(ErrorKind::LimitExceeded,
                                                    "at most " + std::to_string(options.maxInFlight) +
                                                        " requests per connection may wait for a reply");
            error.retryable = true;
            reply(errorResponse(request.id, error));
            return;
        }
        {
            const std::lock_guard lock(queueMutex);
            if (queue.size() >= options.queueCapacity || stopping)
            {
                CommandError error = CommandError::make(ErrorKind::Busy, "the editor is busy; try again");
                error.retryable = true;
                reply(errorResponse(request.id, error));
                return;
            }
            c.inFlight.fetch_add(1);
            queue.push_back(Work{connection, std::move(request)});
        }
        queueReady.notify_all();
        if (options.wake)
        {
            options.wake();
        }
    }

    void authenticate(Connection& c, const Request& request)
    {
        if (c.authenticated.load())
        {
            JsonMutDocument doc;
            JsonMutValue result = doc.makeObject();
            result.add("protocolVersion", doc.makeString(kProtocolVersion));
            result.add("editorVersion", doc.makeString(options.editorVersion));
            result.add("client", doc.makeString(c.client));
            doc.setRoot(result);
            if (!request.notification())
            {
                c.send(resultResponse(request.id, compact(doc)));
            }
            return;
        }
        const std::optional<std::string_view> given = request.params.find("token").asString();
        if (!given || !constantTimeEqual(*given, token))
        {
            // Never log the token, not even the wrong one.
            ILL_LOG_WARN("rpc", "connection {} failed to authenticate; closing it", c.id);
            reject(c, "wrong or missing token");
            return;
        }
        if (const std::optional<std::string_view> client = request.params.find("client").asString())
        {
            c.client = sanitizeClient(*client, c.id);
            c.origin = "rpc:" + c.client;
        }
        c.authenticated.store(true);
        ILL_LOG_INFO("rpc", "client '{}' connected (connection {})", c.client, c.id);
        JsonMutDocument doc;
        JsonMutValue result = doc.makeObject();
        result.add("protocolVersion", doc.makeString(kProtocolVersion));
        result.add("editorVersion", doc.makeString(options.editorVersion));
        result.add("client", doc.makeString(c.client));
        doc.setRoot(result);
        if (!request.notification())
        {
            c.send(resultResponse(request.id, compact(doc)));
        }
    }

    void acceptLoop()
    {
        for (;;)
        {
            {
                const std::lock_guard lock(queueMutex);
                if (stopping)
                {
                    return;
                }
            }
            std::unique_ptr<RpcStream> stream = listener.accept(std::chrono::milliseconds(200));
            if (stream)
            {
                add(std::move(stream), false, {});
            }
        }
    }
};

RpcServer::RpcServer(EditorContext& editor, RpcServerOptions options)
    : m_editor(editor)
    , m_shared(std::make_shared<Shared>())
{
    m_token = makeToken();
    ILL_CHECK(!m_token.empty(), "the system has no secure random source for the RPC token");
    m_discover = buildDiscover(editor);
    Shared& shared = *m_shared;
    shared.options = std::move(options);
    shared.token = m_token;
    shared.discover = m_discover;
    for (const CommandDescriptor* command : editor.commands().commands())
    {
        shared.methods.emplace(std::string(command->info.name),
                               MethodEntry{command->info.permission, command->info.flags.readOnly});
    }
    for (const ServerMethod& method : kServerMethods)
    {
        shared.methods.emplace(std::string(method.name), MethodEntry{method.permission, method.readOnly});
    }
    shared.methods.emplace("rpc.authenticate", MethodEntry{});
    shared.methods.emplace("rpc.discover", MethodEntry{});
    for (const auto& entry : shared.methods)
    {
        shared.names.push_back(entry.first);
    }
    m_subscription = editor.events().subscribe("*", [this](const EditorEvent& event) { deliver(event); });
}

RpcServer::~RpcServer()
{
    m_editor.events().unsubscribe(m_subscription);
    stop();
}

u32 RpcServer::addConnection(std::unique_ptr<RpcStream> stream, bool trusted, std::string_view client)
{
    return m_shared->add(std::move(stream), trusted, client);
}

void RpcServer::listen(SocketListener listener)
{
    Shared& shared = *m_shared;
    ILL_ASSERT(!shared.acceptor.joinable(), "the RPC server listens on one socket");
    shared.listener = std::move(listener);
    shared.acceptor = std::thread([&shared] { shared.acceptLoop(); });
}

usize RpcServer::pump(std::chrono::microseconds budget)
{
    ILL_PROFILE_SCOPE();
    Shared& shared = *m_shared;
    const auto start = std::chrono::steady_clock::now();
    usize ran = 0;
    for (;;)
    {
        Shared::Work work;
        {
            const std::lock_guard lock(shared.queueMutex);
            if (shared.queue.empty())
            {
                break;
            }
            work = std::move(shared.queue.front());
            shared.queue.pop_front();
        }
        Connection& connection = *work.connection;
        if (connection.open.load())
        {
            std::string response = execute(connection, work.request);
            if (!work.request.notification())
            {
                connection.send(response);
            }
        }
        connection.inFlight.fetch_sub(1);
        ++ran;
        if (budget != std::chrono::microseconds::max() && std::chrono::steady_clock::now() - start >= budget)
        {
            break;
        }
    }
    reap();
    return ran;
}

bool RpcServer::waitForWork(std::chrono::milliseconds timeout)
{
    Shared& shared = *m_shared;
    std::unique_lock lock(shared.queueMutex);
    return shared.queueReady.wait_for(lock, timeout, [&] {
        return !shared.queue.empty() || shared.stopping;
    }) && !shared.queue.empty();
}

bool RpcServer::isOpen(u32 id) const
{
    const std::lock_guard lock(m_shared->connectionsMutex);
    return std::any_of(m_shared->connections.begin(), m_shared->connections.end(),
                       [&](const auto& c) { return c->id == id && c->open.load(); });
}

usize RpcServer::connectionCount() const
{
    const std::lock_guard lock(m_shared->connectionsMutex);
    return static_cast<usize>(std::count_if(m_shared->connections.begin(), m_shared->connections.end(),
                                            [](const auto& c) { return c->open.load(); }));
}

void RpcServer::stop()
{
    Shared& shared = *m_shared;
    {
        const std::lock_guard lock(shared.queueMutex);
        shared.stopping = true;
        shared.queue.clear();
    }
    shared.queueReady.notify_all();
    shared.listener.close();
    if (shared.acceptor.joinable())
    {
        shared.acceptor.join();
    }
    shared.listener = SocketListener();
    std::vector<std::shared_ptr<Connection>> connections;
    {
        const std::lock_guard lock(shared.connectionsMutex);
        connections.swap(shared.connections);
    }
    for (const std::shared_ptr<Connection>& connection : connections)
    {
        connection->finish();
    }
    for (const std::shared_ptr<Connection>& connection : connections)
    {
        // The writer sends what is queued (replies to editor.quit), then closes the stream, which ends the
        // reader.
        if (connection->writer.joinable())
        {
            connection->writer.join();
        }
        connection->abort();
        if (connection->reader.joinable())
        {
            connection->reader.join();
        }
    }
}

void RpcServer::reap()
{
    std::vector<std::shared_ptr<Connection>> closed;
    {
        const std::lock_guard lock(m_shared->connectionsMutex);
        auto& connections = m_shared->connections;
        for (auto it = connections.begin(); it != connections.end();)
        {
            if (!(*it)->open.load())
            {
                closed.push_back(std::move(*it));
                it = connections.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (const std::shared_ptr<Connection>& connection : closed)
    {
        connection->abort();
        if (connection->writer.joinable())
        {
            connection->writer.join();
        }
        if (connection->reader.joinable())
        {
            connection->reader.join();
        }
    }
}

void RpcServer::deliver(const EditorEvent& event)
{
    const std::string object =
        R"({"topic":)" + jsonString(event.topic) + R"(,"payload":)" + event.payload + "}";
    std::vector<std::shared_ptr<Connection>> connections;
    {
        const std::lock_guard lock(m_shared->connectionsMutex);
        connections = m_shared->connections;
    }
    for (const std::shared_ptr<Connection>& connection : connections)
    {
        if (!connection->subscribed || !connection->open.load() ||
            !topicMatches(connection->topics, event.topic))
        {
            continue;
        }
        if (connection->push)
        {
            connection->send(notification("event", object));
        }
        else if (connection->events.size() < m_shared->options.eventQueueCapacity)
        {
            connection->events.push_back(object);
        }
        else
        {
            connection->overflow = true;
        }
    }
}

std::string RpcServer::execute(Connection& connection, const Request& request)
{
    ILL_PROFILE_SCOPE();
    const std::string_view method = request.method;
    const JsonValue params = request.params;
    if (method == "editor.subscribe")
    {
        std::vector<std::string> topics;
        const JsonValue list = params.find("topics");
        if (!list.isAbsent() && !list.isArray())
        {
            return errorResponse(request.id,
                                 CommandError::invalid("/topics", "must be an array of topic names"));
        }
        for (const JsonValue topic : list.elements())
        {
            if (!topic.isString())
            {
                return errorResponse(request.id,
                                     CommandError::invalid("/topics", "must be an array of topic names"));
            }
            topics.emplace_back(*topic.asString());
        }
        const std::string_view mode = params.find("mode").asString().value_or("push");
        if (mode != "push" && mode != "pull")
        {
            return errorResponse(request.id, CommandError::invalid("/mode", "must be \"push\" or \"pull\""));
        }
        connection.subscribed = true;
        connection.push = mode == "push";
        connection.topics = std::move(topics);
        connection.events.clear();
        connection.overflow = false;
        JsonMutDocument doc;
        JsonMutValue result = doc.makeObject();
        JsonMutValue names = result.addArray("topics");
        for (const std::string& topic : connection.topics)
        {
            names.push(doc.makeString(topic));
        }
        result.add("mode", doc.makeString(mode));
        doc.setRoot(result);
        return resultResponse(request.id, compact(doc));
    }
    if (method == "editor.unsubscribe")
    {
        connection.subscribed = false;
        connection.topics.clear();
        connection.events.clear();
        connection.overflow = false;
        return resultResponse(request.id, "{}");
    }
    if (method == "editor.poll_events")
    {
        const JsonValue limitValue = params.find("limit");
        const std::optional<u64> limit = limitValue.isAbsent() ? std::optional<u64>(256) : limitValue.asU64();
        if (!limit || *limit == 0)
        {
            return errorResponse(request.id, CommandError::invalid("/limit", "must be a positive integer"));
        }
        std::string result = R"({"events":[)";
        usize taken = 0;
        while (!connection.events.empty() && taken < *limit)
        {
            result += taken++ == 0 ? "" : ",";
            result += connection.events.front();
            connection.events.pop_front();
        }
        result += R"(],"overflow":)";
        result += connection.overflow ? "true" : "false";
        result += '}';
        connection.overflow = false;
        return resultResponse(request.id, result);
    }
    if (method == "editor.quit")
    {
        m_quit = true;
        return resultResponse(request.id, "{}");
    }

    // A repeated clientToken gets the first reply again instead of running the command twice.
    const std::optional<std::string_view> clientToken = params.find("clientToken").asString();
    Shared::TokenCache* cache = nullptr;
    if (clientToken && !clientToken->empty())
    {
        cache = &m_shared->tokens[connection.client];
        if (const auto hit = cache->entries.find(std::string(*clientToken)); hit != cache->entries.end())
        {
            if (hit->second.first != method)
            {
                return errorResponse(
                    request.id,
                    CommandError::invalid("/clientToken", "was already used for " + hit->second.first));
            }
            return resultResponse(request.id, hit->second.second);
        }
    }
    CommandOptions options;
    options.origin = connection.origin;
    Expected<std::string, CommandError> result = m_editor.executeJson(method, params, options);
    if (!result)
    {
        return errorResponse(request.id, result.error());
    }
    if (cache != nullptr)
    {
        cache->entries.emplace(std::string(*clientToken), std::make_pair(std::string(method), *result));
        cache->order.emplace_back(*clientToken);
        while (cache->order.size() > m_shared->options.idempotencyTokens)
        {
            cache->entries.erase(cache->order.front());
            cache->order.pop_front();
        }
    }
    return resultResponse(request.id, *result);
}

} // namespace illusion::editor::rpc
