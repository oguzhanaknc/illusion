#include "EditorCoreTestSupport.hpp"

#include <illusion/core/Log.hpp>

#include <string>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// Records @p count frames with an "update" phase that holds a "physics" scope.
void recordFrames(EditorContext& editor, int count)
{
    FrameProfiler& profiler = editor.profiler();
    for (int i = 0; i < count; ++i)
    {
        profiler.beginFrame();
        {
            const FrameProfiler::Scope update(profiler, "update");
            const FrameProfiler::Scope physics(profiler, "physics");
        }
        profiler.endFrame();
    }
}

// The issues of `diagnostics.get` with @p code.
std::vector<JsonValue> issuesWith(const JsonDocument& result, std::string_view code)
{
    std::vector<JsonValue> out;
    for (const JsonValue issue : result.root().find("issues").elements())
    {
        if (issue.find("code").asString() == code)
        {
            out.push_back(issue);
        }
    }
    return out;
}

} // namespace

TEST_CASE("profiler.get reports the newest frames, oldest first, with the time per place")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    JsonDocument result = ok(editor, "profiler.get");
    CHECK(result.root().find("frames").size() == 0);
    CHECK(result.root().find("averageMs").asF64() == 0.0);
    CHECK(result.root().find("scopes").size() == 0);

    recordFrames(editor, 5);
    result = ok(editor, "profiler.get", R"({"frames":3})");
    CHECK(result.root().find("frames").size() == 3);
    CHECK(result.root().find("frameIndex").asU64() == std::optional<u64>(5));
    CHECK(result.root().find("paused").asBool() == std::optional<bool>(false));
    CHECK(result.root().find("truncated").asBool() == std::optional<bool>(false));
    CHECK(result.root().find("maxMs").asF64().value_or(-1.0) >=
          result.root().find("averageMs").asF64().value_or(0.0));
    REQUIRE(result.root().find("scopes").size() == 2);
    CHECK(result.root().pointer("/scopes/0/name").asString() == "update");
    CHECK(result.root().pointer("/scopes/0/depth").asU64() == std::optional<u64>(0));
    CHECK(result.root().pointer("/scopes/1/name").asString() == "physics");
    CHECK(result.root().pointer("/scopes/1/depth").asU64() == std::optional<u64>(1));
    CHECK(result.root().pointer("/scopes/1/calls").asU64() == std::optional<u64>(1));

    // More frames than recorded report what there is; outside 1..240 is refused.
    CHECK(ok(editor, "profiler.get", R"({"frames":240})").root().find("frames").size() == 5);
    CHECK(fails(editor, "profiler.get", R"({"frames":0})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "profiler.get", R"({"frames":241})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("profiler.set pauses and resumes recording and clears the frames")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    recordFrames(editor, 2);
    JsonDocument result = ok(editor, "profiler.set", R"({"paused":true})");
    CHECK(result.root().find("paused").asBool() == std::optional<bool>(true));
    CHECK(result.root().find("frames").asU64() == std::optional<u64>(2));
    recordFrames(editor, 3);
    CHECK(editor.profiler().frameCount() == 2);
    CHECK(ok(editor, "profiler.get").root().find("paused").asBool() == std::optional<bool>(true));

    // Without paused, the state stays; clear forgets the frames.
    result = ok(editor, "profiler.set", R"({"clear":true})");
    CHECK(result.root().find("paused").asBool() == std::optional<bool>(true));
    CHECK(result.root().find("frames").asU64() == std::optional<u64>(0));
    ok(editor, "profiler.set", R"({"paused":false})");
    recordFrames(editor, 1);
    CHECK(editor.profiler().frameCount() == 1);
}

TEST_CASE("diagnostics.get lists errors first: validation, log, missing assets, then warnings")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "log.clear");
    JsonDocument result = ok(editor, "diagnostics.get");
    CHECK(result.root().find("errors").asU64() == std::optional<u64>(0));
    CHECK(result.root().find("warnings").asU64() == std::optional<u64>(0));
    CHECK(result.root().find("issues").size() == 0);

    // A scene with an instance of a prefab that does not exist, and an entity whose Look refers to a model
    // that does not exist either.
    ok(editor, "project.file_write",
       R"({"path":"assets:/scenes/broken.scene","text":"{\"format\": \"illusion.scene\", \"version\": 2, )"
       R"(\"entities\": [{\"prefab\": \"assets:/props/gone.prefab\", \"id\": \"0000000000001000\"}, )"
       R"({\"id\": \"0000000000000055\", \"components\": {\"test.Look\": {\"material\": )"
       R"(\"assets:/materials/none.material\"}}}]}"})");
    ok(editor, "scene.open", R"({"path":"assets:/scenes/broken.scene","discardChanges":true})");
    fixture.viewports.validationErrors = 2;
    fixture.viewports.stats.lightsDropped = 3;
    ok(editor, "log.clear");
    ILL_LOG_ERROR("Test", "something failed");
    ILL_LOG_WARN("Test", "something looks wrong");
    ILL_LOG_WARN("Test", "something else looks wrong");

    result = ok(editor, "diagnostics.get");
    const JsonValue issues = result.root().find("issues");
    REQUIRE(issues.size() >= 6);
    CHECK(result.root().find("errors").asU64() == std::optional<u64>(4));
    CHECK(issues[0].find("code").asString() == "render.validation");
    CHECK(issues[0].find("count").asU64() == std::optional<u64>(2));
    CHECK(issues[1].find("code").asString() == "log.errors");
    CHECK(issues[1].find("count").asU64() == std::optional<u64>(1));
    CHECK(issues[2].find("code").asString() == "asset.missing");
    CHECK(issues[2].find("subject").asString() == "assets:/materials/none.material");
    CHECK(issues[3].find("subject").asString() == "assets:/props/gone.prefab");
    CHECK(issues[3].find("count").asU64() == std::optional<u64>(1));
    for (u32 i = 0; i < 4; ++i)
    {
        CHECK(issues[i].find("severity").asString() == "error");
    }

    // One lights warning per viewport, the log's warnings and the instance that could not be loaded.
    const std::vector<JsonValue> lights = issuesWith(result, "render.lights_dropped");
    REQUIRE(lights.size() == 2);
    CHECK(lights[0].find("subject").asString() == "scene");
    CHECK(lights[0].find("count").asU64() == std::optional<u64>(3));
    CHECK(lights[1].find("severity").asString() == "warning");
    const std::vector<JsonValue> warnings = issuesWith(result, "log.warnings");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("count").asU64() == std::optional<u64>(2));
    const std::vector<JsonValue> prefabs = issuesWith(result, "prefab.unresolved");
    REQUIRE(prefabs.size() == 1);
    CHECK(prefabs[0].find("subject").asString().value_or("").size() == 16);
    CHECK(prefabs[0].find("message").asString().value_or("").find("assets:/props/gone.prefab") !=
          std::string_view::npos);
    CHECK(result.root().find("warnings").asU64() == std::optional<u64>(4));

    // Without viewports there is no render issue.
    editor.setViewports(nullptr);
    CHECK(issuesWith(ok(editor, "diagnostics.get"), "render.validation").empty());
}
