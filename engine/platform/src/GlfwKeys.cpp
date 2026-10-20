#include "GlfwKeys.hpp"

#include <GLFW/glfw3.h>

namespace illusion::detail
{

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

int glfwFromKey(Key key) noexcept
{
    const int index = static_cast<int>(key);
    if (key >= Key::Num0 && key <= Key::Num9)
    {
        return GLFW_KEY_0 + (index - static_cast<int>(Key::Num0));
    }
    if (key >= Key::A && key <= Key::Z)
    {
        return GLFW_KEY_A + (index - static_cast<int>(Key::A));
    }
    if (key >= Key::F1 && key <= Key::F24)
    {
        return GLFW_KEY_F1 + (index - static_cast<int>(Key::F1));
    }
    if (key >= Key::Keypad0 && key <= Key::Keypad9)
    {
        return GLFW_KEY_KP_0 + (index - static_cast<int>(Key::Keypad0));
    }
    switch (key)
    {
    case Key::Space:
        return GLFW_KEY_SPACE;
    case Key::Apostrophe:
        return GLFW_KEY_APOSTROPHE;
    case Key::Comma:
        return GLFW_KEY_COMMA;
    case Key::Minus:
        return GLFW_KEY_MINUS;
    case Key::Period:
        return GLFW_KEY_PERIOD;
    case Key::Slash:
        return GLFW_KEY_SLASH;
    case Key::Semicolon:
        return GLFW_KEY_SEMICOLON;
    case Key::Equal:
        return GLFW_KEY_EQUAL;
    case Key::LeftBracket:
        return GLFW_KEY_LEFT_BRACKET;
    case Key::Backslash:
        return GLFW_KEY_BACKSLASH;
    case Key::RightBracket:
        return GLFW_KEY_RIGHT_BRACKET;
    case Key::GraveAccent:
        return GLFW_KEY_GRAVE_ACCENT;
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    case Key::Enter:
        return GLFW_KEY_ENTER;
    case Key::Tab:
        return GLFW_KEY_TAB;
    case Key::Backspace:
        return GLFW_KEY_BACKSPACE;
    case Key::Insert:
        return GLFW_KEY_INSERT;
    case Key::Delete:
        return GLFW_KEY_DELETE;
    case Key::Right:
        return GLFW_KEY_RIGHT;
    case Key::Left:
        return GLFW_KEY_LEFT;
    case Key::Down:
        return GLFW_KEY_DOWN;
    case Key::Up:
        return GLFW_KEY_UP;
    case Key::PageUp:
        return GLFW_KEY_PAGE_UP;
    case Key::PageDown:
        return GLFW_KEY_PAGE_DOWN;
    case Key::Home:
        return GLFW_KEY_HOME;
    case Key::End:
        return GLFW_KEY_END;
    case Key::CapsLock:
        return GLFW_KEY_CAPS_LOCK;
    case Key::ScrollLock:
        return GLFW_KEY_SCROLL_LOCK;
    case Key::NumLock:
        return GLFW_KEY_NUM_LOCK;
    case Key::PrintScreen:
        return GLFW_KEY_PRINT_SCREEN;
    case Key::Pause:
        return GLFW_KEY_PAUSE;
    case Key::KeypadDecimal:
        return GLFW_KEY_KP_DECIMAL;
    case Key::KeypadDivide:
        return GLFW_KEY_KP_DIVIDE;
    case Key::KeypadMultiply:
        return GLFW_KEY_KP_MULTIPLY;
    case Key::KeypadSubtract:
        return GLFW_KEY_KP_SUBTRACT;
    case Key::KeypadAdd:
        return GLFW_KEY_KP_ADD;
    case Key::KeypadEnter:
        return GLFW_KEY_KP_ENTER;
    case Key::KeypadEqual:
        return GLFW_KEY_KP_EQUAL;
    case Key::LeftShift:
        return GLFW_KEY_LEFT_SHIFT;
    case Key::LeftControl:
        return GLFW_KEY_LEFT_CONTROL;
    case Key::LeftAlt:
        return GLFW_KEY_LEFT_ALT;
    case Key::LeftSuper:
        return GLFW_KEY_LEFT_SUPER;
    case Key::RightShift:
        return GLFW_KEY_RIGHT_SHIFT;
    case Key::RightControl:
        return GLFW_KEY_RIGHT_CONTROL;
    case Key::RightAlt:
        return GLFW_KEY_RIGHT_ALT;
    case Key::RightSuper:
        return GLFW_KEY_RIGHT_SUPER;
    case Key::Menu:
        return GLFW_KEY_MENU;
    default:
        return GLFW_KEY_UNKNOWN;
    }
}

const char* usCharacter(Key key) noexcept
{
    static constexpr const char* kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    static constexpr const char* kLetters[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i",
                                               "j", "k", "l", "m", "n", "o", "p", "q", "r",
                                               "s", "t", "u", "v", "w", "x", "y", "z"};
    const int index = static_cast<int>(key);
    if (key >= Key::Num0 && key <= Key::Num9)
    {
        return kDigits[static_cast<usize>(index - static_cast<int>(Key::Num0))];
    }
    if (key >= Key::Keypad0 && key <= Key::Keypad9)
    {
        return kDigits[static_cast<usize>(index - static_cast<int>(Key::Keypad0))];
    }
    if (key >= Key::A && key <= Key::Z)
    {
        return kLetters[static_cast<usize>(index - static_cast<int>(Key::A))];
    }
    switch (key)
    {
    case Key::Apostrophe:
        return "'";
    case Key::Comma:
        return ",";
    case Key::Minus:
    case Key::KeypadSubtract:
        return "-";
    case Key::Period:
    case Key::KeypadDecimal:
        return ".";
    case Key::Slash:
    case Key::KeypadDivide:
        return "/";
    case Key::Semicolon:
        return ";";
    case Key::Equal:
    case Key::KeypadEqual:
        return "=";
    case Key::LeftBracket:
        return "[";
    case Key::Backslash:
        return "\\";
    case Key::RightBracket:
        return "]";
    case Key::GraveAccent:
        return "`";
    case Key::KeypadMultiply:
        return "*";
    case Key::KeypadAdd:
        return "+";
    default:
        return nullptr;
    }
}

} // namespace illusion::detail
