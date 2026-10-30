#include "EditorTestSupport.hpp"

#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/text/Utf8.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <imgui.h>
#include <string>

using namespace illusion;
using namespace illusion::editor;

namespace
{

// Every code point of @p text that @p font has no glyph for.
std::u32string missingGlyphs(ImFont* font, std::string_view text)
{
    std::u32string missing;
    for (const char32_t c : text::Utf8View(text))
    {
        if (c >= 0x20 && !font->IsGlyphInFont(static_cast<ImWchar>(c)) &&
            missing.find(c) == std::u32string::npos)
        {
            missing.push_back(c);
        }
    }
    return missing;
}

} // namespace

TEST_CASE("the icon table is sorted, unique and matches the generated constants")
{
    const std::span<const ui::IconEntry> icons = ui::allIcons();
    REQUIRE(icons.size() > 4000);
    for (usize i = 1; i < icons.size(); ++i)
    {
        CHECK(icons[i - 1].name < icons[i].name);
    }
    const auto tree = ui::findIcon("account_tree");
    REQUIRE(tree.has_value());
    CHECK(tree->codepoint == ui::icons::kAccountTree.codepoint);
    CHECK(tree->view() == ui::icons::kAccountTree.view());
    CHECK(ui::findIcon("10k").has_value());
    CHECK(ui::findIcon("zoom_out_map").has_value());
    CHECK_FALSE(ui::findIcon("").has_value());
    CHECK_FALSE(ui::findIcon("account_tre").has_value());
    CHECK_FALSE(ui::findIcon("no_such_icon").has_value());
}

TEST_CASE("icon constants hold the UTF-8 encoding of their code point")
{
    for (const ui::IconEntry& entry : ui::allIcons())
    {
        const text::Utf8Decoded decoded = text::decodeUtf8(entry.icon.view(), 0);
        INFO(entry.name);
        CHECK(decoded.codePoint == entry.icon.codepoint);
        CHECK(decoded.length == entry.icon.view().size());
    }
}

TEST_CASE("every icon name resolves to a glyph of the icon font")
{
    test::GalleryFixture fixture;
    ImFont* iconFont = fixture.fonts->font(ui::FontRole::Icon);
    ImFont* body = fixture.fonts->font(ui::FontRole::Body);
    usize missing = 0;
    for (const ui::IconEntry& entry : ui::allIcons())
    {
        if (!iconFont->IsGlyphInFont(static_cast<ImWchar>(entry.icon.codepoint)))
        {
            ++missing;
            MESSAGE("no glyph for icon '" << entry.name << "'");
        }
    }
    CHECK(missing == 0);
    // Icons are merged into the text fonts too.
    CHECK(body->IsGlyphInFont(static_cast<ImWchar>(ui::icons::kAccountTree.codepoint)));
    CHECK(fixture.fonts->font(ui::FontRole::Mono)
              ->IsGlyphInFont(static_cast<ImWchar>(ui::icons::kTerminal.codepoint)));
}

// Sample values of every argument the editor strings use.
const text::MessageArgument kSampleArguments[] = {
    {"action", "Undo"},    {"client", "claude"},     {"component", "Collider"},
    {"count", 3},          {"era", "2025-06-18"},    {"fps", 144},
    {"height", 360},       {"message", "no"},        {"method", "scene.save"},
    {"number", "2"},       {"width", 640},           {"milestone", "M7m"},
    {"ms", 16.7},          {"path", "game:/a.glb"},  {"permission", "files"},
    {"revision", 42},      {"title", "Save"},        {"version", "0.7.0"},
    {"field", "target"},   {"visible", 12},          {"candidates", 40},
    {"distance", "2.500"}, {"x", "1.500"},           {"y", "-2.000"},
    {"z", "0.000"},        {"elevation", "-53.130"},
};

TEST_CASE("every character of the editor strings in English and en-XA has a glyph in every text font")
{
    test::GalleryFixture fixture;
    const text::StringTableData& table = strings::kTable;
    for (const char* locale : {"en", "en-XA"})
    {
        fixture.setLocale(locale);
        for (u32 index = 0; index < static_cast<u32>(table.keys.size()); ++index)
        {
            std::string formatted;
            text::TextOutput out(formatted);
            const text::TextKey key{&table, index};
            INFO(locale << " " << key.name());
            REQUIRE(fixture.localization.format(key, kSampleArguments, out).hasValue());
            for (const ui::FontRole role : {ui::FontRole::Body, ui::FontRole::Strong, ui::FontRole::Title})
            {
                const std::u32string missing = missingGlyphs(fixture.fonts->font(role), formatted);
                INFO(locale << " " << key.name() << ": " << formatted);
                CHECK(missing.empty());
            }
        }
    }
    // Turkish and other Latin text used in sample data.
    CHECK(missingGlyphs(fixture.fonts->font(ui::FontRole::Body), "ğüşıöçĞÜŞİÖÇ äöüß éèêñ ł").empty());
    CHECK(missingGlyphs(fixture.fonts->font(ui::FontRole::Mono), "ğüşıöç {}[]<>").empty());
}

TEST_CASE("font roles have the sizes of the design and scale through FontScaleDpi")
{
    test::GalleryFixture fixture;
    CHECK(ui::FontManager::size(ui::FontRole::Body) == doctest::Approx(15.0f));
    CHECK(ui::FontManager::size(ui::FontRole::Caption) == doctest::Approx(13.0f));
    CHECK(ui::FontManager::size(ui::FontRole::Title) == doctest::Approx(17.0f));
    CHECK(ui::FontManager::size(ui::FontRole::Mono) == doctest::Approx(14.0f));
    CHECK(fixture.fonts->font(ui::FontRole::Caption) == fixture.fonts->font(ui::FontRole::Body));
    CHECK(ImGui::GetIO().FontDefault == fixture.fonts->font(ui::FontRole::Body));

    fixture.fonts->setScale(1.5f, 2.0f);
    CHECK(ImGui::GetStyle().FontScaleDpi == doctest::Approx(3.0f));
    fixture.headless->beginFrame(test::kFrameSize);
    CHECK(ImGui::GetFontSize() == doctest::Approx(45.0f));
    {
        ui::FontScope mono(*fixture.fonts, ui::FontRole::Mono);
        CHECK(ImGui::GetFont() == fixture.fonts->font(ui::FontRole::Mono));
        CHECK(ImGui::GetFontSize() == doctest::Approx(42.0f));
    }
    CHECK(ImGui::GetFont() == fixture.fonts->font(ui::FontRole::Body));
    fixture.headless->endFrame();
}

TEST_CASE("loading fonts from a directory without them fails with the missing path")
{
    VirtualFileSystem vfs;
    REQUIRE(ui::mountEditorData(vfs));
    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, vfs, "editor:/themes");
    REQUIRE_FALSE(fonts.hasValue());
    CHECK(fonts.error().find("Roboto-Regular.ttf") != std::string::npos);
}
