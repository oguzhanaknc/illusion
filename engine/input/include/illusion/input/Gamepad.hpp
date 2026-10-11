#pragma once

/// @file
/// Gamepads: button/axis/touchpad/motion state, device features (rumble, LED, DualSense adaptive
/// triggers) and connection events.
///
/// Conventions: stick axes are in [-1, 1] with +x right and **+y up**; triggers are in [0, 1]. Buttons
/// are named by position (South is A on Xbox, Cross on PlayStation, B on Nintendo); use input glyphs to
/// show the right symbol (see InputActions.hpp).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <bitset>
#include <optional>
#include <string>
#include <string_view>

namespace illusion
{

/// Process-unique gamepad identifier, stable while the gamepad stays connected and never reused.
enum class GamepadId : u32
{
    Invalid = 0 ///< No gamepad.
};

/// Controller model, as far as it can be detected.
enum class GamepadType : u8
{
    Generic,           ///< Unknown or standard-layout controller.
    Xbox360,           ///< Xbox 360 controller.
    XboxOne,           ///< Xbox One / Series controller.
    PS3,               ///< DualShock 3.
    PS4,               ///< DualShock 4.
    PS5,               ///< DualSense / DualSense Edge.
    SwitchPro,         ///< Nintendo Switch Pro Controller.
    SwitchJoyConLeft,  ///< Single left Joy-Con.
    SwitchJoyConRight, ///< Single right Joy-Con.
    SwitchJoyConPair,  ///< Pair of Joy-Cons used as one controller.
    GameCube,          ///< GameCube controller.
    Steam,             ///< Steam Controller / Steam Deck.
};

/// Button-symbol family of a controller, used to choose glyphs.
enum class GamepadFamily : u8
{
    Generic,     ///< Position names (South, East, ...).
    Xbox,        ///< A B X Y.
    PlayStation, ///< Cross Circle Square Triangle.
    Nintendo,    ///< B A Y X (Nintendo layout).
};

/// Family of @p type.
[[nodiscard]] GamepadFamily gamepadFamily(GamepadType type) noexcept;

/// Stable name of @p type, e.g. "PS5".
[[nodiscard]] std::string_view gamepadTypeName(GamepadType type) noexcept;

/// Gamepad buttons, named by position.
enum class GamepadButton : u8
{
    South,         ///< Bottom face button (A / Cross / B).
    East,          ///< Right face button (B / Circle / A).
    West,          ///< Left face button (X / Square / Y).
    North,         ///< Top face button (Y / Triangle / X).
    Back,          ///< View / Share / Create / Minus.
    Guide,         ///< Xbox / PS / Home button.
    Start,         ///< Menu / Options / Plus.
    LeftStick,     ///< Left stick click.
    RightStick,    ///< Right stick click.
    LeftShoulder,  ///< LB / L1 / L.
    RightShoulder, ///< RB / R1 / R.
    DPadUp,        ///< D-pad up.
    DPadDown,      ///< D-pad down.
    DPadLeft,      ///< D-pad left.
    DPadRight,     ///< D-pad right.
    Misc1,         ///< Share (Xbox Series), Microphone (DualSense), Capture (Switch).
    RightPaddle1,  ///< Upper right back paddle (Elite, Edge, Steam).
    LeftPaddle1,   ///< Upper left back paddle.
    RightPaddle2,  ///< Lower right back paddle.
    LeftPaddle2,   ///< Lower left back paddle.
    Touchpad,      ///< Touchpad click (PlayStation, Steam).
    Count          ///< Number of buttons; not a button.
};

/// Gamepad axes. Sticks are in [-1, 1] (+y up), triggers in [0, 1].
enum class GamepadAxis : u8
{
    LeftX,        ///< Left stick horizontal.
    LeftY,        ///< Left stick vertical (+1 = up).
    RightX,       ///< Right stick horizontal.
    RightY,       ///< Right stick vertical (+1 = up).
    LeftTrigger,  ///< LT / L2 / ZL.
    RightTrigger, ///< RT / R2 / ZR.
    Count         ///< Number of axes; not an axis.
};

/// One of the two analog sticks.
enum class GamepadStick : u8
{
    Left,  ///< Left stick.
    Right, ///< Right stick.
};

/// One of the two triggers.
enum class GamepadTrigger : u8
{
    Left,  ///< LT / L2 / ZL.
    Right, ///< RT / R2 / ZR.
};

/// Stable name of @p button, e.g. "South", "LeftShoulder".
[[nodiscard]] std::string_view gamepadButtonName(GamepadButton button) noexcept;
/// Parses a name produced by gamepadButtonName().
[[nodiscard]] std::optional<GamepadButton> gamepadButtonFromName(std::string_view name) noexcept;
/// Stable name of @p axis, e.g. "LeftX", "RightTrigger".
[[nodiscard]] std::string_view gamepadAxisName(GamepadAxis axis) noexcept;
/// Parses a name produced by gamepadAxisName().
[[nodiscard]] std::optional<GamepadAxis> gamepadAxisFromName(std::string_view name) noexcept;

/// One finger on a touchpad.
struct TouchpadFinger
{
    bool down = false;         ///< True while the finger touches the pad.
    Vec2 position{0.0f, 0.0f}; ///< Normalized position, (0, 0) top-left to (1, 1) bottom-right.
    f32 pressure = 0.0f;       ///< Pressure in [0, 1] (1 if the device does not report pressure).
};

/// Input state of one gamepad for the current frame. Like InputState, it is plain data fed by the
/// platform backend, input recordings or tests.
class GamepadState
{
public:
    /// Maximum touchpad fingers tracked.
    static constexpr usize kMaxFingers = 2;

    /// Controller model.
    [[nodiscard]] GamepadType type() const noexcept { return m_type; }

    /// Returns true while @p button is held.
    [[nodiscard]] bool isDown(GamepadButton button) const noexcept
    {
        return m_down.test(static_cast<usize>(button));
    }
    /// Returns true if @p button went down during this frame.
    [[nodiscard]] bool wasPressed(GamepadButton button) const noexcept
    {
        return m_pressed.test(static_cast<usize>(button));
    }
    /// Returns true if @p button went up during this frame.
    [[nodiscard]] bool wasReleased(GamepadButton button) const noexcept
    {
        return m_released.test(static_cast<usize>(button));
    }

    /// Raw axis value without dead zone: sticks in [-1, 1] (+y up), triggers in [0, 1].
    [[nodiscard]] f32 axis(GamepadAxis axis) const noexcept { return m_axes[static_cast<usize>(axis)]; }

    /// Stick position with a radial dead zone applied and the remaining range rescaled to [0, 1]
    /// magnitude, so small drift reads as exactly zero and full deflection as length 1.
    [[nodiscard]] Vec2 stick(GamepadStick stick) const noexcept;

    /// Trigger value in [0, 1] with the trigger dead zone applied and rescaled.
    [[nodiscard]] f32 trigger(GamepadTrigger trigger) const noexcept;

    /// Radial stick dead zone in [0, 1) (default 0.15).
    void setStickDeadZone(f32 deadZone) noexcept;
    /// Trigger dead zone in [0, 1) (default 0.05).
    void setTriggerDeadZone(f32 deadZone) noexcept;
    /// Current stick dead zone.
    [[nodiscard]] f32 stickDeadZone() const noexcept { return m_stickDeadZone; }
    /// Current trigger dead zone.
    [[nodiscard]] f32 triggerDeadZone() const noexcept { return m_triggerDeadZone; }

    /// Returns true if a stick or trigger moved past half of its range during this frame (an analog
    /// "press"); used to detect which device the player picked up.
    [[nodiscard]] bool wasAnalogActivated() const noexcept { return m_analogActivated; }

    /// Touchpad finger @p index (< kMaxFingers).
    [[nodiscard]] const TouchpadFinger& finger(usize index) const noexcept { return m_fingers[index]; }

    /// Angular velocity in radians per second (x pitch, y yaw, z roll), when motion sensors are enabled.
    [[nodiscard]] Vec3 gyroscope() const noexcept { return m_gyroscope; }
    /// Acceleration in m/s² including gravity, when motion sensors are enabled.
    [[nodiscard]] Vec3 accelerometer() const noexcept { return m_accelerometer; }

    /// @name Backend interface
    /// @{

    /// Starts a new frame: clears pressed/released edges. Held buttons and axes are kept.
    void beginFrame() noexcept;
    /// Sets the controller model.
    void setType(GamepadType type) noexcept { m_type = type; }
    /// Records @p button going down or up.
    void setButton(GamepadButton button, bool down) noexcept;
    /// Sets a raw axis value (clamped to the axis range).
    void setAxis(GamepadAxis axis, f32 value) noexcept;
    /// Sets touchpad finger @p index; ignored if out of range.
    void setFinger(usize index, const TouchpadFinger& finger) noexcept;
    /// Sets the gyroscope reading.
    void setGyroscope(Vec3 value) noexcept { m_gyroscope = value; }
    /// Sets the accelerometer reading.
    void setAccelerometer(Vec3 value) noexcept { m_accelerometer = value; }
    /// Releases every button (reporting released edges) and centers all axes.
    void releaseAll() noexcept;

    /// @}

private:
    static constexpr usize kButtonCount = static_cast<usize>(GamepadButton::Count);
    static constexpr usize kAxisCount = static_cast<usize>(GamepadAxis::Count);

    GamepadType m_type = GamepadType::Generic;
    std::bitset<kButtonCount> m_down;
    std::bitset<kButtonCount> m_pressed;
    std::bitset<kButtonCount> m_released;
    std::array<f32, kAxisCount> m_axes{};
    std::array<TouchpadFinger, kMaxFingers> m_fingers{};
    Vec3 m_gyroscope{0.0f};
    Vec3 m_accelerometer{0.0f};
    f32 m_stickDeadZone = 0.15f;
    f32 m_triggerDeadZone = 0.05f;
    bool m_analogActivated = false;
};

/// DualSense adaptive trigger effect. Positions and strengths are normalized to [0, 1].
struct TriggerEffect
{
    /// Kind of effect.
    enum class Mode : u8
    {
        Off,        ///< No resistance.
        Resistance, ///< Constant resistance from `start` to the end of travel.
        Weapon,     ///< Resistance between `start` and `end` that "breaks" like a trigger pull.
        Vibration,  ///< Vibration from `start` with `amplitude` at `frequency` Hz.
    };

    Mode mode = Mode::Off; ///< Effect kind.
    f32 start = 0.0f;      ///< Start position of the effect.
    f32 end = 1.0f;        ///< End position (Weapon only).
    f32 strength = 1.0f;   ///< Resistance strength (Resistance, Weapon).
    f32 amplitude = 0.0f;  ///< Vibration amplitude (Vibration).
    u8 frequency = 0;      ///< Vibration frequency in Hz (Vibration).

    /// No effect.
    [[nodiscard]] static TriggerEffect off() noexcept { return TriggerEffect{}; }
    /// Constant resistance from @p startPosition.
    [[nodiscard]] static TriggerEffect resistance(f32 startPosition, f32 force) noexcept
    {
        return TriggerEffect{Mode::Resistance, startPosition, 1.0f, force, 0.0f, 0};
    }
    /// Weapon-like resistance between @p startPosition and @p endPosition.
    [[nodiscard]] static TriggerEffect weapon(f32 startPosition, f32 endPosition, f32 force) noexcept
    {
        return TriggerEffect{Mode::Weapon, startPosition, endPosition, force, 0.0f, 0};
    }
    /// Vibration from @p startPosition.
    [[nodiscard]] static TriggerEffect vibration(f32 startPosition, f32 vibrationAmplitude,
                                                 u8 frequencyHz) noexcept
    {
        return TriggerEffect{Mode::Vibration, startPosition, 1.0f, 1.0f, vibrationAmplitude, frequencyHz};
    }
};

/// Encodes @p effect as the 11-byte DualSense trigger effect block (the format SDL forwards to the
/// controller). Values are clamped to their valid ranges.
[[nodiscard]] std::array<u8, 11> encodeDualSenseTriggerEffect(const TriggerEffect& effect) noexcept;

/// Optional features of a connected gamepad.
struct GamepadCapabilities
{
    bool rumble = false;           ///< Two-motor rumble.
    bool triggerRumble = false;    ///< Impulse triggers (Xbox One / Series).
    bool led = false;              ///< Programmable light (DualShock 4, DualSense).
    bool motionSensors = false;    ///< Gyroscope and accelerometer.
    bool touchpad = false;         ///< Touchpad.
    bool adaptiveTriggers = false; ///< DualSense adaptive triggers.
};

/// A connected gamepad, owned by GamepadSystem. The pointer becomes invalid when the gamepad
/// disconnects; keep a GamepadId to refer to it across frames.
class Gamepad : NonCopyable
{
public:
    ~Gamepad();

    /// Identifier, stable while connected.
    [[nodiscard]] GamepadId id() const noexcept { return m_id; }
    /// Human-readable device name.
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    /// Controller model.
    [[nodiscard]] GamepadType type() const noexcept { return m_state.type(); }
    /// Player slot (0-based) assigned on connection; shown on player LEDs where supported.
    [[nodiscard]] u32 playerIndex() const noexcept { return m_playerIndex; }
    /// Features this gamepad supports.
    [[nodiscard]] const GamepadCapabilities& capabilities() const noexcept { return m_capabilities; }

    /// Input state for the current frame.
    [[nodiscard]] const GamepadState& state() const noexcept { return m_state; }
    /// Mutable state, for dead-zone settings and input playback.
    [[nodiscard]] GamepadState& state() noexcept { return m_state; }

    /// Starts low- and high-frequency rumble (each in [0, 1]) for @p durationMs. Returns false if
    /// unsupported. A new call replaces the previous rumble; intensities of 0 stop it.
    bool rumble(f32 lowFrequency, f32 highFrequency, u32 durationMs) noexcept;

    /// Rumbles the trigger motors (Xbox One / Series). Returns false if unsupported.
    bool rumbleTriggers(f32 left, f32 right, u32 durationMs) noexcept;

    /// Sets the light colour. Returns false if unsupported.
    bool setLed(u8 red, u8 green, u8 blue) noexcept;

    /// Applies a DualSense adaptive trigger effect. Returns false if unsupported.
    bool setTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect) noexcept;

    /// Enables or disables the gyroscope and accelerometer (off by default to save battery). Returns
    /// false if unsupported.
    bool setMotionSensorsEnabled(bool enabled) noexcept;

private:
    friend class GamepadSystem;
    Gamepad() = default;

    void* m_handle = nullptr; // SDL_Gamepad*
    GamepadId m_id = GamepadId::Invalid;
    std::string m_name;
    u32 m_playerIndex = 0;
    GamepadCapabilities m_capabilities;
    GamepadState m_state;
};

/// A gamepad was connected. Enqueued on the GamepadSystem's EventBus.
struct GamepadConnectedEvent
{
    GamepadId gamepad; ///< The new gamepad.
    GamepadType type;  ///< Its model.
    u32 playerIndex;   ///< Assigned player slot.
};

/// A gamepad was disconnected. Its Gamepad object is already destroyed.
struct GamepadDisconnectedEvent
{
    GamepadId gamepad; ///< The removed gamepad.
    u32 playerIndex;   ///< The player slot it occupied (now free).
};

} // namespace illusion
