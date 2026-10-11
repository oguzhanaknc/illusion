#pragma once

/// @file
/// Owns the gamepad subsystem (SDL3, gamepads only): hot-plug, per-frame state updates and virtual
/// gamepads for tests, input playback and automation.
///
/// ```cpp
/// auto gamepads = GamepadSystem::create(&events);
/// while (running)
/// {
///     platform->pollEvents();
///     gamepads->update(window->isFocused()); // new frame + connect/disconnect events
///     events.dispatchQueued();
///     for (Gamepad* pad : gamepads->gamepads()) { ... pad->state().stick(GamepadStick::Left) ... }
/// }
/// ```
///
/// Use it on the main thread. Only one GamepadSystem may exist at a time.

#include <illusion/core/Base.hpp>
#include <illusion/input/Gamepad.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace illusion
{

class EventBus;
class VirtualGamepad;

/// Creation parameters for a virtual gamepad.
struct VirtualGamepadDesc
{
    std::string name = "illusion virtual gamepad"; ///< Device name.
    /// Controller model to emulate. Determines the reported type (and therefore glyphs) and which
    /// features exist: PS4/PS5 get a touchpad, motion sensors and an LED; PS5 also adaptive triggers.
    GamepadType type = GamepadType::XboxOne;
};

/// Manages connected gamepads. See the file documentation.
class GamepadSystem : NonCopyable
{
public:
    /// Initializes the gamepad subsystem. Returns nullptr (and logs why) on failure. If @p events is
    /// not null, connection events are enqueued on it; it must outlive the system.
    [[nodiscard]] static std::unique_ptr<GamepadSystem> create(EventBus* events = nullptr);

    /// Disconnects all gamepads and shuts the subsystem down. VirtualGamepads must be destroyed first.
    ~GamepadSystem();

    /// Starts a new input frame for every gamepad, then processes device events: connections,
    /// disconnections, buttons, axes, touchpads and sensors. Pass `acceptInput = false` while the game
    /// has no focus (for example `update(window->isFocused())`): connections are still tracked, but every
    /// gamepad reads as released and centered, so controllers used in other applications do not move
    /// the game.
    void update(bool acceptInput = true);

    /// Connected gamepads, ordered by connection time. Invalidated by update().
    [[nodiscard]] std::span<Gamepad* const> gamepads() const noexcept;

    /// The gamepad with @p id, or nullptr if it is not connected.
    [[nodiscard]] Gamepad* find(GamepadId id) const noexcept;

    /// The gamepad in player slot @p playerIndex, or nullptr.
    [[nodiscard]] Gamepad* findByPlayer(u32 playerIndex) const noexcept;

    /// Connects a simulated gamepad. It appears like a real device after the next update().
    [[nodiscard]] std::unique_ptr<VirtualGamepad> attachVirtualGamepad(const VirtualGamepadDesc& desc = {});

private:
    friend class VirtualGamepad;
    GamepadSystem();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// A simulated gamepad. Inputs set here are reported by the matching Gamepad after the next
/// GamepadSystem::update(); outputs sent to the Gamepad (rumble, LED, effects) are recorded here.
/// Detaches when destroyed.
class VirtualGamepad : NonCopyable
{
public:
    /// Outputs the engine sent to the device.
    struct Outputs
    {
        f32 lowFrequencyRumble = 0.0f;     ///< Last low-frequency rumble intensity.
        f32 highFrequencyRumble = 0.0f;    ///< Last high-frequency rumble intensity.
        f32 leftTriggerRumble = 0.0f;      ///< Last left trigger rumble intensity.
        f32 rightTriggerRumble = 0.0f;     ///< Last right trigger rumble intensity.
        std::array<u8, 3> led{0, 0, 0};    ///< Last LED colour.
        std::vector<u8> lastEffect;        ///< Bytes of the last device effect packet (e.g. DualSense).
        bool motionSensorsEnabled = false; ///< Whether motion sensors were enabled.
    };

    /// Detaches the device.
    ~VirtualGamepad();

    /// The gamepad's identifier (matches Gamepad::id() once connected).
    [[nodiscard]] GamepadId id() const noexcept { return m_id; }

    /// Presses or releases @p button.
    void setButton(GamepadButton button, bool down) noexcept;
    /// Sets @p axis using the engine's conventions (sticks [-1, 1] with +y up, triggers [0, 1]).
    void setAxis(GamepadAxis axis, f32 value) noexcept;
    /// Sets touchpad finger @p index (PS4/PS5 only).
    void setTouchpadFinger(u32 index, const TouchpadFinger& finger) noexcept;
    /// Reports motion sensor readings (PS4/PS5 only; delivered while sensors are enabled).
    void setMotion(Vec3 gyroscope, Vec3 accelerometer) noexcept;

    /// Outputs received so far.
    [[nodiscard]] const Outputs& outputs() const noexcept;

private:
    friend class GamepadSystem;
    VirtualGamepad() = default;

    struct Shared;
    std::unique_ptr<Shared> m_shared;
    GamepadId m_id = GamepadId::Invalid;
    void* m_joystick = nullptr; // SDL_Joystick* opened to feed inputs
};

} // namespace illusion
