#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/World.hpp>
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
