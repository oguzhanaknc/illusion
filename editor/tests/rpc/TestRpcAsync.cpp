#include "rpc/RpcTestSupport.hpp"

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

// Sends @p method on @p peer and returns the parsed reply.
JsonDocument callOn(RpcHarness& rpc, RpcPeer& peer, std::string_view method, std::string_view params = "{}",
                    std::string_view id = "1")
{
    const std::optional<std::string> text = rpc.request(requestLine(method, params, id), &peer);
    REQUIRE_MESSAGE(text.has_value(), "no reply to " << method);
    return test::parseJson(*text);
}

Expected<NoResult, CommandError> ping(CommandContext&, const NoParams&)
{
    return NoResult{};
}

} // namespace

TEST_CASE("an async command reports rpc.progress each frame and later requests do not wait for it")
{
    RpcHarness rpc;
    REQUIRE(rpc.peer->send(requestLine("editor.wait", R"({"frames":3})", R"("w")")));
    REQUIRE(rpc.server->waitForWork(test::kRpcPatience));
    CHECK(rpc.server->pump() == 1); // starts the wait
    REQUIRE(rpc.peer->send(requestLine("scene.info", "{}", "2")));
    REQUIRE(rpc.server->waitForWork(test::kRpcPatience));
    rpc.server->pump(); // the first frame of the wait, then scene.info
    rpc.server->pump(); // the second frame
    rpc.server->pump(); // the third frame finishes it
    std::vector<JsonDocument> lines;
    for (int i = 0; i < 4; ++i)
    {
        const std::optional<std::string> line = rpc.peer->receive();
        REQUIRE(line.has_value());
        lines.push_back(test::parseJson(*line));
    }
    CHECK(lines[0].root().find("method").asString() == "rpc.progress");
    CHECK(lines[0].root().pointer("/params/id").asString() == "w");
    CHECK(lines[0].root().pointer("/params/progress").asF64() == doctest::Approx(1.0));
    CHECK(lines[0].root().pointer("/params/total").asF64() == doctest::Approx(3.0));
    CHECK(lines[1].root().find("id").asI64() == 2);
    CHECK(lines[2].root().pointer("/params/progress").asF64() == doctest::Approx(2.0));
    CHECK(lines[3].root().find("id").asString() == "w");
    CHECK(lines[3].root().pointer("/result/frames").asU64() == 3u);
}

TEST_CASE("rpc.cancel ends a running async command and skips a queued request")
{
    RpcHarness rpc;
    REQUIRE(rpc.peer->send(requestLine("editor.wait", R"({"frames":100000})", "1")));
    REQUIRE(rpc.server->waitForWork(test::kRpcPatience));
    rpc.server->pump();
    REQUIRE(rpc.peer->send(R"({"jsonrpc":"2.0","method":"rpc.cancel","params":{"id":1}})"));
    std::optional<std::string> line;
    do
    {
        line = rpc.reply(*rpc.peer);
        REQUIRE(line.has_value());
    } while (line->find("rpc.progress") != std::string::npos);
    const JsonDocument cancelled = test::parseJson(*line);
    CHECK(cancelled.root().find("id").asI64() == 1);
    CHECK(codeOf(cancelled) == 1004);

    // A queued request is cancelled before it runs: both lines arrive before the next pump.
    REQUIRE(rpc.peer->send(requestLine("entity.create", R"({"name":"Never"})", R"("q")") + "\n" +
                           R"({"jsonrpc":"2.0","id":9,"method":"rpc.cancel","params":{"id":"q"}})"));
    const JsonDocument acknowledged = test::parseJson(*rpc.peer->receive());
    CHECK(acknowledged.root().find("id").asI64() == 9);
    const JsonDocument skipped = test::parseJson(*rpc.reply(*rpc.peer));
    CHECK(skipped.root().find("id").asString() == "q");
    CHECK(codeOf(skipped) == 1004);
    CHECK(rpc.call("scene.info").root().pointer("/result/entities").asU64() == 0u);
    CHECK(codeOf(rpc.call("rpc.cancel", R"({"id":[1]})")) == 1202);
}

TEST_CASE("a transaction belongs to its connection and is cancelled when the connection closes")
{
    RpcHarness rpc;
    std::unique_ptr<RpcPeer> other = rpc.connect(true, "other");
    CHECK(rpc.call("transaction.begin", R"({"label":"Agent"})").root().pointer("/result/label").asString() ==
          "Agent");
    rpc.call("entity.create", R"({"name":"Pending"})");
    const JsonDocument busy = callOn(rpc, *other, "entity.create", R"({"name":"Other"})");
    CHECK(codeOf(busy) == 1002);
    CHECK(busy.root().pointer("/error/data/retryable").asBool() == true);
    CHECK(codeOf(callOn(rpc, *other, "transaction.commit")) == 1205);
    CHECK(callOn(rpc, *other, "scene.info").root().pointer("/result/entities").asU64() == 1u);

    rpc.peer->stream().close();
    rpc.peer.reset();
    // The server notices the closed connection and cancels its transaction.
    for (int i = 0; i < 1000 && rpc.fixture.editor->transaction() != nullptr; ++i)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(2));
        rpc.server->pump();
    }
    CHECK(rpc.fixture.editor->transaction() == nullptr);
    CHECK(callOn(rpc, *other, "scene.info").root().pointer("/result/entities").asU64() == 0u);
    CHECK(callOn(rpc, *other, "entity.create", R"({"name":"Other"})").root().find("result").isObject());
}

TEST_CASE("editor.batch over RPC obeys the permission mode in every step")
{
    RpcServerOptions ask;
    ask.permissions = PermissionMode::Ask;
    RpcHarness rpc(ask);
    const JsonDocument denied = rpc.call(
        "editor.batch", R"({"steps":[{"method":"entity.create","params":{"name":"A"}},)"
                        R"({"method":"project.file_write","params":{"path":"assets:/a.txt","text":"x"}}]})");
    CHECK_MESSAGE(codeOf(denied) == 1001, denied.root().dump());
    CHECK(rpc.call("scene.info").root().pointer("/result/entities").asU64() == 0u);
    const JsonDocument allowed = rpc.call(
        "editor.batch", R"({"steps":[{"as":"a","method":"entity.create","params":{"name":"A"}},)"
                        R"({"method":"entity.rename","params":{"entity":"$a.entity.id","name":"B"}}]})");
    CHECK_MESSAGE(allowed.root().pointer("/result/steps/1/result/entity/name").asString() == "B",
                  allowed.root().dump());
}

TEST_CASE("history.undo with scope mine undoes this connection's step past another's")
{
    RpcHarness rpc;
    std::unique_ptr<RpcPeer> other = rpc.connect(true, "other");
    rpc.call("entity.create", R"({"name":"Mine"})");
    callOn(rpc, *other, "entity.create", R"({"name":"Theirs"})");
    const JsonDocument undone = rpc.call("history.undo", R"({"scope":"mine"})");
    CHECK(undone.root().pointer("/result/steps").asU64() == 1u);
    const JsonDocument tree = rpc.call("entity.find", R"({"name":"*"})");
    const JsonValue entities = tree.root().pointer("/result/entities");
    REQUIRE(entities.size() == 1);
    CHECK(entities[0].find("name").asString() == "Theirs");
}

TEST_CASE("commands registered later become callable after commands.changed")
{
    RpcHarness rpc;
    rpc.call("editor.subscribe", R"({"topics":["commands.changed"],"mode":"pull"})");
    CHECK(codeOf(rpc.call("test.ping")) == -32601);
    rpc.fixture.editor->commands().add<&ping>(
        CommandInfo{.name = "test.ping", .agentSummary = "Answers. Example: {}.", .category = "Test"});
    rpc.fixture.editor->endFrame(0.0);
    CHECK(rpc.call("test.ping").root().find("result").isObject());
    CHECK(rpc.server->discoverDocument().find("\"test.ping\"") != std::string::npos);
    const JsonDocument events = rpc.call("editor.poll_events");
    REQUIRE(events.root().pointer("/result/events").size() == 1);
    CHECK(events.root().pointer("/result/events/0/topic").asString() == "commands.changed");
}
