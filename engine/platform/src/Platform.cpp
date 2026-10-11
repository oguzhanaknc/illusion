#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace illusion
{
namespace
{

std::atomic<bool> g_platformAlive{false};

void onGlfwError(int code, const char* description)
{
    ILL_LOG_ERROR("Platform", "GLFW error 0x{:x}: {}", code, description != nullptr ? description : "");
}

} // namespace

struct Platform::Impl
{
    std::vector<Window*> windows;
};

Platform::Platform(PlatformBackend backend) noexcept
    : m_impl(std::make_unique<Impl>())
    , m_backend(backend)
{
}

std::unique_ptr<Platform> Platform::create(const PlatformDesc& desc)
{
    const bool alreadyAlive = g_platformAlive.exchange(true);
    ILL_ASSERT(!alreadyAlive, "only one Platform may exist at a time");
    if (alreadyAlive)
    {
        return nullptr;
    }

    glfwSetErrorCallback(&onGlfwError);
    glfwInitHint(GLFW_PLATFORM,
                 desc.backend == PlatformBackend::Headless ? GLFW_PLATFORM_NULL : GLFW_ANY_PLATFORM);
    if (glfwInit() != GLFW_TRUE)
    {
        ILL_LOG_ERROR("Platform", "failed to initialize the windowing system");
        g_platformAlive.store(false);
        return nullptr;
    }

    std::unique_ptr<Platform> platform(new Platform(desc.backend));
    ILL_LOG_INFO("Platform", "windowing backend: {}", platform->backendName());
    return platform;
}

Platform::~Platform()
{
    ILL_ASSERT(m_impl->windows.empty(), "Platform destroyed while {} window(s) are alive",
               m_impl->windows.size());
    glfwTerminate();
    g_platformAlive.store(false);
}

void Platform::pollEvents()
{
    ILL_PROFILE_SCOPE();
    for (Window* window : m_impl->windows)
    {
        window->m_input.beginFrame();
    }
    glfwPollEvents();
}

void Platform::waitEvents(f64 timeoutSeconds)
{
    ILL_PROFILE_SCOPE();
    for (Window* window : m_impl->windows)
    {
        window->m_input.beginFrame();
    }
    glfwWaitEventsTimeout(timeoutSeconds);
}

std::string_view Platform::backendName() const noexcept
{
    switch (glfwGetPlatform())
    {
    case GLFW_PLATFORM_WIN32:
        return "win32";
    case GLFW_PLATFORM_COCOA:
        return "cocoa";
    case GLFW_PLATFORM_X11:
        return "x11";
    case GLFW_PLATFORM_WAYLAND:
        return "wayland";
    case GLFW_PLATFORM_NULL:
        return "null";
    default:
        return "unknown";
    }
}

void Platform::registerWindow(Window* window)
{
    m_impl->windows.push_back(window);
}

void Platform::unregisterWindow(Window* window) noexcept
{
    std::erase(m_impl->windows, window);
}

} // namespace illusion
