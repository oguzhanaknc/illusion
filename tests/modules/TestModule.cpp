// A module for the module tests, built in several variants (TEST_MODULE_VARIANT):
// 1: registers ModuleCounter and LegacyComponent; update adds 1 to every counter.
// 2: registers only ModuleCounter; update adds 2.
// 3: widens ModuleCounter (TEST_MODULE_WIDE_COUNTER), which a hot reload must reject.
// 4: reports a wrong build key.
// Every variant is built with USES physics audio input render runtime: the API targets give it the headers
// of those libraries and their service interfaces without their code. update() can call every service
// function (ModuleControl::callServices) and start() can build a mesh body (ModuleControl::createMesh).

#include "TestModuleShared.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/audio/AudioService.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/runtime/SceneService.hpp>

#include <array>
#include <type_traits>

namespace
{

using namespace illusion;

constexpr i32 kVariant = TEST_MODULE_VARIANT;

// The USES targets provide the plain-data Components of the engine libraries.
static_assert(std::is_nothrow_move_constructible_v<RigidBody>);
static_assert(std::is_nothrow_move_constructible_v<AudioSource>);
static_assert(std::is_nothrow_move_constructible_v<MeshRenderer>);
static_assert(static_cast<u32>(GamepadButton::South) == 0);

// Records what start(), stop() and update() see of their World.
void checkWorld(ModuleContext& context, test::ModuleControl& control)
{
    control.simulationOnly =
        control.simulationOnly && context.hasWorld() && context.worldRole() == WorldRole::Simulation;
}

// Calls every function of the service interfaces once, so that the host's counting proxies see each one
// dispatched from module code.
void callServices(ModuleContext& context)
{
    const Vec3 v(1.0f);
    const Quat q(1.0f, 0.0f, 0.0f, 0.0f);
    const Entity e;
    const Collider shape;
    std::array<Entity, 2> entities{};
    if (PhysicsService* physics = context.service<PhysicsService>())
    {
        physics->setGravity(v);
        static_cast<void>(physics->gravity());
        physics->setLayersCollide(0, 1, false);
        static_cast<void>(physics->layersCollide(0, 1));
        physics->rebuild(e);
        physics->setLinearVelocity(e, v);
        static_cast<void>(physics->linearVelocity(e));
        physics->setAngularVelocity(e, v);
        static_cast<void>(physics->angularVelocity(e));
        physics->addForce(e, v);
        physics->addTorque(e, v);
        physics->addImpulse(e, v);
        static_cast<void>(physics->mass(e));
        static_cast<void>(physics->isAwake(e));
        const std::array<Vec3, 3> points{Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
        const std::array<u32, 3> indices{0, 1, 2};
        const PhysicsMeshHandle mesh = physics->createMesh(points, indices);
        static_cast<void>(physics->createConvexHull(points));
        physics->destroyMesh(mesh);
        static_cast<void>(physics->isMeshValid(mesh));
        physics->setCharacterVelocity(e, v);
        static_cast<void>(physics->characterVelocity(e));
        static_cast<void>(physics->isCharacterGrounded(e));
        static_cast<void>(physics->jointPosition(e));
        static_cast<void>(physics->brokenJoints());
        static_cast<void>(physics->raycast(v, v, 1.0f, 0xFFFFFFFFu, e));
        static_cast<void>(physics->shapeCast(shape, v, q, v, 1.0f, 0xFFFFFFFFu, e));
        static_cast<void>(physics->overlap(shape, v, q, entities, 0xFFFFFFFFu, e));
        static_cast<void>(physics->contactEvents());
    }
    if (AudioService* audio = context.service<AudioService>())
    {
        const AudioClipHandle clip = audio->loadClip("game:/clip.wav", AudioClipMode::Decode);
        static_cast<void>(audio->requestClip("game:/clip.wav", AudioClipMode::Stream));
        static_cast<void>(audio->clipState(clip));
        const std::array<f32, 4> samples{};
        static_cast<void>(audio->createClip(samples, 1, 48000));
        audio->unloadClip(clip);
        static_cast<void>(audio->clipInfo(clip));
        audio->play(e);
        audio->stop(e);
        static_cast<void>(audio->isPlaying(e));
        const AudioVoiceHandle voice = audio->playOneShot(clip, AudioPlayParams{});
        audio->stopVoice(voice);
        static_cast<void>(audio->isVoicePlaying(voice));
        audio->setMasterVolume(0.5f);
        static_cast<void>(audio->masterVolume());
        audio->setBusVolume(AudioBus::Music, 0.5f);
        static_cast<void>(audio->busVolume(AudioBus::Music));
        audio->setBusPaused(AudioBus::Music, true);
        static_cast<void>(audio->isBusPaused(AudioBus::Music));
        audio->setBusFilter(AudioBus::Music, AudioBusFilter{});
        static_cast<void>(audio->busFilter(AudioBus::Music));
        audio->setReverb(AudioReverbSettings{});
        static_cast<void>(audio->reverb());
    }
    if (InputService* input = context.service<InputService>())
    {
        const ActionId action = input->addAction("Jump", ActionType::Button).value_or(0);
        static_cast<void>(input->findAction("Jump"));
        static_cast<void>(input->actionCount());
        static_cast<void>(input->actionName(action));
        static_cast<void>(input->actionType(action));
        static_cast<void>(input->bindings(action));
        const std::array<InputBinding, 1> bindings{InputBinding::gamepadButton(GamepadButton::South)};
        input->setBindings(action, bindings);
        static_cast<void>(input->isDown(action));
        static_cast<void>(input->wasPressed(action));
        static_cast<void>(input->wasReleased(action));
        static_cast<void>(input->value(action));
        static_cast<void>(input->vector(action));
        static_cast<void>(input->lastDevice());
        static_cast<void>(input->lastGamepadType());
    }
    if (SceneService* scenes = context.service<SceneService>())
    {
        static_cast<void>(scenes->exists("game:/level.scene"));
        static_cast<void>(scenes->instantiate("game:/level.scene", entities));
        static_cast<void>(scenes->lastError());
    }
}

// Builds a static triangle-mesh body whose geometry the host's PhysicsWorld creates. The geometry and its
// code belong to the host, so the body must survive hot reloads of this module.
void createMeshBody(ModuleContext& context, test::ModuleControl& control)
{
    PhysicsService* physics = context.service<PhysicsService>();
    if (!control.createMesh || physics == nullptr)
    {
        return;
    }
    const std::array<Vec3, 4> vertices{Vec3(-5.0f, 0.0f, -5.0f), Vec3(5.0f, 0.0f, -5.0f),
                                       Vec3(5.0f, 0.0f, 5.0f), Vec3(-5.0f, 0.0f, 5.0f)};
    const std::array<u32, 6> indices{0, 2, 1, 0, 3, 2};
    control.mesh = physics->createMesh(vertices, indices);
    RigidBody body;
    body.motionType = MotionType::Static;
    Collider collider;
    collider.shape = ColliderShape::Mesh;
    collider.geometry = control.mesh;
    control.meshEntity = context.world().create(Transform{}, body, collider);
}

bool loadModule(ModuleContext& context)
{
    test::ModuleControl* control = context.service<test::ModuleControl>();
    if (control == nullptr || control->failLoad)
    {
        return false;
    }
    ++control->loads;
    control->reloadingSeen = control->reloadingSeen || context.isReloading();
    control->worldInLoad = control->worldInLoad || context.hasWorld();
    // The metadata strings live in this module's memory; the registry must copy them.
    reflect<test::ModuleCounter>("test.ModuleCounter")
        .meta({.displayName = "Module counter", .icon = "counter_1", .description = "Counts module updates."})
        .field("value", &test::ModuleCounter::value, {.displayName = "Counter value", .category = "State"});
    if (kVariant == 1)
    {
        reflect<test::LegacyComponent>("test.LegacyComponent").field("value", &test::LegacyComponent::value);
    }
    ILL_LOG_INFO("TestModule", "variant {} loaded", kVariant);
    return true;
}

void startModule(ModuleContext& context)
{
    test::ModuleControl* control = context.service<test::ModuleControl>();
    if (control == nullptr)
    {
        return; // load() refuses to run without it
    }
    ++control->starts;
    checkWorld(context, *control);
    World& world = context.world();
    world.create(test::SharedComponent{11});
    world.create(test::ModuleCounter{});
    if (kVariant == 1)
    {
        world.create(test::ModuleCounter{}, test::LegacyComponent{7});
    }
    createMeshBody(context, *control);
}

void stopModule(ModuleContext& context)
{
    if (test::ModuleControl* control = context.service<test::ModuleControl>())
    {
        ++control->stops;
        checkWorld(context, *control);
    }
}

void unloadModule(ModuleContext& context)
{
    if (test::ModuleControl* control = context.service<test::ModuleControl>())
    {
        ++control->unloads;
        control->reloadingSeen = control->reloadingSeen || context.isReloading();
        control->worldInLoad = control->worldInLoad || context.hasWorld();
    }
}

void updateModule(ModuleContext& context, f64)
{
    test::ModuleControl* service = context.service<test::ModuleControl>();
    if (service == nullptr)
    {
        return; // load() refuses to run without it
    }
    test::ModuleControl& control = *service;
    ++control.updates;
    checkWorld(context, control);
    if (const test::WorldTag* tag = context.service<test::WorldTag>())
    {
        control.tagSum += tag->value;
    }
    control.variant = kVariant;
    control.logger = &Logger::instance();
    control.registry = &ComponentRegistry::instance();
    control.assetPaths = &AssetPathTable::instance();
    control.asset = AssetRef("game:/models/../module.glb#mesh/1").id.value;
    control.counterId = componentId<test::ModuleCounter>();
    control.transformId = componentId<Transform>();
    control.threadIndex = currentThreadIndex();
    context.world().query<test::ModuleCounter>().each(
        [](test::ModuleCounter& counter) { counter.value += kVariant; });

    // A job that only a worker runs (this thread does not help), to see the worker index from inside the
    // module's copy of the engine.
    control.workerIndex.store(-2, std::memory_order_relaxed);
    std::atomic<bool> done{false};
    context.jobs().submit([&control, &done] {
        control.workerIndex.store(JobSystem::currentWorkerIndex(), std::memory_order_relaxed);
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire))
    {
        cpuRelax();
    }
    if (control.callServices)
    {
        callServices(context);
    }
    ILL_ASSERT(!control.assertInUpdate, "assertion from the test module");
}

constexpr ModuleApi makeApi()
{
    ModuleApi api = makeModuleApi("test_module", kVariant == 1 ? "1.0.0" : "1.1.0",
                                  {.load = &loadModule,
                                   .unload = &unloadModule,
                                   .start = &startModule,
                                   .stop = &stopModule,
                                   .update = &updateModule});
    if (kVariant == 4)
    {
        api.buildKey ^= 1u;
    }
    return api;
}

constexpr ModuleApi kTestModule = makeApi();

} // namespace

ILL_MODULE(test_module, kTestModule)
