#pragma once

// Internal SDL helpers for the input module.

#include <illusion/core/Base.hpp>
#include <illusion/input/Gamepad.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace illusion::detail
{

/// Size of the DualSense effects report accepted by SDL_SendGamepadEffect (DS5EffectsState_t).
inline constexpr usize kDualSenseEffectsSize = 47;

/// Converts an intensity in [0, 1] to SDL's 16-bit range.
inline u16 toU16(f32 value) noexcept
{
    return static_cast<u16>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
}

/// Converts an SDL gamepad axis value to the engine convention (sticks [-1, 1] with +y up, triggers [0, 1]).
inline f32 axisFromSdl(GamepadAxis axis, i16 value) noexcept
{
    const f32 normalized = std::max(static_cast<f32>(value) / 32767.0f, -1.0f);
    switch (axis)
    {
    case GamepadAxis::LeftY:
    case GamepadAxis::RightY:
        return -normalized; // SDL reports +y down
    case GamepadAxis::LeftTrigger:
    case GamepadAxis::RightTrigger:
        return std::max(normalized, 0.0f);
    default:
        return normalized;
    }
}

/// Converts an engine axis value to a virtual joystick axis value (the inverse of axisFromSdl()).
/// Virtual trigger axes use the full joystick range, which SDL maps onto the trigger's [0, max].
inline i16 virtualAxisFromEngine(GamepadAxis axis, f32 value) noexcept
{
    switch (axis)
    {
    case GamepadAxis::LeftY:
    case GamepadAxis::RightY:
        return static_cast<i16>(std::lround(std::clamp(-value, -1.0f, 1.0f) * 32767.0f));
    case GamepadAxis::LeftTrigger:
    case GamepadAxis::RightTrigger:
        return static_cast<i16>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f) - 32768);
    default:
        return static_cast<i16>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
    }
}

/// Maps SDL's gamepad type to the engine's.
inline GamepadType typeFromSdl(SDL_GamepadType type) noexcept
{
    switch (type)
    {
    case SDL_GAMEPAD_TYPE_XBOX360:
        return GamepadType::Xbox360;
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return GamepadType::XboxOne;
    case SDL_GAMEPAD_TYPE_PS3:
        return GamepadType::PS3;
    case SDL_GAMEPAD_TYPE_PS4:
        return GamepadType::PS4;
    case SDL_GAMEPAD_TYPE_PS5:
        return GamepadType::PS5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        return GamepadType::SwitchPro;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        return GamepadType::SwitchJoyConLeft;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        return GamepadType::SwitchJoyConRight;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return GamepadType::SwitchJoyConPair;
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return GamepadType::GameCube;
    case SDL_GAMEPAD_TYPE_STEAM:
        return GamepadType::Steam;
    default:
        return GamepadType::Generic;
    }
}

} // namespace illusion::detail
