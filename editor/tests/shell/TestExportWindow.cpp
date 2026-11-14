// The Export window (M8-5): the export settings through `project.settings_set`, and `export.build` with its
// progress, result and diagnostics, on the stand-in SDK of the export tests (FakeCMake.cpp and this build's
// illusion_pack).

#include "shell/ShellTestSupport.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <sstream>
#include <string>

using namespace illusion;
using namespace illusion::editor;
using test::ShellFixture;
using test::ShellSetup;

namespace
{

#if defined(_WIN32)
constexpr std::string_view kPlatformName = "windows";
#elif defined(__APPLE__)
constexpr std::string_view kPlatformName = "macos";
#else
constexpr std::string_view kPlatformName = "linux";
#endif

bool panelOpen(EditorContext& editor, std::string_view id)
{
    const JsonDocument state = test::ok(editor, "ui.state");
    for (const JsonValue panel : state.root().find("panels").elements())
    {
        if (panel.find("id").asString() == std::optional<std::string_view>(id))
        {
            return panel.find("open").asBool().value_or(false);
        }
    }
    return false;
}

void showExportWindow(ShellFixture& fixture, ui::UiDriver& driver)
{
    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"window.export"})");
    driver.runFrames(3);
    REQUIRE(driver.find("export.start").has_value());
}

// The value of the field @p name of the project's export settings, as JSON.
std::string exportSetting(EditorContext& editor, std::string_view name)
{
    const JsonDocument result =
        test::ok(editor, "project.settings_get", R"({"type":"illusion.ExportSettings"})");
    return result.root().pointer("/settings/0/value").find(name).dump();
}

// Replaces the text of the field @p id with @p value and presses Enter.
void typeInto(ui::UiDriver& driver, std::string_view id, std::string_view value)
{
    REQUIRE(driver.click(id));
    driver.pressKey(ImGuiKey_A, ui::UiDriver::shortcutModifier());
    driver.typeText(value);
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(2);
}

// The label of the item @p id in the last frame ("" if there is none).
std::string labelOf(const ui::UiDriver& driver, std::string_view id)
{
    const std::optional<ui::UiItem> item = driver.find(id);
    return item ? item->label : std::string();
}

// Clicks Export and runs frames until the export ended; false if it did not show its progress first.
bool runExport(ui::UiDriver& driver)
{
    REQUIRE(driver.click("export.start"));
    bool progressShown = false;
    // The stand-in tools finish in seconds; the bound only stops a hung export.
    for (int frame = 0; frame < 2000000; ++frame)
    {
        driver.runFrames(1);
        if (driver.find("export.cancel").has_value())
        {
            progressShown = true;
        }
        else if (driver.find("export.start").has_value())
        {
            break;
        }
    }
    return progressShown;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    if (in)
    {
        text << in.rdbuf();
    }
    return text.str();
}

void writeFile(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// The text the recorder of FakeCMake.cpp wrote to @p file once it wrote a whole line (it runs detached).
std::string waitForRecord(const std::filesystem::path& file)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::string text;
    while (text.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline)
    {
        std::ifstream stream(file, std::ios::binary);
        std::ostringstream contents;
        if (stream)
        {
            contents << stream.rdbuf();
        }
        text = contents.str();
    }
    return text;
}

} // namespace

TEST_CASE("the Export window opens from the File menu and from the command palette")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    CHECK_FALSE(panelOpen(*fixture.editor, "window.export"));
    REQUIRE(driver.click("menu.file"));
    driver.runFrames(2);
    REQUIRE(driver.click("menu.file.export"));
    driver.runFrames(3);
    CHECK(panelOpen(*fixture.editor, "window.export"));
    CHECK(driver.find("export.settings.product_name").has_value());
    CHECK(driver.find("export.output").has_value());
    CHECK(driver.find("export.start").has_value());

    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"window.export","open":false})");
    driver.runFrames(2);
    CHECK_FALSE(panelOpen(*fixture.editor, "window.export"));
    driver.pressKey(ImGuiKey_P, ui::UiDriver::shortcutModifier() | ImGuiMod_Shift);
    driver.runFrames(2);
    REQUIRE(fixture.shell->paletteOpen());
    driver.typeText("Export");
    driver.runFrames(2);
    std::string item;
    for (const ui::UiItem& candidate : driver.registry().items())
    {
        if (candidate.stableId().starts_with("palette.item.") &&
            candidate.label.find("Export\xE2\x80\xA6") != std::string::npos)
        {
            item = std::string(candidate.stableId());
        }
    }
    REQUIRE_FALSE(item.empty());
    REQUIRE(driver.click(item));
    driver.runFrames(3);
    CHECK(panelOpen(*fixture.editor, "window.export"));
}

TEST_CASE("an export setting edited in the window is written by project.settings_set and read again")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    showExportWindow(fixture, driver);
    typeInto(driver, "export.settings.product_name", "Space Blocks");
    CHECK(exportSetting(editor, "productName") == R"("Space Blocks")");
    CHECK(readText(fixture.directory.path / "project" / "settings" / "export.json").find("Space Blocks") !=
          std::string::npos);

    // The excluded files are one pattern per line, written when the field loses the keyboard.
    REQUIRE(driver.click("export.settings.exclude"));
    driver.typeText("raw/**");
    driver.pressKey(ImGuiKey_Enter);
    driver.typeText("  *.psd ");
    REQUIRE(driver.click("export.settings.copyright"));
    driver.runFrames(2);
    CHECK(exportSetting(editor, "exclude") == R"(["raw/**","*.psd"])");

    // Reopened, the window shows the stored value: text typed at its end extends it.
    test::ok(editor, "ui.show_panel", R"({"panel":"window.export","open":false})");
    driver.runFrames(2);
    test::ok(editor, "project.settings_set",
             R"({"type":"illusion.ExportSettings","value":{"productName":"Blocks"}})");
    showExportWindow(fixture, driver);
    REQUIRE(driver.click("export.settings.product_name"));
    driver.pressKey(ImGuiKey_End);
    driver.typeText(" 2");
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(2);
    CHECK(exportSetting(editor, "productName") == R"("Blocks 2")");
    CHECK(exportSetting(editor, "exclude") == R"(["raw/**","*.psd"])");

    // Each edit is one undo step.
    test::ok(editor, "history.undo");
    driver.runFrames(2);
    CHECK(exportSetting(editor, "productName") == R"("Blocks")");
}

TEST_CASE("an invalid value shows the command's error next to its field and changes nothing")
{
    ShellFixture fixture(ShellSetup{.exportSdk = true});
    EditorContext& editor = *fixture.editor;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    showExportWindow(fixture, driver);
    typeInto(driver, "export.settings.icon", "icon.png");
    CHECK(labelOf(driver, "export.settings.icon.error").find("is not a valid asset path") !=
          std::string::npos);
    CHECK(exportSetting(editor, "icon") == R"("")");
    // A click on the error gives its field the keyboard again.
    REQUIRE(driver.click("export.settings.icon.error"));
    driver.runFrames(3);
    driver.pressKey(ImGuiKey_A, ui::UiDriver::shortcutModifier());
    driver.typeText("assets:/icons/game.png");
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(2);
    CHECK_FALSE(driver.find("export.settings.icon.error").has_value());
    CHECK(exportSetting(editor, "icon") == R"("assets:/icons/game.png")");

    // An invalid build option of export.build shows under its field, and nothing runs.
    typeInto(driver, "export.configuration", "../x");
    REQUIRE(driver.click("export.start"));
    driver.runFrames(2);
    CHECK(labelOf(driver, "export.configuration.error").find("build configuration name") !=
          std::string::npos);
    CHECK_FALSE(driver.find("export.cancel").has_value());
}

TEST_CASE("an export driven through the window reaches its report and shows the game in the file manager")
{
    ShellFixture fixture(ShellSetup{.exportSdk = true});
    EditorContext& editor = *fixture.editor;
    const std::filesystem::path output = fixture.directory.path / "exported";
    const std::filesystem::path revealed = fixture.directory.path / "revealed.txt";
    Preferences preferences = editor.preferences();
    preferences.fileBrowser =
        "\"" ILLUSION_FAKE_CMAKE "\" --record \"" + revealed.generic_string() + "\" {file}";
    REQUIRE(editor.setPreferences(std::move(preferences)).hasValue());
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    showExportWindow(fixture, driver);
    typeInto(driver, "export.output", output.generic_string());
    CHECK(runExport(driver));
    REQUIRE(driver.find("export.result.show_in_folder").has_value());
    CHECK_FALSE(driver.find("export.diagnostic.0").has_value());
#if defined(__APPLE__)
    CHECK(std::filesystem::is_directory(output / "Test.app"));
#else
    CHECK(std::filesystem::is_directory(output / "Test" / "data"));
#endif

    REQUIRE(driver.click("export.result.show_in_folder"));
    driver.runFrames(1);
    const std::string record = waitForRecord(revealed);
    CHECK(record.find("exported") != std::string::npos);
}

TEST_CASE(
    "a failed export lists its diagnostics and a project file's diagnostic selects it in the Asset Browser")
{
    ShellFixture fixture(ShellSetup{.exportSdk = true});
    EditorContext& editor = *fixture.editor;
    const std::filesystem::path root = fixture.directory.path / "project";
    // The stand-in build prints the ERRORS of every directory of the generated build and fails.
    const std::filesystem::path broken =
        root / ".illusion" / "export" / std::string(kPlatformName) / "broken";
    writeFile(broken / "CMakeLists.txt", "\n");
    writeFile(broken / "ERRORS", (root / "assets" / "scenes" / "main.scene").generic_string() +
                                     ":3:5: error: the scene is broken\n"
                                     "/elsewhere/x.hpp:2:3: note: from here\n");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    showExportWindow(fixture, driver);
    typeInto(driver, "export.output", (fixture.directory.path / "exported").generic_string());
    CHECK(runExport(driver));
    CHECK_FALSE(driver.find("export.result.show_in_folder").has_value());
    CHECK_FALSE(std::filesystem::exists(fixture.directory.path / "exported" / "Test"));
    CHECK(labelOf(driver, "export.diagnostic.0").find("assets:/scenes/main.scene:3") != std::string::npos);
    // The note names a file outside the project: no link.
    CHECK_FALSE(driver.find("export.diagnostic.1").has_value());
    REQUIRE(driver.find("export.result.output").has_value());

    test::ok(editor, "ui.show_panel", R"({"panel":"panel.assets","open":false})");
    driver.runFrames(2);
    REQUIRE(driver.click("export.diagnostic.0"));
    driver.runFrames(3);
    CHECK(panelOpen(editor, "panel.assets"));
    CHECK(labelOf(driver, "asset_browser.tile.0").find("main.scene") != std::string::npos);
}

TEST_CASE("the open Export window with a result does not allocate in idle frames")
{
    // Without an SDK the export fails at once, and the window shows why.
    ShellFixture fixture;
    (void)fixture.editor->assets();
    {
        // The driver's item registry records every item (allocating): it is gone before the count.
        ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
        showExportWindow(fixture, driver);
        REQUIRE(driver.click("export.start"));
        driver.runFrames(2);
        REQUIRE_FALSE(driver.find("export.cancel").has_value());
    }
    for (int i = 0; i < 120; ++i)
    {
        fixture.frame();
    }
    const u64 imguiBefore = ui::UiContext::allocationCount();
    test::ScopedAllocationCounter allocations;
    for (int i = 0; i < 60; ++i)
    {
        fixture.frame();
    }
    CHECK(allocations.count() == 0);
    CHECK(ui::UiContext::allocationCount() == imguiBefore);
}
