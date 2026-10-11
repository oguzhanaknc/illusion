#pragma once

/// @file
/// Keyboard and mouse state.
///
/// InputState is a plain data model: the platform backend feeds it (setKey(), setMousePosition(), ...)
/// while polling events, and game code queries it. It does not depend on GLFW, so it can also be fed by
/// input recordings or tests.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <bitset>
#include <optional>
#include <string_view>

namespace illusion
{

/// Physical keyboard keys, named after the US layout. The names are stable and may be persisted (see
/// keyName()).
enum class Key : u16
{
    Unknown,
    Space,
    Apostrophe,
    Comma,
    Minus,
    Period,
    Slash,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Semicolon,
    Equal,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    LeftBracket,
    Backslash,
    RightBracket,
    GraveAccent,
    Escape,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    KeypadEqual,
    LeftShift,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,
    Menu,
    Count ///< Number of keys; not a key.
};

/// Mouse buttons.
enum class MouseButton : u8
{
    Left,
    Right,
    Middle,
    Button4,
    Button5,
    Count ///< Number of buttons; not a button.
};

/// Modifier keys held during a key or mouse event.
struct KeyModifiers
{
    bool shift = false;   ///< Either Shift key.
    bool control = false; ///< Either Control key.
    bool alt = false;     ///< Either Alt/Option key.
    bool super = false;   ///< Either Windows/Command key.

    /// Modifiers are equal when every flag matches.
    friend constexpr bool operator==(const KeyModifiers&, const KeyModifiers&) noexcept = default;
};

/// Stable, persistable name of @p key, e.g. "A", "Space", "LeftControl", "Keypad7".
[[nodiscard]] std::string_view keyName(Key key) noexcept;

/// Parses a name produced by keyName(). Returns std::nullopt for unknown names.
[[nodiscard]] std::optional<Key> keyFromName(std::string_view name) noexcept;

/// Stable name of @p button, e.g. "Left", "Button4".
[[nodiscard]] std::string_view mouseButtonName(MouseButton button) noexcept;

/// Keyboard and mouse state of one window for the current frame.
class InputState
{
public:
    /// Maximum number of text characters stored per frame; further characters are dropped.
    static constexpr usize kMaxTextPerFrame = 64;

    /// Returns true while @p key is held.
    [[nodiscard]] bool isDown(Key key) const noexcept { return m_keysDown.test(static_cast<usize>(key)); }

    /// Returns true if @p key went down during this frame.
    [[nodiscard]] bool wasPressed(Key key) const noexcept
    {
        return m_keysPressed.test(static_cast<usize>(key));
    }

    /// Returns true if @p key went up during this frame.
    [[nodiscard]] bool wasReleased(Key key) const noexcept
    {
        return m_keysReleased.test(static_cast<usize>(key));
    }

    /// Returns true while @p button is held.
    [[nodiscard]] bool isDown(MouseButton button) const noexcept
    {
        return m_buttonsDown.test(static_cast<usize>(button));
    }

    /// Returns true if @p button went down during this frame.
    [[nodiscard]] bool wasPressed(MouseButton button) const noexcept
    {
        return m_buttonsPressed.test(static_cast<usize>(button));
    }

    /// Returns true if @p button went up during this frame.
    [[nodiscard]] bool wasReleased(MouseButton button) const noexcept
    {
        return m_buttonsReleased.test(static_cast<usize>(button));
    }

    /// Cursor position in window coordinates (pixels from the top-left corner of the client area).
    [[nodiscard]] Vec2 mousePosition() const noexcept { return m_mousePosition; }

    /// Cursor movement during this frame.
    [[nodiscard]] Vec2 mouseDelta() const noexcept { return m_mouseDelta; }

    /// Scroll wheel movement during this frame (y is the usual vertical wheel).
    [[nodiscard]] Vec2 scrollDelta() const noexcept { return m_scrollDelta; }

    /// Unicode characters typed during this frame, in order (layout and IME aware).
    [[nodiscard]] std::u32string_view text() const noexcept { return {m_text.data(), m_textLength}; }

    /// Modifier keys from the most recent key or mouse button event.
    [[nodiscard]] KeyModifiers modifiers() const noexcept { return m_modifiers; }

    /// @name Backend interface
    /// Called by the platform layer (or an input recording) to feed events.
    /// @{

    /// Starts a new frame: clears pressed/released edges, deltas and text. Held state is kept.
    void beginFrame() noexcept;

    /// Records @p key going down or up. Key repeats (down while already down) are ignored.
    void setKey(Key key, bool down) noexcept;

    /// Records @p button going down or up.
    void setMouseButton(MouseButton button, bool down) noexcept;

    /// Records the cursor moving to @p position. The first position after construction or
    /// resetMouseTracking() produces no delta.
    void setMousePosition(Vec2 position) noexcept;

    /// Forgets the last cursor position (call when the cursor mode changes) so no jump is reported.
    void resetMouseTracking() noexcept { m_hasMousePosition = false; }

    /// Adds scroll wheel movement.
    void addScroll(Vec2 delta) noexcept { m_scrollDelta += delta; }

    /// Appends a typed character.
    void addText(char32_t codepoint) noexcept;

    /// Sets the current modifier state.
    void setModifiers(KeyModifiers modifiers) noexcept { m_modifiers = modifiers; }

    /// Releases every held key and button, reporting released edges. Call when the window loses focus
    /// so keys do not stay stuck.
    void releaseAll() noexcept;

    /// @}

private:
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static constexpr usize kButtonCount = static_cast<usize>(MouseButton::Count);

    std::bitset<kKeyCount> m_keysDown;
    std::bitset<kKeyCount> m_keysPressed;
    std::bitset<kKeyCount> m_keysReleased;
    std::bitset<kButtonCount> m_buttonsDown;
    std::bitset<kButtonCount> m_buttonsPressed;
    std::bitset<kButtonCount> m_buttonsReleased;
    Vec2 m_mousePosition{0.0f};
    Vec2 m_mouseDelta{0.0f};
    Vec2 m_scrollDelta{0.0f};
    std::array<char32_t, kMaxTextPerFrame> m_text{};
    usize m_textLength = 0;
    KeyModifiers m_modifiers;
    bool m_hasMousePosition = false;
};

} // namespace illusion
