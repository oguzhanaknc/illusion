// The Animator's section of the Inspector (ADR 0059-0061): the scrubber runs animation.scrub in one undo
// step, the state view follows the running game's graph, its parameters and events, and idle frames do not
// allocate, also because asset fields check their files only after the assets change.

#include "shell/ShellTestSupport.hpp"

#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>

#include <string>

using namespace illusion;
using namespace illusion::editor;
using test::ShellFixture;

namespace
{

constexpr std::string_view kCard = "inspector.card.illusion.Animator";

std::string itemId(std::string_view suffix)
{
    return std::string(kCard) + std::string(suffix);
}

// The showcase column, with or without its graph, selected.
void createColumn(EditorContext& editor, bool graph)
{
    test::installAnimationAssets(editor);
    std::string params =
        R"({"name":"Column","components":{"illusion.Transform":{},"illusion.Animator":{)"
        R"("skeleton":"assets:/models/column.gltf#skin/0","clip":"assets:/models/column.gltf#animation/sway",)";
    if (graph)
    {
        params += R"("graph":"assets:/animations/column.animgraph",)";
    }
    params += R"("speed":0.8,"loop":false}}})";
    test::ok(editor, "entity.create", params);
    test::ok(editor, "selection.set", R"({"entities":["/Column"]})");
}

f64 columnTime(EditorContext& editor)
{
    const JsonDocument status =
        test::ok(editor, "animation.status", R"({"entity":"/Column","world":"edit"})");
    return status.root().pointer("/animators/0/time").asF64().value_or(-1.0);
}

std::string labelOf(const ui::UiDriver& driver, std::string_view id)
{
    const std::optional<ui::UiItem> item = driver.find(id);
    return item ? item->label : std::string();
}

// Poses the edit World as the viewports do, for the fake viewports of the shell tests.
struct EditPoses
{
    explicit EditPoses(EditorContext& editor)
        : streamer(editor.files(), jobs, AssetStreamerDesc{.cacheMount = ""})
        , animation(editor.world(), streamer)
    {
        viewports.animation = &animation;
        editor.setViewports(&viewports);
    }

    JobSystem jobs;
    AssetStreamer streamer;
    AnimationWorld animation;
    test::FakeViewports viewports;
};

} // namespace

TEST_CASE("the Animator's scrubber sets the time through animation.scrub and one undo restores it")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    createColumn(editor, false);
    {
        // Without viewports or a game, only the saved fields and a hint show.
        ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
        driver.runFrames(3);
        CHECK(driver.find(itemId(".scrub")) == std::nullopt);
        CHECK(driver.find(itemId(".layer.0")) == std::nullopt);
    }

    EditPoses poses(editor);
    poses.animation.waitIdle();
    ui::UiDriver driver(fixture.context, [&] {
        poses.animation.update(0.0f, nullptr, false);
        fixture.frame();
    });
    driver.runFrames(3);
    REQUIRE(driver.find(itemId(".scrub")).has_value());
    CHECK(columnTime(editor) == doctest::Approx(0.0));

    REQUIRE(driver.drag(itemId(".scrub"), Vec2(40.0f, 0.0f), 6));
    driver.runFrames(2);
    const f64 scrubbed = columnTime(editor);
    CHECK(scrubbed > 0.0);
    test::ok(editor, "history.undo");
    CHECK(columnTime(editor) == doctest::Approx(0.0));
    test::ok(editor, "history.redo");
    CHECK(columnTime(editor) == doctest::Approx(scrubbed));
    editor.setViewports(nullptr);
}

TEST_CASE("the Animator's state view shows the running graph's states, sets its parameters and lists events")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    EditorContext* current = &editor;
    test::FakePlay play(current);
    editor.setPlay(&play);
    createColumn(editor, true);
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    test::ok(editor, "play.start");
    test::ok(editor, "play.step");
    driver.runFrames(3);

    CHECK(labelOf(driver, itemId(".layer.0")).starts_with("hold###"));
    CHECK(driver.find(itemId(".scrub")) == std::nullopt); // no scrubbing in Play mode
    CHECK(labelOf(driver, itemId(".event.0")).empty());

    // The Bool parameter through animation.set_parameter.
    REQUIRE(driver.click(itemId(".parameter.calm")));
    test::ok(editor, "play.step");
    driver.runFrames(1);
    const JsonDocument status = test::ok(editor, "animation.status", R"({"entity":"/Column"})");
    bool calm = false;
    for (const JsonValue parameter : status.root().pointer("/animators/0/parameters").elements())
    {
        if (parameter.find("name").asString() == "calm")
        {
            calm = parameter.find("value").asF64().value_or(0.0) == 1.0;
        }
    }
    CHECK(calm);

    // The Trigger fires the transition to "swing", which fades "hold" out.
    REQUIRE(driver.click(itemId(".parameter.bow")));
    test::ok(editor, "play.step");
    driver.runFrames(1);
    CHECK(labelOf(driver, itemId(".layer.0")).starts_with("swing (from hold)###"));

    // The state event "peak" halfway through "swing" appears in the event list.
    for (int i = 0; i < 240 && labelOf(driver, itemId(".event.0")).empty(); ++i)
    {
        test::ok(editor, "play.step");
        driver.runFrames(1);
    }
    CHECK(labelOf(driver, itemId(".event.0")).starts_with("peak · base/swing###"));

    play.stop();
    driver.runFrames(1);
    CHECK(driver.find(itemId(".event.0")) == std::nullopt);
    editor.setPlay(nullptr);
}

TEST_CASE("an idle shell frame with an Animator in the Inspector does not allocate")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    createColumn(editor, true);
    EditPoses poses(editor);
    poses.animation.waitIdle();
    (void)editor.assets();
    fixture.settleAssetEvents();
    for (int i = 0; i < 120; ++i)
    {
        poses.animation.update(0.0f, nullptr, false);
        fixture.frame();
    }
    CHECK(fixture.longestIdleRun(60, 600) == 60);
    editor.setViewports(nullptr);
}

TEST_CASE("an asset field warns about a missing file until the file appears, also for a part of a file "
          "(regression)")
{
    ShellFixture fixture(test::ShellSetup{.assetWatching = FileWatchBackend::Polling});
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create",
             R"({"name":"Rock","components":{"illusion.Transform":{},)"
             R"("illusion.MeshRenderer":{"mesh":"assets:/models/column.gltf#mesh/0"}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Rock"]})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    const std::string fix = "inspector.card.illusion.MeshRenderer.mesh.fix.clear";
    CHECK(driver.find(fix).has_value());
    test::installAnimationAssets(editor);
    fixture.settleAssetEvents();
    driver.runFrames(2);
    CHECK(driver.find(fix) == std::nullopt);
}
