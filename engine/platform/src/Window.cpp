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

namespace illusion
{
namespace
{

std::atomic<u32> g_nextWindowId{1};

GLFWwindow* glfw(void* handle) noexcept
{
    return static_cast<GLFWwindow*>(handle);
}

Key keyFromGlfw(int key) noexcept
{
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
    {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (key - GLFW_KEY_0));
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        return static_cast<Key>(static_cast<int>(Key::A) + (key - GLFW_KEY_A));
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24)
    {
        return static_cast<Key>(static_cast<int>(Key::F1) + (key - GLFW_KEY_F1));
    }
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
    {
        return static_cast<Key>(static_cast<int>(Key::Keypad0) + (key - GLFW_KEY_KP_0));
    }
    switch (key)
    {
    case GLFW_KEY_SPACE:
        return Key::Space;
    case GLFW_KEY_APOSTROPHE:
        return Key::Apostrophe;
    case GLFW_KEY_COMMA:
        return Key::Comma;
    case GLFW_KEY_MINUS:
        return Key::Minus;
    case GLFW_KEY_PERIOD:
        return Key::Period;
    case GLFW_KEY_SLASH:
        return Key::Slash;
    case GLFW_KEY_SEMICOLON:
        return Key::Semicolon;
    case GLFW_KEY_EQUAL:
        return Key::Equal;
    case GLFW_KEY_LEFT_BRACKET:
        return Key::LeftBracket;
    case GLFW_KEY_BACKSLASH:
        return Key::Backslash;
    case GLFW_KEY_RIGHT_BRACKET:
        return Key::RightBracket;
    case GLFW_KEY_GRAVE_ACCENT:
        return Key::GraveAccent;
    case GLFW_KEY_ESCAPE:
        return Key::Escape;
    case GLFW_KEY_ENTER:
        return Key::Enter;
    case GLFW_KEY_TAB:
        return Key::Tab;
    case GLFW_KEY_BACKSPACE:
        return Key::Backspace;
    case GLFW_KEY_INSERT:
        return Key::Insert;
    case GLFW_KEY_DELETE:
        return Key::Delete;
    case GLFW_KEY_RIGHT:
        return Key::Right;
    case GLFW_KEY_LEFT:
        return Key::Left;
    case GLFW_KEY_DOWN:
        return Key::Down;
    case GLFW_KEY_UP:
        return Key::Up;
    case GLFW_KEY_PAGE_UP:
        return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return Key::PageDown;
    case GLFW_KEY_HOME:
        return Key::Home;
    case GLFW_KEY_END:
        return Key::End;
    case GLFW_KEY_CAPS_LOCK:
        return Key::CapsLock;
    case GLFW_KEY_SCROLL_LOCK:
        return Key::ScrollLock;
    case GLFW_KEY_NUM_LOCK:
        return Key::NumLock;
    case GLFW_KEY_PRINT_SCREEN:
        return Key::PrintScreen;
    case GLFW_KEY_PAUSE:
        return Key::Pause;
    case GLFW_KEY_KP_DECIMAL:
        return Key::KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE:
        return Key::KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY:
        return Key::KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT:
        return Key::KeypadSubtract;
    case GLFW_KEY_KP_ADD:
        return Key::KeypadAdd;
    case GLFW_KEY_KP_ENTER:
        return Key::KeypadEnter;
    case GLFW_KEY_KP_EQUAL:
        return Key::KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT:
        return Key::LeftShift;
    case GLFW_KEY_LEFT_CONTROL:
        return Key::LeftControl;
    case GLFW_KEY_LEFT_ALT:
        return Key::LeftAlt;
    case GLFW_KEY_LEFT_SUPER:
        return Key::LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT:
        return Key::RightShift;
    case GLFW_KEY_RIGHT_CONTROL:
        return Key::RightControl;
    case GLFW_KEY_RIGHT_ALT:
        return Key::RightAlt;
    case GLFW_KEY_RIGHT_SUPER:
        return Key::RightSuper;
    case GLFW_KEY_MENU:
        return Key::Menu;
    default:
        return Key::Unknown;
    }
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
        const Key key = keyFromGlfw(glfwKey);
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

} // namespace illusion
