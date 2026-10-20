#pragma once

/// @file
/// Platform layer lifetime: initializes the windowing system (GLFW) and pumps OS events.
///
/// Exactly one Platform may exist at a time, and it must be created, used and destroyed on the main
/// thread (an operating-system requirement on macOS).
///
/// ```cpp
/// auto platform = Platform::create();
/// auto window = Window::create(*platform, WindowDesc{.title = "Game"}, &events);
/// while (!window->shouldClose())
/// {
///     platform->pollEvents();   // updates window input state and enqueues window events
///     events.dispatchQueued();
///     // update + render
/// }
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/platform/Input.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class Window;
enum class CursorShape : u8;

/// A connected display.
struct MonitorInfo
{
    std::string name;   ///< Name the system reports (UTF-8).
    IVec2 position;     ///< Top-left corner in screen coordinates.
    UVec2 size;         ///< Size of the current video mode in screen coordinates.
    IVec2 workPosition; ///< Top-left corner of the area not covered by task bars and docks.
    UVec2 workSize;     ///< Size of that area.
    Vec2 contentScale;  ///< Ratio between pixels and screen coordinates (for example 1.5 at 150%).
    bool primary;       ///< True for the primary monitor (listed first).
};

/// Which windowing backend to use.
enum class PlatformBackend : u8
{
    Native,   ///< The OS window system (Win32, Cocoa, X11 or Wayland).
    Headless, ///< No display: windows are simulated. For tests, servers and tools.
};

/// Creation parameters for Platform.
struct PlatformDesc
{
    PlatformBackend backend = PlatformBackend::Native; ///< Windowing backend.
};

/// Owns the windowing system. See the file documentation.
class Platform : NonCopyable
{
public:
    /// Initializes the windowing system. Returns nullptr (and logs an error) if it is unavailable, for
    /// example when no display is connected. Asserts if a Platform already exists.
    [[nodiscard]] static std::unique_ptr<Platform> create(const PlatformDesc& desc = {});

    /// Shuts the windowing system down. All windows must be destroyed first.
    ~Platform();

    /// Starts a new input frame for every window, then processes pending OS events without blocking.
    void pollEvents();

    /// Like pollEvents(), but sleeps until an event arrives or @p timeoutSeconds elapse. Useful for
    /// tools that only redraw on input.
    void waitEvents(f64 timeoutSeconds);

    /// Returns true when running without a display.
    [[nodiscard]] bool isHeadless() const noexcept { return m_backend == PlatformBackend::Headless; }

    /// Name of the active backend: "win32", "cocoa", "x11", "wayland" or "null".
    [[nodiscard]] std::string_view backendName() const noexcept;

    /// The connected monitors, the primary one first. The headless backend reports one simulated monitor.
    [[nodiscard]] std::vector<MonitorInfo> monitors() const;

    /// Makes a waitEvents() that is sleeping on the main thread return now. Thread-safe: call it from any
    /// thread while the Platform exists, for example when a background task has a result to show.
    void wake() noexcept;

    /// The character @p key types in the active keyboard layout, without modifiers, as the layout produces it
    /// (usually lowercase): "z" for Key::Y on a German layout, "ş" for Key::Semicolon on a Turkish one. Falls
    /// back to the US layout's character when the layout cannot be queried, and returns nullopt for keys
    /// that type no character (Space, Shift, arrows, function keys); show their translated label instead
    /// (see glyphLabelKey()).
    [[nodiscard]] std::optional<std::string> keyDisplayName(Key key) const;

private:
    friend class Window;
    explicit Platform(PlatformBackend backend) noexcept;
    void registerWindow(Window* window);
    void unregisterWindow(Window* window) noexcept;
    void* cursor(CursorShape shape) noexcept; // the cached GLFWcursor, or nullptr for the arrow

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    PlatformBackend m_backend;
};

} // namespace illusion
