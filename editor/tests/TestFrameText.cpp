#include "TestSupport.hpp"

#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/ui/FrameText.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/text/Localization.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;

TEST_CASE("labels carry the localized text and the stable id after ###")
{
    text::Localization localization;
    localization.addTable(strings::kTable);
    ui::FrameText text(localization);
    CHECK(std::string_view(text.label(strings::kCommonSave, "menu.file.save")) == "Save###menu.file.save");
    CHECK(std::string_view(text.format(strings::kCommonSave)) == "Save");
    CHECK(std::string_view(text.format(strings::kStatusSelection, {{"count", 2}})) == "2 entities selected");
    CHECK(std::string_view(text.label(strings::kStatusWarnings, "status.warnings", {{"count", 1}})) ==
          "1 warning###status.warnings");
    CHECK(std::string_view(text.iconLabel(ui::icons::kAdd, strings::kGalleryButtonCreateEntity, "create")) ==
          std::string(ui::icons::kAdd.view()) + " Create entity###create");
    CHECK(std::string_view(text.iconOnly(ui::icons::kClose, "close")) ==
          std::string(ui::icons::kClose.view()) + "###close");
    CHECK(std::string_view(text.verbatim("crate.glb", "assets.tile0")) == "crate.glb###assets.tile0");
    CHECK(std::string_view(text.hidden("value")) == "###value");
    CHECK(std::string_view(text.copy("plain")) == "plain");
    CHECK(std::string_view(text.concat("inspector.position", ".x")) == "inspector.position.x");
}

TEST_CASE("the pseudo-locale changes the text but never the id")
{
    text::Localization localization;
    localization.addTable(strings::kTable);
    localization.setLocale(text::pseudoLocale());
    localization.applyPendingLocale();
    ui::FrameText text(localization);
    const std::string_view label = text.label(strings::kCommonSave, "menu.file.save");
    CHECK(label.ends_with("###menu.file.save"));
    CHECK(label.substr(0, label.find("###")) != "Save");
}

TEST_CASE("text stays valid until reset, also when the arena has to grow")
{
    text::Localization localization;
    localization.addTable(strings::kTable);
    ui::FrameText text(localization, 32);
    std::vector<const char*> labels;
    std::vector<std::string> expected;
    labels.reserve(64);
    expected.reserve(64);
    for (int i = 0; i < 64; ++i)
    {
        const std::string id = "item." + std::to_string(i);
        labels.push_back(text.label(strings::kCommonUndo, id));
        expected.push_back("Undo###" + id);
    }
    for (usize i = 0; i < labels.size(); ++i)
    {
        CHECK(std::string_view(labels[i]) == expected[i]);
    }
    const usize used = text.used();
    CHECK(used >= 64 * 12);

    // After the reset one arena holds a whole frame, so the same frame no longer allocates.
    text.reset();
    CHECK(text.used() == 0);
    {
        test::ScopedAllocationCounter allocations;
        for (int i = 0; i < 64; ++i)
        {
            (void)text.label(strings::kCommonUndo, "item.00");
        }
        CHECK(allocations.count() == 0);
    }
    text.reset();
}

TEST_CASE("a frame of text does not allocate once the arena is large enough")
{
    text::Localization localization;
    localization.addTable(strings::kTable);
    ui::FrameText text(localization);
    (void)text.format(strings::kStatusSelection, {{"count", 3}});
    text.reset();
    test::ScopedAllocationCounter allocations;
    for (int frame = 0; frame < 3; ++frame)
    {
        (void)text.label(strings::kCommonSave, "save");
        (void)text.format(strings::kStatusSelection, {{"count", frame}});
        (void)text.iconOnly(ui::icons::kPlayArrow, "play");
        text.reset();
    }
    CHECK(allocations.count() == 0);
}
