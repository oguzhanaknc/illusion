// The static game module of the SDK test game (tests/sdk/game): it ends the game in its third update.

#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/runtime/SceneService.hpp>

namespace
{

constexpr int kExitUpdate = 3;

int g_updates = 0;

bool load(illusion::ModuleContext&)
{
    return true;
}

void start(illusion::ModuleContext&)
{
    ILL_LOG_INFO("SdkGame", "the static module started");
}

void update(illusion::ModuleContext& context, illusion::f64)
{
    if (++g_updates == kExitUpdate)
    {
        if (illusion::SceneService* scenes = context.service<illusion::SceneService>())
        {
            scenes->requestExit();
        }
    }
}

constexpr illusion::ModuleApi kModule =
    illusion::makeModuleApi("hello", "1.0.0", {.load = &load, .start = &start, .update = &update});

} // namespace

ILL_MODULE(hello, kModule)
