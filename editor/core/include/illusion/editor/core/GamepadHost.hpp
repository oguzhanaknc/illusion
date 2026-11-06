#pragma once

/// @file
/// What the gamepad commands (`gamepad.list`, `gamepad.rumble`, ...) and the Gamepad Tester window need from
/// the editor's gamepads: the state of every connected gamepad and its outputs (rumble, light, DualSense
/// adaptive triggers, motion sensors). The editor process (`illusion::editor_play`, EditorGamepads)
/// implements it over the GamepadSystem that Play mode reads and registers with EditorContext::setGamepads();
/// without one, the commands fail with ErrorKind::Unsupported.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// Buttons of a GamepadSnapshot, in the order of illusion::GamepadButton.
inline constexpr usize kGamepadButtons = 21;

/// Axes of a GamepadSnapshot, in the order of illusion::GamepadAxis (sticks, then triggers).
inline constexpr usize kGamepadAxes = 6;

/// Touchpad fingers of a GamepadSnapshot.
inline constexpr usize kGamepadFingers = 2;

/// Stable position name of button @p index of GamepadSnapshot::buttons (< kGamepadButtons), e.g. "South".
[[nodiscard]] std::string_view gamepadButtonId(usize index) noexcept;

/// Optional features of a connected gamepad.
struct GamepadFeatures
{
    bool rumble = false;           ///< Two-motor rumble.
    bool triggerRumble = false;    ///< Impulse triggers (Xbox One / Series).
    bool led = false;              ///< Programmable light (DualShock 4, DualSense).
    bool motionSensors = false;    ///< Gyroscope and accelerometer.
    bool touchpad = false;         ///< Touchpad.
    bool adaptiveTriggers = false; ///< DualSense adaptive triggers.
};

/// One finger on a gamepad's touchpad.
struct GamepadFingerState
{
    bool down = false;         ///< True while the finger touches the pad.
    Vec2 position{0.0f, 0.0f}; ///< Normalized position, (0, 0) top-left to (1, 1) bottom-right.
    f32 pressure = 0.0f;       ///< Pressure in [0, 1].
};

/// The state of one connected gamepad for the current frame.
struct GamepadSnapshot
{
    u32 id = 0;                                  ///< Identifier, stable while it stays connected.
    std::string name;                            ///< Device name.
    std::string type;                            ///< Model name, e.g. "PS5" or "XboxOne".
    u32 player = 0;                              ///< Player slot (0-based).
    GamepadFeatures features;                    ///< What it supports.
    std::array<bool, kGamepadButtons> buttons{}; ///< Held buttons.
    std::array<f32, kGamepadAxes> axes{};        ///< Raw axes: sticks in [-1, 1] (+y up), triggers in [0, 1].
    std::array<GamepadFingerState, kGamepadFingers> fingers{}; ///< Touchpad fingers.
    bool motionEnabled = false; ///< The motion sensors report (see setMotionSensors()).
    Vec3 gyroscope{0.0f};       ///< Angular velocity in rad/s (x pitch, y yaw, z roll).
    Vec3 accelerometer{0.0f};   ///< Acceleration in m/s², gravity included.
};

/// Which trigger a DualSense adaptive trigger effect applies to.
enum class GamepadTriggerSide : u8
{
    Left,  ///< L2.
    Right, ///< R2.
};

/// Kind of a DualSense adaptive trigger effect.
enum class GamepadTriggerMode : u8
{
    Off,        ///< No resistance.
    Resistance, ///< Constant resistance from `start` to the end of travel.
    Weapon,     ///< Resistance between `start` and `end` that "breaks" like a trigger pull.
    Vibration,  ///< Vibration from `start` with `amplitude` at `frequency` Hz.
};

/// A DualSense adaptive trigger effect; positions and strengths are in [0, 1].
struct GamepadTriggerEffect
{
    GamepadTriggerMode mode = GamepadTriggerMode::Off; ///< Effect kind.
    f32 start = 0.0f;                                  ///< Start position.
    f32 end = 1.0f;                                    ///< End position (Weapon).
    f32 strength = 1.0f;                               ///< Resistance strength (Resistance, Weapon).
    f32 amplitude = 0.0f;                              ///< Vibration amplitude (Vibration).
    u8 frequency = 0;                                  ///< Vibration frequency in Hz (Vibration).
};

/// The editor's gamepads (see the file documentation). Use it on the editor thread. The output functions
/// return false when the gamepad is not connected or lacks the feature.
class GamepadHost
{
public:
    virtual ~GamepadHost() = default;

    /// Replaces @p out with the connected gamepads, ordered by connection time. Reuse @p out to avoid
    /// allocations.
    virtual void gamepads(std::vector<GamepadSnapshot>& out) const = 0;

    /// Rumbles the low- and high-frequency motors (each in [0, 1]) for @p durationMs; 0 stops them.
    virtual bool rumble(u32 gamepad, f32 lowFrequency, f32 highFrequency, u32 durationMs) = 0;

    /// Rumbles the trigger motors (each in [0, 1]) for @p durationMs (Xbox One / Series).
    virtual bool rumbleTriggers(u32 gamepad, f32 left, f32 right, u32 durationMs) = 0;

    /// Sets the light colour.
    virtual bool setLed(u32 gamepad, u8 red, u8 green, u8 blue) = 0;

    /// Applies a DualSense adaptive trigger effect to @p side.
    virtual bool setTriggerEffect(u32 gamepad, GamepadTriggerSide side,
                                  const GamepadTriggerEffect& effect) = 0;

    /// Turns the gyroscope and accelerometer on or off (off saves battery).
    virtual bool setMotionSensors(u32 gamepad, bool enabled) = 0;
};

} // namespace illusion::editor
