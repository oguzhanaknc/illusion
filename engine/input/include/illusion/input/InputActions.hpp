#pragma once

/// @file
/// Action mapping: game code asks about *actions* ("Jump", "Move") instead of devices, and players can
/// rebind them. Also provides binding persistence and per-device glyphs.
///
/// ```cpp
/// InputActions actions;
/// const ActionId jump = actions.addAction("Jump", ActionType::Button);
/// const ActionId move = actions.addAction("Move", ActionType::Vector2);
/// actions.addBinding(jump, InputBinding::key(Key::Space));
/// actions.addBinding(jump, InputBinding::gamepadButton(GamepadButton::South));
/// actions.addBinding(move, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
/// actions.addBinding(move, InputBinding::gamepadStick(GamepadStick::Left));
///
/// // every frame, after Platform::pollEvents() and GamepadSystem::update():
/// actions.update(&window->input(), gamepadStates);
/// if (actions.wasPressed(jump)) { ... }
/// const Vec2 direction = actions.vector(move);
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/text/StringTable.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

namespace text
{
class Localization;
} // namespace text

/// Index of an action in an InputActions set.
using ActionId = u32;

/// What kind of value an action produces.
enum class ActionType : u8
{
    Button,  ///< On/off with pressed and released edges (Jump, Fire).
    Axis,    ///< One value in [-1, 1] (Throttle, Zoom).
    Vector2, ///< Two values, length up to 1 (Move, Look).
};

/// Device input that drives an action.
enum class BindingSource : u8
{
    Key,           ///< A keyboard key.
    MouseButton,   ///< A mouse button.
    GamepadButton, ///< A gamepad button.
    GamepadAxis,   ///< A gamepad axis (sticks or triggers), scaled by InputBinding::scale.
    GamepadStick,  ///< A whole gamepad stick as a 2D vector.
    KeyVector,     ///< Four keys forming a 2D vector (up, down, left, right), e.g. WASD.
};

/// One binding of an action. Create with the static helpers.
struct InputBinding
{
    BindingSource source = BindingSource::Key; ///< Device input kind.
    u16 code = 0;              ///< Key, MouseButton, GamepadButton, GamepadAxis or GamepadStick value.
    std::array<Key, 4> keys{}; ///< KeyVector keys: up, down, left, right.
    /// Multiplier for the binding's value. A Key bound to an Axis action with scale -1 pushes it
    /// negative; a GamepadAxis bound to a Button action with scale -1 triggers on the negative side.
    f32 scale = 1.0f;

    /// Binds a key.
    [[nodiscard]] static InputBinding key(Key key, f32 scale = 1.0f) noexcept;
    /// Binds a mouse button.
    [[nodiscard]] static InputBinding mouseButton(MouseButton button) noexcept;
    /// Binds a gamepad button.
    [[nodiscard]] static InputBinding gamepadButton(GamepadButton button, f32 scale = 1.0f) noexcept;
    /// Binds a gamepad axis.
    [[nodiscard]] static InputBinding gamepadAxis(GamepadAxis axis, f32 scale = 1.0f) noexcept;
    /// Binds a gamepad stick (for Vector2 actions).
    [[nodiscard]] static InputBinding gamepadStick(GamepadStick stick) noexcept;
    /// Binds four keys as a vector (for Vector2 actions).
    [[nodiscard]] static InputBinding keyVector(Key up, Key down, Key left, Key right) noexcept;

    /// Returns true if the binding reads a gamepad.
    [[nodiscard]] bool isGamepad() const noexcept
    {
        return source == BindingSource::GamepadButton || source == BindingSource::GamepadAxis ||
               source == BindingSource::GamepadStick;
    }

    /// Bindings are equal when they read the same input with the same scale.
    friend bool operator==(const InputBinding&, const InputBinding&) noexcept = default;
};

/// Text form of a binding, e.g. "Key.Space", "Gamepad.South", "Axis.LeftY*-1", "Stick.Left",
/// "Keys.W.S.A.D", "Mouse.Left". Used by InputActions::serialize().
[[nodiscard]] std::string bindingToString(const InputBinding& binding);

/// Parses a string produced by bindingToString().
[[nodiscard]] std::optional<InputBinding> bindingFromString(std::string_view text);

/// Identifier of the icon to show for @p binding on a @p family controller, e.g. "key_space",
/// "mouse_left", "xbox_a", "ps_cross", "switch_b", "gamepad_left_stick". Map these to your UI art.
[[nodiscard]] std::string glyphId(const InputBinding& binding, GamepadFamily family);

/// Short English label for @p binding, e.g. "Space", "A", "Cross", "LT", "ZR", "WASD". Keys use their stable
/// names ("LeftControl"); see glyphLabel(const InputBinding&, GamepadFamily, const text::Localization&) for
/// labels meant for players.
[[nodiscard]] std::string glyphLabel(const InputBinding& binding, GamepadFamily family);

/// String table key of the label of @p key in the "input" table ("input.key.*", e.g. "Left Ctrl").
[[nodiscard]] text::TextKey keyLabelKey(Key key) noexcept;

/// String table key of the label of @p binding on a @p family controller: "input.glyph." followed by the
/// glyphId() for mouse and gamepad inputs, and keyLabelKey() for keys. For a KeyVector binding it is
/// "input.glyph.key_vector", or "input.glyph.key_vector_compact" when the English labels of the four keys
/// are single characters (as in "WASD"); their patterns take the four key labels as the arguments up, down,
/// left and right.
[[nodiscard]] text::TextKey glyphLabelKey(const InputBinding& binding, GamepadFamily family) noexcept;

/// The label of @p binding in the active language of @p localization, e.g. "Sol Ctrl" in Turkish. The input
/// table (`illusion::input::strings::kTable` from illusion/input/InputStrings.hpp) must be registered
/// with Localization::addTable().
[[nodiscard]] std::string glyphLabel(const InputBinding& binding, GamepadFamily family,
                                     const text::Localization& localization);

/// Kind of device used most recently, for choosing glyphs.
enum class InputDevice : u8
{
    KeyboardMouse, ///< Keyboard or mouse.
    Gamepad,       ///< A gamepad (see InputActions::lastGamepadType()).
};

/// A set of named actions with their bindings and per-frame values. Not thread-safe.
class InputActions
{
public:
    /// Analog values beyond this count as "down" for Button actions.
    static constexpr f32 kPressThreshold = 0.5f;

    /// Adds an action. Names must be unique; asserts otherwise.
    ActionId addAction(std::string_view name, ActionType type);

    /// Finds an action by name.
    [[nodiscard]] std::optional<ActionId> findAction(std::string_view name) const noexcept;

    /// Number of actions.
    [[nodiscard]] u32 actionCount() const noexcept { return static_cast<u32>(m_actions.size()); }

    /// Name of @p action.
    [[nodiscard]] const std::string& actionName(ActionId action) const noexcept;

    /// Type of @p action.
    [[nodiscard]] ActionType actionType(ActionId action) const noexcept;

    /// Adds a binding to @p action (duplicates are ignored).
    void addBinding(ActionId action, const InputBinding& binding);

    /// Replaces all bindings of @p action.
    void setBindings(ActionId action, std::span<const InputBinding> bindings);

    /// Removes one binding. Returns false if it was not bound.
    bool removeBinding(ActionId action, const InputBinding& binding);

    /// Bindings of @p action.
    [[nodiscard]] std::span<const InputBinding> bindings(ActionId action) const noexcept;

    /// Actions other than @p except that use @p binding; use to warn about conflicts when rebinding.
    [[nodiscard]] std::vector<ActionId> findConflicts(const InputBinding& binding, ActionId except) const;

    /// Evaluates every action for this frame from keyboard/mouse state (may be null) and gamepad states
    /// (any number; for split-screen give each player their own InputActions and gamepad).
    void update(const InputState* keyboardMouse, std::span<const GamepadState* const> gamepads);

    /// Returns true while a Button action is held (Axis/Vector2: while non-zero).
    [[nodiscard]] bool isDown(ActionId action) const noexcept;
    /// Returns true in the frame a Button action went down.
    [[nodiscard]] bool wasPressed(ActionId action) const noexcept;
    /// Returns true in the frame a Button action went up.
    [[nodiscard]] bool wasReleased(ActionId action) const noexcept;
    /// Value of an Axis action in [-1, 1] (Button: 0 or 1; Vector2: x).
    [[nodiscard]] f32 value(ActionId action) const noexcept;
    /// Value of a Vector2 action, length at most 1 (Axis: (value, 0)).
    [[nodiscard]] Vec2 vector(ActionId action) const noexcept;

    /// Device kind used most recently (a key, button or axis beyond the threshold).
    [[nodiscard]] InputDevice lastDevice() const noexcept { return m_lastDevice; }
    /// Model of the gamepad used most recently.
    [[nodiscard]] GamepadType lastGamepadType() const noexcept { return m_lastGamepadType; }

    /// Bindings as text, one action per line: `Jump = Key.Space, Gamepad.South`.
    [[nodiscard]] std::string serialize() const;

    /// Applies bindings from text produced by serialize(). Actions not mentioned keep their bindings;
    /// unknown actions and malformed bindings are skipped and reported in @p errors (if not null).
    /// Returns false if anything was skipped.
    bool deserialize(std::string_view text, std::vector<std::string>* errors = nullptr);

private:
    struct Action
    {
        std::string name;
        ActionType type;
        std::vector<InputBinding> bindings;
        Vec2 value{0.0f};
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    const Action& at(ActionId action) const noexcept;

    std::vector<Action> m_actions;
    InputDevice m_lastDevice = InputDevice::KeyboardMouse;
    GamepadType m_lastGamepadType = GamepadType::Generic;
};

/// Listens for the next input a player presses, to create a binding in a rebinding menu.
///
/// ```cpp
/// BindingCapture capture(BindingCapture::Devices::Gamepad, ActionType::Button);
/// // each frame while the "press a button" prompt is shown:
/// if (auto binding = capture.poll(&input, gamepads)) { actions.setBindings(jump, {&*binding, 1}); }
/// ```
class BindingCapture
{
public:
    /// Which devices may produce the binding.
    enum class Devices : u8
    {
        KeyboardMouse, ///< Keys and mouse buttons.
        Gamepad,       ///< Gamepad buttons and axes.
        Any,           ///< Either.
    };

    /// Starts capturing a binding suitable for an action of type @p type. For Vector2 actions a moved
    /// stick produces a stick binding.
    BindingCapture(Devices devices, ActionType type) noexcept;

    /// Returns the binding for the first new input this frame, or std::nullopt. Inputs that were
    /// already held when the capture started are ignored until released.
    [[nodiscard]] std::optional<InputBinding> poll(const InputState* keyboardMouse,
                                                   std::span<const GamepadState* const> gamepads);

private:
    Devices m_devices;
    ActionType m_type;
    bool m_armed = false; // becomes true once nothing is held, so the key that opened the menu is ignored
};

} // namespace illusion
