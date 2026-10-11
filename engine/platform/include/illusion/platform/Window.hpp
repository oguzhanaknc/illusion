#pragma once

/// @file
/// Operating-system window with keyboard and mouse input. Windows have no graphics API attached; the
/// renderer creates its own surface from nativeHandle().

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Input.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace illusion
{

class EventBus;
class Platform;

/// Creation parameters for Window.
struct WindowDesc
{
    std::string title = "illusion"; ///< Title bar text (UTF-8).
    u32 width = 1280;               ///< Client area width in window coordinates.
    u32 height = 720;               ///< Client area height in window coordinates.
    bool resizable = true;          ///< Whether the user can resize the window.
    bool visible = true;            ///< Whether the window is shown immediately.
};

/// How the mouse cursor behaves over the window.
enum class CursorMode : u8
{
    Normal,   ///< Visible and free.
    Hidden,   ///< Invisible over the window but free to leave it.
    Captured, ///< Invisible and locked to the window; reports unbounded (raw where available) motion.
};

/// A top-level window. Must be used on the main thread. See Platform for the event loop.
class Window : NonCopyable
{
public:
    /// Creates a window. Returns nullptr (and logs an error) on failure. If @p events is not null, the
    /// window enqueues its events (see Events.hpp) on it; the bus must outlive the window.
    [[nodiscard]] static std::unique_ptr<Window> create(Platform& platform, const WindowDesc& desc,
                                                        EventBus* events = nullptr);

    /// Destroys the window.
    ~Window();

    /// Returns true once the user asked to close the window or setShouldClose(true) was called.
    [[nodiscard]] bool shouldClose() const noexcept;

    /// Sets or clears the close request.
    void setShouldClose(bool close) noexcept;

    /// Client area size in window coordinates.
    [[nodiscard]] UVec2 size() const noexcept;

    /// Framebuffer size in pixels. Differs from size() on high-DPI displays; (0, 0) while minimized.
    [[nodiscard]] UVec2 framebufferSize() const noexcept;

    /// Ratio between pixels and window coordinates (for example 2.0 on a Retina display).
    [[nodiscard]] Vec2 contentScale() const noexcept;

    /// Process-unique identifier used in window events.
    [[nodiscard]] WindowId id() const noexcept { return m_id; }

    /// Brings the window to the front and gives it input focus (the OS may refuse).
    void focus() noexcept;

    /// Returns true if the window has input focus.
    [[nodiscard]] bool isFocused() const noexcept;

    /// Returns true if the window is minimized (iconified).
    [[nodiscard]] bool isMinimized() const noexcept;

    /// Changes the title bar text (UTF-8).
    void setTitle(std::string_view title);

    /// Current title (UTF-8).
    [[nodiscard]] const std::string& title() const noexcept { return m_title; }

    /// Resizes the client area (window coordinates).
    void setSize(UVec2 size) noexcept;

    /// Changes how the cursor behaves over the window.
    void setCursorMode(CursorMode mode) noexcept;

    /// Current cursor mode.
    [[nodiscard]] CursorMode cursorMode() const noexcept { return m_cursorMode; }

    /// Keyboard and mouse state for the current frame (updated by Platform::pollEvents()).
    [[nodiscard]] const InputState& input() const noexcept { return m_input; }

    /// Mutable input state, for input playback and tools.
    [[nodiscard]] InputState& input() noexcept { return m_input; }

    /// The underlying GLFWwindow pointer, for the renderer. Opaque to keep GLFW out of public headers.
    [[nodiscard]] void* nativeHandle() const noexcept { return m_handle; }

private:
    friend class Platform;
    friend struct WindowCallbacks;
    Window(Platform& platform, void* handle, std::string title, EventBus* events) noexcept;

    Platform& m_platform;
    void* m_handle;
    WindowId m_id;
    std::string m_title;
    EventBus* m_events;
    InputState m_input;
    CursorMode m_cursorMode = CursorMode::Normal;
};

} // namespace illusion
