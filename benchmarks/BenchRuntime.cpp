#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/GameSession.hpp>

using namespace illusion;

namespace
{

bool loadEmpty(ModuleContext&)
{
    return true;
}

void updateEmpty(ModuleContext&, f64)
{
}

constexpr ModuleApi kEmptyModule = makeModuleApi("bench_empty", "1.0.0", &loadEmpty, nullptr, &updateEmpty);

} // namespace

// One update of a GameSession over an empty World with one module attached: the fixed cost of every step
// (input, module dispatch, guids, physics, hierarchy, audio) that every game pays.
ILL_BENCHMARK("runtime/session-step-empty", 50000.0)
{
    JobSystem jobs(JobSystemDesc{2, 1024});
    VirtualFileSystem files;
    ModuleHost modules(jobs);
    if (modules.add(kEmptyModule))
    {
        return;
    }
    GameSessionDesc desc;
    desc.jobs = &jobs;
    desc.files = &files;
    desc.modules = &modules;
    desc.audioOutput = AudioOutput::Offline;
    auto session = GameSession::create(desc);
    if (!session)
    {
        return;
    }
    (*session)->start();
    bench.unit("step").run("GameSession::update", [&] { (*session)->update(1.0 / 60.0); });
}
