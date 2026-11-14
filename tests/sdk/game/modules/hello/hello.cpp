// The static game module of the SDK test game (tests/sdk/game).

#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>

namespace
{

bool load(illusion::ModuleContext&)
{
    return true;
}

void start(illusion::ModuleContext&)
{
    ILL_LOG_INFO("SdkGame", "the static module started");
}

void update(illusion::ModuleContext&, illusion::f64)
{
}

constexpr illusion::ModuleApi kModule =
    illusion::makeModuleApi("hello", "1.0.0", {.load = &load, .start = &start, .update = &update});

} // namespace

ILL_MODULE(hello, kModule)
