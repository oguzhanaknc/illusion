#pragma once

/// @file
/// Feeds a Window's input to Dear ImGui and applies Dear ImGui's requests to the window: cursor shape,
/// clipboard, input method position and display size.

#include <illusion/core/Base.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/platform/Input.hpp>

#include <string>

namespace illusion
{
class Window;
} // namespace illusion

namespace illusion::editor::ui
{

class UiContext;

/// The ImGuiKey of @p key (ImGuiKey_None for keys Dear ImGui does not know), for shortcut chords.
[[nodiscard]] int imguiKey(Key key) noexcept;

/// The platform backend of one UiContext and one Window. Window events reach Dear ImGui in order through
/// the EventBus the window enqueues on, so dispatch the bus before newFrame(). Not thread-safe.
class UiPlatformBackend : NonCopyable
{
public:
    /// Subscribes to @p events and registers the backend with @p context. @p window and @p events must
    /// outlive the backend.
    UiPlatformBackend(UiContext& context, Window& window, EventBus& events);

    /// Unregisters the backend.
    ~UiPlatformBackend();

    /// Starts a Dear ImGui frame's input: display size and scale, @p deltaSeconds, and the cursor shape
    /// requested by the previous frame. Call before ImGui::NewFrame().
    void newFrame(f64 deltaSeconds);

    /// True once since the window's content scale changed, so the caller can rebuild fonts and style.
    [[nodiscard]] bool consumeContentScaleChange() noexcept;

    /// The window's current content scale.
    [[nodiscard]] Vec2 contentScale() const noexcept;

    /// Called by Dear ImGui's platform callbacks. Not for direct use.
    void setClipboardText(const char* text);
    /// Called by Dear ImGui's platform callbacks. Not for direct use.
    [[nodiscard]] const char* clipboardText();
    /// Called by Dear ImGui's platform callbacks. Not for direct use.
    void setImeRect(Vec2 position, f32 lineHeight, bool wantVisible) noexcept;

    /// Asks newFrame() to capture the cursor (`CursorMode::Captured`) for one frame while a drag field is
    /// being scrubbed by the mouse.
    void requestInfiniteDrag() noexcept { m_infiniteDragRequested = true; }

private:
    void subscribe();

    UiContext& m_context;
    Window& m_window;
    EventBus& m_events;
    Subscription m_keys;
    Subscription m_text;
    Subscription m_buttons;
    Subscription m_moves;
    Subscription m_scroll;
    Subscription m_focus;
    Subscription m_enter;
    Subscription m_scale;
    std::string m_clipboard; // keeps the text returned to Dear ImGui alive until the next call
    bool m_scaleChanged = false;
    bool m_infiniteDragRequested = false;
    bool m_infiniteDragging = false;
};

} // namespace illusion::editor::ui
