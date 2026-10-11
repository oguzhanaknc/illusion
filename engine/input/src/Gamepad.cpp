#include "SdlGamepad.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/input/Gamepad.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace illusion
{
namespace
{

constexpr std::array<std::string_view, static_cast<usize>(GamepadButton::Count)> kButtonNames = {
    "South",     "East",       "West",         "North",         "Back",         "Guide",       "Start",
    "LeftStick", "RightStick", "LeftShoulder", "RightShoulder", "DPadUp",       "DPadDown",    "DPadLeft",
    "DPadRight", "Misc1",      "RightPaddle1", "LeftPaddle1",   "RightPaddle2", "LeftPaddle2", "Touchpad",
};
static_assert(kButtonNames[static_cast<usize>(GamepadButton::Touchpad)] == "Touchpad");

constexpr std::array<std::string_view, static_cast<usize>(GamepadAxis::Count)> kAxisNames = {
    "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};

u8 toByte(f32 value) noexcept
{
    return static_cast<u8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Rescales [deadZone, 1] to [0, 1]; values inside the dead zone become 0.
f32 applyDeadZone(f32 magnitude, f32 deadZone) noexcept
{
    if (magnitude <= deadZone)
    {
        return 0.0f;
    }
    return std::min((magnitude - deadZone) / (1.0f - deadZone), 1.0f);
}

} // namespace

GamepadFamily gamepadFamily(GamepadType type) noexcept
{
    switch (type)
    {
    case GamepadType::Xbox360:
    case GamepadType::XboxOne:
    case GamepadType::Steam:
        return GamepadFamily::Xbox;
    case GamepadType::PS3:
    case GamepadType::PS4:
    case GamepadType::PS5:
        return GamepadFamily::PlayStation;
    case GamepadType::SwitchPro:
    case GamepadType::SwitchJoyConLeft:
    case GamepadType::SwitchJoyConRight:
    case GamepadType::SwitchJoyConPair:
    case GamepadType::GameCube:
        return GamepadFamily::Nintendo;
    case GamepadType::Generic:
        return GamepadFamily::Generic;
    }
    return GamepadFamily::Generic;
}

std::string_view gamepadTypeName(GamepadType type) noexcept
{
    switch (type)
    {
    case GamepadType::Generic:
        return "Generic";
    case GamepadType::Xbox360:
        return "Xbox360";
    case GamepadType::XboxOne:
        return "XboxOne";
    case GamepadType::PS3:
        return "PS3";
    case GamepadType::PS4:
        return "PS4";
    case GamepadType::PS5:
        return "PS5";
    case GamepadType::SwitchPro:
        return "SwitchPro";
    case GamepadType::SwitchJoyConLeft:
        return "SwitchJoyConLeft";
    case GamepadType::SwitchJoyConRight:
        return "SwitchJoyConRight";
    case GamepadType::SwitchJoyConPair:
        return "SwitchJoyConPair";
    case GamepadType::GameCube:
        return "GameCube";
    case GamepadType::Steam:
        return "Steam";
    }
    return "Generic";
}

std::string_view gamepadButtonName(GamepadButton button) noexcept
{
    const auto index = static_cast<usize>(button);
    return index < kButtonNames.size() ? kButtonNames[index] : std::string_view("Unknown");
}

std::optional<GamepadButton> gamepadButtonFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kButtonNames.size(); ++i)
    {
        if (kButtonNames[i] == name)
        {
            return static_cast<GamepadButton>(i);
        }
    }
    return std::nullopt;
}

std::string_view gamepadAxisName(GamepadAxis axis) noexcept
{
    const auto index = static_cast<usize>(axis);
    return index < kAxisNames.size() ? kAxisNames[index] : std::string_view("Unknown");
}

std::optional<GamepadAxis> gamepadAxisFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kAxisNames.size(); ++i)
    {
        if (kAxisNames[i] == name)
        {
            return static_cast<GamepadAxis>(i);
        }
    }
    return std::nullopt;
}

// --- GamepadState -------------------------------------------------------------------------------------

Vec2 GamepadState::stick(GamepadStick stick) const noexcept
{
    const bool left = stick == GamepadStick::Left;
    const Vec2 raw(axis(left ? GamepadAxis::LeftX : GamepadAxis::RightX),
                   axis(left ? GamepadAxis::LeftY : GamepadAxis::RightY));
    const f32 length = glm::length(raw);
    if (length <= m_stickDeadZone)
    {
        return Vec2(0.0f);
    }
    return raw * (applyDeadZone(length, m_stickDeadZone) / length);
}

f32 GamepadState::trigger(GamepadTrigger trigger) const noexcept
{
    const f32 raw =
        axis(trigger == GamepadTrigger::Left ? GamepadAxis::LeftTrigger : GamepadAxis::RightTrigger);
    return applyDeadZone(raw, m_triggerDeadZone);
}

void GamepadState::setStickDeadZone(f32 deadZone) noexcept
{
    ILL_ASSERT(deadZone >= 0.0f && deadZone < 1.0f, "dead zone must be in [0, 1)");
    m_stickDeadZone = std::clamp(deadZone, 0.0f, 0.99f);
}

void GamepadState::setTriggerDeadZone(f32 deadZone) noexcept
{
    ILL_ASSERT(deadZone >= 0.0f && deadZone < 1.0f, "dead zone must be in [0, 1)");
    m_triggerDeadZone = std::clamp(deadZone, 0.0f, 0.99f);
}

void GamepadState::beginFrame() noexcept
{
    m_pressed.reset();
    m_released.reset();
    m_analogActivated = false;
}

void GamepadState::setButton(GamepadButton button, bool down) noexcept
{
    const auto index = static_cast<usize>(button);
    if (index >= kButtonCount || m_down.test(index) == down)
    {
        return;
    }
    m_down.set(index, down);
    (down ? m_pressed : m_released).set(index);
}

void GamepadState::setAxis(GamepadAxis axis, f32 value) noexcept
{
    const auto index = static_cast<usize>(axis);
    if (index >= kAxisCount)
    {
        return;
    }
    const bool isTrigger = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
    const f32 clamped = std::clamp(value, isTrigger ? 0.0f : -1.0f, 1.0f);
    if (std::abs(m_axes[index]) < 0.5f && std::abs(clamped) >= 0.5f)
    {
        m_analogActivated = true;
    }
    m_axes[index] = clamped;
}

void GamepadState::setFinger(usize index, const TouchpadFinger& finger) noexcept
{
    if (index < m_fingers.size())
    {
        m_fingers[index] = finger;
    }
}

void GamepadState::releaseAll() noexcept
{
    m_released |= m_down;
    m_down.reset();
    m_axes.fill(0.0f);
    m_fingers.fill(TouchpadFinger{});
}

// --- DualSense ----------------------------------------------------------------------------------------

std::array<u8, 11> encodeDualSenseTriggerEffect(const TriggerEffect& effect) noexcept
{
    // Effect modes documented at https://controllers.fandom.com/wiki/Sony_DualSense#FFB_Trigger_Modes and
    // used by SDL's testcontroller.
    std::array<u8, 11> bytes{};
    switch (effect.mode)
    {
    case TriggerEffect::Mode::Off:
        bytes[0] = 0x05;
        break;
    case TriggerEffect::Mode::Resistance:
        bytes[0] = 0x01;
        bytes[1] = toByte(effect.start);
        bytes[2] = toByte(effect.strength);
        break;
    case TriggerEffect::Mode::Weapon:
        bytes[0] = 0x02;
        bytes[1] = toByte(effect.start);
        bytes[2] = std::max(toByte(effect.end), bytes[1]);
        bytes[3] = toByte(effect.strength);
        break;
    case TriggerEffect::Mode::Vibration:
        bytes[0] = 0x06;
        bytes[1] = effect.frequency;
        bytes[2] = toByte(effect.amplitude);
        bytes[3] = toByte(effect.start);
        break;
    }
    return bytes;
}

// --- Gamepad ------------------------------------------------------------------------------------------

Gamepad::~Gamepad()
{
    if (m_handle != nullptr)
    {
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(m_handle));
    }
}

bool Gamepad::rumble(f32 lowFrequency, f32 highFrequency, u32 durationMs) noexcept
{
    return SDL_RumbleGamepad(static_cast<SDL_Gamepad*>(m_handle), detail::toU16(lowFrequency),
                             detail::toU16(highFrequency), durationMs);
}

bool Gamepad::rumbleTriggers(f32 left, f32 right, u32 durationMs) noexcept
{
    return SDL_RumbleGamepadTriggers(static_cast<SDL_Gamepad*>(m_handle), detail::toU16(left),
                                     detail::toU16(right), durationMs);
}

bool Gamepad::setLed(u8 red, u8 green, u8 blue) noexcept
{
    return SDL_SetGamepadLED(static_cast<SDL_Gamepad*>(m_handle), red, green, blue);
}

bool Gamepad::setTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect) noexcept
{
    if (!m_capabilities.adaptiveTriggers)
    {
        return false;
    }
    // DualSense output report as accepted by SDL_SendGamepadEffect (DS5EffectsState_t, 47 bytes):
    // byte 0 holds "modify" flags (0x04 right trigger, 0x08 left trigger); the right trigger block starts
    // at byte 10 and the left one at byte 21.
    std::array<u8, detail::kDualSenseEffectsSize> report{};
    const std::array<u8, 11> block = encodeDualSenseTriggerEffect(effect);
    const bool right = trigger == GamepadTrigger::Right;
    report[0] = right ? 0x04 : 0x08;
    std::memcpy(report.data() + (right ? 10 : 21), block.data(), block.size());
    return SDL_SendGamepadEffect(static_cast<SDL_Gamepad*>(m_handle), report.data(),
                                 static_cast<int>(report.size()));
}

bool Gamepad::setMotionSensorsEnabled(bool enabled) noexcept
{
    if (!m_capabilities.motionSensors)
    {
        return false;
    }
    auto* handle = static_cast<SDL_Gamepad*>(m_handle);
    const bool gyro = SDL_SetGamepadSensorEnabled(handle, SDL_SENSOR_GYRO, enabled);
    const bool accel = SDL_SetGamepadSensorEnabled(handle, SDL_SENSOR_ACCEL, enabled);
    return gyro && accel;
}

} // namespace illusion
