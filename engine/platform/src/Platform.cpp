#include "GlfwKeys.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

namespace illusion
{
namespace
{

std::atomic<bool> g_platformAlive{false};

void onGlfwError(int code, const char* description)
{
    const char* text = description != nullptr ? description : "";
    // Features a platform lacks (window positions on Wayland, some cursor shapes, an empty clipboard) are
    // expected; the callers handle them.
    if (code == GLFW_FEATURE_UNAVAILABLE || code == GLFW_CURSOR_UNAVAILABLE ||
        code == GLFW_FORMAT_UNAVAILABLE)
    {
        ILL_LOG_DEBUG("Platform", "GLFW: {}", text);
        return;
    }
    ILL_LOG_ERROR("Platform", "GLFW error 0x{:x}: {}", code, text);
}

int glfwCursorShape(CursorShape shape) noexcept
{
    switch (shape)
    {
    case CursorShape::Arrow:
        return GLFW_ARROW_CURSOR;
    case CursorShape::TextInput:
        return GLFW_IBEAM_CURSOR;
    case CursorShape::ResizeAll:
        return GLFW_RESIZE_ALL_CURSOR;
    case CursorShape::ResizeNS:
        return GLFW_RESIZE_NS_CURSOR;
    case CursorShape::ResizeEW:
        return GLFW_RESIZE_EW_CURSOR;
    case CursorShape::ResizeNESW:
        return GLFW_RESIZE_NESW_CURSOR;
    case CursorShape::ResizeNWSE:
        return GLFW_RESIZE_NWSE_CURSOR;
    case CursorShape::Hand:
        return GLFW_POINTING_HAND_CURSOR;
    case CursorShape::NotAllowed:
        return GLFW_NOT_ALLOWED_CURSOR;
    case CursorShape::Crosshair:
        return GLFW_CROSSHAIR_CURSOR;
    }
    return GLFW_ARROW_CURSOR;
}

} // namespace

struct Platform::Impl
{
    std::vector<Window*> windows;
    // Standard cursors, created on first use; a shape the system lacks stays null and shows the arrow.
    std::array<GLFWcursor*, kCursorShapeCount> cursors{};
    std::array<bool, kCursorShapeCount> cursorCreated{};
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
    for (GLFWcursor* cursor : m_impl->cursors)
    {
        if (cursor != nullptr)
        {
            glfwDestroyCursor(cursor);
        }
    }
    glfwTerminate();
    g_platformAlive.store(false);
}

void Platform::pollEvents()
{
    ILL_PROFILE_SCOPE();
    for (Window* window : m_impl->windows)
    {
        window->beginFrame();
    }
    glfwPollEvents();
}

void Platform::waitEvents(f64 timeoutSeconds)
{
    ILL_PROFILE_SCOPE();
    for (Window* window : m_impl->windows)
    {
        window->beginFrame();
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

std::vector<MonitorInfo> Platform::monitors() const
{
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    std::vector<MonitorInfo> infos;
    infos.reserve(static_cast<usize>(std::max(count, 0)));
    for (int i = 0; i < count; ++i)
    {
        GLFWmonitor* monitor = monitors[i];
        MonitorInfo info{};
        const char* name = glfwGetMonitorName(monitor);
        info.name = name != nullptr ? name : "";
        glfwGetMonitorPos(monitor, &info.position.x, &info.position.y);
        if (const GLFWvidmode* mode = glfwGetVideoMode(monitor))
        {
            info.size = UVec2(static_cast<u32>(std::max(mode->width, 0)),
                              static_cast<u32>(std::max(mode->height, 0)));
        }
        int workWidth = 0;
        int workHeight = 0;
        glfwGetMonitorWorkarea(monitor, &info.workPosition.x, &info.workPosition.y, &workWidth, &workHeight);
        info.workSize =
            UVec2(static_cast<u32>(std::max(workWidth, 0)), static_cast<u32>(std::max(workHeight, 0)));
        glfwGetMonitorContentScale(monitor, &info.contentScale.x, &info.contentScale.y);
        info.primary = i == 0; // GLFW lists the primary monitor first
        infos.push_back(std::move(info));
    }
    return infos;
}

void Platform::wake() noexcept
{
    glfwPostEmptyEvent();
}

std::optional<std::string> Platform::keyDisplayName(Key key) const
{
    const char* us = detail::usCharacter(key);
    if (us == nullptr)
    {
        return std::nullopt;
    }
    const char* name = glfwGetKeyName(detail::glfwFromKey(key), 0);
    return std::string(name != nullptr && name[0] != '\0' ? name : us);
}

void* Platform::cursor(CursorShape shape) noexcept
{
    if (shape == CursorShape::Arrow)
    {
        return nullptr; // the system's default cursor
    }
    const auto index = static_cast<usize>(shape);
    if (!m_impl->cursorCreated[index])
    {
        m_impl->cursorCreated[index] = true;
        m_impl->cursors[index] = glfwCreateStandardCursor(glfwCursorShape(shape));
    }
    return m_impl->cursors[index];
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
