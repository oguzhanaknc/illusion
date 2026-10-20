#pragma once

/// @file
/// Operating-system window with keyboard and mouse input. Windows have no graphics API attached; the
/// renderer creates its own surface from nativeHandle().

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Input.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    bool maximized = false;         ///< Whether the window starts maximized.
};

/// How the mouse cursor behaves over the window.
enum class CursorMode : u8
{
    Normal,   ///< Visible and free.
    Hidden,   ///< Invisible over the window but free to leave it.
    Captured, ///< Invisible and locked to the window; reports unbounded (raw where available) motion.
};

/// Shape of the mouse cursor over a window (see Window::setCursorShape()).
enum class CursorShape : u8
{
    Arrow,      ///< The default arrow.
    TextInput,  ///< The text cursor (I-beam).
    ResizeAll,  ///< Moving in any direction.
    ResizeNS,   ///< Resizing vertically.
    ResizeEW,   ///< Resizing horizontally.
    ResizeNESW, ///< Resizing along the diagonal from bottom-left to top-right.
    ResizeNWSE, ///< Resizing along the diagonal from top-left to bottom-right.
    Hand,       ///< A pointing hand, for links and draggable items.
    NotAllowed, ///< An action that is not possible here.
    Crosshair,  ///< Precise selection.
};

/// Number of cursor shapes.
inline constexpr usize kCursorShapeCount = 10;

/// One image of a window icon: 8-bit RGBA pixels, rows from top to bottom, without padding.
struct WindowIcon
{
    UVec2 size;               ///< Size in pixels.
    std::span<const u8> rgba; ///< size.x * size.y * 4 bytes.
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

    /// Shows @p shape while the cursor is over the window. Shapes the system lacks (some diagonal cursors
    /// on X11 and macOS) show the arrow. Cursors are created once per Platform and shared by its windows.
    void setCursorShape(CursorShape shape) noexcept;

    /// The shape last set with setCursorShape().
    [[nodiscard]] CursorShape cursorShape() const noexcept { return m_cursorShape; }

    /// Moves the cursor to @p position in window coordinates. Returns false where applications may not
    /// move the cursor (Wayland, unless the cursor is captured).
    bool setCursorPosition(Vec2 position) noexcept;

    /// Position of the client area in screen coordinates, or nullopt where windows do not know their
    /// position (Wayland).
    [[nodiscard]] std::optional<IVec2> position() const noexcept;

    /// Moves the client area to @p position in screen coordinates. Returns false where applications may not
    /// move windows (Wayland).
    bool setPosition(IVec2 position) noexcept;

    /// Maximizes the window.
    void maximize() noexcept;

    /// Restores a maximized or minimized window to its normal size.
    void restore() noexcept;

    /// Minimizes (iconifies) the window.
    void iconify() noexcept;

    /// Returns true if the window is maximized.
    [[nodiscard]] bool isMaximized() const noexcept;

    /// Sets the window's icon from one or more images; the system picks the size closest to what it needs.
    /// An empty span restores the default icon. Returns false where windows have no icons of their own
    /// (macOS uses the application bundle's icon, Wayland the desktop file's) or if an image is invalid.
    bool setIcon(std::span<const WindowIcon> images);

    /// Tells the input method where text is being typed, so that its candidate window opens next to it.
    /// @p position and @p size are in window coordinates. Has an effect on Windows only; the other systems
    /// place the candidate window themselves.
    void setImeRect(Vec2 position, Vec2 size) noexcept;

    /// Replaces the system clipboard with @p text (UTF-8).
    void setClipboardText(std::string_view text);

    /// The text on the system clipboard (UTF-8), or "" if it holds no text.
    [[nodiscard]] std::string clipboardText() const;

    /// Path @p index of the files dropped since the last Platform::pollEvents() or waitEvents() (see
    /// FilesDroppedEvent).
    [[nodiscard]] std::string_view droppedPath(u32 index) const noexcept;

    /// Number of paths dropped since the last Platform::pollEvents() or waitEvents().
    [[nodiscard]] u32 droppedPathCount() const noexcept { return static_cast<u32>(m_droppedPaths.size()); }

    /// Delivers a drop of @p paths (UTF-8) at @p position, exactly as when the user drops files on the
    /// window: the paths are stored and a FilesDroppedEvent is enqueued. Used by the operating-system
    /// callback, and by input playback and automation.
    void dropFiles(std::span<const std::string_view> paths, Vec2 position);

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
    void beginFrame() noexcept;

    Platform& m_platform;
    void* m_handle;
    WindowId m_id;
    std::string m_title;
    EventBus* m_events;
    InputState m_input;
    CursorMode m_cursorMode = CursorMode::Normal;
    CursorShape m_cursorShape = CursorShape::Arrow;
    std::vector<std::string> m_droppedPaths;
};

} // namespace illusion
