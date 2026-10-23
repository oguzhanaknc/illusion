#include "EditorCoreTestSupport.hpp"

#include <illusion/core/Log.hpp>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// Logs from Info on during a test and restores the logger's level afterwards.
struct LevelGuard
{
    LogLevel level = Logger::instance().level();
    LevelGuard() { Logger::instance().setLevel(LogLevel::Info); }
    ~LevelGuard() { Logger::instance().setLevel(level); }
    LevelGuard(const LevelGuard&) = delete;
    LevelGuard& operator=(const LevelGuard&) = delete;
};

} // namespace

TEST_CASE("log.read returns recent messages filtered by level, category and text, after a cursor")
{
    LevelGuard guard;
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ILL_LOG_INFO("TestLog", "hello {}", 1);
    ILL_LOG_WARN("TestLog", "careful");
    ILL_LOG_INFO("OtherLog", "elsewhere");

    const JsonDocument mine = ok(editor, "log.read", R"({"category":"TestLog"})");
    REQUIRE(mine.root().find("entries").size() == 2);
    CHECK(mine.root().pointer("/entries/0/text").asString() == "hello 1");
    CHECK(mine.root().pointer("/entries/0/level").asString() == "info");
    CHECK(mine.root().pointer("/entries/1/level").asString() == "warn");
    CHECK(
        ok(editor, "log.read", R"({"category":"TestLog","minLevel":"warn"})").root().find("entries").size() ==
        1);
    CHECK(ok(editor, "log.read", R"({"contains":"elsew"})").root().find("entries").size() == 1);
    const JsonDocument newest = ok(editor, "log.read", R"({"category":"TestLog","limit":1})");
    REQUIRE(newest.root().find("entries").size() == 1);
    CHECK(newest.root().pointer("/entries/0/text").asString() == "careful");

    const std::string last(ok(editor, "log.read").root().find("last").asString().value_or(""));
    ILL_LOG_INFO("TestLog", "later");
    const JsonDocument after = ok(editor, "log.read", R"({"since":")" + last + R"("})");
    REQUIRE(after.root().find("entries").size() == 1);
    CHECK(after.root().pointer("/entries/0/text").asString() == "later");

    CHECK(fails(editor, "log.read", R"({"minLevel":"loud"})").errors.at(0).pointer == "/minLevel");
    CHECK(fails(editor, "log.read", R"({"minLevel":"off"})").errors.at(0).pointer == "/minLevel");
    CHECK(fails(editor, "log.read", R"({"limit":0})").errors.at(0).pointer == "/limit");
    CHECK(fails(editor, "log.read", R"({"limit":1001})").errors.at(0).pointer == "/limit");
}

TEST_CASE("log.clear hides earlier messages and log.set_level changes what is logged")
{
    LevelGuard guard;
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ILL_LOG_WARN("TestLog", "before");
    ok(editor, "log.clear", R"({"dryRun":true})");
    CHECK(ok(editor, "log.read", R"({"category":"TestLog"})").root().find("entries").size() == 1);
    ok(editor, "log.clear");
    CHECK(ok(editor, "log.read", R"({"category":"TestLog"})").root().find("entries").size() == 0);

    CHECK(ok(editor, "log.set_level", R"({"level":"error"})").root().find("level").asString() == "error");
    CHECK(Logger::instance().level() == LogLevel::Error);
    ILL_LOG_WARN("TestLog", "dropped");
    ILL_LOG_ERROR("TestLog", "kept");
    const JsonDocument read = ok(editor, "log.read", R"({"category":"TestLog"})");
    REQUIRE(read.root().find("entries").size() == 1);
    CHECK(read.root().pointer("/entries/0/text").asString() == "kept");
    ok(editor, "log.set_level", R"({"level":"warn","dryRun":true})");
    CHECK(Logger::instance().level() == LogLevel::Error);
    const CommandError level = fails(editor, "log.set_level", R"({"level":"eror"})");
    CHECK(level.errors.at(0).pointer == "/level");
    CHECK(level.suggestions.at(0) == "error");
}

TEST_CASE("an editor without a log ring says so")
{
    EditorContext editor(EditorContextOptions{.seed = 4, .logCapacity = 0});
    CHECK(editor.log() == nullptr);
    CHECK(fails(editor, "log.read", "{}").kind == ErrorKind::Unsupported);
    CHECK(fails(editor, "log.clear", "{}").kind == ErrorKind::Unsupported);
}

TEST_CASE("log.read with a cursor pages from the oldest message, so none is lost")
{
    LevelGuard guard;
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const std::string start(ok(editor, "log.read").root().find("last").asString().value_or(""));
    for (int i = 0; i < 5; ++i)
    {
        ILL_LOG_INFO("PageLog", "page {}", i);
    }
    std::string cursor = start;
    std::vector<std::string> texts;
    for (int call = 0; call < 3; ++call)
    {
        const JsonDocument page =
            ok(editor, "log.read", R"({"category":"PageLog","limit":2,"since":")" + cursor + R"("})");
        for (const JsonValue entry : page.root().find("entries").elements())
        {
            texts.emplace_back(entry.find("text").asString().value_or(""));
        }
        cursor = std::string(page.root().find("last").asString().value_or(""));
    }
    CHECK(texts == std::vector<std::string>{"page 0", "page 1", "page 2", "page 3", "page 4"});
    // Without a cursor the newest come back.
    const JsonDocument newest = ok(editor, "log.read", R"({"category":"PageLog","limit":2})");
    CHECK(newest.root().pointer("/entries/1/text").asString() == "page 4");
}
