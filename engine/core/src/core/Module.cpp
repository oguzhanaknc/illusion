#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/ecs/Component.hpp>

#include <atomic>

namespace illusion
{
namespace detail
{
namespace
{

// Each binary that links the engine has its own copy of this value: 0 in the host, the id the host gave
// it in a shared module.
std::atomic<u32> g_moduleId{0};

} // namespace

u32 currentModuleId() noexcept
{
    return g_moduleId.load(std::memory_order_relaxed);
}

void installModuleServices(const ModuleServices& services) noexcept
{
    // See docs/adr/0003-type-identity-across-modules.md and docs/adr/0027-module-system-and-hot-reload.md.
    Logger::setInstance(services.logger);
    ComponentRegistry::setInstance(services.registry);
    setAssertHandler(services.assertHandler);
    setThreadIndexSource(services.threadIndex);
    setWorkerIndexSource(services.workerIndex);
    g_moduleId.store(services.moduleId, std::memory_order_relaxed);
}

ModuleServices hostModuleServices() noexcept
{
    ModuleServices services;
    services.logger = &Logger::instance();
    services.registry = &ComponentRegistry::instance();
    services.assertHandler = &dispatchAssertion; // follows later setAssertHandler() calls of the host
    services.threadIndex = &currentThreadIndex;
    services.workerIndex = &JobSystem::currentWorkerIndex;
    services.moduleId = currentModuleId();
    return services;
}

} // namespace detail
} // namespace illusion
