#include <illusion/core/Json.hpp>
#include <illusion/editor/ui/Theme.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>

namespace illusion::editor::ui
{

namespace
{

constexpr std::array<std::string_view, kThemeTokenCount> kTokenNames = {
    "bg.base",     "bg.panel",    "bg.raised",          "bg.input",       "bg.hover",      "bg.active",
    "border",      "separator",   "text.primary",       "text.secondary", "text.disabled", "accent",
    "accent.fill", "accent.text", "accent.subtle",      "success",        "warning",       "error",
    "info",        "play.tint",   "viewport.selection", "viewport.hover", "axis.x",        "axis.y",
    "axis.z",      "axis.cb.x",   "axis.cb.y",          "axis.cb.z",
};

int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

f32 channelToLinear(f32 c) noexcept
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

ImVec4 toImGui(Vec4 color) noexcept
{
    return ImVec4(color.r, color.g, color.b, color.a);
}

ImVec4 withAlpha(Vec4 color, f32 alpha) noexcept
{
    return ImVec4(color.r, color.g, color.b, color.a * alpha);
}

// The accent colours of a preset, for dark and light backgrounds: accent, fill, subtle (dark: mixed into the
// panel colour; light: a pale tint).
struct AccentColors
{
    Vec4 accent;
    Vec4 fill;
};

AccentColors presetColors(AccentPreset preset, bool darkBackground) noexcept
{
    const auto hex = [](std::string_view text) {
        return parseHexColor(text).value_or(Vec4(1.0f));
    };
    switch (preset)
    {
    case AccentPreset::Violet:
        return darkBackground ? AccentColors{hex("#A78BFA"), hex("#7C3AED")}
                              : AccentColors{hex("#6D28D9"), hex("#6D28D9")};
    case AccentPreset::Teal:
        return darkBackground ? AccentColors{hex("#2DD4BF"), hex("#0F766E")}
                              : AccentColors{hex("#0F766E"), hex("#0F766E")};
    case AccentPreset::Amber:
        return darkBackground ? AccentColors{hex("#F5B83D"), hex("#B45309")}
                              : AccentColors{hex("#92400E"), hex("#92400E")};
    case AccentPreset::Azure:
        break;
    }
    return AccentColors{};
}

Vec4 mix(Vec4 a, Vec4 b, f32 t) noexcept
{
    return a + (b - a) * t;
}

} // namespace

std::string_view themeTokenName(ThemeToken token) noexcept
{
    const auto index = static_cast<usize>(token);
    return index < kTokenNames.size() ? kTokenNames[index] : std::string_view();
}

f32 relativeLuminance(Vec4 color) noexcept
{
    return 0.2126f * channelToLinear(color.r) + 0.7152f * channelToLinear(color.g) +
           0.0722f * channelToLinear(color.b);
}

f32 contrastRatio(Vec4 a, Vec4 b) noexcept
{
    const f32 la = relativeLuminance(a);
    const f32 lb = relativeLuminance(b);
    const f32 lighter = std::max(la, lb);
    const f32 darker = std::min(la, lb);
    return (lighter + 0.05f) / (darker + 0.05f);
}

std::optional<Vec4> parseHexColor(std::string_view text) noexcept
{
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
    {
        return std::nullopt;
    }
    f32 channels[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (usize i = 0; 1 + 2 * i < text.size(); ++i)
    {
        const int high = hexDigit(text[1 + 2 * i]);
        const int low = hexDigit(text[2 + 2 * i]);
        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }
        channels[i] = static_cast<f32>(high * 16 + low) / 255.0f;
    }
    return Vec4(channels[0], channels[1], channels[2], channels[3]);
}

Expected<Theme, std::string> Theme::parse(std::string_view json)
{
    auto parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return unexpected(
            std::format("{}:{}: {}", parsed.error().line, parsed.error().column, parsed.error().message));
    }
    const JsonValue root = parsed->root();
    if (root.find("format").asString() != std::optional<std::string_view>("illusion.theme"))
    {
        return unexpected(std::string("\"format\" must be \"illusion.theme\""));
    }
    if (root.find("version").asU64() != std::optional<u64>(1))
    {
        return unexpected(std::string("\"version\" must be 1"));
    }
    const std::optional<std::string_view> name = root.find("name").asString();
    if (!name || name->empty())
    {
        return unexpected(std::string("\"name\" must be a non-empty string"));
    }
    Theme theme;
    theme.m_name = *name;
    theme.m_highContrast = root.find("highContrast").asBool().value_or(false);

    const JsonValue tokens = root.find("tokens");
    if (!tokens.isObject())
    {
        return unexpected(std::string("\"tokens\" must be an object"));
    }
    std::array<bool, kThemeTokenCount> seen{};
    for (const JsonMember member : tokens.members())
    {
        usize index = kThemeTokenCount;
        for (usize i = 0; i < kTokenNames.size(); ++i)
        {
            if (kTokenNames[i] == member.key)
            {
                index = i;
            }
        }
        if (index == kThemeTokenCount)
        {
            return unexpected(std::format("unknown token \"{}\"", member.key));
        }
        const std::optional<Vec4> color = parseHexColor(member.value.asString().value_or(""));
        if (!color)
        {
            return unexpected(std::format("token \"{}\" must be a colour like \"#RRGGBB\"", member.key));
        }
        theme.m_colors[index] = *color;
        seen[index] = true;
    }
    for (usize i = 0; i < kThemeTokenCount; ++i)
    {
        if (!seen[i])
        {
            return unexpected(std::format("token \"{}\" is missing", kTokenNames[i]));
        }
    }
    return theme;
}

Theme Theme::withAccent(AccentPreset preset) const
{
    if (preset == AccentPreset::Azure || m_highContrast)
    {
        return *this;
    }
    Theme theme = *this;
    const Vec4 panel = color(ThemeToken::BgPanel);
    const bool dark = relativeLuminance(panel) < 0.2f;
    const AccentColors colors = presetColors(preset, dark);
    theme.m_colors[static_cast<usize>(ThemeToken::Accent)] = colors.accent;
    theme.m_colors[static_cast<usize>(ThemeToken::AccentFill)] = colors.fill;
    theme.m_colors[static_cast<usize>(ThemeToken::AccentSubtle)] =
        dark ? mix(panel, colors.accent, 0.18f) : mix(Vec4(1.0f), colors.accent, 0.14f);
    return theme;
}

void Theme::apply(ImGuiStyle& style, f32 scale) const
{
    // Metrics on the 4 px grid (see the editor architecture page), then scaled once.
    ImGuiStyle metrics;
    metrics.WindowPadding = ImVec2(10.0f, 10.0f);
    metrics.FramePadding = ImVec2(8.0f, 5.0f);
    metrics.ItemSpacing = ImVec2(8.0f, 6.0f);
    metrics.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    metrics.CellPadding = ImVec2(6.0f, 3.0f);
    metrics.IndentSpacing = 14.0f;
    metrics.ScrollbarSize = 12.0f;
    metrics.WindowRounding = 6.0f;
    metrics.ChildRounding = 6.0f;
    metrics.PopupRounding = 6.0f;
    metrics.FrameRounding = 4.0f;
    metrics.TabRounding = 5.0f;
    metrics.GrabRounding = 4.0f;
    metrics.ScrollbarRounding = 9.0f;
    metrics.WindowBorderSize = 0.0f;
    metrics.ChildBorderSize = 0.0f;
    metrics.PopupBorderSize = 1.0f;
    metrics.FrameBorderSize = m_highContrast ? 1.0f : 0.0f;
    metrics.TabBorderSize = 0.0f;
    metrics.TabBarBorderSize = 1.0f;
    metrics.TabBarOverlineSize = 2.0f;
    metrics.DockingSeparatorSize = 2.0f;
    metrics.WindowMenuButtonPosition = ImGuiDir_None;
    metrics.GrabMinSize = 12.0f;
    metrics.ScaleAllSizes(scale);
    metrics.FontSizeBase = style.FontSizeBase;
    metrics.FontScaleMain = style.FontScaleMain;
    metrics.FontScaleDpi = style.FontScaleDpi;

    ImVec4* c = metrics.Colors;
    const auto token = [this](ThemeToken t) {
        return toImGui(color(t));
    };
    const Vec4 transparent(0.0f);
    c[ImGuiCol_Text] = token(ThemeToken::TextPrimary);
    c[ImGuiCol_TextDisabled] = token(ThemeToken::TextDisabled);
    c[ImGuiCol_WindowBg] = token(ThemeToken::BgPanel);
    c[ImGuiCol_ChildBg] = toImGui(transparent);
    c[ImGuiCol_PopupBg] = token(ThemeToken::BgRaised);
    c[ImGuiCol_Border] = token(ThemeToken::Border);
    c[ImGuiCol_BorderShadow] = toImGui(transparent);
    c[ImGuiCol_FrameBg] = token(ThemeToken::BgInput);
    c[ImGuiCol_FrameBgHovered] = token(ThemeToken::BgHover);
    c[ImGuiCol_FrameBgActive] = token(ThemeToken::BgActive);
    c[ImGuiCol_TitleBg] = token(ThemeToken::BgBase);
    c[ImGuiCol_TitleBgActive] = token(ThemeToken::BgPanel);
    c[ImGuiCol_TitleBgCollapsed] = token(ThemeToken::BgBase);
    c[ImGuiCol_MenuBarBg] = token(ThemeToken::BgBase);
    c[ImGuiCol_ScrollbarBg] = toImGui(transparent);
    c[ImGuiCol_ScrollbarGrab] = token(ThemeToken::BgActive);
    c[ImGuiCol_ScrollbarGrabHovered] = token(ThemeToken::TextDisabled);
    c[ImGuiCol_ScrollbarGrabActive] = token(ThemeToken::TextSecondary);
    c[ImGuiCol_CheckMark] = token(ThemeToken::AccentText);
    c[ImGuiCol_CheckboxSelectedBg] = token(ThemeToken::AccentFill);
    c[ImGuiCol_SliderGrab] = token(ThemeToken::Accent);
    c[ImGuiCol_SliderGrabActive] = token(ThemeToken::AccentFill);
    c[ImGuiCol_Button] = token(ThemeToken::BgRaised);
    c[ImGuiCol_ButtonHovered] = token(ThemeToken::BgHover);
    c[ImGuiCol_ButtonActive] = token(ThemeToken::BgActive);
    c[ImGuiCol_Header] = token(ThemeToken::AccentSubtle);
    c[ImGuiCol_HeaderHovered] = token(ThemeToken::BgHover);
    c[ImGuiCol_HeaderActive] = token(ThemeToken::AccentSubtle);
    c[ImGuiCol_Separator] = token(ThemeToken::Separator);
    c[ImGuiCol_SeparatorHovered] = token(ThemeToken::Accent);
    c[ImGuiCol_SeparatorActive] = token(ThemeToken::Accent);
    c[ImGuiCol_ResizeGrip] = toImGui(transparent);
    c[ImGuiCol_ResizeGripHovered] = token(ThemeToken::Accent);
    c[ImGuiCol_ResizeGripActive] = token(ThemeToken::Accent);
    c[ImGuiCol_InputTextCursor] = token(ThemeToken::TextPrimary);
    c[ImGuiCol_Tab] = token(ThemeToken::BgBase);
    c[ImGuiCol_TabHovered] = token(ThemeToken::BgHover);
    c[ImGuiCol_TabSelected] = token(ThemeToken::BgPanel);
    c[ImGuiCol_TabSelectedOverline] = token(ThemeToken::Accent);
    c[ImGuiCol_TabDimmed] = token(ThemeToken::BgBase);
    c[ImGuiCol_TabDimmedSelected] = token(ThemeToken::BgPanel);
    c[ImGuiCol_TabDimmedSelectedOverline] = token(ThemeToken::Separator);
    c[ImGuiCol_DockingPreview] = withAlpha(color(ThemeToken::Accent), 0.4f);
    c[ImGuiCol_DockingEmptyBg] = token(ThemeToken::BgBase);
    c[ImGuiCol_PlotLines] = token(ThemeToken::Accent);
    c[ImGuiCol_PlotLinesHovered] = token(ThemeToken::Warning);
    c[ImGuiCol_PlotHistogram] = token(ThemeToken::Accent);
    c[ImGuiCol_PlotHistogramHovered] = token(ThemeToken::Warning);
    c[ImGuiCol_TableHeaderBg] = token(ThemeToken::BgRaised);
    c[ImGuiCol_TableBorderStrong] = token(ThemeToken::Border);
    c[ImGuiCol_TableBorderLight] = token(ThemeToken::Separator);
    c[ImGuiCol_TableRowBg] = toImGui(transparent);
    c[ImGuiCol_TableRowBgAlt] = withAlpha(color(ThemeToken::BgRaised), 0.45f);
    c[ImGuiCol_TextLink] = token(ThemeToken::Accent);
    c[ImGuiCol_TextSelectedBg] = withAlpha(color(ThemeToken::Accent), 0.35f);
    c[ImGuiCol_TreeLines] = token(ThemeToken::Separator);
    c[ImGuiCol_DragDropTarget] = token(ThemeToken::Accent);
    c[ImGuiCol_DragDropTargetBg] = withAlpha(color(ThemeToken::Accent), 0.12f);
    c[ImGuiCol_UnsavedMarker] = token(ThemeToken::TextPrimary);
    c[ImGuiCol_NavCursor] = token(ThemeToken::Accent);
    c[ImGuiCol_NavWindowingHighlight] = token(ThemeToken::Accent);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    style = metrics;
}

} // namespace illusion::editor::ui
