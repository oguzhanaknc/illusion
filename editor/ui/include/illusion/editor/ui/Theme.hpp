#pragma once

/// @file
/// Editor themes: named colour tokens loaded from editor-data/themes/*.json and turned into a Dear ImGui
/// style, with the metrics of the editor's design (4 px grid, rounding, spacing) scaled for UI scale and
/// DPI. The style is always rebuilt from the tokens, never scaled cumulatively.
///
/// ```json
/// {"format": "illusion.theme", "version": 1, "name": "illusion Dark",
///  "tokens": {"bg.base": "#0F1115", "text.primary": "#E3E6EB", "accent": "#4C8DFF"}}
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct ImGuiStyle;

namespace illusion::editor::ui
{

/// A colour token of the theme.
enum class ThemeToken : u8
{
    BgBase,            ///< `bg.base`: behind the dock space.
    BgPanel,           ///< `bg.panel`: panels.
    BgRaised,          ///< `bg.raised`: frames, cards.
    BgInput,           ///< `bg.input`: text fields.
    BgHover,           ///< `bg.hover`: hovered items.
    BgActive,          ///< `bg.active`: pressed items.
    Border,            ///< `border`: popups and tooltips.
    Separator,         ///< `separator`: 1 px lines between sections.
    TextPrimary,       ///< `text.primary`
    TextSecondary,     ///< `text.secondary`
    TextDisabled,      ///< `text.disabled`
    Accent,            ///< `accent`: focus, links, outlines.
    AccentFill,        ///< `accent.fill`: primary buttons.
    AccentText,        ///< `accent.text`: text on `accent.fill`.
    AccentSubtle,      ///< `accent.subtle`: selected rows.
    Success,           ///< `success`
    Warning,           ///< `warning`
    Error,             ///< `error`
    Info,              ///< `info`
    PlayTint,          ///< `play.tint`: marks play mode.
    ViewportSelection, ///< `viewport.selection`
    ViewportHover,     ///< `viewport.hover`
    AxisX,             ///< `axis.x`
    AxisY,             ///< `axis.y`
    AxisZ,             ///< `axis.z`
    AxisCbX,           ///< `axis.cb.x`: colour-blind preset (Okabe-Ito).
    AxisCbY,           ///< `axis.cb.y`
    AxisCbZ,           ///< `axis.cb.z`
};

/// Number of theme tokens.
inline constexpr usize kThemeTokenCount = 28;

/// The JSON name of @p token, e.g. "bg.base".
[[nodiscard]] std::string_view themeTokenName(ThemeToken token) noexcept;

/// Accent presets the user can pick; Azure is the theme's own accent.
enum class AccentPreset : u8
{
    Azure,  ///< The theme's accent (blue in the standard themes).
    Violet, ///< Violet.
    Teal,   ///< Teal.
    Amber,  ///< Amber.
};

/// WCAG relative luminance of an sRGB colour (alpha ignored).
[[nodiscard]] f32 relativeLuminance(Vec4 color) noexcept;

/// WCAG contrast ratio between two sRGB colours, from 1 to 21.
[[nodiscard]] f32 contrastRatio(Vec4 a, Vec4 b) noexcept;

/// Parses "#RRGGBB" or "#RRGGBBAA" into sRGB components in [0, 1].
[[nodiscard]] std::optional<Vec4> parseHexColor(std::string_view text) noexcept;

/// A complete set of tokens.
class Theme
{
public:
    /// Parses a theme file. Every token must be present; unknown tokens are rejected.
    [[nodiscard]] static Expected<Theme, std::string> parse(std::string_view json);

    /// The theme's name, e.g. "illusion Dark".
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

    /// True for high-contrast themes (name or file flag "highContrast").
    [[nodiscard]] bool isHighContrast() const noexcept { return m_highContrast; }

    /// The colour of @p token (sRGB, straight alpha).
    [[nodiscard]] Vec4 color(ThemeToken token) const noexcept { return m_colors[static_cast<usize>(token)]; }

    /// A copy with the accent tokens replaced by @p preset (Azure returns the theme unchanged).
    [[nodiscard]] Theme withAccent(AccentPreset preset) const;

    /// Writes the editor's style for this theme into @p style: colours from the tokens and metrics scaled by
    /// @p scale (UI scale times content scale).
    void apply(ImGuiStyle& style, f32 scale) const;

private:
    std::string m_name;
    bool m_highContrast = false;
    std::array<Vec4, kThemeTokenCount> m_colors{};
};

} // namespace illusion::editor::ui
