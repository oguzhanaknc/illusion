#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputActionsService.hpp>
#include <illusion/modules/ModuleHost.hpp>

#include <array>
#include <format>
#include <string>

using namespace illusion;

namespace
{

u64 g_updates = 0;

bool loadBench(ModuleContext&)
{
    return true;
}

void updateBench(ModuleContext& context, f64)
{
    g_updates += context.moduleName().size();
}

// Eight static modules with distinct names.
constexpr std::array<const char*, 8> kNames{"bench0", "bench1", "bench2", "bench3",
                                            "bench4", "bench5", "bench6", "bench7"};
constexpr std::array<ModuleApi, 8> kModules = [] {
    std::array<ModuleApi, 8> modules{};
    for (usize i = 0; i < modules.size(); ++i)
    {
        modules[i] = makeModuleApi(kNames[i], "1.0.0", &loadBench, nullptr, &updateBench);
    }
    return modules;
}();

// A static module that calls the InputService of its World kServiceCalls times per update().
constexpr usize kServiceCalls = 64;
ActionId g_jump = 0;
u32 g_down = 0;

void updateServiceBench(ModuleContext& context, f64)
{
    const InputService* input = context.service<InputService>();
    if (input == nullptr)
    {
        return;
    }
    for (usize i = 0; i < kServiceCalls; ++i)
    {
        g_down += input->isDown(g_jump) ? 1u : 0u;
    }
}

constexpr ModuleApi kServiceModule =
    makeModuleApi("service_bench", "1.0.0", &loadBench, nullptr, &updateServiceBench);

} // namespace

// Budgets are roughly 5-10x a desktop result; see Budget.hpp.

// Per module and Simulation World; the edit World is attached as in the editor and never updated.
ILL_BENCHMARK("modules/update", 25.0)
{
    JobSystem jobs(JobSystemDesc{1, 64});
    World edit;
    std::array<World, 2> play;
    ModuleHost host(jobs);
    host.attachWorld(edit, WorldRole::Edit);
    for (World& world : play)
    {
        host.attachWorld(world, WorldRole::Simulation);
    }
    for (const ModuleApi& api : kModules)
    {
        if (host.add(api))
        {
            return;
        }
    }
    bench.batch(kModules.size() * play.size()).unit("module").run("update 8 modules on 2 Worlds", [&] {
        host.update(1.0 / 60.0);
        ankerl::nanobench::doNotOptimizeAway(g_updates);
    });
    for (World& world : play)
    {
        host.detachWorld(world);
    }
    host.detachWorld(edit);
}

// One call of a host service from module code: an indirect call through the host's vtable into the
// adapter, which forwards to the engine library (ADR 0041). A static module's update() finds the service
// through its ModuleContext, as module code does, and calls it kServiceCalls times; the host's dispatch
// of update() is shared by those calls.
ILL_BENCHMARK("services/virtual-call", 10.0)
{
    InputActions actions;
    g_jump = actions.addAction("Jump", ActionType::Button);
    InputActionsService service(actions);
    JobSystem jobs(JobSystemDesc{1, 64});
    World play;
    ModuleHost host(jobs);
    ServiceSet services;
    services.provide<InputService>(service);
    host.attachWorld(play, WorldRole::Simulation, std::move(services));
    if (host.add(kServiceModule))
    {
        return;
    }
    bench.batch(kServiceCalls).unit("call").run("InputService::isDown", [&] {
        host.update(play, 1.0 / 60.0);
        ankerl::nanobench::doNotOptimizeAway(g_down);
    });
    host.detachWorld(play);
}
