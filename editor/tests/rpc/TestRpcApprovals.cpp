#include "rpc/RpcTestSupport.hpp"

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::rpc;
using test::RpcHarness;
using test::RpcPeer;

namespace
{

std::string requestLine(std::string_view method, std::string_view params = "{}", std::string_view id = "1")
{
    return std::string(R"({"jsonrpc":"2.0","id":)") + std::string(id) + R"(,"method":")" +
           std::string(method) + R"(","params":)" + std::string(params) + "}";
}

RpcServerOptions askOptions()
{
    RpcServerOptions options;
    options.permissions = PermissionMode::Ask;
    options.interactive = true;
    return options;
}

// Runs the queue until @p count approvals wait (bounded by the patience of the RPC tests).
bool waitForApprovals(RpcHarness& rpc, usize count)
{
    const auto start = std::chrono::steady_clock::now();
    while (rpc.server->approvals().size() < count)
    {
        if (std::chrono::steady_clock::now() - start > test::kRpcPatience)
        {
            return false;
        }
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    return true;
}

i64 codeOf(const JsonDocument& reply)
{
    return reply.root().find("error").find("code").asI64().value_or(0);
}

} // namespace

TEST_CASE("in the ask mode of a window a files command waits for approval and runs once allowed")
{
    RpcHarness rpc(askOptions());
    REQUIRE(rpc.peer->send(requestLine("project.file_write", R"({"path":"assets:/note.txt","text":"hi"})")));
    REQUIRE(waitForApprovals(rpc, 1));
    const PendingApproval approval = rpc.server->approvals().front();
    CHECK(approval.method == "project.file_write");
    CHECK(approval.permission == PermissionClass::Files);
    CHECK(approval.client == "test");
    CHECK(approval.params.find("note.txt") != std::string::npos);
    // Nothing is answered while it waits.
    rpc.server->pump();
    CHECK_FALSE(rpc.peer->receive(std::chrono::milliseconds(20)).has_value());
    CHECK(rpc.fixture.editor->files().exists("assets:/note.txt") == false);

    CHECK(rpc.server->decide(approval.id, ApprovalDecision::AllowOnce));
    CHECK_FALSE(rpc.server->decide(approval.id, ApprovalDecision::AllowOnce));
    const std::optional<std::string> reply = rpc.reply(*rpc.peer);
    REQUIRE(reply.has_value());
    CHECK(test::parseJson(*reply).root().find("result").isObject());
    CHECK(rpc.fixture.editor->files().exists("assets:/note.txt"));
    CHECK(rpc.server->approvals().empty());

    // Allowed once: the next one asks again.
    REQUIRE(
        rpc.peer->send(requestLine("project.file_write", R"({"path":"assets:/two.txt","text":"2"})", "2")));
    REQUIRE(waitForApprovals(rpc, 1));
}

TEST_CASE("a denied request fails with PermissionDenied and the activity feed records it")
{
    RpcHarness rpc(askOptions());
    REQUIRE(rpc.peer->send(requestLine("history.clear")));
    REQUIRE(waitForApprovals(rpc, 1));
    CHECK(rpc.server->approvals().front().permission == PermissionClass::Destructive);
    CHECK(rpc.server->decide(rpc.server->approvals().front().id, ApprovalDecision::Deny));
    const std::optional<std::string> reply = rpc.reply(*rpc.peer);
    REQUIRE(reply.has_value());
    CHECK(codeOf(test::parseJson(*reply)) == static_cast<i64>(ErrorKind::PermissionDenied));
    std::vector<ActivityEntry> feed;
    rpc.server->activity(0, feed);
    REQUIRE_FALSE(feed.empty());
    CHECK(feed.back().method == "history.clear");
    CHECK(feed.back().code == static_cast<i32>(ErrorKind::PermissionDenied));
}

TEST_CASE("allowing for the session lets later requests of that class run without asking")
{
    RpcHarness rpc(askOptions());
    REQUIRE(rpc.peer->send(requestLine("project.file_write", R"({"path":"assets:/a.txt","text":"a"})")));
    REQUIRE(waitForApprovals(rpc, 1));
    CHECK(rpc.server->decide(rpc.server->approvals().front().id, ApprovalDecision::AllowForSession));
    REQUIRE(rpc.reply(*rpc.peer).has_value());
    const JsonDocument second = rpc.call("project.file_write", R"({"path":"assets:/b.txt","text":"b"})", "2");
    CHECK(second.root().find("result").isObject());
    CHECK(rpc.server->approvals().empty());
    REQUIRE(rpc.server->clients().size() == 1);
    CHECK((rpc.server->clients().front().allowed & (1u << static_cast<u32>(PermissionClass::Files))) != 0);
    // Other classes still ask.
    REQUIRE(rpc.peer->send(requestLine("history.clear", "{}", "3")));
    REQUIRE(waitForApprovals(rpc, 1));
}

TEST_CASE("later requests of a waiting connection wait behind it in order, other connections go on")
{
    RpcHarness rpc(askOptions());
    std::unique_ptr<RpcPeer> other = rpc.connect(true, "other");
    REQUIRE(rpc.peer->send(requestLine("history.clear", "{}", "1")));
    REQUIRE(rpc.peer->send(requestLine("scene.info", "{}", "2")));
    REQUIRE(waitForApprovals(rpc, 1));
    // The other client is not blocked.
    const std::optional<std::string> otherReply =
        rpc.request(requestLine("scene.info", "{}", "9"), other.get());
    REQUIRE(otherReply.has_value());
    CHECK(test::parseJson(*otherReply).root().find("id").asI64() == 9);
    CHECK_FALSE(rpc.peer->receive(std::chrono::milliseconds(20)).has_value());
    CHECK(rpc.server->decide(rpc.server->approvals().front().id, ApprovalDecision::AllowOnce));
    const std::optional<std::string> first = rpc.reply(*rpc.peer);
    const std::optional<std::string> second = rpc.reply(*rpc.peer);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(test::parseJson(*first).root().find("id").asI64() == 1);
    CHECK(test::parseJson(*second).root().find("id").asI64() == 2);
}

TEST_CASE("a batch with a destructive step asks once for the whole batch")
{
    RpcHarness rpc(askOptions());
    REQUIRE(rpc.peer->send(requestLine(
        "editor.batch",
        R"({"steps":[{"method":"entity.create","params":{"name":"A"}},{"method":"project.file_write","params":{"path":"assets:/c.txt","text":"c"}}]})")));
    REQUIRE(waitForApprovals(rpc, 1));
    const PendingApproval approval = rpc.server->approvals().front();
    CHECK(approval.method == "editor.batch");
    CHECK(approval.permission == PermissionClass::Files);
    CHECK(rpc.server->decide(approval.id, ApprovalDecision::AllowOnce));
    const std::optional<std::string> reply = rpc.reply(*rpc.peer);
    REQUIRE(reply.has_value());
    CHECK(test::parseJson(*reply).root().find("result").isObject());
    CHECK(rpc.fixture.editor->files().exists("assets:/c.txt"));
}

TEST_CASE("reads and edits never ask, and headless ask mode still denies without asking")
{
    {
        RpcHarness rpc(askOptions());
        CHECK(rpc.call("entity.create", R"({"name":"Free"})").root().find("result").isObject());
        CHECK(rpc.server->approvals().empty());
    }
    RpcServerOptions headless;
    headless.permissions = PermissionMode::Ask;
    RpcHarness rpc(headless);
    CHECK(codeOf(rpc.call("history.clear")) == static_cast<i64>(ErrorKind::PermissionDenied));
    CHECK(rpc.server->approvals().empty());
}

TEST_CASE("cancelling or closing a waiting request drops its approval")
{
    RpcHarness rpc(askOptions());
    REQUIRE(rpc.peer->send(requestLine("history.clear", "{}", "5")));
    REQUIRE(waitForApprovals(rpc, 1));
    REQUIRE(rpc.peer->send(R"({"jsonrpc":"2.0","method":"rpc.cancel","params":{"id":5}})"));
    const std::optional<std::string> reply = rpc.reply(*rpc.peer);
    REQUIRE(reply.has_value());
    CHECK(codeOf(test::parseJson(*reply)) == static_cast<i64>(ErrorKind::Cancelled));
    CHECK(rpc.server->approvals().empty());

    REQUIRE(rpc.peer->send(requestLine("history.clear", "{}", "6")));
    REQUIRE(waitForApprovals(rpc, 1));
    rpc.peer->stream().close();
    const auto start = std::chrono::steady_clock::now();
    while (!rpc.server->approvals().empty() && std::chrono::steady_clock::now() - start < test::kRpcPatience)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    CHECK(rpc.server->approvals().empty());
}

TEST_CASE("a paused connection's requests are held until it is resumed, and disconnect closes it")
{
    RpcHarness rpc(askOptions());
    REQUIRE(rpc.server->clients().size() == 1);
    const u32 id = rpc.server->clients().front().connection;
    const u64 version = rpc.server->stateVersion();
    CHECK(rpc.server->setPaused(id, true));
    CHECK(rpc.server->stateVersion() != version);
    CHECK(rpc.server->clients().front().paused);
    REQUIRE(rpc.peer->send(requestLine("scene.info")));
    for (int i = 0; i < 5; ++i)
    {
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    CHECK_FALSE(rpc.peer->receive(std::chrono::milliseconds(20)).has_value());
    CHECK(rpc.server->setPaused(id, false));
    REQUIRE(rpc.reply(*rpc.peer).has_value());
    CHECK_FALSE(rpc.server->setPaused(999, true));

    CHECK(rpc.server->disconnect(id));
    CHECK(rpc.peer->waitClosed());
    rpc.server->pump();
    CHECK(rpc.server->clients().empty());
    CHECK_FALSE(rpc.server->disconnect(id));
}

TEST_CASE("changing the permission mode applies to the next requests")
{
    RpcHarness rpc(askOptions());
    CHECK(rpc.server->permissions() == PermissionMode::Ask);
    rpc.server->setPermissions(PermissionMode::Auto);
    CHECK(rpc.call("history.clear").root().find("result").isObject());
    rpc.server->setPermissions(PermissionMode::ReadOnly);
    CHECK(codeOf(rpc.call("entity.create", R"({"name":"No"})", "2")) ==
          static_cast<i64>(ErrorKind::PermissionDenied));
}

TEST_CASE("the activity feed keeps a summary of the params")
{
    RpcHarness rpc;
    CHECK(rpc.call("entity.create", R"({"name":"Summarized"})").root().find("result").isObject());
    std::vector<ActivityEntry> feed;
    rpc.server->activity(0, feed);
    REQUIRE(feed.size() == 1);
    CHECK(feed.front().params == R"({"name":"Summarized"})");
    const JsonDocument listed = rpc.call("editor.activity", "{}", "2");
    CHECK(listed.root().find("result").find("entries")[0].find("params").asString() ==
          std::optional<std::string_view>(R"({"name":"Summarized"})"));
}
