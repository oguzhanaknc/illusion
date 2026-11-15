// The tech demo's game UI (samples/techdemo): its module instantiates assets:/scenes/ui.scene, a HUD and a
// modal pause menu, and drives them through UiService. The forest scene needs downloaded assets, so these
// tests run the module in a project that has only the committed UI scene, string tables and font.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/ui/UiComponents.hpp>

#include <filesystem>
#include <string>
#include <string_view>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

// A project with the tech demo's UI files, in Play mode with the tech demo module.
struct TechDemoFixture
{
    TechDemoFixture()
    {
        const std::filesystem::path source = utf8Path(ILLUSION_SAMPLES_DIR) / "techdemo" / "assets";
        const std::filesystem::path assets = base.directory.path / "project" / "assets";
        std::filesystem::create_directories(assets / "scenes");
        std::filesystem::copy_file(source / "scenes" / "ui.scene", assets / "scenes" / "ui.scene");
        for (const std::string_view folder : {"locales", "fonts"})
        {
            std::filesystem::copy(source / folder, assets / folder, std::filesystem::copy_options::recursive);
        }
        play = std::make_unique<PlaySession>(
            *base.editor, PlaySessionDesc{.jobs = &jobs, .audioOutput = AudioOutput::Offline});
        const Expected<std::string, CommandError> loaded = play->loadModule(TECHDEMO_MODULE);
        REQUIRE_MESSAGE(loaded.hasValue(), (loaded ? std::string() : loaded.error().message));
        ok(editor(), "play.start", R"({"fixedStep":true})");
        step();
    }

    ~TechDemoFixture()
    {
        play->stop();
        (void)play->unloadModule("techdemo");
    }

    TechDemoFixture(const TechDemoFixture&) = delete;
    TechDemoFixture& operator=(const TechDemoFixture&) = delete;

    EditorContext& editor() { return *base.editor; }

    void step(u32 steps = 1) { ok(editor(), "play.step", std::format(R"({{"steps":{}}})", steps)); }

    void key(std::string_view name, bool down)
    {
        ok(editor(), "input.inject",
           std::format(R"({{"keys":[{{"key":"{}","down":{}}}]}})", name, down ? "true" : "false"));
    }

    // Presses and releases @p name, then gives the module the update that reads the UI's events.
    void press(std::string_view name)
    {
        key(name, true);
        step();
        key(name, false);
        step(2);
    }

    // Clicks the element at @p path and gives the module the update that reads the click.
    void click(std::string_view path)
    {
        const JsonDocument clicked = ok(editor(), "gameui.click", std::format(R"({{"entity":"{}"}})", path));
        CHECK(clicked.root().pointer("/events/0/entity/path").asString() == path);
        step();
    }

    // Whether the element at @p path is laid out and drawn in the play World.
    bool shown(std::string_view path)
    {
        const JsonDocument layout = ok(editor(), "gameui.layout", std::format(R"({{"entity":"{}"}})", path));
        const JsonValue elements = layout.root().find("elements");
        return elements.size() > 0 && elements[0].find("visible").asBool().value_or(false);
    }

    // The value of the action @p name: x and y.
    Vec2 action(std::string_view name)
    {
        const JsonDocument read = ok(editor(), "input.read", std::format(R"({{"actions":["{}"]}})", name));
        const JsonValue value = read.root().pointer("/actions/0/value");
        return {static_cast<f32>(value[0].asF64().value_or(-1.0)),
                static_cast<f32>(value[1].asF64().value_or(-1.0))};
    }

    // The play World's Entity named @p name.
    Entity named(std::string_view name)
    {
        Entity found{};
        play->game()->world().query<const Name>().each([&](Entity entity, const Name& entityName) {
            if (entityName.value == name)
            {
                found = entity;
            }
        });
        return found;
    }

    // The number argument @p argument of the UiText named @p name.
    f64 number(std::string_view name, std::string_view argument)
    {
        const UiTextArguments* stored = play->game()->world().tryGet<UiTextArguments>(named(name));
        REQUIRE(stored != nullptr);
        for (u32 i = 0; i < stored->count; ++i)
        {
            const UiTextArgumentValue& value = stored->arguments[i];
            if (std::string_view(value.name.data(), value.nameLength) == argument)
            {
                return value.value;
            }
        }
        REQUIRE_MESSAGE(false, (std::string("no argument ") + std::string(argument)));
        return 0.0;
    }

    EditorFixture base;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    std::unique_ptr<PlaySession> play;
};

} // namespace

TEST_CASE("the tech demo instantiates its UI scene: the HUD shows and the pause menu is hidden")
{
    TechDemoFixture f;
    CHECK(f.shown("/HUD/Title"));
    CHECK(f.shown("/HUD/Hint/Menu Glyph"));
    CHECK(f.shown("/HUD/Frame Time"));
    CHECK_FALSE(f.shown("/Pause Menu/Panel"));
    CHECK(f.play->game()->ui().statistics().missingKeys == 0);
}

TEST_CASE("the tech demo's frame-time readout shows the frame rate twice a second")
{
    TechDemoFixture f;
    f.step(30);
    CHECK(f.number("Frame Time", "fps") == doctest::Approx(60.0));
    CHECK(f.number("Frame Time", "ms") == doctest::Approx(16.7));
}

TEST_CASE("Escape opens the tech demo's pause menu, which masks the game's actions, and closes it again")
{
    TechDemoFixture f;
    f.key("W", true);
    f.step();
    CHECK(f.action("Move").y == doctest::Approx(1.0));
    f.press("Escape");
    CHECK(f.shown("/Pause Menu/Panel/Resume"));
    CHECK(f.play->gameUi()->focused() == f.named("Resume"));
    CHECK(f.play->gameUi()->modalActive());
    // W still held: the walker's action reads nothing while the modal menu shows.
    CHECK(f.action("Move").y == doctest::Approx(0.0));
    f.press("Escape");
    CHECK_FALSE(f.shown("/Pause Menu/Panel"));
    f.step();
    CHECK(f.action("Move").y == doctest::Approx(1.0));
    f.key("W", false);
}

TEST_CASE("Resume closes the tech demo's pause menu")
{
    TechDemoFixture f;
    f.press("Escape");
    REQUIRE(f.shown("/Pause Menu/Panel/Resume"));
    f.click("/Pause Menu/Panel/Resume");
    CHECK_FALSE(f.shown("/Pause Menu/Panel"));
}

TEST_CASE("the tech demo's volume slider sets the master volume and its label")
{
    TechDemoFixture f;
    f.press("Escape");
    CHECK(f.number("Volume Label", "percent") == doctest::Approx(100.0));
    // A click at the middle of the slider.
    f.click("/Pause Menu/Panel/Volume");
    CHECK(static_cast<f64>(f.play->game()->audio().masterVolume()) == doctest::Approx(0.5));
    CHECK(f.number("Volume Label", "percent") == doctest::Approx(50.0));
}

TEST_CASE("the tech demo's language button switches between English and Turkish")
{
    TechDemoFixture f;
    f.press("Escape");
    CHECK(f.play->game()->localization().locale().tag() == "en");
    f.click("/Pause Menu/Panel/Language");
    CHECK(f.play->game()->localization().locale().tag() == "tr");
    f.click("/Pause Menu/Panel/Language");
    CHECK(f.play->game()->localization().locale().tag() == "en");
}

TEST_CASE("Quit in the tech demo's pause menu ends Play mode")
{
    TechDemoFixture f;
    f.press("Escape");
    static_cast<void>(ok(f.editor(), "gameui.click", R"({"entity":"/Pause Menu/Panel/Quit"})"));
    f.step();
    CHECK(ok(f.editor(), "play.status").root().find("state").asString() == "stopped");
}
