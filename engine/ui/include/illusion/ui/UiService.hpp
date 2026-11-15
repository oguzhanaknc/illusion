#pragma once

/// @file
/// The UI part of the scripting API (ADR 0057): what a game module may ask of the host's game UI.
///
/// @code
/// if (illusion::UiService* ui = context.service<illusion::UiService>())
/// {
///     const illusion::Entity score = ui->find({}, "Score");
///     const illusion::UiTextArgument points[] = {illusion::UiTextArgument::number("points", 1200.0)};
///     ui->setTextKey(score, "hud.score", points);
///
///     illusion::UiEvent events[16];
///     const u32 count = ui->pollEvents(events);
///     for (u32 i = 0; i < count && i < 16; ++i)
///     {
///         if (events[i].type == illusion::UiEventType::Clicked && events[i].entity == resumeButton)
///         {
///             ui->setVisible(pauseMenu, false);
///         }
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ui/UiGeometry.hpp>
#include <illusion/ui/UiWidgets.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

/// A named argument of a string table message (MessageFormat), for UiService::setTextKey().
struct UiTextArgument
{
    std::string_view name{}; ///< The argument's name in the message, such as "points".
    std::string_view text{}; ///< Its value when it is text.
    f64 value = 0.0;         ///< Its value when it is a number.
    bool isNumber = false;   ///< Which of the two is used.

    /// A number argument (plural rules and number formats apply).
    [[nodiscard]] static constexpr UiTextArgument number(std::string_view name, f64 value) noexcept
    {
        return {.name = name, .text = {}, .value = value, .isNumber = true};
    }
    /// A text argument.
    [[nodiscard]] static constexpr UiTextArgument string(std::string_view name,
                                                         std::string_view text) noexcept
    {
        return {.name = name, .text = text, .value = 0.0, .isNumber = false};
    }
};

/// The game UI of one Simulation World, implemented by the host (UiWorldService over the session's UiWorld)
/// and found by modules with `context.service<UiService>()`. Nothing here calls into a module: events are
/// polled. Functions that take an Entity return false (or nothing) for a dead Entity or one without the
/// Component they need. Changes made here reach the layout without `World::markChanged()`. Adding a
/// function changes the module API version.
/// @ingroup scripting_api
class UiService : NonCopyable
{
public:
    /// The first Entity named @p name (its Name Component) among @p root and its descendants, in hierarchy
    /// order; a null @p root searches every canvas in draw order. Null when there is none.
    [[nodiscard]] virtual Entity find(Entity root, std::string_view name) const = 0;

    /// Shows the literal @p utf8 in the UiText of @p entity (and clears its key).
    virtual bool setText(Entity entity, std::string_view utf8) = 0;
    /// Shows the string table message @p key in the UiText of @p entity, formatted with @p arguments in the
    /// game's current language; it follows later language changes. Up to 8 arguments, each text up to 255
    /// bytes; false beyond that.
    virtual bool setTextKey(Entity entity, std::string_view key,
                            std::span<const UiTextArgument> arguments = {}) = 0;

    /// Shows or hides @p entity and its descendants (the Inactive tag, as setEnabled() of Hierarchy.hpp).
    virtual bool setVisible(Entity entity, bool visible) = 0;
    /// True when @p entity is alive and neither it nor an ancestor is hidden.
    [[nodiscard]] virtual bool isVisible(Entity entity) const = 0;
    /// Sets UiInteractable::enabled of @p entity.
    virtual bool setInteractable(Entity entity, bool enabled) = 0;

    /// Sets UiToggle::on of @p entity; reports no event.
    virtual bool setToggle(Entity entity, bool on) = 0;
    /// UiToggle::on of @p entity.
    [[nodiscard]] virtual std::optional<bool> toggle(Entity entity) const = 0;
    /// Sets UiSlider::value of @p entity, clamped and snapped like input; reports no event.
    virtual bool setSliderValue(Entity entity, f32 value) = 0;
    /// UiSlider::value of @p entity.
    [[nodiscard]] virtual std::optional<f32> sliderValue(Entity entity) const = 0;

    /// Moves the focus to @p entity (null clears it); false when the element cannot be focused now.
    virtual bool setFocus(Entity entity) = 0;
    /// The focused element; null for none.
    [[nodiscard]] virtual Entity focused() const = 0;
    /// The rectangle the last layout gave @p entity, in canvas units.
    [[nodiscard]] virtual std::optional<UiRect> rect(Entity entity) const = 0;
    /// Tells the UI that Components of UI Entities were written directly through the World.
    virtual void invalidateLayout() = 0;
    /// Moves the UiTransition of @p entity towards its changed state or back to rest, whatever its trigger;
    /// false without one.
    virtual bool playTransition(Entity entity, bool towardsChanged) = 0;
    /// Progress of the UiTransition of @p entity: 0 at rest, 1 in the changed state.
    [[nodiscard]] virtual std::optional<f32> transitionProgress(Entity entity) const = 0;

    /// Copies the events of the UI's last update (the previous frame, as the UI runs after the modules) into
    /// @p events and returns how many there were, which may be more than fit.
    virtual u32 pollEvents(std::span<UiEvent> events) const = 0;

    /// Switches the game's language to @p locale (a BCP 47 tag such as "tr" or "en-GB" that the project has
    /// a locale file for); false when it has none. Every UiText with a key follows.
    virtual bool setLanguage(std::string_view locale) = 0;
    /// The game's current language tag; valid until the next setLanguage().
    [[nodiscard]] virtual std::string_view language() const = 0;

protected:
    UiService() = default;
    virtual ~UiService() = default;
};

} // namespace illusion
