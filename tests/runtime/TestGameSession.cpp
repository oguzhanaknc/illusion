#include "FontTestSupport.hpp"
#include "TestSupport.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/audio/AudioService.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/runtime/GameLocalization.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/store/NullStoreService.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiDrawList.hpp>
#include <illusion/ui/UiService.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace illusion;
using test::at;
using test::body;
using test::box;

namespace
{

constexpr f64 kStep = 1.0 / 60.0;

void reflectAll()
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    reflectAudioComponents();
    reflectParticleComponents();
    reflectRuntimeStructs();
}

struct Fixture
{
    Fixture()
    {
        reflectAll();
        REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
        desc.jobs = &jobs;
        desc.files = &files;
        desc.audioOutput = AudioOutput::Offline;
    }

    [[nodiscard]] std::unique_ptr<GameSession> create()
    {
        auto session = GameSession::create(desc);
        REQUIRE(session.hasValue());
        return std::move(*session);
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    GameSessionDesc desc;
};

// A static module that records its calls and what the session provides.
struct ModuleLog
{
    std::string events;
    i32 updates = 0;
    bool servicesFound = false;
    std::string_view store;
    f64 lastDelta = 0.0;
    bool jumpDown = false;
};

ModuleLog g_log;

bool loadLogged(ModuleContext&)
{
    return true;
}

void startLogged(ModuleContext& context)
{
    g_log.events += "start;";
    g_log.servicesFound =
        context.service<PhysicsService>() != nullptr && context.service<AudioService>() != nullptr &&
        context.service<InputService>() != nullptr && context.service<SceneService>() != nullptr;
    const StoreService* store = context.service<StoreService>();
    g_log.store = store != nullptr ? store->storeName() : std::string_view("absent");
}

void updateLogged(ModuleContext& context, f64 deltaSeconds)
{
    ++g_log.updates;
    g_log.lastDelta = deltaSeconds;
    if (InputService* input = context.service<InputService>())
    {
        if (const std::optional<ActionId> jump = input->findAction("Jump"))
        {
            g_log.jumpDown = input->isDown(*jump);
        }
    }
}

void stopLogged(ModuleContext&)
{
    g_log.events += "stop;";
}

constexpr ModuleApi kSessionModule =
    makeModuleApi("session_test", "1.0.0",
                  {.load = &loadLogged, .start = &startLogged, .stop = &stopLogged, .update = &updateLogged});

// The reflected state of a World: its scene file in id order, hashed.
u64 stateHash(const World& world)
{
    SceneSaveOptions options;
    options.order = SceneOrder::ById;
    const SceneSaveResult saved = saveScene(world, options);
    REQUIRE(saved.ok());
    return xxhash64(std::as_bytes(std::span(saved.json.data(), saved.json.size())));
}

void writeActions(VirtualFileSystem& files, std::string_view path)
{
    InputActions actions;
    const ActionId jump = actions.addAction("Jump", ActionType::Button);
    actions.addBinding(jump, InputBinding::key(Key::Space));
    actions.addBinding(jump, InputBinding::gamepadButton(GamepadButton::South));
    const ActionId move = actions.addAction("Move", ActionType::Vector2);
    actions.addBinding(move, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
    REQUIRE(files.writeText(path, writeInputActions(actions)) == FileError::None);
}

// A menu of two buttons, one above the other, on a full-screen canvas of the session's World.
struct SessionMenu
{
    explicit SessionMenu(GameSession& game, bool modal = false)
        : session(game)
    {
        registerUiComponents();
        UiCanvas settings;
        settings.useSafeArea = false;
        settings.modal = modal;
        canvas = session.world().create(settings);
        start = button({800.0f, 400.0f}, {1100.0f, 480.0f});
        quit = button({800.0f, 500.0f}, {1100.0f, 580.0f});
        session.setUiFrame({{1920, 1080}});
    }

    Entity button(Vec2 min, Vec2 max)
    {
        UiElement element;
        element.placement.offsetMin = min;
        element.placement.offsetMax = max;
        const Entity entity = session.world().create(element, UiImage{}, UiInteractable{}, UiButton{});
        REQUIRE(setParent(session.world(), entity, canvas).hasValue());
        return entity;
    }

    [[nodiscard]] bool clicked(Entity entity) const
    {
        for (const UiEvent& event : session.ui().events())
        {
            if (event.type == UiEventType::Clicked && event.entity == entity)
            {
                return true;
            }
        }
        return false;
    }

    GameSession& session;
    Entity canvas{};
    Entity start{};
    Entity quit{};
};

} // namespace

TEST_CASE("a game session moves bodies and propagates the hierarchy on every update")
{
    Fixture f;
    auto session = f.create();
    World& world = session->world();
    test::createGround(world);
    const Entity crate = world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    const Entity label = world.create(Transform{}, LocalTransform{});
    REQUIRE(setParent(world, label, crate, ParentMode::KeepLocal));
    world.get<LocalTransform>(label).position = Vec3(0.0f, 1.0f, 0.0f);

    session->start();
    for (int i = 0; i < 30; ++i)
    {
        session->update(kStep);
    }
    const f32 y = world.get<Transform>(crate).position.y;
    CHECK(y < 5.0f);
    CHECK(world.get<Transform>(label).position.y == doctest::Approx(y + 1.0f));
    CHECK(session->physics().bodyCount() == 2);
    CHECK(session->updateCount() == 30);
    CHECK(session->time() == doctest::Approx(30.0 * kStep));
}

TEST_CASE("a game session emits particles that fall with the physics gravity")
{
    Fixture f;
    auto session = f.create();
    World& world = session->world();
    ParticleEmitter emitter;
    emitter.rate = 0.0f;
    emitter.burst = 4;
    emitter.speedMin = 0.0f;
    emitter.speedMax = 0.0f;
    emitter.lifetimeMin = 10.0f;
    emitter.lifetimeMax = 10.0f;
    emitter.gravityFactor = 1.0f;
    const Entity sparks = world.create(at(Vec3(0.0f, 5.0f, 0.0f)), emitter);
    session->start();
    session->physics().setGravity(Vec3(0.0f, -2.0f, 0.0f));
    for (int i = 0; i < 30; ++i)
    {
        session->update(kStep);
    }
    CHECK(session->particles().aliveCount(sparks) == 4);
    CHECK(session->particles().gravity() == Vec3(0.0f, -2.0f, 0.0f));
    CHECK(world.get<ParticlePool>(sparks).position(0).y < 5.0f);
}

TEST_CASE("a game session starts, updates and stops its modules on its World")
{
    Fixture f;
    g_log = {};
    ModuleHost modules(f.jobs);
    REQUIRE_FALSE(modules.add(kSessionModule));
    f.desc.modules = &modules;
    {
        auto session = f.create();
        session->update(kStep);
        CHECK(g_log.updates == 0); // not started: modules see nothing
        session->start();
        session->start();
        CHECK(modules.isAttached(session->world()));
        CHECK(modules.roleOf(session->world()) == WorldRole::Simulation);
        CHECK(g_log.servicesFound);
        session->update(0.25);
        CHECK(g_log.updates == 1);
        CHECK(g_log.lastDelta == doctest::Approx(0.25));
        session->stop();
        CHECK_FALSE(modules.isAttached(session->world()));
        session->start();
    }
    // The destructor stops a started session.
    CHECK(g_log.events == "start;stop;start;stop;");
    CHECK(modules.worldCount() == 0);
}

TEST_CASE("a game session hands its modules the store of its description, and none without one")
{
    Fixture f;
    g_log = {};
    ModuleHost modules(f.jobs);
    REQUIRE_FALSE(modules.add(kSessionModule));
    f.desc.modules = &modules;
    f.create()->start();
    CHECK(g_log.store == "absent");

    NullStoreService store(f.files, NullStoreDesc{.cloudDirectory = "assets:/cloud"});
    f.desc.store = &store;
    f.create()->start();
    CHECK(g_log.store == "none");
}

TEST_CASE("simulate mode moves bodies without running modules or input")
{
    Fixture f;
    g_log = {};
    writeActions(f.files, "assets:/game.input");
    f.desc.settings.input.actions = AssetRef("assets:/game.input");
    ModuleHost modules(f.jobs);
    REQUIRE_FALSE(modules.add(kSessionModule));
    f.desc.modules = &modules;
    f.desc.mode = GameSessionMode::Simulate;
    auto session = f.create();
    CHECK(session->mode() == GameSessionMode::Simulate);
    const Entity crate =
        session->world().create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    session->start();
    session->keyboardMouse().setKey(Key::Space, true);
    for (int i = 0; i < 10; ++i)
    {
        session->update(kStep);
    }
    CHECK(session->world().get<Transform>(crate).position.y < 5.0f);
    CHECK_FALSE(modules.isAttached(session->world()));
    CHECK(g_log.updates == 0);
    CHECK(g_log.events.empty());
    CHECK_FALSE(session->actions().isDown(*session->actions().findAction("Jump")));
}

TEST_CASE("fixed steps advance one physics step per update whatever the time passed")
{
    Fixture f;
    f.desc.fixedStep = true;
    f.desc.settings.physics.stepSeconds = 1.0 / 50.0;
    auto session = f.create();
    session->update(1.0);
    session->update(0.0);
    session->update(0.001);
    CHECK(session->physics().stepCount() == 3);
    CHECK(session->time() == doctest::Approx(3.0 / 50.0));
}

TEST_CASE("the same scene and seed give the same reflected state after 600 fixed steps")
{
    const auto run = [](u64 seed) {
        Fixture f;
        f.desc.fixedStep = true;
        f.desc.seed = seed;
        auto session = f.create();
        World& world = session->world();
        world.create(at(Vec3(0.0f, -0.5f, 0.0f)), body(MotionType::Static), box(Vec3(50.0f, 0.5f, 50.0f)),
                     EntityGuid{1});
        for (int i = 0; i < 8; ++i)
        {
            world.create(at(Vec3(0.1f * static_cast<f32>(i), 1.0f + 1.1f * static_cast<f32>(i), 0.0f)),
                         body(MotionType::Dynamic), box(Vec3(0.5f)), EntityGuid{static_cast<u64>(i) + 2});
        }
        session->start();
        for (int step = 0; step < 600; ++step)
        {
            if (step % 100 == 0) // runtime spawns, which get guids from the seeded generator
            {
                world.create(at(Vec3(0.0f, 10.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.25f)),
                             Name{"Spawned"});
            }
            session->update(kStep);
        }
        return stateHash(world);
    };
    const u64 first = run(42);
    CHECK(run(42) == first);
    CHECK(run(43) != first); // the spawned entities get other guids
}

namespace
{

EntityGuid g_spawnedGuid;

// Instantiates assets:/spawn.scene through SceneService and records the EntityGuid it got.
void startSpawning(ModuleContext& context)
{
    std::array<Entity, 1> created{};
    if (SceneService* scenes = context.service<SceneService>();
        scenes != nullptr && scenes->instantiate("assets:/spawn.scene", created).value_or(0) == 1)
    {
        g_spawnedGuid = context.world().get<EntityGuid>(created[0]);
    }
}

constexpr ModuleApi kSpawnModule =
    makeModuleApi("session_spawn", "1.0.0", {.load = &loadLogged, .start = &startSpawning});

} // namespace

TEST_CASE("in fixed steps, scenes that game code instantiates get EntityGuids from the session's seed")
{
    // Regression test: SceneService took new ids from the process-wide generator, so a fixed-step run
    // that spawned a prefab or scene ended in another World each time.
    const auto run = [](bool fixedStep, u64 seed) {
        Fixture f;
        World scene;
        scene.create(Name{"Spawned"}, EntityGuid{99});
        const SceneSaveResult saved = saveScene(scene);
        REQUIRE(saved.ok());
        REQUIRE(f.files.writeText("assets:/spawn.scene", saved.json) == FileError::None);
        ModuleHost modules(f.jobs);
        REQUIRE_FALSE(modules.add(kSpawnModule));
        f.desc.modules = &modules;
        f.desc.fixedStep = fixedStep;
        f.desc.seed = seed;
        g_spawnedGuid = EntityGuid{};
        auto session = f.create();
        session->start();
        return g_spawnedGuid;
    };
    const EntityGuid first = run(true, 5);
    CHECK(first != EntityGuid{});
    CHECK(first != EntityGuid{99}); // instantiate() makes new ids
    CHECK(run(true, 5) == first);
    CHECK(run(true, 6) != first);
    CHECK(run(false, 5) != first); // without fixed steps, ids are random
}

TEST_CASE("injected keys drive the actions of the project's input asset while the game has the focus")
{
    Fixture f;
    g_log = {};
    writeActions(f.files, "assets:/game.input");
    f.desc.settings.input.actions = AssetRef("assets:/game.input");
    ModuleHost modules(f.jobs);
    REQUIRE_FALSE(modules.add(kSessionModule));
    f.desc.modules = &modules;
    auto session = f.create();
    session->start();
    InputActions& actions = session->actions();
    REQUIRE(actions.actionCount() == 5); // and the three ui.* actions
    const ActionId jump = *actions.findAction("Jump");
    const ActionId move = *actions.findAction("Move");

    session->keyboardMouse().setKey(Key::Space, true);
    session->keyboardMouse().setKey(Key::D, true);
    session->update(kStep);
    CHECK(actions.wasPressed(jump));
    CHECK(g_log.jumpDown); // modules see the actions of this update
    CHECK(actions.vector(move).x == doctest::Approx(1.0f));
    session->update(kStep);
    CHECK(actions.isDown(jump));
    CHECK_FALSE(actions.wasPressed(jump)); // the edge was cleared after the last update

    session->setInputFocus(false);
    CHECK_FALSE(session->hasInputFocus());
    session->update(kStep);
    CHECK(actions.wasReleased(jump)); // without the focus the game sees nothing held
    CHECK_FALSE(g_log.jumpDown);
}

TEST_CASE("virtual gamepads drive the actions with the focus, or without it when allowed")
{
    Fixture f;
    writeActions(f.files, "assets:/game.input");
    f.desc.settings.input.actions = AssetRef("assets:/game.input");
    auto gamepads = GamepadSystem::create();
    REQUIRE(gamepads != nullptr);
    f.desc.gamepads = gamepads.get();
    auto pad = gamepads->attachVirtualGamepad();
    REQUIRE(pad != nullptr);
    // SDL may deliver a change over a couple of pumps.
    const auto updateUntil = [&](auto predicate) {
        for (int i = 0; i < 10 && !predicate(); ++i)
        {
            gamepads->update();
        }
        return predicate();
    };
    REQUIRE(updateUntil([&] { return gamepads->find(pad->id()) != nullptr; }));
    pad->setButton(GamepadButton::South, true);
    REQUIRE(updateUntil([&] { return gamepads->find(pad->id())->state().isDown(GamepadButton::South); }));

    SUBCASE("only with the focus")
    {
        auto session = f.create();
        const ActionId jump = *session->actions().findAction("Jump");
        session->update(kStep);
        CHECK(session->actions().isDown(jump));
        CHECK(session->actions().lastDevice() == InputDevice::Gamepad);
        session->setInputFocus(false);
        session->update(kStep);
        CHECK_FALSE(session->actions().isDown(jump));
    }
    SUBCASE("in the background")
    {
        f.desc.settings.input.gamepadsInBackground = true;
        auto session = f.create();
        const ActionId jump = *session->actions().findAction("Jump");
        session->setInputFocus(false);
        session->update(kStep);
        CHECK(session->actions().isDown(jump));
    }
}

TEST_CASE("a game session applies the project's physics and audio settings")
{
    Fixture f;
    f.desc.settings.physics.gravity = Vec3(0.0f, -2.0f, 0.0f);
    f.desc.settings.physics.collisionMatrix[1] &= ~(1u << 2);
    f.desc.settings.physics.collisionMatrix[3] &= ~(1u << 3);
    f.desc.settings.audio.masterVolume = 0.5f;
    f.desc.settings.audio.busVolumes[static_cast<usize>(AudioBus::Music)] = 0.25f;
    auto session = f.create();
    CHECK(session->physics().gravity().y == doctest::Approx(-2.0f));
    CHECK_FALSE(session->physics().layersCollide(1, 2));
    CHECK_FALSE(session->physics().layersCollide(2, 1)); // one side of the matrix is enough to separate them
    CHECK_FALSE(session->physics().layersCollide(3, 3));
    CHECK(session->physics().layersCollide(0, 15));
    CHECK(session->audio().masterVolume() == doctest::Approx(0.5f));
    CHECK(session->audio().busVolume(AudioBus::Music) == doctest::Approx(0.25f));
    CHECK(session->audio().busVolume(AudioBus::Sfx) == doctest::Approx(1.0f));
    CHECK(session->settings().physics.gravity.y == doctest::Approx(-2.0f));
}

TEST_CASE("a game session fails to start when its input actions asset cannot be loaded")
{
    Fixture f;
    f.desc.settings.input.actions = AssetRef("assets:/missing.input");
    auto missing = GameSession::create(f.desc);
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().find("assets:/missing.input") != std::string::npos);

    REQUIRE(f.files.writeText("assets:/broken.input", "{}") == FileError::None);
    f.desc.settings.input.actions = AssetRef("assets:/broken.input");
    auto broken = GameSession::create(f.desc);
    REQUIRE_FALSE(broken.hasValue());
    CHECK(broken.error().find("not an input actions asset") != std::string::npos);
}

TEST_CASE("a game session needs jobs and files")
{
    reflectAll();
    test::ScopedAssertCatcher asserts;
    GameSessionDesc desc;
    CHECK_THROWS_AS(static_cast<void>(GameSession::create(desc)), test::AssertionFailure);
}

TEST_CASE("a headless game session lays out its UI and records solid colours")
{
    Fixture f;
    registerUiComponents();
    auto session = f.create();
    World& world = session->world();
    UiCanvas canvas;
    canvas.useSafeArea = false;
    const Entity root = world.create(canvas);
    UiElement fill;
    fill.placement.anchorMax = {1.0f, 1.0f};
    UiImage tint;
    tint.tint = {1.0f, 0.0f, 0.0f, 1.0f};
    const Entity panel = world.create(fill, tint);
    REQUIRE(setParent(world, panel, root).hasValue());
    UiText label;
    label.text = "Hi";
    label.font = AssetRef("assets:/fonts/game.ttf");
    const Entity text = world.create(fill, label);
    REQUIRE(setParent(world, text, panel).hasValue());

    session->setUiFrame({{1280, 720}});
    session->update(kStep);
    const UiWorldStatistics& statistics = session->ui().statistics();
    CHECK(statistics.canvases == 1);
    CHECK(statistics.elements == 2);
    CHECK(session->ui().drawData().vertices.size() == 4);
    CHECK(static_cast<f64>(session->ui().drawData().canvas.pixelsPerUnit) ==
          doctest::Approx(1280.0 / 1920.0));
    CHECK(world.get<UiComputedRect>(text).rect.max == Vec2(1920.0f, 1080.0f));
    session->update(kStep);
    CHECK(session->ui().statistics().layoutReused);
}

TEST_CASE("a game session loads the project's locale files for UiText keys")
{
    Fixture f;
    REQUIRE(f.files.writeText("assets:/locales/en.json", R"({"format": "illusion.locale", "version": 1,
        "locale": "en", "displayName": "English", "plural": "en",
        "strings": {"game.title": "Illusion", "game.score": "Score"}})") == FileError::None);
    REQUIRE(f.files.writeText("assets:/locales/tr.json", R"({"format": "illusion.locale", "version": 1,
        "locale": "tr", "displayName": "Turkce", "plural": "tr", "fallback": "en",
        "strings": {"game.title": "Yanilsama", "game.extra": "Ekstra"}})") == FileError::None);
    REQUIRE(f.files.writeText("assets:/locales/broken.json", "{") == FileError::None);
    auto session = f.create();
    text::Localization& localization = session->localization();
    const auto formatted = [&](std::string_view key) {
        const text::MessageFormat* message = localization.find(key);
        REQUIRE(message != nullptr);
        std::string out;
        text::TextOutput output(out);
        REQUIRE(message->format(std::span<const text::MessageArgument>(), localization.context(), output)
                    .hasValue());
        return out;
    };
    CHECK(formatted("game.title") == "Illusion");
    CHECK(formatted("game.extra") == "Ekstra");
    CHECK(localization.find("game.unknown") == nullptr);

    World& world = session->world();
    const Entity root = world.create(UiCanvas{});
    UiText known;
    known.key = "game.title";
    UiText unknown;
    unknown.key = "game.unknown";
    REQUIRE(setParent(world, world.create(UiElement{}, known), root).hasValue());
    REQUIRE(setParent(world, world.create(UiElement{}, unknown), root).hasValue());
    session->update(kStep);
    CHECK(session->ui().statistics().missingKeys == 1);

    localization.setLocale(*text::LocaleId::parse("tr"));
    session->update(kStep);
    CHECK_FALSE(session->ui().statistics().layoutReused);
    CHECK(formatted("game.title") == "Yanilsama");
    CHECK(formatted("game.score") == "Score");
}

TEST_CASE("loadGameLocalization builds the game table from every locale file, English first")
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
    text::Localization empty;
    GameStringTable none;
    loadGameLocalization(files, empty, none);
    CHECK(none.keys.empty());
    CHECK(empty.find("input.glyph.gamepad_back") != nullptr);

    REQUIRE(files.writeText("assets:/locales/de.json", R"({"format": "illusion.locale", "version": 1,
        "locale": "de", "displayName": "Deutsch", "plural": "de", "fallback": "en",
        "strings": {"game.title": "Illusion DE", "game.only": "Nur", "game.bad": "{broken"}})") ==
            FileError::None);
    REQUIRE(files.writeText("assets:/locales/en.json", R"({"format": "illusion.locale", "version": 1,
        "locale": "en", "displayName": "English", "plural": "en",
        "strings": {"game.title": "Illusion {count}"}})") == FileError::None);
    text::Localization localization;
    GameStringTable table;
    loadGameLocalization(files, localization, table);
    CHECK(table.table.name == "game");
    CHECK(table.keys == std::vector<std::string>{"game.only", "game.title"});
    CHECK(table.english == std::vector<std::string>{"Nur", "Illusion {count}"});
    CHECK(localization.find("game.bad") == nullptr);
    CHECK(localization.find("game.title") != nullptr);
    CHECK(localization.find("input.glyph.gamepad_back") != nullptr);
    CHECK(localization.availableLocales().size() == 3); // en, en-XA and de
}

TEST_CASE("a game session without locale files has no game strings")
{
    Fixture f;
    auto session = f.create();
    CHECK(session->localization().find("game.title") == nullptr);
}

TEST_CASE("a game session adds the ui actions with default bindings when its input asset lacks them")
{
    Fixture f;
    InputActions own;
    const ActionId submit = own.addAction(kUiSubmitAction, ActionType::Button);
    own.addBinding(submit, InputBinding::key(Key::F));
    REQUIRE(f.files.writeText("assets:/game.input", writeInputActions(own)) == FileError::None);
    f.desc.settings.input.actions = AssetRef("assets:/game.input");
    auto session = f.create();
    InputActions& actions = session->actions();
    REQUIRE(actions.actionCount() == 3);
    const std::span<const InputBinding> kept = actions.bindings(*actions.findAction(kUiSubmitAction));
    REQUIRE(kept.size() == 1); // the project's own bindings stay
    CHECK(kept[0] == InputBinding::key(Key::F));

    const ActionId navigate = *actions.findAction(kUiNavigateAction);
    CHECK(actions.actionType(navigate) == ActionType::Vector2);
    const std::span<const InputBinding> moves = actions.bindings(navigate);
    CHECK(std::ranges::find(moves, InputBinding::keyVector(Key::Up, Key::Down, Key::Left, Key::Right)) !=
          moves.end());
    CHECK(std::ranges::find(moves, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D)) != moves.end());
    CHECK(std::ranges::find(moves, InputBinding::gamepadStick(GamepadStick::Left)) != moves.end());
    CHECK(std::ranges::find(moves, InputBinding::gamepadStick(GamepadStick::DPad)) != moves.end());
    const std::span<const InputBinding> cancels = actions.bindings(*actions.findAction(kUiCancelAction));
    CHECK(std::ranges::find(cancels, InputBinding::key(Key::Escape)) != cancels.end());
    CHECK(std::ranges::find(cancels, InputBinding::gamepadButton(GamepadButton::East)) != cancels.end());

    Fixture empty;
    auto bare = empty.create();
    const ActionId defaultSubmit = *bare->actions().findAction(kUiSubmitAction);
    CHECK(bare->actions().bindings(defaultSubmit).size() == 3);
}

TEST_CASE("a headless game session drives a menu with the keyboard")
{
    Fixture f;
    auto session = f.create();
    SessionMenu menu(*session);
    InputState& keys = session->keyboardMouse();
    session->update(kStep);
    CHECK(session->ui().focused().isNull());

    keys.setKey(Key::Down, true);
    session->update(kStep);
    CHECK(session->ui().focused() == menu.start); // the first navigation focuses the first button
    keys.setKey(Key::Down, false);
    session->update(kStep);
    keys.setKey(Key::S, true);
    session->update(kStep);
    CHECK(session->ui().focused() == menu.quit);
    keys.setKey(Key::S, false);
    session->update(kStep);

    keys.setKey(Key::Enter, true);
    session->update(kStep);
    CHECK(session->world().get<UiInteraction>(menu.quit).pressed);
    keys.setKey(Key::Enter, false);
    session->update(kStep);
    CHECK(menu.clicked(menu.quit));

    keys.setKey(Key::Escape, true);
    session->update(kStep);
    REQUIRE(session->ui().events().size() == 1);
    CHECK(session->ui().events()[0].type == UiEventType::Cancelled);
    CHECK(session->ui().events()[0].entity == menu.canvas);
}

TEST_CASE("a headless game session drives a menu with the mouse")
{
    Fixture f;
    auto session = f.create();
    SessionMenu menu(*session);
    InputState& mouse = session->keyboardMouse();
    mouse.setMousePosition({900.0f, 540.0f});
    session->update(kStep);
    CHECK(session->world().get<UiInteraction>(menu.quit).hovered);
    mouse.setMouseButton(MouseButton::Left, true);
    session->update(kStep);
    mouse.setMouseButton(MouseButton::Left, false);
    session->update(kStep);
    CHECK(menu.clicked(menu.quit));

    // A host whose window is twice the size of the UI frame scales the cursor.
    session->setUiFrame({{1920, 1080}}, Vec2(0.5f));
    mouse.setMousePosition({1800.0f, 880.0f});
    session->update(kStep);
    CHECK(session->world().get<UiInteraction>(menu.start).hovered);

    // Without the focus the pointer is not there.
    session->setInputFocus(false);
    session->update(kStep);
    CHECK_FALSE(session->world().get<UiInteraction>(menu.start).hovered);
}

TEST_CASE("a pointer set on a game session reaches its UI without the focus and leaves the mouse alone")
{
    Fixture f;
    auto session = f.create();
    SessionMenu menu(*session);
    InputState& mouse = session->keyboardMouse();
    mouse.setMousePosition({5.0f, 5.0f});
    session->setInputFocus(false);
    CHECK(session->uiFrame().targetPixels == UVec2(1920, 1080));
    session->setUiPointer(GameUiPointer{.pixels = {900.0f, 540.0f}, .down = true});
    session->update(kStep);
    CHECK(session->world().get<UiInteraction>(menu.quit).pressed);
    session->setUiPointer(GameUiPointer{.pixels = {900.0f, 540.0f}, .down = false});
    session->update(kStep);
    CHECK(menu.clicked(menu.quit));
    CHECK(mouse.mousePosition() == Vec2(5.0f, 5.0f));
    CHECK_FALSE(mouse.isDown(MouseButton::Left));
    // Back to the cursor, which the UI does not see without the focus.
    session->setUiPointer(std::nullopt);
    session->update(kStep);
    CHECK_FALSE(session->world().get<UiInteraction>(menu.quit).hovered);
}

TEST_CASE("a headless game session drives a menu with a virtual gamepad")
{
    Fixture f;
    auto gamepads = GamepadSystem::create();
    REQUIRE(gamepads != nullptr);
    f.desc.gamepads = gamepads.get();
    auto pad = gamepads->attachVirtualGamepad();
    REQUIRE(pad != nullptr);
    const auto updateUntil = [&](auto predicate) {
        for (int i = 0; i < 10 && !predicate(); ++i)
        {
            gamepads->update();
        }
        return predicate();
    };
    REQUIRE(updateUntil([&] { return gamepads->find(pad->id()) != nullptr; }));
    const auto hold = [&](GamepadButton button, bool down) {
        pad->setButton(button, down);
        REQUIRE(updateUntil([&] { return gamepads->find(pad->id())->state().isDown(button) == down; }));
    };
    auto session = f.create();
    SessionMenu menu(*session);

    hold(GamepadButton::DPadDown, true);
    session->update(kStep);
    CHECK(session->ui().focused() == menu.start);
    hold(GamepadButton::DPadDown, false);
    session->update(kStep);
    hold(GamepadButton::DPadDown, true);
    session->update(kStep);
    CHECK(session->ui().focused() == menu.quit);
    hold(GamepadButton::DPadDown, false);
    hold(GamepadButton::South, true);
    session->update(kStep);
    hold(GamepadButton::South, false);
    session->update(kStep);
    CHECK(menu.clicked(menu.quit));
}

TEST_CASE("while a modal canvas is shown the game's other actions see no input")
{
    Fixture f;
    writeActions(f.files, "assets:/game.input");
    f.desc.settings.input.actions = AssetRef("assets:/game.input");
    auto session = f.create();
    SessionMenu menu(*session, true);
    const ActionId jump = *session->actions().findAction("Jump");
    const ActionId submit = *session->actions().findAction(kUiSubmitAction);
    session->keyboardMouse().setKey(Key::Space, true);
    session->update(kStep); // the first UI update shows the modal canvas
    CHECK(session->ui().modalActive());
    CHECK(session->actions().isDown(jump));
    session->update(kStep);
    CHECK_FALSE(session->actions().isDown(jump));
    CHECK(session->actions().isDown(submit));

    setEnabled(session->world(), menu.canvas, false);
    session->update(kStep); // the canvas goes away in this UI update
    CHECK_FALSE(session->ui().modalActive());
    session->update(kStep);
    CHECK(session->actions().isDown(jump));
    CHECK(session->actions().wasPressed(jump));
}

namespace
{

// What the UI module saw: it finds Score and Start, shows the clicks on Score by key and asks to end the
// game at update exitAt.
struct UiModuleLog
{
    Entity score{};
    Entity start{};
    u32 clicks = 0;
    i32 updates = 0;
    i32 exitAt = -1;
    bool textSet = false;
};

UiModuleLog g_uiLog;

void updateUiModule(ModuleContext& context, f64)
{
    ++g_uiLog.updates;
    if (UiService* ui = context.service<UiService>())
    {
        g_uiLog.score = ui->find({}, "Score");
        g_uiLog.start = ui->find({}, "Start");
        std::array<UiEvent, 8> events{};
        const u32 count = std::min<u32>(ui->pollEvents(events), static_cast<u32>(events.size()));
        for (const UiEvent& event : std::span(events).first(count))
        {
            g_uiLog.clicks += event.type == UiEventType::Clicked && event.entity == g_uiLog.start ? 1u : 0u;
        }
        const std::array points{UiTextArgument::number("points", 100.0 * g_uiLog.clicks)};
        g_uiLog.textSet = ui->setTextKey(g_uiLog.score, "game.score", points);
    }
    if (g_uiLog.updates == g_uiLog.exitAt)
    {
        if (SceneService* scenes = context.service<SceneService>())
        {
            scenes->requestExit();
        }
    }
}

constexpr ModuleApi kUiModule =
    makeModuleApi("session_ui", "1.0.0", {.load = &loadLogged, .update = &updateUiModule});

} // namespace

TEST_CASE("a module of a game session finds UI elements, reads a click and shows it in a keyed text")
{
    Fixture f;
    REQUIRE(f.files.writeText("assets:/locales/en.json", R"({"format": "illusion.locale", "version": 1,
        "locale": "en", "displayName": "English", "plural": "en",
        "strings": {"game.score": "Score {points}"}})") == FileError::None);
    g_uiLog = {};
    ModuleHost modules(f.jobs);
    REQUIRE_FALSE(modules.add(kUiModule));
    f.desc.modules = &modules;
    auto session = f.create();
    SessionMenu menu(*session);
    World& world = session->world();
    world.emplace<Name>(menu.start, Name{"Start"});
    UiText text;
    text.key = "game.title";
    const Entity score = world.create(UiElement{}, text, Name{"Score"});
    REQUIRE(setParent(world, score, menu.canvas).hasValue());
    session->start();

    InputState& mouse = session->keyboardMouse();
    mouse.setMousePosition({900.0f, 440.0f});
    session->update(kStep);
    CHECK(g_uiLog.score == score);
    CHECK(g_uiLog.start == menu.start);
    CHECK(g_uiLog.textSet);
    CHECK(world.get<UiText>(score).key == "game.score");
    mouse.setMouseButton(MouseButton::Left, true);
    session->update(kStep);
    mouse.setMouseButton(MouseButton::Left, false);
    session->update(kStep); // the click happens in this UI update
    CHECK(g_uiLog.clicks == 0);
    session->update(kStep); // and the module sees it in the next update
    CHECK(g_uiLog.clicks == 1);
    REQUIRE(world.has<UiTextArguments>(score));
    const UiTextArguments& arguments = world.get<UiTextArguments>(score);
    REQUIRE(arguments.count == 1);
    CHECK(arguments.arguments[0].value == 100.0);
    CHECK(session->ui().statistics().missingKeys == 0);
    CHECK_FALSE(session->exitRequested());
}

TEST_CASE("a game session reports that a module asked to end the game")
{
    Fixture f;
    g_uiLog = {};
    g_uiLog.exitAt = 3;
    ModuleHost modules(f.jobs);
    REQUIRE_FALSE(modules.add(kUiModule));
    f.desc.modules = &modules;
    auto session = f.create();
    session->start();
    session->update(kStep);
    session->update(kStep);
    CHECK_FALSE(session->exitRequested());
    session->update(kStep);
    CHECK(session->exitRequested());
    session->update(kStep); // the host decides what happens; the request stays
    CHECK(session->exitRequested());
}

namespace
{

// A line of text on the left of a full-screen canvas: a text field, a glyph, or a button below it.
struct SessionForm
{
    explicit SessionForm(GameSession& game)
        : session(game)
    {
        registerUiComponents();
        UiCanvas settings;
        settings.useSafeArea = false;
        canvas = session.world().create(settings);
        session.setUiFrame({{1920, 1080}});
    }

    template <class... Ts>
    Entity line(f32 top, Ts&&... components)
    {
        UiElement element;
        element.placement.offsetMin = {100.0f, top};
        element.placement.offsetMax = {900.0f, top + 50.0f};
        const Entity entity = session.world().create(element, std::forward<Ts>(components)...);
        REQUIRE(setParent(session.world(), entity, canvas).hasValue());
        return entity;
    }

    [[nodiscard]] u32 count(UiEventType type) const
    {
        u32 found = 0;
        for (const UiEvent& event : session.ui().events())
        {
            found += event.type == type ? 1u : 0u;
        }
        return found;
    }

    GameSession& session;
    Entity canvas{};
};

// The test font for every font reference.
class SessionUiAssets final : public UiAssets
{
public:
    UiFontFace font(const AssetRef&) override { return {&test::testFont(), UiTextureId{1}}; }
    UiImageSource image(const AssetRef&) override { return {}; }
};

} // namespace

TEST_CASE("a game session types the keyboard into a focused text field and masks the game's actions")
{
    Fixture f;
    writeActions(f.files, "assets:/game.input");
    f.desc.settings.input.actions = AssetRef("assets:/game.input");
    auto session = f.create();
    SessionForm form(*session);
    const Entity field = form.line(100.0f, UiText{}, UiInteractable{}, UiTextField{});
    const Entity below = form.line(200.0f, UiImage{}, UiInteractable{}, UiButton{});
    const ActionId jump = *session->actions().findAction("Jump");
    InputState& keys = session->keyboardMouse();
    session->update(kStep);
    session->ui().setFocus(field);
    const auto type = [&](Key key, char32_t character) {
        keys.setKey(key, true);
        keys.addText(character);
        session->update(kStep);
        keys.setKey(key, false);
        session->update(kStep);
    };
    const auto press = [&](Key key) {
        keys.setKey(key, true);
        session->update(kStep);
        keys.setKey(key, false);
        session->update(kStep);
    };

    // A and S are ui.navigate and Move, Space is ui.submit and Jump: while the field has the focus they type.
    type(Key::A, U'a');
    keys.setKey(Key::Space, true);
    keys.addText(U' ');
    session->update(kStep);
    CHECK_FALSE(session->actions().isDown(jump));
    CHECK(form.count(UiEventType::Submitted) == 0);
    keys.setKey(Key::Space, false);
    session->update(kStep);
    type(Key::S, U's');
    CHECK(session->world().get<UiTextField>(field).text == "a s");
    CHECK(session->ui().focused() == field);

    // Left moves the caret, Backspace deletes before it and repeats while held.
    press(Key::Left);
    type(Key::X, U'x');
    CHECK(session->world().get<UiTextField>(field).text == "a xs");
    press(Key::End);
    for (const char32_t c : std::u32string_view(U"abcdefghij"))
    {
        keys.addText(c);
    }
    session->update(kStep);
    REQUIRE(session->world().get<UiTextField>(field).text == "a xsabcdefghij");
    keys.setKey(Key::Backspace, true);
    for (int i = 0; i < 36; ++i) // 0.6 s: the press, then repeats after 0.4 s every 0.05 s
    {
        session->update(kStep);
    }
    keys.setKey(Key::Backspace, false);
    session->update(kStep);
    const usize left = session->world().get<UiTextField>(field).text.size();
    CHECK(left < 14 - 2);
    CHECK(left > 14 - 8);

    keys.setKey(Key::Enter, true);
    session->update(kStep);
    CHECK(form.count(UiEventType::Submitted) == 1);
    keys.setKey(Key::Enter, false);
    session->update(kStep);
    CHECK(session->ui().focused() == field);

    // Down moves the focus to the button; then Space is Jump again.
    keys.setKey(Key::Down, true);
    session->update(kStep);
    keys.setKey(Key::Down, false);
    CHECK(session->ui().focused() == below);
    session->update(kStep);
    keys.setKey(Key::Space, true);
    session->update(kStep);
    CHECK(session->actions().isDown(jump));
}

TEST_CASE("a glyph in a game session shows the binding for the keyboard, then for a gamepad used after it")
{
    Fixture f;
    auto gamepads = GamepadSystem::create();
    REQUIRE(gamepads != nullptr);
    f.desc.gamepads = gamepads.get();
    SessionUiAssets assets;
    f.desc.uiAssets = &assets;
    VirtualGamepadDesc padDesc;
    padDesc.type = GamepadType::PS5;
    auto pad = gamepads->attachVirtualGamepad(padDesc);
    REQUIRE(pad != nullptr);
    const auto updateUntil = [&](auto predicate) {
        for (int i = 0; i < 10 && !predicate(); ++i)
        {
            gamepads->update();
        }
        return predicate();
    };
    REQUIRE(updateUntil([&] { return gamepads->find(pad->id()) != nullptr; }));
    auto session = f.create();
    SessionForm form(*session);
    UiText label;
    label.text = "?";
    label.font = AssetRef("assets:/fonts/test.ttf");
    UiGlyph glyph;
    glyph.action = std::string(kUiSubmitAction);
    const Entity shown = form.line(100.0f, label, glyph);

    // The quads of @p text drawn as the glyph's UiText draws it.
    const auto drawn = [&](std::string_view text) {
        UiDrawList list;
        list.begin(session->ui().drawData().canvas);
        UiTextStyle style;
        style.size = label.size;
        (void)list.text({&test::testFont(), UiTextureId{1}}, {100.0f, 100.0f}, text, style);
        const UiDrawData expected = list.data();
        const UiDrawData actual = session->ui().drawData();
        if (expected.vertices.size() != actual.vertices.size())
        {
            return false;
        }
        for (usize v = 0; v < expected.vertices.size(); ++v)
        {
            if (expected.vertices[v].position != actual.vertices[v].position ||
                expected.vertices[v].uv != actual.vertices[v].uv)
            {
                return false;
            }
        }
        return true;
    };

    session->keyboardMouse().setKey(Key::Enter, true);
    session->update(kStep);
    const std::string keyboard =
        glyphLabel(InputBinding::key(Key::Enter), GamepadFamily::Generic, session->localization());
    CHECK(drawn(keyboard));
    session->keyboardMouse().setKey(Key::Enter, false);
    session->update(kStep);

    pad->setButton(GamepadButton::South, true);
    REQUIRE(updateUntil([&] { return gamepads->find(pad->id())->state().isDown(GamepadButton::South); }));
    session->update(kStep);
    REQUIRE(session->actions().lastDevice() == InputDevice::Gamepad);
    const std::string gamepad = glyphLabel(InputBinding::gamepadButton(GamepadButton::South),
                                           gamepadFamily(GamepadType::PS5), session->localization());
    CHECK(gamepad != keyboard);
    CHECK(drawn(gamepad));
    CHECK(session->world().get<UiText>(shown).text == "?");
}
