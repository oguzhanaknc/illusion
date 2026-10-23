#include <illusion/core/Json.hpp>
#include <illusion/editor/client/RpcClient.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <cstring>

namespace illusion::editor::client
{
namespace
{

// The editor's stdin and stdout, seen through a Process, as an RpcStream.
class ChildStream final : public rpc::RpcStream
{
public:
    explicit ChildStream(Process& process) noexcept
        : m_process(process)
    {
    }

    rpc::ReadStatus read(std::span<char> buffer, usize& count, std::chrono::milliseconds timeout) override
    {
        count = 0;
        if (m_closed)
        {
            return rpc::ReadStatus::Closed;
        }
        if (m_pending.empty())
        {
            std::string line;
            if (!m_process.readLine(ProcessStream::Output, line, timeout))
            {
                return m_process.drained(ProcessStream::Output) ? rpc::ReadStatus::Closed
                                                                : rpc::ReadStatus::Timeout;
            }
            m_pending = std::move(line);
            m_pending += '\n';
        }
        count = std::min(buffer.size(), m_pending.size());
        std::memcpy(buffer.data(), m_pending.data(), count);
        m_pending.erase(0, count);
        return rpc::ReadStatus::Data;
    }

    bool write(std::string_view bytes) override { return !m_closed && m_process.writeInput(bytes); }

    void close() noexcept override
    {
        m_closed = true;
        m_process.closeInput();
    }

private:
    Process& m_process;
    std::string m_pending;
    bool m_closed = false;
};

std::chrono::milliseconds remaining(std::chrono::milliseconds timeout,
                                    std::chrono::steady_clock::time_point start)
{
    if (timeout == std::chrono::milliseconds::max())
    {
        return timeout;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return elapsed >= timeout ? std::chrono::milliseconds(0) : timeout - elapsed;
}

} // namespace

RpcClient::RpcClient() noexcept = default;
RpcClient::RpcClient(RpcClient&& other) noexcept = default;

RpcClient& RpcClient::operator=(RpcClient&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_stream = std::move(other.m_stream);
        m_process = std::move(other.m_process);
        m_framer = std::move(other.m_framer);
        m_notifications = std::move(other.m_notifications);
        m_lines = std::move(other.m_lines);
        m_nextId = other.m_nextId;
        m_closed = other.m_closed;
    }
    return *this;
}

RpcClient::~RpcClient()
{
    close();
}

RpcClient RpcClient::overStream(std::unique_ptr<rpc::RpcStream> stream)
{
    RpcClient client;
    client.m_stream = std::move(stream);
    return client;
}

Expected<RpcClient, std::string> RpcClient::connect(const std::filesystem::path& socket,
                                                    std::string_view token, std::string_view client)
{
    Expected<std::unique_ptr<rpc::RpcStream>, std::string> stream = rpc::connectSocket(socket);
    if (!stream)
    {
        return unexpected(std::move(stream).error());
    }
    RpcClient connection = overStream(std::move(stream).value());
    JsonMutDocument doc;
    JsonMutValue params = doc.makeObject();
    params.add("token", doc.makeString(token));
    params.add("client", doc.makeString(client));
    doc.setRoot(params);
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return unexpected(std::string("cannot write the authentication request"));
    }
    Expected<std::string, RpcError> authenticated =
        connection.call("rpc.authenticate", text, std::chrono::milliseconds(10000));
    if (!authenticated)
    {
        return unexpected("authentication failed: " + authenticated.error().message);
    }
    return connection;
}

Expected<RpcClient, std::string> RpcClient::connectEndpoint(const std::filesystem::path& endpoint,
                                                            std::string_view client)
{
    Expected<rpc::EndpointInfo, std::string> info = rpc::readEndpointFile(endpoint);
    if (!info)
    {
        return unexpected(std::move(info).error());
    }
    if (info->transport != "unix")
    {
        return unexpected("unsupported transport '" + info->transport + "'");
    }
    return connect(utf8Path(info->address), info->token, client);
}

Expected<RpcClient, std::string> RpcClient::launch(const LaunchOptions& options)
{
    ProcessOptions process;
    process.executable = options.editor;
    process.arguments = options.arguments;
    process.arguments.emplace_back("--headless");
    process.arguments.emplace_back("--rpc-stdio");
    process.workingDirectory = options.workingDirectory;
    process.pipeInput = true;
    process.captureOutput = true;
    process.captureError = false;
    // Room for the largest message and its line break, so lines are never split.
    process.bufferCapacity = rpc::kMaxMessageSize + 1024;
    Expected<Process, std::string> started = Process::spawn(process);
    if (!started)
    {
        return unexpected(std::move(started).error());
    }
    RpcClient client;
    client.m_process = std::make_unique<Process>(std::move(started).value());
    client.m_stream = std::make_unique<ChildStream>(*client.m_process);
    return client;
}

bool RpcClient::readMessage(std::string& message, std::chrono::milliseconds timeout)
{
    const auto start = std::chrono::steady_clock::now();
    std::vector<char> buffer(64 * 1024);
    while (m_lines.empty())
    {
        if (!connected())
        {
            return false;
        }
        usize count = 0;
        const rpc::ReadStatus status = m_stream->read(buffer, count, remaining(timeout, start));
        if (status == rpc::ReadStatus::Closed)
        {
            m_closed = true;
            return false;
        }
        if (status == rpc::ReadStatus::Timeout)
        {
            return false;
        }
        if (!m_framer.feed(std::string_view(buffer.data(), count),
                           [&](std::string_view line) { m_lines.emplace_back(line); }))
        {
            m_closed = true;
            return false;
        }
    }
    message = std::move(m_lines.front());
    m_lines.pop_front();
    return true;
}

Expected<std::string, RpcError> RpcClient::call(std::string_view method, std::string_view params,
                                                std::chrono::milliseconds timeout)
{
    if (!connected())
    {
        return unexpected(RpcError{0, "not connected"});
    }
    const u64 id = m_nextId++;
    JsonMutDocument doc;
    JsonMutValue request = doc.makeObject();
    request.add("jsonrpc", doc.makeString("2.0"));
    request.add("id", doc.makeU64(id));
    request.add("method", doc.makeString(method));
    doc.setRoot(request);
    std::string line;
    if (!doc.write(line, JsonWriteOptions{.pretty = false}))
    {
        return unexpected(RpcError{0, "cannot write the request"});
    }
    // Splice the params text in as given (it is checked by the editor, which reports parse errors).
    line.pop_back();
    line += R"(,"params":)";
    line += params.empty() ? std::string_view("{}") : params;
    line += "}\n";
    if (!m_stream->write(line))
    {
        m_closed = true;
        return unexpected(RpcError{0, "the connection is closed"});
    }

    const auto start = std::chrono::steady_clock::now();
    std::string message;
    for (;;)
    {
        if (!readMessage(message, remaining(timeout, start)))
        {
            return unexpected(RpcError{0, connected() ? "no reply to " + std::string(method) + " in time"
                                                      : "the editor closed the connection"});
        }
        Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(message);
        if (!parsed || !parsed->root().isObject())
        {
            continue;
        }
        const JsonValue root = parsed->root();
        if (!root.find("method").isAbsent())
        {
            m_notifications.push_back(std::move(message));
            continue;
        }
        const JsonValue replyId = root.find("id");
        if (replyId.isNull())
        {
            // An error the editor could not attribute to a request (e.g. a parse error): it answers this one.
            const JsonValue error = root.find("error");
            return unexpected(RpcError{static_cast<i32>(error.find("code").asI64().value_or(0)),
                                       std::string(error.find("message").asString().value_or("")),
                                       error.find("data").dump(), error.dump()});
        }
        if (replyId.asU64() != id)
        {
            continue;
        }
        if (const JsonValue error = root.find("error"); !error.isAbsent())
        {
            return unexpected(RpcError{static_cast<i32>(error.find("code").asI64().value_or(0)),
                                       std::string(error.find("message").asString().value_or("")),
                                       error.find("data").dump(), error.dump()});
        }
        return root.find("result").dump();
    }
}

bool RpcClient::notify(std::string_view method, std::string_view params)
{
    if (!connected())
    {
        return false;
    }
    JsonMutDocument doc;
    JsonMutValue request = doc.makeObject();
    request.add("jsonrpc", doc.makeString("2.0"));
    request.add("method", doc.makeString(method));
    doc.setRoot(request);
    std::string line;
    if (!doc.write(line, JsonWriteOptions{.pretty = false}))
    {
        return false;
    }
    line.pop_back();
    line += R"(,"params":)";
    line += params.empty() ? std::string_view("{}") : params;
    line += "}\n";
    return m_stream->write(line);
}

std::optional<std::string> RpcClient::nextNotification(std::chrono::milliseconds timeout)
{
    const auto start = std::chrono::steady_clock::now();
    while (m_notifications.empty())
    {
        std::string message;
        if (!readMessage(message, remaining(timeout, start)))
        {
            return std::nullopt;
        }
        Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(message);
        if (parsed && !parsed->root().find("method").isAbsent())
        {
            m_notifications.push_back(std::move(message));
        }
    }
    std::string next = std::move(m_notifications.front());
    m_notifications.pop_front();
    return next;
}

std::optional<i32> RpcClient::close(std::chrono::milliseconds timeout)
{
    std::optional<i32> code;
    if (m_process)
    {
        if (connected())
        {
            (void)call("editor.quit", "{}", std::chrono::milliseconds(10000));
        }
        if (m_stream)
        {
            m_stream->close();
        }
        code = m_process->wait(timeout);
        if (!code)
        {
            m_process->terminate();
            code = m_process->wait(std::chrono::milliseconds::max());
        }
    }
    else if (m_stream)
    {
        m_stream->close();
    }
    m_stream.reset();
    m_process.reset();
    m_closed = true;
    return code;
}

} // namespace illusion::editor::client
