#include "rpc/RpcTestSupport.hpp"

#include <atomic>
#include <thread>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::rpc;
using test::RpcHarness;
using test::RpcPeer;

namespace
{

i64 codeOf(const JsonDocument& reply)
{
    return reply.root().find("error").find("code").asI64().value_or(0);
}

std::string requestLine(std::string_view method, std::string_view params = "{}", std::string_view id = "1")
{
    return std::string(R"({"jsonrpc":"2.0","id":)") + std::string(id) + R"(,"method":")" +
           std::string(method) + R"(","params":)" + std::string(params) + "}";
}

} // namespace

TEST_CASE("a request runs its command and the reply carries the id and _editor")
{
    RpcHarness rpc;
    const JsonDocument created = rpc.call("entity.create", R"({"name":"Board"})", R"("c-7")");
    CHECK(created.root().find("jsonrpc").asString() == "2.0");
    CHECK(created.root().find("id").asString() == "c-7");
    const JsonValue result = created.root().find("result");
    CHECK(result.find("entity").find("name").asString() == "Board");
    CHECK(result.find("_editor").find("transaction").asU64() > 0);

    const JsonDocument numeric = rpc.call("scene.info", "{}", "42");
    CHECK(numeric.root().find("id").asI64() == 42);
    CHECK(numeric.root().find("result").find("entities").asU64() == 1);
}

TEST_CASE("commands run with the rpc origin of the connection")
{
    RpcHarness rpc;
    rpc.call("entity.create", R"({"name":"A"})");
    const JsonDocument history = rpc.call("history.list");
    const JsonValue steps = history.root().find("result").find("steps");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].find("origin").asString() == "rpc:test");
}

TEST_CASE("notifications run but get no reply, and replies keep the order of requests")
{
    RpcHarness rpc;
    REQUIRE(rpc.peer->send(R"({"jsonrpc":"2.0","method":"entity.create","params":{"name":"Silent"}})"));
    REQUIRE(rpc.peer->send(R"({"jsonrpc":"2.0","method":"no.such_method"})"));
    REQUIRE(rpc.peer->send(requestLine("scene.info", "{}", "1")));
    REQUIRE(rpc.peer->send(requestLine("entity.create", R"({"name":"Second"})", "2")));
    const JsonDocument first = test::parseJson(*rpc.reply(*rpc.peer));
    const JsonDocument second = test::parseJson(*rpc.reply(*rpc.peer));
    CHECK(first.root().find("id").asI64() == 1);
    CHECK(first.root().find("result").find("entities").asU64() == 1);
    CHECK(second.root().find("id").asI64() == 2);
}

TEST_CASE("command failures use the application error codes with data")
{
    RpcHarness rpc;
    const JsonDocument missing = rpc.call("entity.get", R"({"entity":"/Nope"})");
    CHECK(codeOf(missing) == 1200);
    CHECK(missing.root().find("error").find("data").find("kind").asString() == "not_found");

    const JsonDocument invalid = rpc.call("entity.create", R"({"name":5})");
    CHECK(codeOf(invalid) == 1202);
    CHECK(invalid.root().find("error").find("data").find("errors")[0].find("path").asString() == "/name");

    const JsonDocument conflict = rpc.call("entity.create", R"({"name":"A","expectRevision":999})");
    CHECK(codeOf(conflict) == 1203);
}

TEST_CASE("unknown methods get -32601 with suggestions")
{
    RpcHarness rpc;
    const JsonDocument reply = rpc.call("entity.craete");
    CHECK(codeOf(reply) == -32601);
    const JsonValue suggestions = reply.root().find("error").find("data").find("suggestions");
    REQUIRE(suggestions.size() > 0);
    CHECK(suggestions[0].asString() == "entity.create");
    CHECK(codeOf(rpc.call("rpc.nope")) == -32601);
}

TEST_CASE("protocol errors are answered and the connection stays usable")
{
    RpcHarness rpc;
    CHECK(test::parseJson(*rpc.request("{oops")).root().find("error").find("code").asI64() == -32700);
    CHECK(test::parseJson(*rpc.request(R"([{"jsonrpc":"2.0","id":1,"method":"scene.info"}])"))
              .root()
              .find("error")
              .find("code")
              .asI64() == -32600);
    CHECK(test::parseJson(*rpc.request(R"({"jsonrpc":"2.0","id":3,"method":"scene.info","params":[]})"))
              .root()
              .find("error")
              .find("code")
              .asI64() == -32602);
    CHECK(rpc.call("scene.info").root().find("result").isObject());
}

TEST_CASE("an oversized message is rejected and closes the connection")
{
    RpcHarness rpc;
    const std::string huge = R"({"jsonrpc":"2.0","id":1,"method":"x","params":{"a":")" +
                             std::string(kMaxMessageSize, 'a') + R"("}})";
    REQUIRE(rpc.peer->send(huge));
    const std::optional<std::string> reply = rpc.peer->receive();
    REQUIRE(reply.has_value());
    CHECK(test::parseJson(*reply).root().find("error").find("code").asI64() == -32600);
    CHECK(rpc.peer->waitClosed());
}

TEST_CASE("socket clients must authenticate first, with the right token, in time")
{
    test::LogCapture log;
    RpcServerOptions options;
    options.authenticationTimeout = std::chrono::milliseconds(100);
    RpcHarness rpc(options, false);
    const std::string& token = rpc.server->token();
    CHECK(token.size() == 64);

    // Anything before rpc.authenticate closes the connection.
    const std::optional<std::string> early = rpc.request(requestLine("scene.info"));
    REQUIRE(early.has_value());
    CHECK(test::parseJson(*early).root().find("error").find("code").asI64() == 1000);
    CHECK(rpc.peer->waitClosed());

    // A wrong token too.
    std::unique_ptr<RpcPeer> wrong = rpc.connect(false);
    const std::optional<std::string> denied =
        rpc.request(requestLine("rpc.authenticate", R"({"token":"0000"})"), wrong.get());
    REQUIRE(denied.has_value());
    CHECK(test::parseJson(*denied).root().find("error").find("code").asI64() == 1000);
    CHECK(wrong->waitClosed());

    // Silence past the timeout.
    std::unique_ptr<RpcPeer> silent = rpc.connect(false);
    const std::optional<std::string> timedOut = silent->receive();
    REQUIRE(timedOut.has_value());
    CHECK(test::parseJson(*timedOut).root().find("error").find("code").asI64() == 1000);
    CHECK(silent->waitClosed());

    // The right token works, names the client and allows commands.
    std::unique_ptr<RpcPeer> good = rpc.connect(false);
    const std::optional<std::string> accepted = rpc.request(
        requestLine("rpc.authenticate", R"({"token":")" + token + R"(","client":"agent one!"})"), good.get());
    REQUIRE(accepted.has_value());
    const JsonDocument hello = test::parseJson(*accepted);
    CHECK(hello.root().find("result").find("client").asString() == "agentone");
    CHECK(hello.root().find("result").find("protocolVersion").asString() == kProtocolVersion);
    const std::optional<std::string> info = rpc.request(requestLine("scene.info"), good.get());
    REQUIRE(info.has_value());
    CHECK(test::parseJson(*info).root().find("result").isObject());

    CHECK(log.text().find(token) == std::string::npos);
    CHECK(log.text().find("failed to authenticate") != std::string::npos);
}

TEST_CASE("permission modes decide which command classes run")
{
    CHECK(permits(PermissionMode::ReadOnly, PermissionClass::Read, true));
    CHECK_FALSE(permits(PermissionMode::ReadOnly, PermissionClass::Edit, false));
    CHECK(permits(PermissionMode::Ask, PermissionClass::Edit, false));
    CHECK_FALSE(permits(PermissionMode::Ask, PermissionClass::Files, false));
    CHECK(permits(PermissionMode::Ask, PermissionClass::Files, false, true));
    CHECK(permits(PermissionMode::Auto, PermissionClass::System, false));
    CHECK(parsePermissionMode("read-only") == PermissionMode::ReadOnly);
    CHECK_FALSE(parsePermissionMode("everything").has_value());

    RpcServerOptions readOnly;
    readOnly.permissions = PermissionMode::ReadOnly;
    RpcHarness reader(readOnly);
    CHECK(codeOf(reader.call("entity.create", R"({"name":"A"})")) == 1001);
    CHECK(reader.call("scene.info").root().find("result").isObject());
    CHECK(codeOf(reader.call("editor.quit")) == 1001);

    RpcServerOptions ask;
    ask.permissions = PermissionMode::Ask;
    RpcHarness asker(ask);
    CHECK(asker.call("entity.create", R"({"name":"A"})").root().find("result").isObject());
    CHECK(codeOf(asker.call("project.file_write", R"({"path":"assets:/a.txt","text":"x"})")) == 1001);
    CHECK(codeOf(asker.call("history.clear")) == 1001);
}

TEST_CASE("a repeated clientToken returns the first reply without running the command again")
{
    RpcHarness rpc;
    const JsonDocument first = rpc.call("entity.create", R"({"name":"Once","clientToken":"t-1"})", "1");
    const JsonDocument again = rpc.call("entity.create", R"({"name":"Once","clientToken":"t-1"})", "2");
    CHECK(again.root().find("id").asI64() == 2);
    CHECK(first.root().find("result").dump() == again.root().find("result").dump());
    CHECK(rpc.call("scene.info").root().find("result").find("entities").asU64() == 1);
    CHECK(codeOf(rpc.call("entity.rename", R"({"entity":"/Once","name":"X","clientToken":"t-1"})")) == 1202);
    CHECK(codeOf(rpc.call("entity.create", R"({"name":"Bad","clientToken":5})")) == 1202);
}

TEST_CASE("idempotency tokens are remembered up to the limit")
{
    RpcServerOptions options;
    options.idempotencyTokens = 2;
    RpcHarness rpc(options);
    rpc.call("entity.create", R"({"name":"A","clientToken":"a"})");
    rpc.call("entity.create", R"({"name":"B","clientToken":"b"})");
    rpc.call("entity.create", R"({"name":"C","clientToken":"c"})");
    // "a" was forgotten, so it runs again.
    rpc.call("entity.create", R"({"name":"A","clientToken":"a"})");
    CHECK(rpc.call("scene.info").root().find("result").find("entities").asU64() == 4);
}

TEST_CASE("pull subscriptions keep events for poll_events and report overflow")
{
    RpcServerOptions options;
    options.eventQueueCapacity = 2;
    RpcHarness rpc(options);
    const JsonDocument subscribed =
        rpc.call("editor.subscribe", R"({"topics":["scene.changed"],"mode":"pull"})");
    CHECK(subscribed.root().find("result").find("mode").asString() == "pull");
    rpc.call("entity.create", R"({"name":"A"})");
    rpc.fixture.editor->endFrame(0.0);
    JsonDocument polled = rpc.call("editor.poll_events");
    JsonValue events = polled.root().find("result").find("events");
    REQUIRE(events.size() == 1);
    CHECK(events[0].find("topic").asString() == "scene.changed");
    CHECK(events[0].find("payload").find("revision").isInteger());
    CHECK(polled.root().find("result").find("overflow").asBool() == false);

    for (int i = 0; i < 3; ++i)
    {
        rpc.call("entity.create", R"({"name":"A"})");
        rpc.fixture.editor->endFrame(0.0);
    }
    polled = rpc.call("editor.poll_events", R"({"limit":10})");
    CHECK(polled.root().find("result").find("events").size() == 2);
    CHECK(polled.root().find("result").find("overflow").asBool() == true);
    CHECK(rpc.call("editor.poll_events").root().find("result").find("overflow").asBool() == false);

    CHECK(codeOf(rpc.call("editor.subscribe", R"({"mode":"sideways"})")) == 1202);
    CHECK(codeOf(rpc.call("editor.poll_events", R"({"limit":0})")) == 1202);
    rpc.call("editor.unsubscribe");
    rpc.call("entity.create", R"({"name":"A"})");
    rpc.fixture.editor->endFrame(0.0);
    CHECK(rpc.call("editor.poll_events").root().find("result").find("events").size() == 0);
}

TEST_CASE("push subscriptions send event notifications")
{
    RpcHarness rpc;
    rpc.call("editor.subscribe", R"({"topics":["history.changed"]})");
    rpc.call("entity.create", R"({"name":"A"})");
    rpc.fixture.editor->endFrame(0.0);
    const std::optional<std::string> event = rpc.peer->receive();
    REQUIRE(event.has_value());
    const JsonDocument doc = test::parseJson(*event);
    CHECK(doc.root().find("method").asString() == "event");
    CHECK(doc.root().find("id").isAbsent());
    CHECK(doc.root().find("params").find("topic").asString() == "history.changed");
}

TEST_CASE("connection, in-flight and queue limits answer instead of piling up")
{
    RpcServerOptions options;
    options.maxConnections = 1;
    options.maxInFlight = 2;
    RpcHarness rpc(options);

    // A second connection is refused with limit_exceeded.
    std::unique_ptr<RpcPeer> extra = rpc.connect();
    CHECK(rpc.connection == 0);
    const std::optional<std::string> refused = extra->receive();
    REQUIRE(refused.has_value());
    CHECK(test::parseJson(*refused).root().find("error").find("code").asI64() == 1401);

    // Without pumping, the third request of the connection is refused at once.
    for (int id = 1; id <= 3; ++id)
    {
        REQUIRE(rpc.peer->send(requestLine("scene.info", "{}", std::to_string(id))));
    }
    const std::optional<std::string> third = rpc.peer->receive();
    REQUIRE(third.has_value());
    const JsonDocument limited = test::parseJson(*third);
    CHECK(limited.root().find("id").asI64() == 3);
    CHECK(limited.root().find("error").find("code").asI64() == 1401);
    CHECK(limited.root().find("error").find("data").find("retryable").asBool() == true);
    CHECK(rpc.server->pump() == 2);

    RpcServerOptions small;
    small.queueCapacity = 1;
    RpcHarness busy(small);
    REQUIRE(busy.peer->send(requestLine("scene.info", "{}", "1")));
    REQUIRE(busy.peer->send(requestLine("scene.info", "{}", "2")));
    const std::optional<std::string> second = busy.peer->receive();
    REQUIRE(second.has_value());
    CHECK(test::parseJson(*second).root().find("error").find("code").asI64() == 1002);
}

TEST_CASE("rpc.discover returns an OpenRPC document of every method")
{
    RpcHarness rpc;
    const JsonDocument reply = rpc.call("rpc.discover");
    const JsonValue document = reply.root().find("result");
    CHECK(document.find("openrpc").asString() == "1.3.2");
    CHECK(document.find("info").find("version").asString() == kProtocolVersion);
    const JsonValue methods = document.find("methods");
    CHECK(methods.size() == rpc.fixture.editor->commands().commands().size() + 8);
    bool sawRename = false;
    for (const JsonValue method : methods.elements())
    {
        CHECK(method.find("params").isArray());
        CHECK(method.find("result").find("schema").isObject());
        if (method.find("name").asString() == "entity.rename")
        {
            sawRename = true;
            CHECK(method.find("x-illusion").find("permission").asString() == "edit");
            bool nameRequired = false;
            for (const JsonValue param : method.find("params").elements())
            {
                nameRequired = nameRequired || (param.find("name").asString() == "name" &&
                                                param.find("required").asBool() == true);
            }
            CHECK(nameRequired);
        }
    }
    CHECK(sawRename);
    CHECK(reply.root().find("result").dump() == rpc.server->discoverDocument());
}

TEST_CASE("editor.quit asks the editor loop to end")
{
    RpcHarness rpc;
    CHECK_FALSE(rpc.server->quitRequested());
    CHECK(rpc.call("editor.quit").root().find("result").isObject());
    CHECK(rpc.server->quitRequested());
}

TEST_CASE("a closed peer is noticed and its queued requests are dropped")
{
    RpcHarness rpc;
    const u32 id = rpc.connection;
    CHECK(rpc.server->isOpen(id));
    REQUIRE(rpc.peer->send(requestLine("entity.create", R"({"name":"Lost"})")));
    rpc.peer->stream().close();
    for (int i = 0; i < 1000 && rpc.server->isOpen(id); ++i)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    CHECK_FALSE(rpc.server->isOpen(id));
    CHECK(rpc.server->connectionCount() == 0);
}

TEST_CASE("eight clients with a thousand requests each are answered in order")
{
    constexpr int kClients = 8;
    constexpr int kRequests = 1000;
    RpcHarness rpc;
    std::vector<std::unique_ptr<RpcPeer>> peers;
    peers.push_back(std::move(rpc.peer));
    for (int i = 1; i < kClients; ++i)
    {
        peers.push_back(rpc.connect(true, "client" + std::to_string(i)));
    }
    std::atomic<int> finished{0};
    std::atomic<int> disorder{0};
    std::vector<std::thread> clients;
    for (int c = 0; c < kClients; ++c)
    {
        clients.emplace_back([&, c] {
            RpcPeer& peer = *peers[static_cast<usize>(c)];
            // Pipeline up to 16 requests at a time, so replies interleave across connections.
            int sent = 0;
            int received = 0;
            while (received < kRequests)
            {
                while (sent < kRequests && sent - received < 16)
                {
                    peer.send(requestLine("selection.get", "{}", std::to_string(++sent)));
                }
                const std::optional<std::string> reply = peer.receive();
                if (!reply)
                {
                    break;
                }
                const JsonDocument doc = test::parseJson(*reply);
                if (doc.root().find("id").asI64() != ++received || !doc.root().find("result").isObject())
                {
                    disorder.fetch_add(1);
                }
            }
            finished.fetch_add(1);
        });
    }
    while (finished.load() < kClients)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    for (std::thread& client : clients)
    {
        client.join();
    }
    CHECK(disorder.load() == 0);
}

TEST_CASE("stopping the server while clients send is clean")
{
    RpcHarness rpc;
    std::vector<std::unique_ptr<RpcPeer>> peers;
    for (int i = 0; i < 4; ++i)
    {
        peers.push_back(rpc.connect());
    }
    std::atomic<bool> run{true};
    std::vector<std::thread> clients;
    for (auto& peer : peers)
    {
        clients.emplace_back([&run, raw = peer.get()] {
            for (int id = 1; run.load() && id < 100000; ++id)
            {
                if (!raw->send(requestLine("selection.get", "{}", std::to_string(id))))
                {
                    break;
                }
            }
        });
    }
    for (int i = 0; i < 20; ++i)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(1));
        rpc.server->pump();
    }
    rpc.server->stop();
    run.store(false);
    for (std::thread& client : clients)
    {
        client.join();
    }
    CHECK(rpc.server->connectionCount() == 0);
    CHECK(rpc.server->pump() == 0);
}
