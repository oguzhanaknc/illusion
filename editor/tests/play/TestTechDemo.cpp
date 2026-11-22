// The tech demo (samples/techdemo). Its module instantiates assets:/scenes/ui.scene, a HUD and a modal pause
// menu, and drives them through UiService; most tests run it in a project that has only the committed UI
// scene, string tables and font. The creature's tests open the forest scene with the committed assets only:
// the trees, rocks and plants of the download are missing, so their colliders are too (the baked navmesh
// still avoids them).

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/ai/AiComponents.hpp>
#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/save/SaveComponents.hpp>
#include <illusion/ui/UiComponents.hpp>

#include <filesystem>
#include <optional>
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
        registerSaveComponents();
        // Stands for what the forest scene marks as saved (the player, the sky, the props).
        ok(editor(), "entity.create",
           R"({"name":"Marker","components":{"illusion.Transform":{"position":[1,0,0]},"illusion.Saved":{}}})");
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

    // Whether the element at @p path reacts to the pointer.
    bool interactable(std::string_view path)
    {
        const JsonDocument layout = ok(editor(), "gameui.layout", std::format(R"({{"entity":"{}"}})", path));
        return layout.root().pointer("/elements/0/interactable").asBool().value_or(false);
    }

    // The string table key that the status line of the pause menu shows.
    std::string status()
    {
        const UiText* text = play->game()->world().tryGet<UiText>(named("Status"));
        REQUIRE(text != nullptr);
        return text->key;
    }

    // The X of the Marker entity's world position.
    f64 markerX()
    {
        const JsonDocument got =
            ok(editor(), "component.get", R"({"entity":"/Marker","type":"illusion.Transform"})");
        return got.root().pointer("/value/position/0").asF64().value_or(-1000.0);
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

TEST_CASE("the tech demo's pause menu saves the game and loads it back")
{
    TechDemoFixture f;
    f.press("Escape");
    REQUIRE(f.shown("/Pause Menu/Panel/Save"));
    // There is no slot yet: Load is disabled.
    CHECK(f.interactable("/Pause Menu/Panel/Save"));
    CHECK_FALSE(f.interactable("/Pause Menu/Panel/Load"));
    CHECK(fails(f.editor(), "gameui.click", R"({"entity":"/Pause Menu/Panel/Load"})")
              .message.find("disabled") != std::string::npos);

    f.click("/Pause Menu/Panel/Save");
    f.step(4);
    CHECK(f.status() == "techdemo.menu.saved");
    CHECK(f.interactable("/Pause Menu/Panel/Load"));
    CHECK(f.play->saves()->slot("slot1").has_value());

    ok(f.editor(), "transform.translate", R"({"entities":["/Marker"],"offset":[4,0,0]})");
    CHECK(f.markerX() == doctest::Approx(5.0));
    f.click("/Pause Menu/Panel/Load");
    f.step(4);
    CHECK(f.status() == "techdemo.menu.loaded");
    CHECK(f.markerX() == doctest::Approx(1.0));
    CHECK(f.shown("/Pause Menu/Panel/Resume"));
    CHECK(f.play->game()->ui().statistics().missingKeys == 0);
}

namespace
{

constexpr u32 kSteps = 600;       // ten seconds of fixed steps
constexpr u32 kWanderSteps = 180; // before the scripted player sets off
constexpr f32 kReach = 2.0f;      // metres between the two that count as caught up (moveTo acceptance 1.6)

// The forest scene with the tech demo module in fixed-step Play mode, in a project with every committed asset
// of the tech demo.
struct ForestFixture
{
    ForestFixture()
    {
        reflectRenderComponents();
        reflectAudioComponents();
        registerAnimationComponents(ComponentRegistry::instance());
        registerSaveComponents();
        const std::filesystem::path source = utf8Path(ILLUSION_SAMPLES_DIR) / "techdemo" / "assets";
        const std::filesystem::path assets = base.directory.path / "project" / "assets";
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(source))
        {
            if (entry.is_directory() && entry.path().filename() != "external")
            {
                std::filesystem::copy(entry.path(), assets / entry.path().filename(),
                                      std::filesystem::copy_options::recursive |
                                          std::filesystem::copy_options::overwrite_existing);
            }
        }
        ok(editor(), "scene.open", R"({"path":"assets:/scenes/forest.scene"})");
        streamer =
            std::make_unique<AssetStreamer>(editor().files(), jobs, AssetStreamerDesc{.cacheMount = ""});
        play = std::make_unique<PlaySession>(
            *base.editor,
            PlaySessionDesc{.jobs = &jobs, .streamer = streamer.get(), .audioOutput = AudioOutput::Offline});
        const Expected<std::string, CommandError> loaded = play->loadModule(TECHDEMO_MODULE);
        REQUIRE_MESSAGE(loaded.hasValue(), (loaded ? std::string() : loaded.error().message));
        ok(editor(), "play.start", R"({"fixedStep":true})");
        creature = named("Creature");
        player = named("Player");
        REQUIRE(world().isAlive(creature));
        REQUIRE(world().isAlive(player));
    }

    ~ForestFixture()
    {
        play->stop();
        (void)play->unloadModule("techdemo");
    }

    ForestFixture(const ForestFixture&) = delete;
    ForestFixture& operator=(const ForestFixture&) = delete;

    EditorContext& editor() { return *base.editor; }
    World& world() { return play->game()->world(); }

    void step(u32 steps = 1)
    {
        ok(editor(), "play.step", std::format(R"({{"steps":{}}})", steps));
        stepped += steps;
    }

    void key(std::string_view name, bool down)
    {
        ok(editor(), "input.inject",
           std::format(R"({{"keys":[{{"key":"{}","down":{}}}]}})", name, down ? "true" : "false"));
    }

    Entity named(std::string_view name)
    {
        Entity found{};
        world().query<const Name>().each([&](Entity entity, const Name& entityName) {
            if (entityName.value == name)
            {
                found = entity;
            }
        });
        return found;
    }

    Vec3 position(Entity entity) { return world().get<Transform>(entity).position; }

    // The perception's target of the creature: the player once it has noticed them.
    Entity target()
    {
        const AiAgentState* state = world().tryGet<AiAgentState>(creature);
        return state != nullptr ? state->target : Entity{};
    }

    bool shown(std::string_view path)
    {
        const JsonDocument layout = ok(editor(), "gameui.layout", std::format(R"({{"entity":"{}"}})", path));
        const JsonValue elements = layout.root().find("elements");
        return elements.size() > 0 && elements[0].find("visible").asBool().value_or(false);
    }

    EditorFixture base;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    std::unique_ptr<AssetStreamer> streamer;
    std::unique_ptr<PlaySession> play;
    Entity creature{};
    Entity player{};
    u32 stepped = 0;
};

f32 flatDistance(const Vec3& a, const Vec3& b)
{
    return glm::length(Vec2(a.x - b.x, a.z - b.z));
}

// What one run of the creature's scenario saw.
struct CreatureRun
{
    f32 wandered = 0.0f;          // metres from the den after kWanderSteps
    std::optional<u32> noticed{}; // the step at which it chose the player as its target
    std::optional<u32> reached{}; // the step at which it was within kReach of the player
    bool alertShown = false;      // the HUD's alert showed when it reached the player
    Vec3 creatureEnd{0.0f};
    Vec3 playerEnd{0.0f};
};

// The creature wanders; the player walks towards its den (W and A: forward and left, away from the fire) and
// stops once it has noticed them; it runs up to the player. Ten seconds of fixed steps in all.
CreatureRun runCreature()
{
    ForestFixture f;
    CreatureRun run;
    const Vec3 den = f.position(f.creature);
    f.step(kWanderSteps);
    run.wandered = flatDistance(f.position(f.creature), den);
    CHECK(f.target().isNull());
    f.key("W", true);
    f.key("A", true);
    while (f.stepped < kSteps && !run.noticed)
    {
        f.step();
        if (f.target() == f.player)
        {
            run.noticed = f.stepped;
        }
    }
    f.key("W", false);
    f.key("A", false);
    while (f.stepped < kSteps && !run.reached)
    {
        f.step();
        if (flatDistance(f.position(f.creature), f.position(f.player)) <= kReach)
        {
            run.reached = f.stepped;
            run.alertShown = f.shown("/HUD/Creature Alert");
        }
    }
    if (f.stepped < kSteps)
    {
        f.step(kSteps - f.stepped);
    }
    run.creatureEnd = f.position(f.creature);
    run.playerEnd = f.position(f.player);
    return run;
}

} // namespace

TEST_CASE("the tech demo's creature wanders, notices the player walking up and reaches them, the same twice")
{
    const CreatureRun first = runCreature();
    CHECK(first.wandered > 0.5f);
    REQUIRE(first.noticed.has_value());
    REQUIRE(first.reached.has_value());
    CHECK(*first.reached > *first.noticed);
    CHECK(first.alertShown);

    const CreatureRun second = runCreature();
    CHECK(second.wandered == first.wandered);
    CHECK(second.noticed == first.noticed);
    CHECK(second.reached == first.reached);
    CHECK(second.creatureEnd == first.creatureEnd);
    CHECK(second.playerEnd == first.playerEnd);
}

TEST_CASE("the tech demo's pause menu stops the creature and resuming lets it go on")
{
    ForestFixture f;
    f.step(60);
    REQUIRE(f.world().get<BehaviorAgent>(f.creature).enabled);
    f.key("Escape", true);
    f.step();
    f.key("Escape", false);
    f.step(2);
    CHECK_FALSE(f.world().get<BehaviorAgent>(f.creature).enabled);
    CHECK_FALSE(f.world().get<NavAgent>(f.creature).destination.has_value());
    // It comes to a halt, then stands still while the menu shows.
    f.step(60);
    const Vec3 halted = f.position(f.creature);
    f.step(120);
    CHECK(flatDistance(f.position(f.creature), halted) < 0.01f);

    f.key("Escape", true);
    f.step();
    f.key("Escape", false);
    f.step(2);
    CHECK(f.world().get<BehaviorAgent>(f.creature).enabled);
}
