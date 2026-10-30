#pragma once

/// @file
/// The host's implementation of InputService over InputActions.
///
/// @code
/// illusion::InputActions actions; // updated by the host every frame
/// illusion::InputActionsService inputService(actions);
/// modules.provide<illusion::InputService>(inputService);
/// @endcode

#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputService.hpp>

namespace illusion
{

/// InputService that forwards every call to an InputActions, which must outlive it. Created by the host; a
/// shared module that constructs one fails to link, since only the host has the library's code (ADR 0041).
class InputActionsService final : public InputService
{
public:
    /// Serves @p actions.
    explicit InputActionsService(InputActions& actions) noexcept
        : m_actions(actions)
    {
    }
    ~InputActionsService() override;

    /// The InputActions it serves.
    [[nodiscard]] InputActions& actions() const noexcept { return m_actions; }

    /// @name InputService
    /// @{
    std::optional<ActionId> addAction(std::string_view name, ActionType type) override;
    [[nodiscard]] std::optional<ActionId> findAction(std::string_view name) const override;
    [[nodiscard]] u32 actionCount() const override;
    [[nodiscard]] std::string_view actionName(ActionId action) const override;
    [[nodiscard]] ActionType actionType(ActionId action) const override;
    [[nodiscard]] std::span<const InputBinding> bindings(ActionId action) const override;
    void setBindings(ActionId action, std::span<const InputBinding> bindings) override;
    [[nodiscard]] bool isDown(ActionId action) const override;
    [[nodiscard]] bool wasPressed(ActionId action) const override;
    [[nodiscard]] bool wasReleased(ActionId action) const override;
    [[nodiscard]] f32 value(ActionId action) const override;
    [[nodiscard]] Vec2 vector(ActionId action) const override;
    [[nodiscard]] InputDevice lastDevice() const override;
    [[nodiscard]] GamepadType lastGamepadType() const override;
    /// @}

private:
    InputActions& m_actions;
};

} // namespace illusion
