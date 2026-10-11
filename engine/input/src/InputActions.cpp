#include <illusion/core/Assert.hpp>
#include <illusion/input/InputActions.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace illusion
{
namespace
{

constexpr usize kKeyCount = static_cast<usize>(Key::Count);
constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Count);
constexpr usize kGamepadButtonCount = static_cast<usize>(GamepadButton::Count);
constexpr usize kGamepadAxisCount = static_cast<usize>(GamepadAxis::Count);

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    usize start = 0;
    while (start <= text.size())
    {
        const usize end = std::min(text.find(separator, start), text.size());
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

// "LeftControl" -> "left_control", "Keypad7" -> "keypad7".
std::string snakeCase(std::string_view name)
{
    std::string result;
    for (usize i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        if (std::isupper(static_cast<unsigned char>(c)) != 0)
        {
            if (i > 0 && std::islower(static_cast<unsigned char>(name[i - 1])) != 0)
            {
                result.push_back('_');
            }
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        else
        {
            result.push_back(c);
        }
    }
    return result;
}

bool isTriggerAxis(GamepadAxis axis) noexcept
{
    return axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
}

// Axis value with the gamepad's dead zones applied (sticks radially).
f32 deadZonedAxis(const GamepadState& pad, GamepadAxis axis) noexcept
{
    switch (axis)
    {
    case GamepadAxis::LeftX:
        return pad.stick(GamepadStick::Left).x;
    case GamepadAxis::LeftY:
        return pad.stick(GamepadStick::Left).y;
    case GamepadAxis::RightX:
        return pad.stick(GamepadStick::Right).x;
    case GamepadAxis::RightY:
        return pad.stick(GamepadStick::Right).y;
    case GamepadAxis::LeftTrigger:
        return pad.trigger(GamepadTrigger::Left);
    case GamepadAxis::RightTrigger:
        return pad.trigger(GamepadTrigger::Right);
    case GamepadAxis::Count:
        break;
    }
    return 0.0f;
}

struct Contribution
{
    Vec2 value{0.0f};
    bool tapped = false; // a digital input went down this frame (catches press + release within a frame)
};

Vec2 strongest(Vec2 a, Vec2 b) noexcept
{
    return glm::dot(b, b) > glm::dot(a, a) ? b : a;
}

Contribution evaluate(const InputBinding& binding, const InputState* keyboardMouse,
                      std::span<const GamepadState* const> gamepads) noexcept
{
    Contribution result;
    switch (binding.source)
    {
    case BindingSource::Key:
        if (keyboardMouse != nullptr)
        {
            const auto key = static_cast<Key>(binding.code);
            result.value.x = keyboardMouse->isDown(key) ? binding.scale : 0.0f;
            result.tapped = keyboardMouse->wasPressed(key);
        }
        break;
    case BindingSource::MouseButton:
        if (keyboardMouse != nullptr)
        {
            const auto button = static_cast<MouseButton>(binding.code);
            result.value.x = keyboardMouse->isDown(button) ? binding.scale : 0.0f;
            result.tapped = keyboardMouse->wasPressed(button);
        }
        break;
    case BindingSource::KeyVector:
        if (keyboardMouse != nullptr)
        {
            const auto held = [&](usize i) {
                return keyboardMouse->isDown(binding.keys[i]) ? 1.0f : 0.0f;
            };
            Vec2 v(held(3) - held(2), held(0) - held(1));
            if (glm::dot(v, v) > 1.0f)
            {
                v = glm::normalize(v);
            }
            result.value = v;
            for (const Key key : binding.keys)
            {
                result.tapped = result.tapped || keyboardMouse->wasPressed(key);
            }
        }
        break;
    case BindingSource::GamepadButton:
        for (const GamepadState* pad : gamepads)
        {
            const auto button = static_cast<GamepadButton>(binding.code);
            if (pad->isDown(button))
            {
                result.value.x = binding.scale;
            }
            result.tapped = result.tapped || pad->wasPressed(button);
        }
        break;
    case BindingSource::GamepadAxis:
        for (const GamepadState* pad : gamepads)
        {
            const f32 v = deadZonedAxis(*pad, static_cast<GamepadAxis>(binding.code)) * binding.scale;
            result.value = strongest(result.value, Vec2(v, 0.0f));
        }
        break;
    case BindingSource::GamepadStick:
        for (const GamepadState* pad : gamepads)
        {
            result.value = strongest(result.value, pad->stick(static_cast<GamepadStick>(binding.code)));
        }
        break;
    }
    return result;
}

bool anyHeld(const InputState* keyboardMouse, std::span<const GamepadState* const> gamepads) noexcept
{
    if (keyboardMouse != nullptr)
    {
        for (usize i = 1; i < kKeyCount; ++i)
        {
            if (keyboardMouse->isDown(static_cast<Key>(i)))
            {
                return true;
            }
        }
        for (usize i = 0; i < kMouseButtonCount; ++i)
        {
            if (keyboardMouse->isDown(static_cast<MouseButton>(i)))
            {
                return true;
            }
        }
    }
    for (const GamepadState* pad : gamepads)
    {
        for (usize i = 0; i < kGamepadButtonCount; ++i)
        {
            if (pad->isDown(static_cast<GamepadButton>(i)))
            {
                return true;
            }
        }
        for (usize i = 0; i < kGamepadAxisCount; ++i)
        {
            if (std::abs(deadZonedAxis(*pad, static_cast<GamepadAxis>(i))) >= InputActions::kPressThreshold)
            {
                return true;
            }
        }
    }
    return false;
}

std::string scaleSuffix(f32 scale)
{
    return scale == 1.0f ? std::string() : std::format("*{}", scale);
}

struct ButtonGlyph
{
    std::string_view xbox;
    std::string_view xboxLabel;
    std::string_view ps;
    std::string_view psLabel;
    std::string_view nintendo;
    std::string_view nintendoLabel;
};

// Indexed by GamepadButton. Generic glyphs use the snake_case button name.
constexpr std::array<ButtonGlyph, kGamepadButtonCount> kButtonGlyphs = {{
    {"a", "A", "cross", "Cross", "b", "B"},
    {"b", "B", "circle", "Circle", "a", "A"},
    {"x", "X", "square", "Square", "y", "Y"},
    {"y", "Y", "triangle", "Triangle", "x", "X"},
    {"view", "View", "share", "Share", "minus", "-"},
    {"guide", "Xbox", "ps", "PS", "home", "Home"},
    {"menu", "Menu", "options", "Options", "plus", "+"},
    {"ls", "LS", "l3", "L3", "ls", "LS"},
    {"rs", "RS", "r3", "R3", "rs", "RS"},
    {"lb", "LB", "l1", "L1", "l", "L"},
    {"rb", "RB", "r1", "R1", "r", "R"},
    {"dpad_up", "D-Pad Up", "dpad_up", "D-Pad Up", "dpad_up", "D-Pad Up"},
    {"dpad_down", "D-Pad Down", "dpad_down", "D-Pad Down", "dpad_down", "D-Pad Down"},
    {"dpad_left", "D-Pad Left", "dpad_left", "D-Pad Left", "dpad_left", "D-Pad Left"},
    {"dpad_right", "D-Pad Right", "dpad_right", "D-Pad Right", "dpad_right", "D-Pad Right"},
    {"share", "Share", "mic", "Mic", "capture", "Capture"},
    {"p1", "P1", "rb_paddle", "RB Paddle", "p1", "P1"},
    {"p2", "P2", "lb_paddle", "LB Paddle", "p2", "P2"},
    {"p3", "P3", "rb_paddle2", "RB Paddle 2", "p3", "P3"},
    {"p4", "P4", "lb_paddle2", "LB Paddle 2", "p4", "P4"},
    {"touchpad", "Touchpad", "touchpad", "Touchpad", "touchpad", "Touchpad"},
}};

std::string_view familyPrefix(GamepadFamily family) noexcept
{
    switch (family)
    {
    case GamepadFamily::Xbox:
        return "xbox_";
    case GamepadFamily::PlayStation:
        return "ps_";
    case GamepadFamily::Nintendo:
        return "switch_";
    case GamepadFamily::Generic:
        return "gamepad_";
    }
    return "gamepad_";
}

std::string_view mouseLabel(MouseButton button) noexcept
{
    switch (button)
    {
    case MouseButton::Left:
        return "LMB";
    case MouseButton::Right:
        return "RMB";
    case MouseButton::Middle:
        return "MMB";
    case MouseButton::Button4:
        return "Mouse 4";
    case MouseButton::Button5:
        return "Mouse 5";
    case MouseButton::Count:
        break;
    }
    return "Mouse";
}

} // namespace

// --- InputBinding ---------------------------------------------------------------------------------------

InputBinding InputBinding::key(Key key, f32 scale) noexcept
{
    InputBinding binding;
    binding.source = BindingSource::Key;
    binding.code = static_cast<u16>(key);
    binding.scale = scale;
    return binding;
}

InputBinding InputBinding::mouseButton(MouseButton button) noexcept
{
    InputBinding binding;
    binding.source = BindingSource::MouseButton;
    binding.code = static_cast<u16>(button);
    return binding;
}

InputBinding InputBinding::gamepadButton(GamepadButton button, f32 scale) noexcept
{
    InputBinding binding;
    binding.source = BindingSource::GamepadButton;
    binding.code = static_cast<u16>(button);
    binding.scale = scale;
    return binding;
}

InputBinding InputBinding::gamepadAxis(GamepadAxis axis, f32 scale) noexcept
{
    InputBinding binding;
    binding.source = BindingSource::GamepadAxis;
    binding.code = static_cast<u16>(axis);
    binding.scale = scale;
    return binding;
}

InputBinding InputBinding::gamepadStick(GamepadStick stick) noexcept
{
    InputBinding binding;
    binding.source = BindingSource::GamepadStick;
    binding.code = static_cast<u16>(stick);
    return binding;
}

InputBinding InputBinding::keyVector(Key up, Key down, Key left, Key right) noexcept
{
    InputBinding binding;
    binding.source = BindingSource::KeyVector;
    binding.keys = {up, down, left, right};
    return binding;
}

std::string bindingToString(const InputBinding& binding)
{
    switch (binding.source)
    {
    case BindingSource::Key:
        return std::format("Key.{}{}", keyName(static_cast<Key>(binding.code)), scaleSuffix(binding.scale));
    case BindingSource::MouseButton:
        return std::format("Mouse.{}{}", mouseButtonName(static_cast<MouseButton>(binding.code)),
                           scaleSuffix(binding.scale));
    case BindingSource::GamepadButton:
        return std::format("Gamepad.{}{}", gamepadButtonName(static_cast<GamepadButton>(binding.code)),
                           scaleSuffix(binding.scale));
    case BindingSource::GamepadAxis:
        return std::format("Axis.{}{}", gamepadAxisName(static_cast<GamepadAxis>(binding.code)),
                           scaleSuffix(binding.scale));
    case BindingSource::GamepadStick:
        return binding.code == static_cast<u16>(GamepadStick::Left) ? "Stick.Left" : "Stick.Right";
    case BindingSource::KeyVector:
        return std::format("Keys.{}.{}.{}.{}", keyName(binding.keys[0]), keyName(binding.keys[1]),
                           keyName(binding.keys[2]), keyName(binding.keys[3]));
    }
    return {};
}

std::optional<InputBinding> bindingFromString(std::string_view text)
{
    text = trim(text);
    f32 scale = 1.0f;
    if (const usize star = text.find('*'); star != std::string_view::npos)
    {
        const std::string number(trim(text.substr(star + 1)));
        char* end = nullptr;
        scale = std::strtof(number.c_str(), &end);
        if (number.empty() || end != number.c_str() + number.size() || scale == 0.0f)
        {
            return std::nullopt;
        }
        text = trim(text.substr(0, star));
    }
    const std::vector<std::string_view> parts = split(text, '.');
    if (parts.size() < 2)
    {
        return std::nullopt;
    }
    const std::string_view kind = parts[0];
    if (kind == "Keys")
    {
        if (parts.size() != 5)
        {
            return std::nullopt;
        }
        std::array<Key, 4> keys{};
        for (usize i = 0; i < 4; ++i)
        {
            const auto key = keyFromName(parts[i + 1]);
            if (!key)
            {
                return std::nullopt;
            }
            keys[i] = *key;
        }
        return InputBinding::keyVector(keys[0], keys[1], keys[2], keys[3]);
    }
    if (parts.size() != 2)
    {
        return std::nullopt;
    }
    const std::string_view name = parts[1];
    if (kind == "Key")
    {
        if (const auto key = keyFromName(name); key && *key != Key::Unknown)
        {
            return InputBinding::key(*key, scale);
        }
    }
    else if (kind == "Mouse")
    {
        for (usize i = 0; i < kMouseButtonCount; ++i)
        {
            if (mouseButtonName(static_cast<MouseButton>(i)) == name)
            {
                InputBinding binding = InputBinding::mouseButton(static_cast<MouseButton>(i));
                binding.scale = scale;
                return binding;
            }
        }
    }
    else if (kind == "Gamepad")
    {
        if (const auto button = gamepadButtonFromName(name))
        {
            return InputBinding::gamepadButton(*button, scale);
        }
    }
    else if (kind == "Axis")
    {
        if (const auto axis = gamepadAxisFromName(name))
        {
            return InputBinding::gamepadAxis(*axis, scale);
        }
    }
    else if (kind == "Stick")
    {
        if (name == "Left")
        {
            return InputBinding::gamepadStick(GamepadStick::Left);
        }
        if (name == "Right")
        {
            return InputBinding::gamepadStick(GamepadStick::Right);
        }
    }
    return std::nullopt;
}

std::string glyphId(const InputBinding& binding, GamepadFamily family)
{
    switch (binding.source)
    {
    case BindingSource::Key:
        return "key_" + snakeCase(keyName(static_cast<Key>(binding.code)));
    case BindingSource::MouseButton:
        return "mouse_" + snakeCase(mouseButtonName(static_cast<MouseButton>(binding.code)));
    case BindingSource::KeyVector:
        return std::format("keys_{}_{}_{}_{}", snakeCase(keyName(binding.keys[0])),
                           snakeCase(keyName(binding.keys[1])), snakeCase(keyName(binding.keys[2])),
                           snakeCase(keyName(binding.keys[3])));
    case BindingSource::GamepadButton:
    {
        const auto button = static_cast<GamepadButton>(binding.code);
        const ButtonGlyph& glyph = kButtonGlyphs[static_cast<usize>(button)];
        switch (family)
        {
        case GamepadFamily::Xbox:
            return std::string(familyPrefix(family)) + std::string(glyph.xbox);
        case GamepadFamily::PlayStation:
            return std::string(familyPrefix(family)) + std::string(glyph.ps);
        case GamepadFamily::Nintendo:
            return std::string(familyPrefix(family)) + std::string(glyph.nintendo);
        case GamepadFamily::Generic:
            break;
        }
        return std::string(familyPrefix(family)) + snakeCase(gamepadButtonName(button));
    }
    case BindingSource::GamepadAxis:
    {
        const auto axis = static_cast<GamepadAxis>(binding.code);
        if (isTriggerAxis(axis))
        {
            const bool left = axis == GamepadAxis::LeftTrigger;
            switch (family)
            {
            case GamepadFamily::Xbox:
                return left ? "xbox_lt" : "xbox_rt";
            case GamepadFamily::PlayStation:
                return left ? "ps_l2" : "ps_r2";
            case GamepadFamily::Nintendo:
                return left ? "switch_zl" : "switch_zr";
            case GamepadFamily::Generic:
                break;
            }
            return left ? "gamepad_left_trigger" : "gamepad_right_trigger";
        }
        const bool left = axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY;
        return std::string(familyPrefix(family)) + (left ? "left_stick" : "right_stick");
    }
    case BindingSource::GamepadStick:
        return std::string(familyPrefix(family)) +
               (binding.code == static_cast<u16>(GamepadStick::Left) ? "left_stick" : "right_stick");
    }
    return {};
}

std::string glyphLabel(const InputBinding& binding, GamepadFamily family)
{
    switch (binding.source)
    {
    case BindingSource::Key:
        return std::string(keyName(static_cast<Key>(binding.code)));
    case BindingSource::MouseButton:
        return std::string(mouseLabel(static_cast<MouseButton>(binding.code)));
    case BindingSource::KeyVector:
    {
        std::string label;
        bool singleCharacters = true;
        for (const Key key : binding.keys)
        {
            singleCharacters = singleCharacters && keyName(key).size() == 1;
            label += keyName(key);
        }
        if (!singleCharacters)
        {
            label = std::format("{}/{}/{}/{}", keyName(binding.keys[0]), keyName(binding.keys[1]),
                                keyName(binding.keys[2]), keyName(binding.keys[3]));
        }
        return label;
    }
    case BindingSource::GamepadButton:
    {
        const auto button = static_cast<GamepadButton>(binding.code);
        const ButtonGlyph& glyph = kButtonGlyphs[static_cast<usize>(button)];
        switch (family)
        {
        case GamepadFamily::Xbox:
            return std::string(glyph.xboxLabel);
        case GamepadFamily::PlayStation:
            return std::string(glyph.psLabel);
        case GamepadFamily::Nintendo:
            return std::string(glyph.nintendoLabel);
        case GamepadFamily::Generic:
            break;
        }
        return std::string(gamepadButtonName(button));
    }
    case BindingSource::GamepadAxis:
    {
        const auto axis = static_cast<GamepadAxis>(binding.code);
        if (isTriggerAxis(axis))
        {
            const bool left = axis == GamepadAxis::LeftTrigger;
            switch (family)
            {
            case GamepadFamily::Xbox:
                return left ? "LT" : "RT";
            case GamepadFamily::PlayStation:
                return left ? "L2" : "R2";
            case GamepadFamily::Nintendo:
                return left ? "ZL" : "ZR";
            case GamepadFamily::Generic:
                break;
            }
            return left ? "Left Trigger" : "Right Trigger";
        }
        const bool left = axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY;
        return left ? "Left Stick" : "Right Stick";
    }
    case BindingSource::GamepadStick:
        return binding.code == static_cast<u16>(GamepadStick::Left) ? "Left Stick" : "Right Stick";
    }
    return {};
}

// --- InputActions ---------------------------------------------------------------------------------------

ActionId InputActions::addAction(std::string_view name, ActionType type)
{
    ILL_ASSERT(!findAction(name).has_value(), "action '{}' already exists", name);
    ILL_ASSERT(!name.empty() && name.find_first_of("=,\n#") == std::string_view::npos && trim(name) == name,
               "invalid action name '{}'", name);
    m_actions.push_back(Action{std::string(name), type, {}, Vec2(0.0f), false, false, false});
    return static_cast<ActionId>(m_actions.size() - 1);
}

std::optional<ActionId> InputActions::findAction(std::string_view name) const noexcept
{
    for (usize i = 0; i < m_actions.size(); ++i)
    {
        if (m_actions[i].name == name)
        {
            return static_cast<ActionId>(i);
        }
    }
    return std::nullopt;
}

const InputActions::Action& InputActions::at(ActionId action) const noexcept
{
    ILL_ASSERT(action < m_actions.size(), "invalid action id {}", action);
    return m_actions[action];
}

const std::string& InputActions::actionName(ActionId action) const noexcept
{
    return at(action).name;
}

ActionType InputActions::actionType(ActionId action) const noexcept
{
    return at(action).type;
}

void InputActions::addBinding(ActionId action, const InputBinding& binding)
{
    ILL_ASSERT(action < m_actions.size(), "invalid action id {}", action);
    std::vector<InputBinding>& bindings = m_actions[action].bindings;
    if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end())
    {
        bindings.push_back(binding);
    }
}

void InputActions::setBindings(ActionId action, std::span<const InputBinding> bindings)
{
    ILL_ASSERT(action < m_actions.size(), "invalid action id {}", action);
    m_actions[action].bindings.clear();
    for (const InputBinding& binding : bindings)
    {
        addBinding(action, binding);
    }
}

bool InputActions::removeBinding(ActionId action, const InputBinding& binding)
{
    ILL_ASSERT(action < m_actions.size(), "invalid action id {}", action);
    return std::erase(m_actions[action].bindings, binding) > 0;
}

std::span<const InputBinding> InputActions::bindings(ActionId action) const noexcept
{
    return at(action).bindings;
}

std::vector<ActionId> InputActions::findConflicts(const InputBinding& binding, ActionId except) const
{
    std::vector<ActionId> conflicts;
    for (usize i = 0; i < m_actions.size(); ++i)
    {
        if (i == except)
        {
            continue;
        }
        const std::vector<InputBinding>& bindings = m_actions[i].bindings;
        const bool uses = std::any_of(bindings.begin(), bindings.end(), [&](const InputBinding& b) {
            return b.source == binding.source && b.code == binding.code && b.keys == binding.keys;
        });
        if (uses)
        {
            conflicts.push_back(static_cast<ActionId>(i));
        }
    }
    return conflicts;
}

void InputActions::update(const InputState* keyboardMouse, std::span<const GamepadState* const> gamepads)
{
    // Which device was used last (for glyphs).
    bool keyboardUsed = false;
    if (keyboardMouse != nullptr)
    {
        for (usize i = 1; i < kKeyCount && !keyboardUsed; ++i)
        {
            keyboardUsed = keyboardMouse->wasPressed(static_cast<Key>(i));
        }
        for (usize i = 0; i < kMouseButtonCount && !keyboardUsed; ++i)
        {
            keyboardUsed = keyboardMouse->wasPressed(static_cast<MouseButton>(i));
        }
    }
    for (const GamepadState* pad : gamepads)
    {
        bool used = false;
        for (usize i = 0; i < kGamepadButtonCount && !used; ++i)
        {
            used = pad->wasPressed(static_cast<GamepadButton>(i));
        }
        // Edges only: a stick resting deflected must not keep claiming "last used device".
        used = used || pad->wasAnalogActivated();
        if (used)
        {
            m_lastDevice = InputDevice::Gamepad;
            m_lastGamepadType = pad->type();
        }
    }
    if (keyboardUsed)
    {
        m_lastDevice = InputDevice::KeyboardMouse;
    }

    for (Action& action : m_actions)
    {
        Vec2 value(0.0f);
        bool tapped = false;
        for (const InputBinding& binding : action.bindings)
        {
            const Contribution contribution = evaluate(binding, keyboardMouse, gamepads);
            value = strongest(value, contribution.value);
            tapped = tapped || contribution.tapped;
        }

        bool down = false;
        switch (action.type)
        {
        case ActionType::Button:
            down = std::max(std::abs(value.x), glm::length(value)) >= kPressThreshold;
            value = Vec2(down ? 1.0f : 0.0f, 0.0f);
            break;
        case ActionType::Axis:
            value = Vec2(std::clamp(value.x, -1.0f, 1.0f), 0.0f);
            down = value.x != 0.0f;
            break;
        case ActionType::Vector2:
            if (glm::dot(value, value) > 1.0f)
            {
                value = glm::normalize(value);
            }
            down = value != Vec2(0.0f);
            break;
        }

        const bool wasDown = action.down;
        action.pressed = !wasDown && (down || tapped);
        action.released = (wasDown && !down) || (!wasDown && tapped && !down);
        action.down = down;
        action.value = value;
    }
}

bool InputActions::isDown(ActionId action) const noexcept
{
    return at(action).down;
}

bool InputActions::wasPressed(ActionId action) const noexcept
{
    return at(action).pressed;
}

bool InputActions::wasReleased(ActionId action) const noexcept
{
    return at(action).released;
}

f32 InputActions::value(ActionId action) const noexcept
{
    return at(action).value.x;
}

Vec2 InputActions::vector(ActionId action) const noexcept
{
    return at(action).value;
}

std::string InputActions::serialize() const
{
    std::string text = "# illusion input bindings v1\n";
    for (const Action& action : m_actions)
    {
        text += action.name;
        text += " =";
        for (usize i = 0; i < action.bindings.size(); ++i)
        {
            text += i == 0 ? " " : ", ";
            text += bindingToString(action.bindings[i]);
        }
        text += '\n';
    }
    return text;
}

bool InputActions::deserialize(std::string_view text, std::vector<std::string>* errors)
{
    bool ok = true;
    const auto report = [&](std::string message) {
        ok = false;
        if (errors != nullptr)
        {
            errors->push_back(std::move(message));
        }
    };

    usize lineNumber = 0;
    for (std::string_view line : split(text, '\n'))
    {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        const usize equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            report(std::format("line {}: expected 'Action = bindings'", lineNumber));
            continue;
        }
        const std::string_view name = trim(line.substr(0, equals));
        const auto action = findAction(name);
        if (!action)
        {
            report(std::format("line {}: unknown action '{}'", lineNumber, name));
            continue;
        }
        std::vector<InputBinding> bindings;
        const std::string_view list = trim(line.substr(equals + 1));
        if (!list.empty())
        {
            for (const std::string_view item : split(list, ','))
            {
                if (const auto binding = bindingFromString(item))
                {
                    bindings.push_back(*binding);
                }
                else
                {
                    report(std::format("line {}: invalid binding '{}'", lineNumber, trim(item)));
                }
            }
        }
        setBindings(*action, bindings);
    }
    return ok;
}

// --- BindingCapture -------------------------------------------------------------------------------------

BindingCapture::BindingCapture(Devices devices, ActionType type) noexcept
    : m_devices(devices)
    , m_type(type)
{
}

std::optional<InputBinding> BindingCapture::poll(const InputState* keyboardMouse,
                                                 std::span<const GamepadState* const> gamepads)
{
    const bool keyboard = m_devices != Devices::Gamepad;
    const bool gamepad = m_devices != Devices::KeyboardMouse;
    const InputState* km = keyboard ? keyboardMouse : nullptr;
    const std::span<const GamepadState* const> pads =
        gamepad ? gamepads : std::span<const GamepadState* const>{};

    if (!m_armed)
    {
        // Arm once nothing is held. A tap that started and ended this frame is not "held" and is still
        // captured below.
        m_armed = !anyHeld(km, pads);
        if (!m_armed)
        {
            return std::nullopt;
        }
    }

    if (km != nullptr)
    {
        for (usize i = 1; i < kKeyCount; ++i)
        {
            if (km->wasPressed(static_cast<Key>(i)))
            {
                return InputBinding::key(static_cast<Key>(i));
            }
        }
        for (usize i = 0; i < kMouseButtonCount; ++i)
        {
            if (km->wasPressed(static_cast<MouseButton>(i)))
            {
                return InputBinding::mouseButton(static_cast<MouseButton>(i));
            }
        }
    }

    for (const GamepadState* pad : pads)
    {
        for (usize i = 0; i < kGamepadButtonCount; ++i)
        {
            if (pad->wasPressed(static_cast<GamepadButton>(i)))
            {
                return InputBinding::gamepadButton(static_cast<GamepadButton>(i));
            }
        }
        if (m_type == ActionType::Vector2)
        {
            for (const GamepadStick stick : {GamepadStick::Left, GamepadStick::Right})
            {
                if (glm::length(pad->stick(stick)) >= InputActions::kPressThreshold)
                {
                    return InputBinding::gamepadStick(stick);
                }
            }
            continue;
        }
        for (usize i = 0; i < kGamepadAxisCount; ++i)
        {
            const auto axis = static_cast<GamepadAxis>(i);
            const f32 v = deadZonedAxis(*pad, axis);
            if (std::abs(v) >= InputActions::kPressThreshold)
            {
                return InputBinding::gamepadAxis(axis, v < 0.0f ? -1.0f : 1.0f);
            }
        }
    }
    return std::nullopt;
}

} // namespace illusion
