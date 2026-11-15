#pragma once

/// @file
/// The host's implementation of UiService over a World, its UiWorld and the game's Localization.
///
/// @code
/// illusion::UiWorld ui(world);
/// illusion::UiWorldService uiService(world, ui, localization);
/// services.provide<illusion::UiService>(uiService);
/// @endcode

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ui/UiService.hpp>

#include <string>

namespace illusion
{

class UiWorld;
class World;

namespace text
{
class Localization;
}

/// UiService that writes the UI Components of a World and asks its UiWorld for focus and events; the World,
/// the UiWorld and the Localization must outlive it. setLanguage() requests the locale, which takes effect
/// at the next `Localization::applyPendingLocale()` (GameSession calls it before its UI update). Created by
/// the host; a shared module that constructs one fails to link, since only the host has the library's code
/// (ADR 0041).
class UiWorldService final : public UiService
{
public:
    /// Serves the UI of @p world laid out by @p ui, with the string tables of @p localization.
    UiWorldService(World& world, UiWorld& ui, text::Localization& localization) noexcept
        : m_world(world)
        , m_ui(ui)
        , m_localization(localization)
    {
    }
    ~UiWorldService() override;

    /// @name UiService
    /// @{
    [[nodiscard]] Entity find(Entity root, std::string_view name) const override;
    bool setText(Entity entity, std::string_view utf8) override;
    bool setTextKey(Entity entity, std::string_view key,
                    std::span<const UiTextArgument> arguments = {}) override;
    bool setVisible(Entity entity, bool visible) override;
    [[nodiscard]] bool isVisible(Entity entity) const override;
    bool setInteractable(Entity entity, bool enabled) override;
    bool setToggle(Entity entity, bool on) override;
    [[nodiscard]] std::optional<bool> toggle(Entity entity) const override;
    bool setSliderValue(Entity entity, f32 value) override;
    [[nodiscard]] std::optional<f32> sliderValue(Entity entity) const override;
    bool setFocus(Entity entity) override;
    [[nodiscard]] Entity focused() const override;
    [[nodiscard]] std::optional<UiRect> rect(Entity entity) const override;
    void invalidateLayout() override;
    u32 pollEvents(std::span<UiEvent> events) const override;
    bool setLanguage(std::string_view locale) override;
    [[nodiscard]] std::string_view language() const override;
    /// @}

private:
    // The first Entity named @p name in the subtree of @p root (pre-order), or null.
    [[nodiscard]] Entity findIn(Entity root, std::string_view name) const;

    World& m_world;
    UiWorld& m_ui;
    text::Localization& m_localization;
    mutable HierarchyIndex m_hierarchy; // rebuilt by find() when the hierarchy changed
    std::string m_language;             // the tag setLanguage() requested; empty before
};

} // namespace illusion
