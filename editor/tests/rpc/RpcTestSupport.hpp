#pragma once

// Shared setup of the editor RPC tests: an RpcServer on the fixture editor, a client end of an in-memory
// stream and helpers that send lines and wait for replies while running the server's queue on the test
// thread.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::test
{

/// Bounds waits so a broken server fails a test instead of hanging it.
inline constexpr std::chrono::milliseconds kRpcPatience{30000};

/// The client end of a connection: writes lines and reads complete reply lines.
class RpcPeer
{
public:
    explicit RpcPeer(std::unique_ptr<editor::rpc::RpcStream> stream)
        : m_stream(std::move(stream))
    {
    }

    /// Writes @p line and a line break.
    bool send(std::string_view line) { return m_stream->write(std::string(line) + "\n"); }

    /// The next line, waiting up to @p timeout; nothing on timeout or when the stream closed.
    std::optional<std::string> receive(std::chrono::milliseconds timeout = kRpcPatience)
    {
        const auto start = std::chrono::steady_clock::now();
        std::vector<char> buffer(64 * 1024);
        while (m_lines.empty())
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= timeout)
            {
                return std::nullopt;
            }
            usize count = 0;
            const editor::rpc::ReadStatus status = m_stream->read(buffer, count, timeout - elapsed);
            if (status == editor::rpc::ReadStatus::Closed)
            {
                m_closed = true;
                return std::nullopt;
            }
            if (status == editor::rpc::ReadStatus::Data)
            {
                m_framer.feed(std::string_view(buffer.data(), count),
                              [&](std::string_view line) { m_lines.emplace_back(line); });
            }
        }
        std::string line = std::move(m_lines.front());
        m_lines.pop_front();
        return line;
    }

    /// True once a read saw the stream closed.
    [[nodiscard]] bool closed() const noexcept { return m_closed; }

    /// Waits until the server closes the stream; false if it is still open after @p timeout.
    bool waitClosed(std::chrono::milliseconds timeout = kRpcPatience)
    {
        while (!m_closed)
        {
            if (!receive(timeout))
            {
                return m_closed;
            }
        }
        return true;
    }

    editor::rpc::RpcStream& stream() noexcept { return *m_stream; }

private:
    std::unique_ptr<editor::rpc::RpcStream> m_stream;
    editor::rpc::LineFramer m_framer;
    std::deque<std::string> m_lines;
    bool m_closed = false;
};

/// The fixture editor with an RpcServer and one connected peer.
struct RpcHarness
{
    EditorFixture fixture;
    std::unique_ptr<editor::rpc::RpcServer> server;
    std::unique_ptr<RpcPeer> peer;
    u32 connection = 0;

    explicit RpcHarness(editor::rpc::RpcServerOptions options = {}, bool trusted = true)
    {
        server = std::make_unique<editor::rpc::RpcServer>(*fixture.editor, std::move(options));
        peer = connect(trusted);
    }

    ~RpcHarness()
    {
        // Stop the server before the editor goes away.
        server.reset();
    }

    RpcHarness(const RpcHarness&) = delete;
    RpcHarness& operator=(const RpcHarness&) = delete;

    /// Opens another connection.
    std::unique_ptr<RpcPeer> connect(bool trusted = true, std::string_view client = "test")
    {
        auto [server_end, client_end] = editor::rpc::makeStreamPair();
        connection = server->addConnection(std::move(server_end), trusted, client);
        return std::make_unique<RpcPeer>(std::move(client_end));
    }

    /// Sends @p line on @p on (default: the peer) and runs the queue until a reply arrives.
    std::optional<std::string> request(std::string_view line, RpcPeer* on = nullptr)
    {
        RpcPeer& target = on != nullptr ? *on : *peer;
        if (!target.send(line))
        {
            return std::nullopt;
        }
        return reply(target);
    }

    /// Runs the queue until @p target receives a line.
    std::optional<std::string> reply(RpcPeer& target)
    {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < kRpcPatience)
        {
            server->waitForWork(std::chrono::milliseconds(5));
            server->pump();
            if (std::optional<std::string> line = target.receive(std::chrono::milliseconds(5)))
            {
                return line;
            }
            if (target.closed())
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// Calls @p method with @p params on the peer; returns the parsed reply.
    JsonDocument call(std::string_view method, std::string_view params = "{}", std::string_view id = "1")
    {
        const std::string line = std::string(R"({"jsonrpc":"2.0","id":)") + std::string(id) +
                                 R"(,"method":")" + std::string(method) + R"(","params":)" +
                                 std::string(params) + "}";
        const std::optional<std::string> text = request(line);
        REQUIRE_MESSAGE(text.has_value(), "no reply to " << method);
        return parseJson(*text);
    }
};

/// Collects log messages (to check what is never logged).
class LogCapture final : public LogSink
{
public:
    LogCapture()
    {
        Logger::instance().addSink(m_self = std::shared_ptr<LogCapture>(this, [](LogCapture*) {}));
    }
    ~LogCapture() override { Logger::instance().removeSink(this); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    void write(const LogMessage& message) override
    {
        const std::lock_guard lock(m_mutex);
        m_text += message.text;
        m_text += '\n';
    }

    /// Everything logged so far.
    std::string text()
    {
        const std::lock_guard lock(m_mutex);
        return m_text;
    }

private:
    std::shared_ptr<LogCapture> m_self;
    std::mutex m_mutex;
    std::string m_text;
};

} // namespace illusion::test
