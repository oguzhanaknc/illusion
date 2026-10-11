#include <illusion/platform/Input.hpp>

namespace illusion
{
namespace
{

// Indexed by Key; must list every enumerator in declaration order.
constexpr std::array<std::string_view, static_cast<usize>(Key::Count)> kKeyNames = {
    "Unknown",
    "Space",
    "Apostrophe",
    "Comma",
    "Minus",
    "Period",
    "Slash",
    "Num0",
    "Num1",
    "Num2",
    "Num3",
    "Num4",
    "Num5",
    "Num6",
    "Num7",
    "Num8",
    "Num9",
    "Semicolon",
    "Equal",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "LeftBracket",
    "Backslash",
    "RightBracket",
    "GraveAccent",
    "Escape",
    "Enter",
    "Tab",
    "Backspace",
    "Insert",
    "Delete",
    "Right",
    "Left",
    "Down",
    "Up",
    "PageUp",
    "PageDown",
    "Home",
    "End",
    "CapsLock",
    "ScrollLock",
    "NumLock",
    "PrintScreen",
    "Pause",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
    "F13",
    "F14",
    "F15",
    "F16",
    "F17",
    "F18",
    "F19",
    "F20",
    "F21",
    "F22",
    "F23",
    "F24",
    "Keypad0",
    "Keypad1",
    "Keypad2",
    "Keypad3",
    "Keypad4",
    "Keypad5",
    "Keypad6",
    "Keypad7",
    "Keypad8",
    "Keypad9",
    "KeypadDecimal",
    "KeypadDivide",
    "KeypadMultiply",
    "KeypadSubtract",
    "KeypadAdd",
    "KeypadEnter",
    "KeypadEqual",
    "LeftShift",
    "LeftControl",
    "LeftAlt",
    "LeftSuper",
    "RightShift",
    "RightControl",
    "RightAlt",
    "RightSuper",
    "Menu",
};

constexpr std::array<std::string_view, static_cast<usize>(MouseButton::Count)> kMouseButtonNames = {
    "Left", "Right", "Middle", "Button4", "Button5"};

// Spot checks that the table is aligned with the enum.
static_assert(kKeyNames[static_cast<usize>(Key::A)] == "A");
static_assert(kKeyNames[static_cast<usize>(Key::Escape)] == "Escape");
static_assert(kKeyNames[static_cast<usize>(Key::F24)] == "F24");
static_assert(kKeyNames[static_cast<usize>(Key::KeypadEqual)] == "KeypadEqual");
static_assert(kKeyNames[static_cast<usize>(Key::Menu)] == "Menu");

} // namespace

std::string_view keyName(Key key) noexcept
{
    const auto index = static_cast<usize>(key);
    return index < kKeyNames.size() ? kKeyNames[index] : kKeyNames[0];
}

std::optional<Key> keyFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kKeyNames.size(); ++i)
    {
        if (kKeyNames[i] == name)
        {
            return static_cast<Key>(i);
        }
    }
    return std::nullopt;
}

std::string_view mouseButtonName(MouseButton button) noexcept
{
    const auto index = static_cast<usize>(button);
    return index < kMouseButtonNames.size() ? kMouseButtonNames[index] : std::string_view("Unknown");
}

void InputState::beginFrame() noexcept
{
    m_keysPressed.reset();
    m_keysReleased.reset();
    m_buttonsPressed.reset();
    m_buttonsReleased.reset();
    m_mouseDelta = Vec2(0.0f);
    m_scrollDelta = Vec2(0.0f);
    m_textLength = 0;
}

void InputState::setKey(Key key, bool down) noexcept
{
    const auto index = static_cast<usize>(key);
    if (key == Key::Unknown || index >= kKeyCount || m_keysDown.test(index) == down)
    {
        return;
    }
    m_keysDown.set(index, down);
    (down ? m_keysPressed : m_keysReleased).set(index);
}

void InputState::setMouseButton(MouseButton button, bool down) noexcept
{
    const auto index = static_cast<usize>(button);
    if (index >= kButtonCount || m_buttonsDown.test(index) == down)
    {
        return;
    }
    m_buttonsDown.set(index, down);
    (down ? m_buttonsPressed : m_buttonsReleased).set(index);
}

void InputState::setMousePosition(Vec2 position) noexcept
{
    if (m_hasMousePosition)
    {
        m_mouseDelta += position - m_mousePosition;
    }
    m_mousePosition = position;
    m_hasMousePosition = true;
}

void InputState::addText(char32_t codepoint) noexcept
{
    if (m_textLength < m_text.size())
    {
        m_text[m_textLength++] = codepoint;
    }
}

void InputState::releaseAll() noexcept
{
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
    m_modifiers = KeyModifiers{};
}

} // namespace illusion
