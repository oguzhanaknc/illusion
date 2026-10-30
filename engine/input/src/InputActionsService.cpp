#include <illusion/input/InputActionsService.hpp>

namespace illusion
{

// Out of line so that the vtable is emitted in the library, which only the host links.
InputActionsService::~InputActionsService() = default;

std::optional<ActionId> InputActionsService::addAction(std::string_view name, ActionType type)
{
    if (const std::optional<ActionId> existing = m_actions.findAction(name))
    {
        return m_actions.actionType(*existing) == type ? existing : std::nullopt;
    }
    if (!InputActions::isValidActionName(name))
    {
        return std::nullopt;
    }
    return m_actions.addAction(name, type);
}

std::optional<ActionId> InputActionsService::findAction(std::string_view name) const
{
    return m_actions.findAction(name);
}

u32 InputActionsService::actionCount() const
{
    return m_actions.actionCount();
}

std::string_view InputActionsService::actionName(ActionId action) const
{
    return m_actions.actionName(action);
}

ActionType InputActionsService::actionType(ActionId action) const
{
    return m_actions.actionType(action);
}

std::span<const InputBinding> InputActionsService::bindings(ActionId action) const
{
    return m_actions.bindings(action);
}

void InputActionsService::setBindings(ActionId action, std::span<const InputBinding> bindings)
{
    m_actions.setBindings(action, bindings);
}

bool InputActionsService::isDown(ActionId action) const
{
    return m_actions.isDown(action);
}

bool InputActionsService::wasPressed(ActionId action) const
{
    return m_actions.wasPressed(action);
}

bool InputActionsService::wasReleased(ActionId action) const
{
    return m_actions.wasReleased(action);
}

f32 InputActionsService::value(ActionId action) const
{
    return m_actions.value(action);
}

Vec2 InputActionsService::vector(ActionId action) const
{
    return m_actions.vector(action);
}

InputDevice InputActionsService::lastDevice() const
{
    return m_actions.lastDevice();
}

GamepadType InputActionsService::lastGamepadType() const
{
    return m_actions.lastGamepadType();
}

} // namespace illusion
