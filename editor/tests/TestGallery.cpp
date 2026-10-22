#include "EditorTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/ui/UiDriver.hpp>

#include <doctest/doctest.h>

#include <imgui.h>
#include <set>
#include <string>

using namespace illusion;
using namespace illusion::editor;

namespace
{

std::set<std::string> stableIds(const ui::UiItemRegistry& registry)
{
    std::set<std::string> ids;
    for (const ui::UiItem& item : registry.items())
    {
        if (!item.label.empty())
        {
            ids.emplace(item.stableId());
        }
    }
    return ids;
}

} // namespace

TEST_CASE("the driver finds gallery items by stable id and clicks them")
{
    test::GalleryFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    gallery::Gallery::State& state = fixture.galleryPage->state();

    REQUIRE(driver.find("gallery.save").has_value());
    CHECK(driver.find("gallery.save")->label == "Save###gallery.save");
    CHECK_FALSE(driver.find("gallery.no_such_item").has_value());
    CHECK_FALSE(driver.click("gallery.no_such_item"));

    CHECK(driver.click("gallery.save"));
    CHECK(state.clicks == 1);

    CHECK(state.snap);
    CHECK(driver.click("gallery.snap"));
    CHECK_FALSE(state.snap);

    CHECK(driver.click("gallery.space.1"));
    CHECK(state.space == 1);
    CHECK(driver.click("gallery.tile2"));
    CHECK(state.selectedTile == 2);
    CHECK(driver.click("gallery.tool2"));
    CHECK(state.tool == 2);

    // The card's enable toggle sits on top of its header.
    CHECK(state.meshEnabled);
    CHECK(driver.click("gallery.mesh.enabled"));
    CHECK_FALSE(state.meshEnabled);
    CHECK(driver.find("gallery.mesh").has_value());
}

TEST_CASE("text typed into the search field arrives and the clear button empties it")
{
    test::GalleryFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    gallery::Gallery::State& state = fixture.galleryPage->state();

    CHECK_FALSE(driver.find("gallery.search.clear").has_value());
    REQUIRE(driver.click("gallery.search"));
    driver.typeText("crate ğ");
    driver.runFrames(1);
    CHECK(std::string_view(state.search.data()) == "crate ğ");
    REQUIRE(driver.find("gallery.search.clear").has_value());
    CHECK(driver.click("gallery.search.clear"));
    CHECK(state.search[0] == '\0');
}

TEST_CASE("dragging a vector field changes only its component")
{
    test::GalleryFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    gallery::Gallery::State& state = fixture.galleryPage->state();
    const Vec3 before = state.position;
    CHECK(driver.drag("gallery.position.y", Vec2(40.0f, 0.0f)));
    CHECK(state.position.y > before.y);
    CHECK(state.position.x == before.x);
    CHECK(state.position.z == before.z);
}

TEST_CASE("a toast shows its action, reports a click and goes away")
{
    test::GalleryFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(2);
    CHECK(driver.click("gallery.toast"));
    driver.runFrames(12);
    const auto action = driver.find("toast.0.action");
    REQUIRE(action.has_value());
    CHECK(action->label == "Undo###toast.0.action");
    CHECK(driver.click("toast.0.action"));
    CHECK(fixture.galleryPage->undoCount() == 1);
    driver.runFrames(2);
    CHECK_FALSE(driver.find("toast.0.action").has_value());
}

TEST_CASE("the pseudo-locale keeps every id and the saved settings")
{
    test::GalleryFixture fixture;
    ui::UiDriver driver(fixture.context, [&] {
        fixture.headless->beginFrame(test::kFrameSize);
        fixture.galleryPage->draw(fixture.time);
        // A window and a table that save their state, as panels do.
        ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(fixture.galleryPage->text().label(strings::kPanelOutliner, "panel.outliner")))
        {
            if (ImGui::BeginTable("###panel.outliner.table", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable))
            {
                ImGui::TableSetupColumn(fixture.galleryPage->text().label(strings::kPropertyName, "name"));
                ImGui::TableSetupColumn(fixture.galleryPage->text().label(strings::kPropertyMesh, "mesh"));
                ImGui::TableHeadersRow();
                ImGui::EndTable();
            }
        }
        ImGui::End();
        fixture.headless->endFrame();
    });

    for (const gallery::GalleryPage page :
         {gallery::GalleryPage::Widgets, gallery::GalleryPage::Typography, gallery::GalleryPage::Editor})
    {
        fixture.galleryPage->setPage(page);
        fixture.setLocale("en");
        driver.runFrames(3);
        const std::set<std::string> english = stableIds(driver.registry());
        const std::string englishSettings = fixture.context.saveSettings();

        fixture.setLocale("en-XA");
        driver.runFrames(3);
        const std::set<std::string> pseudo = stableIds(driver.registry());
        CHECK(english == pseudo);
        CHECK(fixture.context.saveSettings() == englishSettings);
        // Dear ImGui saves windows under the part of their title from "###" on.
        CHECK(englishSettings.find("[Window][panel.outliner]") != std::string::npos);
    }
    REQUIRE(driver.find("gallery.tab.widgets").has_value());
    CHECK(driver.find("gallery.tab.widgets")->label != "Widgets###gallery.tab.widgets");
}

TEST_CASE("an idle gallery frame does not allocate")
{
    for (const gallery::GalleryPage page :
         {gallery::GalleryPage::Widgets, gallery::GalleryPage::Typography, gallery::GalleryPage::Editor})
    {
        test::GalleryFixture fixture;
        fixture.galleryPage->setPage(page);
        fixture.galleryPage->showToast(0.0);
        // Warm up: windows, tables, glyphs and buffers are created in the first frames.
        for (int i = 0; i < 30; ++i)
        {
            fixture.frame();
        }
        const u64 imguiBefore = ui::UiContext::allocationCount();
        test::ScopedAllocationCounter allocations;
        for (int i = 0; i < 10; ++i)
        {
            fixture.frame();
        }
        INFO("page " << static_cast<int>(page));
        CHECK(allocations.count() == 0);
        CHECK(ui::UiContext::allocationCount() == imguiBefore);
    }
}

TEST_CASE("the headless renderer creates the font atlas texture on demand")
{
    test::GalleryFixture fixture;
    CHECK(fixture.headless->textureCount() == 0);
    fixture.frame();
    CHECK(fixture.headless->textureCount() >= 1);
}
