// The saves.* commands (ADR 0058) on a real Play mode session: they queue the request on the running game
// and finish with its event.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/save/SaveComponents.hpp>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

constexpr f64 kFrame = 1.0 / 60.0;

struct SavesFixture
{
    SavesFixture()
    {
        reflectPhysicsComponents();
        reflectAudioComponents();
        reflectParticleComponents();
        registerSaveComponents();
        play = std::make_unique<PlaySession>(
            *base.editor, PlaySessionDesc{.jobs = &jobs, .audioOutput = AudioOutput::Offline});
        ok(editor(), "entity.create",
           R"({"name":"Hero","components":{"illusion.Transform":{"position":[1,0,0]},"illusion.Saved":{}}})");
    }

    EditorContext& editor() { return *base.editor; }

    // Runs an async saves command, advancing the game between its updates as the editor's frames do.
    Expected<std::string, CommandError> run(std::string_view name, std::string_view params)
    {
        const JsonDocument doc = parseJson(params);
        Expected<std::unique_ptr<RunningCommand>, CommandError> started =
            editor().startJson(name, doc.root());
        if (!started)
        {
            return unexpected(std::move(started).error());
        }
        for (u32 frame = 0; frame < 20; ++frame)
        {
            Expected<std::optional<std::string>, CommandError> updated = started.value()->update();
            if (!updated)
            {
                return unexpected(std::move(updated).error());
            }
            if (updated->has_value())
            {
                play->update(kFrame); // the slot list and the World see the finished request
                return std::move(**updated);
            }
            play->update(kFrame);
        }
        return unexpected(CommandError::make(ErrorKind::Timeout, "no event after 20 updates"));
    }

    JsonDocument okRun(std::string_view name, std::string_view params)
    {
        Expected<std::string, CommandError> result = run(name, params);
        REQUIRE_MESSAGE(result.hasValue(),
                        (std::string(name) + ": " + (result ? "" : result.error().message)));
        return parseJson(*result);
    }

    f64 heroX()
    {
        const JsonDocument got =
            ok(editor(), "component.get", R"({"entity":"/Hero","type":"illusion.Transform"})");
        return got.root().pointer("/value/position/0").asF64().value_or(-1000.0);
    }

    EditorFixture base;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    std::unique_ptr<PlaySession> play;
};

} // namespace

TEST_CASE("saves.write then saves.list shows the slot with its metadata")
{
    SavesFixture f;
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    f.play->update(kFrame);
    CHECK(ok(f.editor(), "saves.list").root().find("slots").size() == 0);

    const JsonDocument written =
        f.okRun("saves.write",
                R"({"slot":"slot1","title":"Chapter 2","playSeconds":12.5,"gameVersion":3,"custom":"x"})");
    CHECK(written.root().find("slot").asString() == "slot1");
    CHECK(written.root().find("event").asString() == "Saved");

    const JsonDocument listed = ok(f.editor(), "saves.list");
    REQUIRE(listed.root().find("slots").size() == 1);
    const JsonValue slot = listed.root().pointer("/slots/0");
    CHECK(slot.find("name").asString() == "slot1");
    CHECK(slot.find("title").asString() == "Chapter 2");
    CHECK(slot.find("playSeconds").asF64() == 12.5);
    CHECK(slot.find("gameVersion").asF64() == 3.0);
    CHECK(slot.find("custom").asString() == "x");
    CHECK(slot.find("state").asString() == "Ok");
    CHECK(slot.find("savedAt").asF64().value_or(0.0) > 0.0);
}

TEST_CASE("saves.load restores a saved entity that the game changed")
{
    SavesFixture f;
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    f.play->update(kFrame);
    f.okRun("saves.write", R"({"slot":"slot1"})");
    ok(f.editor(), "transform.translate", R"({"entities":["/Hero"],"offset":[4,0,0]})");
    CHECK(f.heroX() == 5.0);

    const JsonDocument loaded = f.okRun("saves.load", R"({"slot":"slot1"})");
    CHECK(loaded.root().find("event").asString() == "Loaded");
    CHECK(f.heroX() == 1.0);
}

TEST_CASE("saves.remove deletes the slot and reports a missing one")
{
    SavesFixture f;
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    f.play->update(kFrame);
    f.okRun("saves.write", R"({"slot":"slot1"})");

    CHECK(f.okRun("saves.remove", R"({"slot":"slot1"})").root().find("event").asString() == "Removed");
    CHECK(ok(f.editor(), "saves.list").root().find("slots").size() == 0);

    const Expected<std::string, CommandError> again = f.run("saves.remove", R"({"slot":"slot1"})");
    REQUIRE_FALSE(again.hasValue());
    CHECK(again.error().kind == ErrorKind::NotFound);
    const Expected<std::string, CommandError> missing = f.run("saves.load", R"({"slot":"nothing"})");
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().kind == ErrorKind::NotFound);
    CHECK(missing.error().message.find("NotFound") != std::string::npos);
}

TEST_CASE("the saves commands need a running Play mode game")
{
    SavesFixture f;
    for (const std::string_view name : {"saves.list", "saves.write", "saves.load", "saves.remove"})
    {
        CAPTURE(name);
        const CommandError error =
            fails(f.editor(), name, name == "saves.list" ? "{}" : R"({"slot":"slot1"})");
        CHECK(error.kind == ErrorKind::WrongMode);
        CHECK(error.message.find("play.start") != std::string::npos);
    }
    ok(f.editor(), "play.start", R"({"mode":"simulate"})");
    CHECK(fails(f.editor(), "saves.list", "{}").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(f.editor(), "saves.write", R"({"slot":"slot1"})").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("a request that the game's end interrupts fails")
{
    SavesFixture f;
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    const JsonDocument doc = parseJson(R"({"slot":"slot1"})");
    Expected<std::unique_ptr<RunningCommand>, CommandError> started =
        f.editor().startJson("saves.write", doc.root());
    REQUIRE(started.hasValue());
    ok(f.editor(), "play.stop");
    const Expected<std::optional<std::string>, CommandError> updated = started.value()->update();
    REQUIRE_FALSE(updated.hasValue());
    CHECK(updated.error().kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("the saves commands reject invalid slot names and metadata")
{
    SavesFixture f;
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    for (const std::string_view name : {"saves.write", "saves.load", "saves.remove"})
    {
        for (const std::string_view slot :
             {"", "../x", "a b", "slot.1", "a234567890123456789012345678901234"})
        {
            CAPTURE(name);
            CAPTURE(slot);
            const CommandError error = fails(f.editor(), name, R"({"slot":")" + std::string(slot) + R"("})");
            CHECK(error.kind == ErrorKind::ValidationFailed);
            REQUIRE_FALSE(error.errors.empty());
            CHECK(error.errors.front().pointer == "/slot");
        }
    }
    const std::string longText(200, 'a');
    CHECK(fails(f.editor(), "saves.write", R"({"slot":"s","title":")" + longText + R"("})")
              .errors.front()
              .pointer == "/title");
    CHECK(fails(f.editor(), "saves.write", R"({"slot":"s","custom":")" + std::string(2000, 'a') + R"("})")
              .errors.front()
              .pointer == "/custom");
    CHECK(fails(f.editor(), "saves.write", R"({"slot":"s","playSeconds":-1})").errors.front().pointer ==
          "/playSeconds");
}
