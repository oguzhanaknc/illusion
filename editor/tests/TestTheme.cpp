#include "EditorTestSupport.hpp"

#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/Theme.hpp>

#include <doctest/doctest.h>

#include <format>
#include <imgui.h>
#include <string>

using namespace illusion;
using namespace illusion::editor;
using ui::ThemeToken;

namespace
{

// A complete theme file with every token set to @p color, optionally with one token changed.
std::string themeJson(std::string_view color, std::string_view overrideKey = {},
                      std::string_view overrideValue = {})
{
    std::string tokens;
    for (usize i = 0; i < ui::kThemeTokenCount; ++i)
    {
        const std::string_view name = ui::themeTokenName(static_cast<ThemeToken>(i));
        tokens += std::format("{}\"{}\": \"{}\"", i == 0 ? "" : ", ", name,
                              name == overrideKey ? overrideValue : color);
    }
    return std::format(
        R"json({{"format": "illusion.theme", "version": 1, "name": "Test", "tokens": {{{}}}}})json", tokens);
}

void checkContrast(const ui::Theme& theme, ThemeToken foreground, ThemeToken background, f32 minimum)
{
    const f32 ratio = ui::contrastRatio(theme.color(foreground), theme.color(background));
    INFO(theme.name() << ": " << ui::themeTokenName(foreground) << " on " << ui::themeTokenName(background)
                      << " is " << ratio << ":1, needs " << minimum << ":1");
    CHECK(ratio >= minimum);
}

// Text >= 4.5:1 (7:1 for high contrast), graphics >= 3:1, on every surface text is drawn on.
void checkThemeContrast(const ui::Theme& theme)
{
    const f32 text = theme.isHighContrast() ? 7.0f : 4.5f;
    const f32 graphics = 3.0f;
    for (const ThemeToken surface : {ThemeToken::BgBase, ThemeToken::BgPanel, ThemeToken::BgRaised,
                                     ThemeToken::BgInput, ThemeToken::BgHover})
    {
        checkContrast(theme, ThemeToken::TextPrimary, surface, text);
        checkContrast(theme, ThemeToken::TextSecondary, surface, text);
    }
    checkContrast(theme, ThemeToken::TextPrimary, ThemeToken::AccentSubtle, text);
    checkContrast(theme, ThemeToken::TextPrimary, ThemeToken::BgActive, theme.isHighContrast() ? text : 4.5f);
    checkContrast(theme, ThemeToken::AccentText, ThemeToken::AccentFill, text);
    for (const ThemeToken graphic :
         {ThemeToken::Accent, ThemeToken::Success, ThemeToken::Warning, ThemeToken::Error, ThemeToken::Info,
          ThemeToken::PlayTint, ThemeToken::AxisX, ThemeToken::AxisY, ThemeToken::AxisZ, ThemeToken::AxisCbX,
          ThemeToken::AxisCbY, ThemeToken::AxisCbZ})
    {
        checkContrast(theme, graphic, ThemeToken::BgPanel, graphics);
    }
}

} // namespace

TEST_CASE("hex colours parse with and without alpha and reject malformed text")
{
    const auto opaque = ui::parseHexColor("#FF8000");
    REQUIRE(opaque.has_value());
    CHECK(opaque->r == doctest::Approx(1.0f));
    CHECK(opaque->g == doctest::Approx(128.0f / 255.0f));
    CHECK(opaque->b == doctest::Approx(0.0f));
    CHECK(opaque->a == doctest::Approx(1.0f));
    const auto translucent = ui::parseHexColor("#00000080");
    REQUIRE(translucent.has_value());
    CHECK(translucent->a == doctest::Approx(128.0f / 255.0f));
    CHECK(ui::parseHexColor("#abcdef").has_value());
    for (const std::string_view bad :
         {"", "#", "FF8000", "#FF800", "#FF80000", "#GG0000", "#FF8000 ", "#ff80zz"})
    {
        CHECK_FALSE(ui::parseHexColor(bad).has_value());
    }
}

TEST_CASE("contrast ratios follow the WCAG formula")
{
    const Vec4 black(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 white(1.0f);
    CHECK(ui::relativeLuminance(black) == doctest::Approx(0.0f));
    CHECK(ui::relativeLuminance(white) == doctest::Approx(1.0f));
    CHECK(ui::contrastRatio(white, black) == doctest::Approx(21.0f));
    CHECK(ui::contrastRatio(black, white) == doctest::Approx(21.0f));
    CHECK(ui::contrastRatio(white, white) == doctest::Approx(1.0f));
    // The worked example from the design table: #E3E6EB on #16191F is about 14.1:1.
    CHECK(ui::contrastRatio(*ui::parseHexColor("#E3E6EB"), *ui::parseHexColor("#16191F")) ==
          doctest::Approx(14.1f).epsilon(0.01));
}

TEST_CASE("every token has a unique dotted name")
{
    for (usize i = 0; i < ui::kThemeTokenCount; ++i)
    {
        const std::string_view name = ui::themeTokenName(static_cast<ThemeToken>(i));
        CHECK_FALSE(name.empty());
        for (usize j = i + 1; j < ui::kThemeTokenCount; ++j)
        {
            CHECK(name != ui::themeTokenName(static_cast<ThemeToken>(j)));
        }
    }
    CHECK(ui::themeTokenName(ThemeToken::BgBase) == "bg.base");
    CHECK(ui::themeTokenName(ThemeToken::AxisCbZ) == "axis.cb.z");
}

TEST_CASE("a theme file needs every token and only known tokens")
{
    const auto complete = ui::Theme::parse(themeJson("#102030"));
    REQUIRE(complete.hasValue());
    CHECK(complete.value().name() == "Test");
    CHECK_FALSE(complete.value().isHighContrast());
    CHECK(complete.value().color(ThemeToken::AxisZ).b == doctest::Approx(48.0f / 255.0f));

    const auto badColor = ui::Theme::parse(themeJson("#102030", "accent", "blue"));
    REQUIRE_FALSE(badColor.hasValue());
    CHECK(badColor.error().find("accent") != std::string::npos);

    std::string missing = themeJson("#102030");
    const usize position = missing.find(", \"axis.cb.z\": \"#102030\"");
    REQUIRE(position != std::string::npos);
    missing.erase(position, std::string_view(", \"axis.cb.z\": \"#102030\"").size());
    const auto withoutToken = ui::Theme::parse(missing);
    REQUIRE_FALSE(withoutToken.hasValue());
    CHECK(withoutToken.error().find("axis.cb.z") != std::string::npos);

    std::string unknown = themeJson("#102030");
    unknown.insert(unknown.find("\"bg.base\""), "\"bg.sparkle\": \"#000000\", ");
    const auto withUnknown = ui::Theme::parse(unknown);
    REQUIRE_FALSE(withUnknown.hasValue());
    CHECK(withUnknown.error().find("bg.sparkle") != std::string::npos);

    CHECK_FALSE(ui::Theme::parse("{}").hasValue());
    CHECK_FALSE(ui::Theme::parse("not json").hasValue());
    CHECK_FALSE(ui::Theme::parse(R"({"format": "illusion.theme", "version": 2, "name": "x", "tokens": {}})")
                    .hasValue());
}

TEST_CASE("the shipped themes meet the contrast requirements")
{
    VirtualFileSystem vfs;
    for (const std::string_view name : ui::kThemeNames)
    {
        const ui::Theme theme = test::loadTestTheme(vfs, name);
        CHECK(theme.isHighContrast() == (name == "high-contrast"));
        checkThemeContrast(theme);
    }
}

TEST_CASE("accent presets keep the contrast of accents and their text")
{
    VirtualFileSystem vfs;
    for (const std::string_view name : {"dark", "light"})
    {
        const ui::Theme theme = test::loadTestTheme(vfs, name);
        for (const ui::AccentPreset preset : {ui::AccentPreset::Azure, ui::AccentPreset::Violet,
                                              ui::AccentPreset::Teal, ui::AccentPreset::Amber})
        {
            const ui::Theme accented = theme.withAccent(preset);
            checkContrast(accented, ThemeToken::AccentText, ThemeToken::AccentFill, 4.5f);
            checkContrast(accented, ThemeToken::Accent, ThemeToken::BgPanel, 3.0f);
            checkContrast(accented, ThemeToken::TextPrimary, ThemeToken::AccentSubtle, 4.5f);
        }
        const ui::Theme violet = theme.withAccent(ui::AccentPreset::Violet);
        CHECK(violet.color(ThemeToken::Accent) != theme.color(ThemeToken::Accent));
        CHECK(violet.color(ThemeToken::TextPrimary) == theme.color(ThemeToken::TextPrimary));
    }
    const ui::Theme highContrast = test::loadTestTheme(vfs, "high-contrast");
    CHECK(highContrast.withAccent(ui::AccentPreset::Teal).color(ThemeToken::Accent) ==
          highContrast.color(ThemeToken::Accent));
}

TEST_CASE("applying a theme rebuilds the style from the tokens without accumulating scale")
{
    VirtualFileSystem vfs;
    const ui::Theme theme = test::loadTestTheme(vfs, "dark");
    ImGuiStyle style;
    style.FontSizeBase = 15.0f;
    style.FontScaleDpi = 1.5f;
    theme.apply(style, 2.0f);
    CHECK(style.WindowPadding.x == doctest::Approx(20.0f));
    CHECK(style.FrameRounding == doctest::Approx(8.0f));
    CHECK(style.FontSizeBase == doctest::Approx(15.0f));
    CHECK(style.FontScaleDpi == doctest::Approx(1.5f));
    theme.apply(style, 2.0f);
    CHECK(style.WindowPadding.x == doctest::Approx(20.0f));
    theme.apply(style, 1.0f);
    CHECK(style.ItemSpacing.x == doctest::Approx(8.0f));
    CHECK(style.DockingSeparatorSize == doctest::Approx(2.0f));

    const Vec4 text = theme.color(ThemeToken::TextPrimary);
    CHECK(style.Colors[ImGuiCol_Text].x == doctest::Approx(text.r));
    CHECK(style.Colors[ImGuiCol_Text].y == doctest::Approx(text.g));
    const Vec4 panel = theme.color(ThemeToken::BgPanel);
    CHECK(style.Colors[ImGuiCol_WindowBg].z == doctest::Approx(panel.b));
    const Vec4 accent = theme.color(ThemeToken::Accent);
    CHECK(style.Colors[ImGuiCol_TabSelectedOverline].x == doctest::Approx(accent.r));
}
