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
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/SceneService.hpp>

#include <doctest/doctest.h>

#include <span>
#include <string>

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
    REQUIRE(actions.actionCount() == 2);
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
