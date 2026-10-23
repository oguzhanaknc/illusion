#include "rpc/RpcTestSupport.hpp"

#include <illusion/editor/client/RpcClient.hpp>
#include <illusion/editor/client/Transcript.hpp>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::client;

namespace
{

std::optional<std::string> match(std::string_view expected, std::string_view actual)
{
    const JsonDocument e = test::parseJson(expected);
    const JsonDocument a = test::parseJson(actual);
    return matchJson(e.root(), a.root());
}

LaunchOptions editorLaunch(const test::TempDirectory& workspace)
{
    LaunchOptions options;
    options.editor = utf8Path(ILLUSION_EDITOR_EXECUTABLE);
    const std::u8string path = workspace.path.u8string();
    options.arguments = {"--deterministic", "--workspace",
                         std::string(reinterpret_cast<const char*>(path.data()), path.size())};
    return options;
}

} // namespace

TEST_CASE("matchJson matches subsets of objects and whole arrays")
{
    CHECK_FALSE(match(R"({"a":1})", R"({"a":1,"b":2})"));
    CHECK(match(R"({"a":1,"c":3})", R"({"a":1,"b":2})") == "/c: expected 3, got nothing");
    CHECK(match(R"({"a":{"b":"x"}})", R"({"a":{"b":"y"}})") == R"(/a/b: expected "x", got "y")");
    CHECK_FALSE(match(R"([1,{"k":true}])", R"([1,{"k":true,"z":0}])"));
    CHECK(match("[1,2]", "[1,2,3]").has_value());
    CHECK_FALSE(match("1", "1.0"));
    CHECK(match(R"("1")", "1").has_value());
    CHECK_FALSE(match("null", "null"));
    CHECK(match(R"({"a/b":1})", R"({"a/b":2})") == "/a~1b: expected 1, got 2");
}

TEST_CASE("matchJson operators")
{
    CHECK_FALSE(match(R"({"$approx":1.0})", "1.00001"));
    CHECK(match(R"({"$approx":[1.0,0.01]})", "1.1").has_value());
    CHECK_FALSE(match(R"({"$regex":"[0-9a-f]{16}"})", R"("9f2c41aa0b7d3e15")"));
    CHECK(match(R"({"$regex":"[0-9a-f]{16}"})", R"("9f2c")").has_value());
    CHECK(match(R"({"$regex":"("})", R"("x")")->find("invalid") != std::string::npos);
    CHECK_FALSE(match(R"({"$length":2})", "[1,2]"));
    CHECK_FALSE(match(R"({"$length":{"min":1}})", R"("abc")"));
    CHECK(match(R"({"$length":{"max":1}})", R"({"a":1,"b":2})").has_value());
    CHECK_FALSE(match(R"({"a":{"$exists":false}})", R"({"b":1})"));
    CHECK(match(R"({"a":{"$exists":false}})", R"({"a":1})").has_value());
    CHECK_FALSE(match(R"({"a":{"$exists":true}})", R"({"a":null})"));
    CHECK_FALSE(match(R"({"$oneOf":[1,"two"]})", R"("two")"));
    CHECK(match(R"({"$oneOf":[1,"two"]})", "3").has_value());
    CHECK_FALSE(match(R"({"$contains":{"name":"Board"}})", R"([{"name":"Wall"},{"name":"Board","id":"1"}])"));
    CHECK(match(R"({"$contains":{"name":"Board"}})", R"([{"name":"Wall"}])").has_value());
    CHECK(match(R"({"$nope":1})", "1")->find("unknown matcher") != std::string::npos);
}

TEST_CASE("a launched editor answers calls over stdio and exits cleanly")
{
    test::TempDirectory workspace;
    Expected<RpcClient, std::string> client = RpcClient::launch(editorLaunch(workspace));
    REQUIRE_MESSAGE(client.hasValue(), (client ? std::string() : client.error()));
    Expected<std::string, RpcError> created =
        client->call("project.create", R"({"directory":"game","name":"Game","template":"3d-starter"})");
    REQUIRE_MESSAGE(created.hasValue(), (created ? std::string() : created.error().message));
    CHECK(test::parseJson(*created).root().find("name").asString() == "Game");
    Expected<std::string, RpcError> missing = client->call("entity.get", R"({"entity":"/Nope"})");
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().code == 1200);
    CHECK(test::parseJson(missing.error().data).root().find("kind").asString() == "not_found");
    Expected<std::string, RpcError> bad = client->call("scene.info", "[nope");
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().code == -32700);
    CHECK(client->process() != nullptr);
    CHECK(client->close() == 0);
    CHECK_FALSE(client->connected());
    CHECK(std::filesystem::exists(workspace.path / "game" / "project.illusion"));
}

TEST_CASE("the transcript runner captures, substitutes, restarts and checks")
{
    test::TempDirectory workspace;
    RunnerOptions options;
    options.launch = editorLaunch(workspace);
    std::vector<std::string> log;
    options.log = [&](std::string_view line) {
        log.emplace_back(line);
    };
    TranscriptRunner runner(std::move(options));
    const std::string_view transcript = R"(
{"comment": "a project with one entity, saved"}
{"call": "project.create", "params": {"directory": "game", "name": "Game"}, "capture": {"root": "/root"}}
{"call": "entity.create", "params": {"name": "Board"}, "capture": {"board": "/entity/id"}, "expect": {"result": {"entity": {"name": "Board", "path": "/Board"}}}}
{"call": "entity.get", "params": {"entity": "${board}"}, "expect": {"result": {"entity": {"id": "${board}"}}}}
{"call": "entity.get", "params": {"entity": "/Missing"}, "expect": {"error": {"code": 1200, "data": {"kind": "not_found"}}}}
{"call": "scene.save", "capture": {"saved": "/path"}}
{"call": "scene.info", "capture": {"hash": "/hash"}}
{"call": "render.stats", "requires": "gpu", "otherwise": {"error": {"code": -32601}}}
{"restart": {"arguments": ["--project", "${root}"]}}
)";
    const RunReport replay = runner.run(transcript, "scripted.jsonl");
    for (const StepFailure& failure : replay.failures)
    {
        MESSAGE(failure.line << ": " << failure.description << ": " << failure.reason);
    }
    CHECK(replay.passed());
    CHECK(replay.steps == 8);
    CHECK(runner.variables().at("saved") == R"("assets:/scenes/main.scene")");

    const std::string_view checks = R"(
{"check": "P1", "title": "the scene came back", "call": "scene.info", "expect": {"result": {"hash": "${hash}", "entities": 1}}}
{"check": "P1", "call": "entity.find", "params": {"name": "Board"}, "expect": {"result": {"entities": {"$length": 1}}}}
{"check": "P2", "title": "a failing check", "call": "scene.info", "expect": {"result": {"entities": 5}}}
{"check": "P2", "call": "scene.info"}
{"check": "P3", "title": "runs after a failed check", "call": "entity.find", "params": {"name": "${unknown}"}}
{"check": "P4", "title": "ok", "call": "selection.get"}
)";
    const RunReport report = runner.run(checks, "checks.jsonl");
    CHECK(report.passedChecks == std::vector<std::string>{"P1", "P4"});
    CHECK(report.failedChecks == std::vector<std::string>{"P2", "P3"});
    REQUIRE(report.failures.size() == 2);
    CHECK(report.failures[0].line == 4);
    CHECK(report.failures[0].reason == "/entities: expected 5, got 1");
    CHECK(report.failures[1].reason == "unknown variable ${unknown}");
    CHECK(runner.stop() == 0);
    CHECK_FALSE(log.empty());
}

TEST_CASE("a replay stops at the first diverging step")
{
    test::TempDirectory workspace;
    RunnerOptions options;
    options.launch = editorLaunch(workspace);
    TranscriptRunner runner(std::move(options));
    const RunReport report = runner.run(
        "{\"call\": \"selection.get\"}\n{\"call\": \"entity.get\", \"params\": {\"entity\": \"/X\"}}\n"
        "{\"call\": \"selection.get\"}\nnot json\n",
        "broken.jsonl");
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures[0].line == 2);
    CHECK(report.failures[0].reason.find("1200") != std::string::npos);
    CHECK(report.steps == 2);

    const RunReport garbage = runner.run("not json\n", "garbage.jsonl");
    REQUIRE(garbage.failures.size() == 1);
    CHECK(garbage.failures[0].reason == "line is not a JSON object");
    runner.stop();
}
