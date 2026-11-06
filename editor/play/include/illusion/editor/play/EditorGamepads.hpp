#pragma once

/// @file
/// EditorGamepads: the GamepadHost of the gamepad commands (`gamepad.list`, ...) and the Gamepad Tester
/// window, over the GamepadSystem that Play mode reads (PlaySessionDesc::gamepads).
///
/// @code
/// std::unique_ptr<illusion::GamepadSystem> system = illusion::GamepadSystem::create(&events);
/// illusion::editor::EditorGamepads gamepads(editor, *system);
/// while (running)
/// {
///     system->update(window->isFocused()); // the gamepads' state for this frame
///     ...
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/editor/core/GamepadHost.hpp>

#include <vector>

namespace illusion
{
class GamepadSystem;
} // namespace illusion

namespace illusion::editor
{

class EditorContext;

/// The editor's gamepads. Registers itself with EditorContext::setGamepads(); the destructor unregisters.
/// Use it on the editor thread; @p system must outlive it.
class EditorGamepads final : public GamepadHost, NonCopyable
{
public:
    /// Registers with @p editor as its gamepads, reading and driving @p system.
    EditorGamepads(EditorContext& editor, GamepadSystem& system);

    /// Unregisters from the editor.
    ~EditorGamepads() override;

    /// @copydoc GamepadHost::gamepads
    void gamepads(std::vector<GamepadSnapshot>& out) const override;

    /// @copydoc GamepadHost::rumble
    bool rumble(u32 gamepad, f32 lowFrequency, f32 highFrequency, u32 durationMs) override;

    /// @copydoc GamepadHost::rumbleTriggers
    bool rumbleTriggers(u32 gamepad, f32 left, f32 right, u32 durationMs) override;

    /// @copydoc GamepadHost::setLed
    bool setLed(u32 gamepad, u8 red, u8 green, u8 blue) override;

    /// @copydoc GamepadHost::setTriggerEffect
    bool setTriggerEffect(u32 gamepad, GamepadTriggerSide side, const GamepadTriggerEffect& effect) override;

    /// @copydoc GamepadHost::setMotionSensors
    bool setMotionSensors(u32 gamepad, bool enabled) override;

private:
    EditorContext& m_editor;
    GamepadSystem& m_system;
    std::vector<u32> m_motion; // gamepads whose motion sensors this host turned on
};

} // namespace illusion::editor
