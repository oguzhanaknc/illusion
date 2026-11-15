#pragma once

/// @file
/// Interaction of the game UI (ADR 0057): the saved Components that make an element react to the pointer,
/// the keyboard and gamepads, the input a UiWorld reads each frame and the events it reports.
///
/// @code
/// illusion::UiElement placement;
/// placement.placement.offsetMax = {240.0f, 64.0f};
/// const illusion::Entity button =
///     world.create(placement, illusion::UiImage{}, illusion::UiInteractable{}, illusion::UiButton{});
/// illusion::setParent(world, button, canvas);
/// // after the session's update:
/// for (const illusion::UiEvent& event : ui.events())
/// {
///     if (event.type == illusion::UiEventType::Clicked && event.entity == button)
///     {
///         startGame();
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ui/UiComponents.hpp>

#include <string_view>

namespace illusion
{

/// Makes an element react to input (reflected as "illusion.UiInteractable"): it can be hovered, pressed
/// and focused, and its UiImage tint is multiplied by the tint of its state.
struct UiInteractable
{
    bool enabled = true;   ///< A disabled element ignores input, cannot be focused and shows disabledTint.
    bool focusable = true; ///< Reachable by navigation with the keyboard and gamepads.
    Vec4 hoveredTint{1.15f, 1.15f, 1.15f, 1.0f}; ///< Under the pointer.
    Vec4 pressedTint{0.8f, 0.8f, 0.8f, 1.0f};    ///< Held by the pointer or by submit.
    Vec4 focusedTint{1.15f, 1.15f, 1.15f, 1.0f}; ///< Focused by navigation.
    Vec4 disabledTint{0.5f, 0.5f, 0.5f, 0.5f};   ///< Not enabled.
    /// Explicit neighbours for navigation; a null Entity (or one that cannot take the focus) picks the
    /// nearest focusable element in that direction on the canvases that can be focused.
    Entity up{};
    Entity down{};  ///< @copydoc up
    Entity left{};  ///< @copydoc up
    Entity right{}; ///< @copydoc up
};

/// Reports UiEventType::Clicked when it is activated: pressed and released under the pointer, or submit
/// while focused (reflected as "illusion.UiButton"). Needs UiInteractable.
struct UiButton
{
};

/// A value that activation switches on and off (reflected as "illusion.UiToggle"). Needs UiInteractable.
/// The element's first child is shown only while the toggle is on (a check mark).
struct UiToggle
{
    bool on = false; ///< The value.
};

/// A value in a range, changed by dragging or by navigation along its axis (reflected as
/// "illusion.UiSlider"). Needs UiInteractable. The first child is stretched from the start of the element
/// to the value (the fill); the second child is centred on the value (the handle).
struct UiSlider
{
    f32 value = 0.0f;                 ///< The value, kept between min and max.
    f32 min = 0.0f;                   ///< Value at the left (or top).
    f32 max = 1.0f;                   ///< Value at the right (or bottom).
    f32 step = 0.0f;                  ///< Values snap to multiples of it from min; 0 is continuous.
    UiAxis axis = UiAxis::Horizontal; ///< Direction of the slider.
    /// Change per navigation press as a fraction of the range (when step is 0).
    f32 navigationStep = 0.1f;
};

/// State of an interactable element this frame (runtime only, written by the UI System).
struct UiInteraction
{
    bool hovered = false; ///< Topmost interactable element under the pointer.
    bool pressed = false; ///< Held by the pointer or by submit.
    bool focused = false; ///< Has the focus of navigation.
};

/// What the UI reads from the devices each frame. The session fills it from the window's keyboard and mouse
/// and from the actions `ui.navigate`, `ui.submit` and `ui.cancel`; tests fill it by hand.
struct UiInput
{
    Vec2 pointerPixels{0.0f};    ///< Pointer position in target pixels.
    bool pointerPresent = false; ///< False hides hover (no mouse, or the cursor is outside or captured).
    bool pointerDown = false;    ///< The primary button is held.
    Vec2 navigate{0.0f};         ///< Direction of `ui.navigate`, x right, y up, each in [-1, 1].
    bool submitDown = false;     ///< `ui.submit` is held.
    bool cancelPressed = false;  ///< `ui.cancel` was pressed this frame.
    f64 deltaSeconds = 0.0;      ///< Time since the last update, for the repeat of held navigation.
};

/// What happened to an element.
enum class UiEventType : u8
{
    Clicked,      ///< A UiButton was activated.
    ValueChanged, ///< A UiToggle or UiSlider changed; `value` is the new value (0 or 1 for a toggle).
    FocusChanged, ///< The element got the focus; `value` is 1 when navigation moved it, 0 for the pointer.
    Cancelled,    ///< `ui.cancel` was pressed; `entity` is the focused element's canvas, or null.
};

/// One event of a frame.
struct UiEvent
{
    UiEventType type = UiEventType::Clicked; ///< What happened.
    Entity entity{};                         ///< The element it happened to.
    f32 value = 0.0f;                        ///< See UiEventType.
};

/// Names of the input actions the UI reads; every project's input asset gets them with default bindings.
inline constexpr std::string_view kUiNavigateAction = "ui.navigate";
/// @copydoc kUiNavigateAction
inline constexpr std::string_view kUiSubmitAction = "ui.submit";
/// @copydoc kUiNavigateAction
inline constexpr std::string_view kUiCancelAction = "ui.cancel";

} // namespace illusion
