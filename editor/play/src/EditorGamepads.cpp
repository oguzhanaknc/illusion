#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/play/EditorGamepads.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/input/GamepadSystem.hpp>

#include <algorithm>

namespace illusion::editor
{
namespace
{

static_assert(kGamepadButtons == static_cast<usize>(GamepadButton::Count));
static_assert(kGamepadAxes == static_cast<usize>(GamepadAxis::Count));
static_assert(kGamepadFingers == GamepadState::kMaxFingers);

TriggerEffect engineEffect(const GamepadTriggerEffect& effect) noexcept
{
    switch (effect.mode)
    {
        case GamepadTriggerMode::Resistance:
            return TriggerEffect::resistance(effect.start, effect.strength);
        case GamepadTriggerMode::Weapon:
            return TriggerEffect::weapon(effect.start, effect.end, effect.strength);
        case GamepadTriggerMode::Vibration:
            return TriggerEffect::vibration(effect.start, effect.amplitude, effect.frequency);
        case GamepadTriggerMode::Off:
            break;
    }
    return TriggerEffect::off();
}

} // namespace

EditorGamepads::EditorGamepads(EditorContext& editor, GamepadSystem& system)
    : m_editor(editor)
    , m_system(system)
{
    m_editor.setGamepads(this);
}

EditorGamepads::~EditorGamepads()
{
    if (m_editor.gamepads() == this)
    {
        m_editor.setGamepads(nullptr);
    }
}

void EditorGamepads::gamepads(std::vector<GamepadSnapshot>& out) const
{
    const std::span<Gamepad* const> pads = m_system.gamepads();
    out.resize(pads.size());
    for (usize i = 0; i < pads.size(); ++i)
    {
        const Gamepad& pad = *pads[i];
        const GamepadState& state = pad.state();
        GamepadSnapshot& snapshot = out[i];
        snapshot.id = static_cast<u32>(pad.id());
        snapshot.name.assign(pad.name());
        snapshot.type.assign(gamepadTypeName(pad.type()));
        snapshot.player = pad.playerIndex();
        const GamepadCapabilities& caps = pad.capabilities();
        snapshot.features = {caps.rumble,        caps.triggerRumble, caps.led,
                             caps.motionSensors, caps.touchpad,      caps.adaptiveTriggers};
        for (usize b = 0; b < kGamepadButtons; ++b)
        {
            snapshot.buttons[b] = state.isDown(static_cast<GamepadButton>(b));
        }
        for (usize a = 0; a < kGamepadAxes; ++a)
        {
            snapshot.axes[a] = state.axis(static_cast<GamepadAxis>(a));
        }
        for (usize f = 0; f < kGamepadFingers; ++f)
        {
            const TouchpadFinger& finger = state.finger(f);
            snapshot.fingers[f] = {finger.down, finger.position, finger.pressure};
        }
        snapshot.motionEnabled =
            std::find(m_motion.begin(), m_motion.end(), snapshot.id) != m_motion.end();
        snapshot.gyroscope = state.gyroscope();
        snapshot.accelerometer = state.accelerometer();
    }
}

bool EditorGamepads::rumble(u32 gamepad, f32 lowFrequency, f32 highFrequency, u32 durationMs)
{
    Gamepad* pad = m_system.find(static_cast<GamepadId>(gamepad));
    return pad != nullptr && pad->rumble(lowFrequency, highFrequency, durationMs);
}

bool EditorGamepads::rumbleTriggers(u32 gamepad, f32 left, f32 right, u32 durationMs)
{
    Gamepad* pad = m_system.find(static_cast<GamepadId>(gamepad));
    return pad != nullptr && pad->rumbleTriggers(left, right, durationMs);
}

bool EditorGamepads::setLed(u32 gamepad, u8 red, u8 green, u8 blue)
{
    Gamepad* pad = m_system.find(static_cast<GamepadId>(gamepad));
    return pad != nullptr && pad->setLed(red, green, blue);
}

bool EditorGamepads::setTriggerEffect(u32 gamepad, GamepadTriggerSide side, const GamepadTriggerEffect& effect)
{
    Gamepad* pad = m_system.find(static_cast<GamepadId>(gamepad));
    const GamepadTrigger trigger = side == GamepadTriggerSide::Left ? GamepadTrigger::Left : GamepadTrigger::Right;
    return pad != nullptr && pad->setTriggerEffect(trigger, engineEffect(effect));
}

bool EditorGamepads::setMotionSensors(u32 gamepad, bool enabled)
{
    Gamepad* pad = m_system.find(static_cast<GamepadId>(gamepad));
    if (pad == nullptr || !pad->setMotionSensorsEnabled(enabled))
    {
        return false;
    }
    std::erase(m_motion, gamepad);
    if (enabled)
    {
        m_motion.push_back(gamepad);
    }
    return true;
}

} // namespace illusion::editor
