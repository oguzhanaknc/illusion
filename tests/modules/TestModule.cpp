// A module for the module tests, built in several variants (TEST_MODULE_VARIANT):
// 1: registers ModuleCounter and LegacyComponent; update adds 1 to every counter.
// 2: registers only ModuleCounter; update adds 2.
// 3: widens ModuleCounter (TEST_MODULE_WIDE_COUNTER), which a hot reload must reject.
// 4: reports a wrong build key.

#include "TestModuleShared.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

namespace
{

using namespace illusion;

constexpr i32 kVariant = TEST_MODULE_VARIANT;

bool loadModule(ModuleContext& context)
{
    test::ModuleControl* control = context.service<test::ModuleControl>();
    if (control == nullptr || control->failLoad)
    {
        return false;
    }
    ++control->loads;
    control->reloadingSeen = control->reloadingSeen || context.isReloading();
    reflect<test::ModuleCounter>("test.ModuleCounter").field("value", &test::ModuleCounter::value);
    if (kVariant == 1)
    {
        reflect<test::LegacyComponent>("test.LegacyComponent").field("value", &test::LegacyComponent::value);
    }
    World& world = context.world();
    bool haveCounter = false;
    world.query<const test::ModuleCounter>().each([&](const test::ModuleCounter&) { haveCounter = true; });
    if (!haveCounter)
    {
        world.create(test::ModuleCounter{});
        if (kVariant == 1)
        {
            world.create(test::ModuleCounter{}, test::LegacyComponent{7});
        }
    }
    ILL_LOG_INFO("TestModule", "variant {} loaded", kVariant);
    return true;
}

void unloadModule(ModuleContext& context)
{
    if (test::ModuleControl* control = context.service<test::ModuleControl>())
    {
        ++control->unloads;
        control->reloadingSeen = control->reloadingSeen || context.isReloading();
    }
}

void updateModule(ModuleContext& context, f64)
{
    test::ModuleControl& control = *context.service<test::ModuleControl>();
    ++control.updates;
    control.variant = kVariant;
    control.logger = &Logger::instance();
    control.registry = &ComponentRegistry::instance();
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
    ModuleApi api = makeModuleApi("test_module", kVariant == 1 ? "1.0.0" : "1.1.0", &loadModule,
                                  &unloadModule, &updateModule);
    if (kVariant == 4)
    {
        api.buildKey ^= 1u;
    }
    return api;
}

constexpr ModuleApi kTestModule = makeApi();

} // namespace

ILL_MODULE(test_module, kTestModule)
