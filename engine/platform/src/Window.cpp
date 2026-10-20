#include "GlfwKeys.hpp"
#include "Ime.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

std::atomic<u32> g_nextWindowId{1};

GLFWwindow* glfw(void* handle) noexcept
{
    return static_cast<GLFWwindow*>(handle);
}

KeyModifiers modifiersFromGlfw(int mods) noexcept
{
    return KeyModifiers{(mods & GLFW_MOD_SHIFT) != 0, (mods & GLFW_MOD_CONTROL) != 0,
                        (mods & GLFW_MOD_ALT) != 0, (mods & GLFW_MOD_SUPER) != 0};
}

Vec2 cursorPosition(GLFWwindow* window) noexcept
{
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    return Vec2(static_cast<f32>(x), static_cast<f32>(y));
}

UVec2 toSize(int width, int height) noexcept
{
    return UVec2(static_cast<u32>(std::max(width, 0)), static_cast<u32>(std::max(height, 0)));
}

bool isWayland() noexcept
{
    return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
}

} // namespace

/// GLFW callbacks; a friend of Window so they can update its input state.
struct WindowCallbacks
{
    static Window& self(GLFWwindow* handle) noexcept
    {
        return *static_cast<Window*>(glfwGetWindowUserPointer(handle));
    }

    template <class E>
    static void emit(Window& window, const E& event)
    {
        if (window.m_events != nullptr)
        {
            window.m_events->enqueue(event);
        }
    }

    static void onClose(GLFWwindow* handle)
    {
        Window& window = self(handle);
        emit(window, WindowCloseRequestedEvent{window.m_id});
    }

    static void onSize(GLFWwindow* handle, int width, int height)
    {
        Window& window = self(handle);
        emit(window, WindowResizedEvent{window.m_id, toSize(width, height)});
    }

    static void onFramebufferSize(GLFWwindow* handle, int width, int height)
    {
        Window& window = self(handle);
        emit(window, FramebufferResizedEvent{window.m_id, toSize(width, height)});
    }

    static void onFocus(GLFWwindow* handle, int focused)
    {
        Window& window = self(handle);
        if (focused == GLFW_FALSE)
        {
            // The OS will not report releases that happen while another window has focus. Release
            // everything now and tell event consumers too, so no key or button stays stuck.
            const KeyModifiers none{};
            for (u16 i = 1; i < static_cast<u16>(Key::Count); ++i)
            {
                const auto key = static_cast<Key>(i);
                if (window.m_input.isDown(key))
                {
                    emit(window, KeyEvent{window.m_id, key, false, false, none});
                }
            }
            for (u8 i = 0; i < static_cast<u8>(MouseButton::Count); ++i)
            {
                const auto button = static_cast<MouseButton>(i);
                if (window.m_input.isDown(button))
                {
                    emit(window,
                         MouseButtonEvent{window.m_id, button, false, none, window.m_input.mousePosition()});
                }
            }
            window.m_input.releaseAll();
        }
        emit(window, WindowFocusEvent{window.m_id, focused == GLFW_TRUE});
    }

    static void onKey(GLFWwindow* handle, int glfwKey, int /*scancode*/, int action, int mods)
    {
        Window& window = self(handle);
        const Key key = detail::keyFromGlfw(glfwKey);
        const KeyModifiers modifiers = modifiersFromGlfw(mods);
        const bool down = action != GLFW_RELEASE;
        window.m_input.setModifiers(modifiers);
        window.m_input.setKey(key, down);
        emit(window, KeyEvent{window.m_id, key, down, action == GLFW_REPEAT, modifiers});
    }

    static void onChar(GLFWwindow* handle, unsigned int codepoint)
    {
        Window& window = self(handle);
        window.m_input.addText(static_cast<char32_t>(codepoint));
        emit(window, TextInputEvent{window.m_id, static_cast<char32_t>(codepoint)});
    }

    static void onMouseButton(GLFWwindow* handle, int glfwButton, int action, int mods)
    {
        Window& window = self(handle);
        if (glfwButton < 0 || glfwButton >= static_cast<int>(MouseButton::Count))
        {
            return;
        }
        const auto button = static_cast<MouseButton>(glfwButton);
        const KeyModifiers modifiers = modifiersFromGlfw(mods);
        const bool down = action == GLFW_PRESS;
        window.m_input.setModifiers(modifiers);
        window.m_input.setMouseButton(button, down);
        emit(window, MouseButtonEvent{window.m_id, button, down, modifiers, window.m_input.mousePosition()});
    }

    static void onCursorPos(GLFWwindow* handle, double x, double y)
    {
        Window& window = self(handle);
        const Vec2 position(static_cast<f32>(x), static_cast<f32>(y));
        window.m_input.setMousePosition(position);
        emit(window, MouseMovedEvent{window.m_id, position});
    }

    static void onScroll(GLFWwindow* handle, double x, double y)
    {
        Window& window = self(handle);
        const Vec2 delta(static_cast<f32>(x), static_cast<f32>(y));
        window.m_input.addScroll(delta);
        emit(window, MouseScrolledEvent{window.m_id, delta});
    }

    static void onDrop(GLFWwindow* handle, int count, const char** paths)
    {
        Window& window = self(handle);
        std::vector<std::string_view> views;
        views.reserve(static_cast<usize>(std::max(count, 0)));
        for (int i = 0; i < count; ++i)
        {
            views.emplace_back(paths[i] != nullptr ? paths[i] : "");
        }
        window.dropFiles(views, cursorPosition(handle));
    }

    static void onContentScale(GLFWwindow* handle, float x, float y)
    {
        Window& window = self(handle);
        emit(window, ContentScaleChangedEvent{window.m_id, Vec2(x, y)});
    }

    static void onPosition(GLFWwindow* handle, int x, int y)
    {
        Window& window = self(handle);
        emit(window, WindowMovedEvent{window.m_id, IVec2(x, y)});
    }

    static void onCursorEnter(GLFWwindow* handle, int entered)
    {
        Window& window = self(handle);
        emit(window, CursorEnteredEvent{window.m_id, entered == GLFW_TRUE});
    }

    static void onRefresh(GLFWwindow* handle)
    {
        Window& window = self(handle);
        emit(window, WindowRefreshEvent{window.m_id});
    }
};

Window::Window(Platform& platform, void* handle, std::string title, EventBus* events) noexcept
    : m_platform(platform)
    , m_handle(handle)
    , m_id(static_cast<WindowId>(g_nextWindowId.fetch_add(1, std::memory_order_relaxed)))
    , m_title(std::move(title))
    , m_events(events)
{
}

std::unique_ptr<Window> Window::create(Platform& platform, const WindowDesc& desc, EventBus* events)
{
    constexpr u32 kMaxDimension = static_cast<u32>(std::numeric_limits<int>::max());
    if (desc.width == 0 || desc.height == 0 || desc.width > kMaxDimension || desc.height > kMaxDimension)
    {
        ILL_LOG_ERROR("Platform", "invalid window size {}x{}", desc.width, desc.height);
        return nullptr;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, desc.maximized ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* handle = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                                          desc.title.c_str(), nullptr, nullptr);
    if (handle == nullptr)
    {
        ILL_LOG_ERROR("Platform", "failed to create window '{}'", desc.title);
        return nullptr;
    }

    std::unique_ptr<Window> window(new Window(platform, handle, desc.title, events));
    glfwSetWindowUserPointer(handle, window.get());
    glfwSetWindowCloseCallback(handle, &WindowCallbacks::onClose);
    glfwSetWindowSizeCallback(handle, &WindowCallbacks::onSize);
    glfwSetFramebufferSizeCallback(handle, &WindowCallbacks::onFramebufferSize);
    glfwSetWindowFocusCallback(handle, &WindowCallbacks::onFocus);
    glfwSetKeyCallback(handle, &WindowCallbacks::onKey);
    glfwSetCharCallback(handle, &WindowCallbacks::onChar);
    glfwSetMouseButtonCallback(handle, &WindowCallbacks::onMouseButton);
    glfwSetCursorPosCallback(handle, &WindowCallbacks::onCursorPos);
    glfwSetScrollCallback(handle, &WindowCallbacks::onScroll);
    glfwSetDropCallback(handle, &WindowCallbacks::onDrop);
    glfwSetWindowContentScaleCallback(handle, &WindowCallbacks::onContentScale);
    glfwSetWindowPosCallback(handle, &WindowCallbacks::onPosition);
    glfwSetCursorEnterCallback(handle, &WindowCallbacks::onCursorEnter);
    glfwSetWindowRefreshCallback(handle, &WindowCallbacks::onRefresh);

    window->m_input.setMousePosition(cursorPosition(handle));
    platform.registerWindow(window.get());
    return window;
}

Window::~Window()
{
    m_platform.unregisterWindow(this);
    glfwDestroyWindow(glfw(m_handle));
}

bool Window::shouldClose() const noexcept
{
    return glfwWindowShouldClose(glfw(m_handle)) == GLFW_TRUE;
}

void Window::setShouldClose(bool close) noexcept
{
    glfwSetWindowShouldClose(glfw(m_handle), close ? GLFW_TRUE : GLFW_FALSE);
}

UVec2 Window::size() const noexcept
{
    int width = 0;
    int height = 0;
    glfwGetWindowSize(glfw(m_handle), &width, &height);
    return toSize(width, height);
}

UVec2 Window::framebufferSize() const noexcept
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(glfw(m_handle), &width, &height);
    return toSize(width, height);
}

Vec2 Window::contentScale() const noexcept
{
    float x = 1.0f;
    float y = 1.0f;
    glfwGetWindowContentScale(glfw(m_handle), &x, &y);
    return Vec2(x, y);
}

void Window::focus() noexcept
{
    glfwFocusWindow(glfw(m_handle));
}

bool Window::isFocused() const noexcept
{
    return glfwGetWindowAttrib(glfw(m_handle), GLFW_FOCUSED) == GLFW_TRUE;
}

bool Window::isMinimized() const noexcept
{
    return glfwGetWindowAttrib(glfw(m_handle), GLFW_ICONIFIED) == GLFW_TRUE;
}

void Window::setTitle(std::string_view title)
{
    m_title.assign(title);
    glfwSetWindowTitle(glfw(m_handle), m_title.c_str());
}

void Window::setSize(UVec2 size) noexcept
{
    ILL_ASSERT(size.x > 0 && size.y > 0, "window size must be positive");
    glfwSetWindowSize(glfw(m_handle), static_cast<int>(size.x), static_cast<int>(size.y));
}

void Window::setCursorMode(CursorMode mode) noexcept
{
    GLFWwindow* handle = glfw(m_handle);
    switch (mode)
    {
    case CursorMode::Normal:
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        break;
    case CursorMode::Hidden:
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        break;
    case CursorMode::Captured:
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        break;
    }
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION,
                         mode == CursorMode::Captured ? GLFW_TRUE : GLFW_FALSE);
    }
    m_cursorMode = mode;
    m_input.resetMouseTracking();
}

void Window::setCursorShape(CursorShape shape) noexcept
{
    glfwSetCursor(glfw(m_handle), static_cast<GLFWcursor*>(m_platform.cursor(shape)));
    m_cursorShape = shape;
}

bool Window::setCursorPosition(Vec2 position) noexcept
{
    if (isWayland() && m_cursorMode != CursorMode::Captured)
    {
        return false;
    }
    glfwSetCursorPos(glfw(m_handle), static_cast<double>(position.x), static_cast<double>(position.y));
    return true;
}

std::optional<IVec2> Window::position() const noexcept
{
    if (isWayland())
    {
        return std::nullopt;
    }
    IVec2 position(0);
    glfwGetWindowPos(glfw(m_handle), &position.x, &position.y);
    return position;
}

bool Window::setPosition(IVec2 position) noexcept
{
    if (isWayland())
    {
        return false;
    }
    glfwSetWindowPos(glfw(m_handle), position.x, position.y);
    return true;
}

void Window::maximize() noexcept
{
    glfwMaximizeWindow(glfw(m_handle));
}

void Window::restore() noexcept
{
    glfwRestoreWindow(glfw(m_handle));
}

void Window::iconify() noexcept
{
    glfwIconifyWindow(glfw(m_handle));
}

bool Window::isMaximized() const noexcept
{
    return glfwGetWindowAttrib(glfw(m_handle), GLFW_MAXIMIZED) == GLFW_TRUE;
}

bool Window::setIcon(std::span<const WindowIcon> images)
{
    const int platform = glfwGetPlatform();
    if (platform == GLFW_PLATFORM_WAYLAND || platform == GLFW_PLATFORM_COCOA)
    {
        return false;
    }
    std::vector<GLFWimage> glfwImages;
    glfwImages.reserve(images.size());
    for (const WindowIcon& image : images)
    {
        constexpr u32 kMaxSide = 1u << 15;
        if (image.size.x == 0 || image.size.y == 0 || image.size.x > kMaxSide || image.size.y > kMaxSide ||
            image.rgba.size() != usize{image.size.x} * image.size.y * 4)
        {
            return false;
        }
        // GLFW copies the pixels and never writes to them.
        glfwImages.push_back(GLFWimage{static_cast<int>(image.size.x), static_cast<int>(image.size.y),
                                       const_cast<unsigned char*>(image.rgba.data())});
    }
    glfwSetWindowIcon(glfw(m_handle), static_cast<int>(glfwImages.size()),
                      glfwImages.empty() ? nullptr : glfwImages.data());
    return true;
}

void Window::setImeRect(Vec2 position, Vec2 size) noexcept
{
    detail::setImeRect(m_handle, position, size);
}

void Window::setClipboardText(std::string_view text)
{
    const std::string terminated(text); // GLFW needs a null-terminated string
    glfwSetClipboardString(glfw(m_handle), terminated.c_str());
}

std::string Window::clipboardText() const
{
    const char* text = glfwGetClipboardString(glfw(m_handle));
    return text != nullptr ? std::string(text) : std::string();
}

std::string_view Window::droppedPath(u32 index) const noexcept
{
    ILL_ASSERT(index < m_droppedPaths.size(), "dropped path {} of {}", index, m_droppedPaths.size());
    return index < m_droppedPaths.size() ? std::string_view(m_droppedPaths[index]) : std::string_view();
}

void Window::dropFiles(std::span<const std::string_view> paths, Vec2 position)
{
    if (paths.empty())
    {
        return;
    }
    const auto first = static_cast<u32>(m_droppedPaths.size());
    for (const std::string_view path : paths)
    {
        m_droppedPaths.emplace_back(path);
    }
    WindowCallbacks::emit(*this, FilesDroppedEvent{m_id, first, static_cast<u32>(paths.size()), position});
}

void Window::beginFrame() noexcept
{
    m_input.beginFrame();
    m_droppedPaths.clear();
}

} // namespace illusion
