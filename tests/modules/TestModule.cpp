// A module for the module tests, built in several variants (TEST_MODULE_VARIANT):
// 1: registers ModuleCounter and LegacyComponent; update adds 1 to every counter.
// 2: registers only ModuleCounter; update adds 2.
// 3: widens ModuleCounter (TEST_MODULE_WIDE_COUNTER), which a hot reload must reject.
// 4: reports a wrong build key.
// Every variant is built with USES physics audio input render: the API targets give it the headers of
// those libraries without their code.

#include "TestModuleShared.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/SceneComponents.hpp>

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
    test::ModuleControl& control = *context.service<test::ModuleControl>(); // load() checked it
    ++control.starts;
    checkWorld(context, control);
    World& world = context.world();
    world.create(test::SharedComponent{11});
    world.create(test::ModuleCounter{});
    if (kVariant == 1)
    {
        world.create(test::ModuleCounter{}, test::LegacyComponent{7});
    }
}

void stopModule(ModuleContext& context)
{
    test::ModuleControl& control = *context.service<test::ModuleControl>();
    ++control.stops;
    checkWorld(context, control);
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
