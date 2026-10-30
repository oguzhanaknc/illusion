#pragma once

/// @file
/// The input part of the scripting API: what a game module may ask of the host's InputActions.
///
/// @code
/// if (illusion::InputService* input = context.service<illusion::InputService>())
/// {
///     const std::optional<illusion::ActionId> jump = input->findAction("Jump");
///     if (jump && input->wasPressed(*jump))
///     {
///         // jump
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/input/InputActions.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

/// Input actions of the game, implemented by the host (InputActionsService over an InputActions that the
/// host updates every frame) and found by modules with `context.service<InputService>()`. The functions
/// behave like the InputActions functions of the same name. Adding a function changes the module API
/// version.
/// @ingroup scripting_api
class InputService : NonCopyable
{
public:
    /// Adds an action, or returns the existing one of that name and type; nullopt if an action of that
    /// name has another type or the name is not valid (InputActions::isValidActionName()).
    virtual std::optional<ActionId> addAction(std::string_view name, ActionType type) = 0;
    /// The action called @p name, if there is one.
    [[nodiscard]] virtual std::optional<ActionId> findAction(std::string_view name) const = 0;
    /// Number of actions; their ids are 0 to actionCount() - 1.
    [[nodiscard]] virtual u32 actionCount() const = 0;
    /// Name of an action; valid until actions are added.
    [[nodiscard]] virtual std::string_view actionName(ActionId action) const = 0;
    /// Type of an action.
    [[nodiscard]] virtual ActionType actionType(ActionId action) const = 0;
    /// Bindings of an action; valid until they change.
    [[nodiscard]] virtual std::span<const InputBinding> bindings(ActionId action) const = 0;
    /// Replaces the bindings of an action (rebinding).
    virtual void setBindings(ActionId action, std::span<const InputBinding> bindings) = 0;

    /// True while a Button action is held.
    [[nodiscard]] virtual bool isDown(ActionId action) const = 0;
    /// True in the frame a Button action was pressed.
    [[nodiscard]] virtual bool wasPressed(ActionId action) const = 0;
    /// True in the frame a Button action was released.
    [[nodiscard]] virtual bool wasReleased(ActionId action) const = 0;
    /// Value of a Button (0 or 1) or Axis action, or the x of a Vector2 action.
    [[nodiscard]] virtual f32 value(ActionId action) const = 0;
    /// Value of a Vector2 action, length up to 1.
    [[nodiscard]] virtual Vec2 vector(ActionId action) const = 0;
    /// Device that changed an action last, for glyphs.
    [[nodiscard]] virtual InputDevice lastDevice() const = 0;
    /// Type of the gamepad used last, for glyphs.
    [[nodiscard]] virtual GamepadType lastGamepadType() const = 0;

protected:
    InputService() = default;
    virtual ~InputService() = default;
};

} // namespace illusion
