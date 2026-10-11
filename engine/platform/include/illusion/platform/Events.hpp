#pragma once

/// @file
/// Events emitted by windows. Windows enqueue them on their EventBus while Platform::pollEvents() runs;
/// they are delivered by EventBus::dispatchQueued(). All events are trivially copyable and identify their
/// window by WindowId rather than by pointer, because a window may be destroyed before its queued events
/// are delivered.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/platform/Input.hpp>

namespace illusion
{

/// Identifies a window in events. Ids are unique for the lifetime of the process and never reused, so an
/// event that is dispatched after its window was destroyed simply matches no live window.
enum class WindowId : u32
{
    Invalid = 0 ///< No window.
};

/// The user asked to close the window (close button, Alt+F4, Cmd+Q).
struct WindowCloseRequestedEvent
{
    WindowId window; ///< Source window (compare with Window::id()).
};

/// The window's client area changed size, in window coordinates.
struct WindowResizedEvent
{
    WindowId window; ///< Source window (compare with Window::id()).
    UVec2 size;      ///< New size in window coordinates.
};

/// The window's framebuffer changed size, in pixels. Recreate swapchains on this event.
struct FramebufferResizedEvent
{
    WindowId window; ///< Source window (compare with Window::id()).
    UVec2 size;      ///< New size in pixels (0 x 0 while minimized).
};

/// The window gained or lost input focus.
struct WindowFocusEvent
{
    WindowId window; ///< Source window (compare with Window::id()).
    bool focused;    ///< True if the window now has focus.
};

/// A key went down, up, or repeated.
struct KeyEvent
{
    WindowId window;        ///< Source window (compare with Window::id()).
    Key key;                ///< Physical key.
    bool down;              ///< True for press and repeat, false for release.
    bool repeat;            ///< True for auto-repeat while held.
    KeyModifiers modifiers; ///< Modifier keys held.
};

/// A mouse button went down or up.
struct MouseButtonEvent
{
    WindowId window;        ///< Source window (compare with Window::id()).
    MouseButton button;     ///< Button.
    bool down;              ///< True for press, false for release.
    KeyModifiers modifiers; ///< Modifier keys held.
    Vec2 position;          ///< Cursor position in window coordinates.
};

/// The cursor moved.
struct MouseMovedEvent
{
    WindowId window; ///< Source window (compare with Window::id()).
    Vec2 position;   ///< New position in window coordinates.
};

/// The scroll wheel or touchpad scrolled.
struct MouseScrolledEvent
{
    WindowId window; ///< Source window (compare with Window::id()).
    Vec2 delta;      ///< Scroll amount; y is the vertical wheel.
};

/// A Unicode character was typed (after keyboard layout and IME processing).
struct TextInputEvent
{
    WindowId window;    ///< Source window (compare with Window::id()).
    char32_t codepoint; ///< UTF-32 code point.
};

} // namespace illusion
