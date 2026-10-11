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

#include <memory>
#include <string_view>

namespace illusion
{

class Window;

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

private:
    friend class Window;
    explicit Platform(PlatformBackend backend) noexcept;
    void registerWindow(Window* window);
    void unregisterWindow(Window* window) noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    PlatformBackend m_backend;
};

} // namespace illusion
