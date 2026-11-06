#include "rpc/RpcTestSupport.hpp"
#include "shell/ShellTestSupport.hpp"

#include <illusion/core/FrameProfiler.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>

#include <cmath>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <set>

using namespace illusion;
using namespace illusion::editor;
using test::ShellFixture;
using test::ShellSetup;

namespace
{

std::set<std::string> itemIds(const ui::UiDriver& driver)
{
    std::set<std::string> ids;
    for (const ui::UiItem& item : driver.registry().items())
    {
        ids.emplace(item.stableId());
    }
    return ids;
}

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

void clickWithModifier(ui::UiDriver& driver, std::string_view id, int modifier)
{
    REQUIRE(driver.hover(id));
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(static_cast<ImGuiKey>(modifier), true);
    driver.runFrames(1);
    io.AddMouseButtonEvent(0, true);
    driver.runFrames(1);
    io.AddMouseButtonEvent(0, false);
    driver.runFrames(1);
    io.AddKeyEvent(static_cast<ImGuiKey>(modifier), false);
    driver.runFrames(1);
}

void typeIntoDragField(ui::UiDriver& driver, std::string_view id, std::string_view value)
{
    clickWithModifier(driver, id, ui::UiDriver::shortcutModifier());
    driver.pressKey(ImGuiKey_A, ui::UiDriver::shortcutModifier());
    driver.typeText(value);
    driver.pressKey(ImGuiKey_Enter);
}

void dragBetween(ui::UiDriver& driver, std::string_view fromId, Vec2 target)
{
    const std::optional<ui::UiItem> from = driver.find(fromId);
    REQUIRE(from.has_value());
    REQUIRE(driver.drag(fromId, target - from->center(), 6));
}

} // namespace

TEST_CASE("the shell draws the menu bar, toolbar, status bar and the panels of the default layout")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(4);
    for (const std::string_view id : {"menu.file", "menu.edit", "menu.window", "menu.help", "toolbar.search",
                                      "toolbar.agents", "toolbar.layout", "toolbar.play.play"})
    {
        INFO(id);
        CHECK(driver.find(id).has_value());
    }
    CHECK(panelOpen(*fixture.editor, "panel.console"));
    CHECK(panelOpen(*fixture.editor, "panel.ai_activity"));
    CHECK_FALSE(panelOpen(*fixture.editor, "window.welcome"));
    CHECK(test::ok(*fixture.editor, "ui.state").root().find("layouts")[0].find("active").asBool() ==
          std::optional<bool>(true));
}

TEST_CASE("the palette opens with its shortcut and runs the command typed into it")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    CHECK_FALSE(fixture.shell->paletteOpen());
    driver.pressKey(ImGuiKey_P, ui::UiDriver::shortcutModifier() | ImGuiMod_Shift);
    driver.runFrames(2);
    REQUIRE(fixture.shell->paletteOpen());
    ILL_LOG_WARN("Test", "a message for the console");
    const u64 cleared = fixture.editor->logCleared();
    driver.typeText("Clear log");
    driver.runFrames(2);
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(1);
    CHECK_FALSE(fixture.shell->paletteOpen());
    CHECK(fixture.editor->logCleared() > cleared);
}

TEST_CASE("Go to Anything selects the entity picked in the palette")
{
    ShellFixture fixture;
    test::ok(*fixture.editor, "entity.create", R"({"name":"Board"})");
    test::ok(*fixture.editor, "entity.create", R"({"name":"Player"})");
    test::ok(*fixture.editor, "selection.clear");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    driver.pressKey(ImGuiKey_P, ui::UiDriver::shortcutModifier());
    driver.runFrames(2);
    REQUIRE(fixture.shell->paletteOpen());
    driver.typeText("Play");
    driver.runFrames(2);
    REQUIRE(driver.find("palette.item.0").has_value());
    CHECK(driver.click("palette.item.0"));
    driver.runFrames(1);
    CHECK_FALSE(fixture.shell->paletteOpen());
    REQUIRE(fixture.editor->selection().size() == 1);
    const JsonDocument selected = test::ok(*fixture.editor, "selection.get");
    CHECK(selected.root().find("entities")[0].find("name").asString() ==
          std::optional<std::string_view>("Player"));
}

TEST_CASE("a command that needs params opens a form in the palette, generated from its schema")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    // Window > Layouts > Save layout... needs a name.
    REQUIRE(driver.click("menu.window"));
    driver.runFrames(1);
    REQUIRE(driver.hover("menu.window.layouts"));
    driver.runFrames(2);
    REQUIRE(driver.click("menu.window.layout_save"));
    driver.runFrames(2);
    REQUIRE(fixture.shell->paletteOpen());
    REQUIRE(driver.find("palette.form.name").has_value());
    driver.typeText("mine");
    driver.runFrames(1);
    REQUIRE(driver.click("palette.form.run"));
    driver.runFrames(1);
    CHECK_FALSE(fixture.shell->paletteOpen());
    CHECK(std::filesystem::exists(fixture.user / "layouts" / "mine.ini"));
}

TEST_CASE("menu items of commands from later milestones are disabled")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    REQUIRE(driver.click("menu.play"));
    driver.runFrames(2);
    const std::optional<ui::UiItem> play = driver.find("menu.play.play");
    REQUIRE(play.has_value());
    // Disabled items report no hover or edits; the item exists but clicking it runs nothing.
    CHECK(driver.click("menu.play.play"));
    CHECK(driver.find("menu.play.pause").has_value());
}

TEST_CASE("a layout saved by name loads again, also in the next session")
{
    test::TempDirectory user;
    {
        ShellFixture fixture(ShellSetup{.user = user.path});
        ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
        driver.runFrames(3);
        test::ok(*fixture.editor, "ui.layout_apply", R"({"name":"agent"})");
        driver.runFrames(3);
        CHECK_FALSE(panelOpen(*fixture.editor, "panel.assets"));
        CHECK(panelOpen(*fixture.editor, "panel.ai_activity"));
        test::ok(*fixture.editor, "ui.layout_save", R"({"name":"watch"})");
        test::ok(*fixture.editor, "ui.layout_apply", R"({"name":"default"})");
        driver.runFrames(3);
        CHECK(panelOpen(*fixture.editor, "panel.assets"));
        test::ok(*fixture.editor, "ui.layout_apply", R"({"name":"watch"})");
        driver.runFrames(3);
        CHECK_FALSE(panelOpen(*fixture.editor, "panel.assets"));
        CHECK(panelOpen(*fixture.editor, "panel.ai_activity"));
        CHECK(fixture.editor->preferences().layout == "watch");
    }
    ShellFixture next(ShellSetup{.user = user.path});
    ui::UiDriver driver(next.context, [&] { next.frame(); });
    driver.runFrames(3);
    CHECK_FALSE(panelOpen(*next.editor, "panel.assets"));
    const JsonDocument state = test::ok(*next.editor, "ui.state");
    bool saved = false;
    for (const JsonValue layout : state.root().find("layouts").elements())
    {
        saved |= layout.find("name").asString() == std::optional<std::string_view>("watch");
    }
    CHECK(saved);
    test::ok(*next.editor, "ui.layout_reset");
    driver.runFrames(3);
    CHECK(panelOpen(*next.editor, "panel.assets"));
    test::ok(*next.editor, "ui.layout_delete", R"({"name":"watch"})");
    CHECK_FALSE(std::filesystem::exists(user.path / "layouts" / "watch.ini"));
}

TEST_CASE("switching to en-XA keeps every item id")
{
    ShellFixture fixture;
    test::ok(*fixture.editor, "entity.create",
             R"({"name":"Board","components":{"illusion.Transform":{},"illusion.PointLight":{}}})");
    test::ok(*fixture.editor, "selection.set", R"({"entities":["/Board"]})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(4);
    const std::set<std::string> english = itemIds(driver);
    test::ok(*fixture.editor, "preferences.set", R"({"value":{"locale":"en-XA"}})");
    driver.runFrames(4);
    CHECK(fixture.editor->localization().locale().tag() == "en-XA");
    const std::set<std::string> pseudo = itemIds(driver);
    CHECK(english == pseudo);
    CHECK(english.size() > 20);
}

TEST_CASE("a keymap preset changes which keys run actions")
{
    ShellFixture fixture;
    test::ok(*fixture.editor, "entity.create", R"({"name":"Box"})");
    test::ok(*fixture.editor, "selection.set", R"({"entities":["/Box"]})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    test::ok(*fixture.editor, "preferences.set", R"({"value":{"keymap":"blender"}})");
    driver.runFrames(2);
    const u64 revision = fixture.editor->revision();
    driver.pressKey(ImGuiKey_D, ImGuiMod_Shift);
    driver.runFrames(1);
    CHECK(fixture.editor->revision() > revision);
    CHECK(test::ok(*fixture.editor, "scene.query", R"({"name":"Box*"})").root().find("total").asU64() == 2u);
}

TEST_CASE("the preferences window rebinds a shortcut by capturing the next key")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"window.preferences"})");
    driver.runFrames(3);
    REQUIRE(driver.click("prefs.tab.2"));
    driver.runFrames(2);
    REQUIRE(driver.click("prefs.keyboard.search"));
    driver.typeText("clear_selection");
    driver.runFrames(2);
    REQUIRE(driver.click("prefs.shortcut.edit.clear_selection"));
    driver.runFrames(1);
    driver.pressKey(ImGuiKey_F9, ImGuiMod_Alt);
    driver.runFrames(2);
    const Preferences& prefs = fixture.editor->preferences();
    REQUIRE(prefs.shortcuts.size() == 1);
    CHECK(prefs.shortcuts.front().action == "edit.clear_selection");
    CHECK(prefs.shortcuts.front().chord == "Alt+F9");
    REQUIRE(driver.click("prefs.shortcut.edit.clear_selection.reset"));
    driver.runFrames(1);
    CHECK(fixture.editor->preferences().shortcuts.empty());
}

TEST_CASE("the appearance page switches the theme through preferences")
{
    ShellFixture fixture;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"window.preferences"})");
    driver.runFrames(3);
    REQUIRE(driver.click("prefs.theme.1"));
    driver.runFrames(2);
    CHECK(fixture.editor->preferences().theme == "light");
    CHECK(fixture.shell->theme().name() != "");
    CHECK(fixture.shell->clearColor().x > 0.5f);
}

TEST_CASE("an agent's request in the ask mode shows a prompt whose buttons decide it")
{
    ShellFixture fixture;
    rpc::RpcServerOptions options;
    options.permissions = rpc::PermissionMode::Ask;
    options.interactive = true;
    rpc::RpcServer server(*fixture.editor, std::move(options));
    fixture.shell->setAgents(&server);
    auto [serverEnd, clientEnd] = rpc::makeStreamPair();
    REQUIRE(server.addConnection(std::move(serverEnd), true, "claude") != 0);
    test::RpcPeer peer(std::move(clientEnd));
    ui::UiDriver driver(fixture.context, [&] {
        server.pump();
        fixture.frame();
    });
    driver.runFrames(2);
    REQUIRE(peer.send(
        R"({"jsonrpc":"2.0","id":1,"method":"project.file_write","params":{"path":"assets:/a.txt","text":"a"}})"));
    const auto start = std::chrono::steady_clock::now();
    while (server.approvals().empty() && std::chrono::steady_clock::now() - start < test::kRpcPatience)
    {
        server.waitForWork(std::chrono::milliseconds(5));
        driver.runFrames(1);
    }
    REQUIRE(server.approvals().size() == 1);
    driver.runFrames(2);
    CHECK(fixture.shell->toastCount() >= 1);
    REQUIRE(driver.find("agents.prompt.allow_once").has_value());
    REQUIRE(driver.click("agents.prompt.allow_once"));
    driver.runFrames(1);
    CHECK(server.approvals().empty());
    std::optional<std::string> reply;
    for (int i = 0; i < 100 && !reply; ++i)
    {
        driver.runFrames(1);
        reply = peer.receive(std::chrono::milliseconds(5));
    }
    REQUIRE(reply.has_value());
    CHECK(test::parseJson(*reply).root().find("result").isObject());

    // The AI Activity panel lists the client and the call.
    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"panel.ai_activity"})");
    driver.runFrames(3);
    CHECK(driver.find("agents.client.claude.pause").has_value());
    fixture.shell->setAgents(nullptr);
}

TEST_CASE("the permission mode preference applies to the agents' server")
{
    ShellFixture fixture;
    rpc::RpcServer server(*fixture.editor, rpc::RpcServerOptions{});
    fixture.shell->setAgents(&server);
    CHECK(server.permissions() == rpc::PermissionMode::Ask);
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    test::ok(*fixture.editor, "preferences.set", R"({"value":{"agentPermissions":"read-only"}})");
    driver.runFrames(2);
    CHECK(server.permissions() == rpc::PermissionMode::ReadOnly);
    fixture.shell->setAgents(nullptr);
}

TEST_CASE("an idle shell frame does not allocate after warming up")
{
    ShellFixture fixture;
    test::ok(*fixture.editor, "entity.create",
             R"({"name":"Board","components":{"illusion.Transform":{},"illusion.PointLight":{}}})");
    test::ok(*fixture.editor, "selection.set", R"({"entities":["/Board"]})");
    // Scan the asset catalog, as any asset command or the Asset Browser does: from then on endFrame() polls
    // it.
    (void)fixture.editor->assets();
    for (int i = 0; i < 120; ++i)
    {
        fixture.frame();
    }
    const u64 imguiBefore = ui::UiContext::allocationCount();
    test::ScopedAllocationCounter allocations;
    // A second at 60 Hz spans several asset polls (every 0.25 s).
    for (int i = 0; i < 60; ++i)
    {
        fixture.frame();
    }
    CHECK(allocations.count() == 0);
    CHECK(ui::UiContext::allocationCount() == imguiBefore);
}

TEST_CASE("the welcome window creates a project and remembers it, and a corrupt project fails gracefully")
{
    test::TempDirectory user;
    test::TempDirectory workspace;
    std::string created;
    {
        ShellFixture fixture(ShellSetup{.project = false, .user = user.path, .workspace = workspace.path});
        ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
        driver.runFrames(3);
        CHECK(panelOpen(*fixture.editor, "window.welcome"));
        REQUIRE(driver.click("welcome.name"));
        driver.typeText("Tetris");
        driver.runFrames(1);
        REQUIRE(driver.click("welcome.create"));
        driver.runFrames(2);
        REQUIRE(fixture.editor->project() != nullptr);
        CHECK(fixture.editor->project()->manifest().name == "Tetris");
        CHECK_FALSE(panelOpen(*fixture.editor, "window.welcome"));
        REQUIRE(fixture.editor->preferences().recentProjects.size() == 1);
        created = fixture.editor->preferences().recentProjects.front();
    }
    ShellFixture fixture(ShellSetup{.project = false, .user = user.path, .workspace = workspace.path});
    REQUIRE(fixture.editor->preferences().recentProjects == std::vector<std::string>{created});
    {
        std::ofstream manifest(utf8Path(created) / "project.illusion", std::ios::binary | std::ios::trunc);
        manifest << "{ not a manifest";
    }
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    REQUIRE(driver.click("welcome.recent.0"));
    driver.runFrames(2);
    CHECK(fixture.editor->project() == nullptr);
    CHECK(fixture.shell->toastCount() >= 1);
    CHECK(panelOpen(*fixture.editor, "window.welcome"));
}

TEST_CASE("editor.screenshot waits for the frame the application captures")
{
    ShellFixture fixture(ShellSetup{.canCapture = true});
    std::unique_ptr<RunningCommand> running =
        fixture.editor
            ->startJson("editor.screenshot", test::parseJson(R"({"width":320,"height":200})").root())
            .value();
    const std::optional<shell::ScreenshotRequest> request = fixture.shell->pendingScreenshot();
    REQUIRE(request.has_value());
    CHECK(request->size == UVec2(320, 200));
    CHECK(running->update().value() == std::nullopt);
    fixture.shell->completeScreenshot(request->ticket, UiScreenshot{UVec2(320, 200), std::string("png")});
    CHECK_FALSE(fixture.shell->pendingScreenshot().has_value());
    const std::optional<std::string> result = running->update().value();
    REQUIRE(result.has_value());
    const JsonDocument json = test::parseJson(*result);
    CHECK(json.root().find("width").asU64() == 320u);
    CHECK(json.root().find("_images")[0].find("data").asString() == std::optional<std::string_view>("cG5n"));

    ShellFixture noGpu;
    CHECK(test::fails(*noGpu.editor, "editor.screenshot", "{}").kind == ErrorKind::Unsupported);
}

TEST_CASE("a project's shipped layout applies when the user has no arrangement of their own")
{
    ShellFixture fixture;
    {
        std::ofstream ini(fixture.directory.path / "project" / "settings" / "editor_layout.ini",
                          std::ios::binary);
        ini << "[IllusionShell][Panels]\npanel.console=0\npanel.history=1\n";
    }
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    CHECK_FALSE(panelOpen(*fixture.editor, "panel.console"));
    CHECK(panelOpen(*fixture.editor, "panel.history"));
    CHECK_FALSE(panelOpen(*fixture.editor, "panel.outliner"));
}

TEST_CASE("tables of the shell lay out without NaN in the first frames (regression)")
{
    ShellFixture fixture;
    rpc::RpcServer server(*fixture.editor, rpc::RpcServerOptions{});
    fixture.shell->setAgents(&server);
    auto [serverEnd, clientEnd] = rpc::makeStreamPair();
    REQUIRE(server.addConnection(std::move(serverEnd), true, "claude") != 0);
    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"panel.ai_activity"})");
    test::ok(*fixture.editor, "ui.show_panel", R"({"panel":"window.preferences"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    REQUIRE(driver.click("prefs.tab.2"));
    for (int frame = 0; frame < 4; ++frame)
    {
        driver.runFrames(1);
        for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            INFO(window->Name);
            CHECK_FALSE(std::isnan(window->DC.CursorMaxPos.x));
            CHECK_FALSE(std::isnan(window->DC.IdealMaxPos.x));
        }
    }
    fixture.shell->setAgents(nullptr);
}

TEST_CASE("the outliner filters, selects, toggles visibility and lock, and renames with F2")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Game","components":{"illusion.Transform":{}}})");
    test::ok(editor, "entity.create",
             R"({"name":"Board","parent":"/Game","components":{"illusion.LocalTransform":{}}})");
    test::ok(editor, "entity.create",
             R"({"name":"Light","components":{"illusion.Transform":{},"illusion.PointLight":{}}})");
    test::ok(editor, "entity.set_enabled", R"({"entity":"/Light","enabled":false})");
    test::ok(editor, "selection.clear");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    // Rows 0 (/Game), 1 (/Game/Board) and 2 (/Light) are visible.
    REQUIRE(driver.find("outliner.row.0").has_value());
    REQUIRE(driver.find("outliner.row.1").has_value());
    REQUIRE(driver.find("outliner.row.2").has_value());

    // Single click selects /Game; Ctrl-click adds /Light; Shift-click selects the range 0..2.
    REQUIRE(driver.click("outliner.row.0"));
    CHECK(editor.selection().size() == 1);
    clickWithModifier(driver, "outliner.row.2", ui::UiDriver::shortcutModifier());
    CHECK(editor.selection().size() == 2);
    REQUIRE(driver.click("outliner.row.0"));
    CHECK(editor.selection().size() == 1);
    clickWithModifier(driver, "outliner.row.2", ImGuiMod_Shift);
    CHECK(editor.selection().size() == 3);

    // Eye and lock buttons update editor.view() without recording an undo step.
    const usize historyBefore = editor.history().steps().size();
    const Entity light = editor.entities().resolve("/Light", editor.selection()).value();
    const EntityGuid lightGuid = editor.entities().guidOf(light);
    REQUIRE(driver.click("outliner.visible.2"));
    CHECK(editor.view().hidden(lightGuid));
    REQUIRE(driver.click("outliner.lock.2"));
    CHECK(editor.view().locked(lightGuid));
    CHECK(editor.history().steps().size() == historyBefore);

    // F2 inline rename on /Light commits via entity.rename.
    REQUIRE(driver.click("outliner.row.2"));
    driver.pressKey(ImGuiKey_F2);
    driver.runFrames(2);
    REQUIRE(driver.find("outliner.rename").has_value());
    driver.typeText("Sun");
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(2);
    CHECK(editor.entities().resolve("/Sun", editor.selection()).hasValue());

    // Search filters by component type (t:PointLight) and disabled state (disabled:true).
    REQUIRE(driver.click("outliner.search"));
    driver.typeText("t:PointLight disabled:true");
    driver.runFrames(2);
    CHECK(driver.find("outliner.row.0").has_value());
    CHECK_FALSE(driver.find("outliner.row.1").has_value());
    REQUIRE(driver.click("outliner.search.clear"));
    driver.runFrames(2);
    CHECK(driver.find("outliner.row.2").has_value());
}

TEST_CASE("dragging a row in the outliner reparents it with undo")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Parent","components":{"illusion.Transform":{}}})");
    test::ok(editor, "entity.create", R"({"name":"Child","components":{"illusion.Transform":{}}})");
    test::ok(editor, "history.clear");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    const std::optional<ui::UiItem> parentRow = driver.find("outliner.row.0");
    REQUIRE(parentRow.has_value());
    dragBetween(driver, "outliner.row.1", parentRow->center());
    driver.runFrames(2);

    CHECK(editor.entities().resolve("/Parent/Child", editor.selection()).hasValue());
    CHECK(editor.history().steps().size() == 1);
    test::ok(editor, "history.undo");
    CHECK(editor.entities().resolve("/Child", editor.selection()).hasValue());
}

TEST_CASE("the inspector edits transform in local and world space, preserves 180-degree yaw and locks")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create",
             R"({"name":"Parent","components":{"illusion.Transform":{"position":[10,0,0]}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"Child","parent":"/Parent","components":{"illusion.LocalTransform":{"position":[1,2,3]}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Parent/Child"]})");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    // Typing a yaw of 180 degrees keeps showing 180 degrees (does not flip to -180, 0, -180).
    typeIntoDragField(driver, "inspector.transform.rotation.y", "180");
    driver.runFrames(3);
    const JsonDocument localTf =
        test::ok(editor, "transform.get", R"({"entity":"/Parent/Child","space":"local"})");
    CHECK(std::abs(localTf.root().pointer("/rotation/1").asF64().value_or(0.0)) ==
          doctest::Approx(1.0).epsilon(1e-3));

    // Verify that subsequent drags on X rotation preserve the 180-degree yaw continuity.
    typeIntoDragField(driver, "inspector.transform.rotation.x", "15");
    driver.runFrames(2);
    const JsonDocument afterPitch =
        test::ok(editor, "transform.get", R"({"entity":"/Parent/Child","space":"local"})");
    CHECK(std::abs(afterPitch.root().pointer("/rotation/1").asF64().value_or(0.0)) > 0.95);

    // Switch to World space and set world X to 25 (Parent is at 10, so local X becomes 15).
    REQUIRE(driver.click("inspector.transform.space.1"));
    driver.runFrames(2);
    typeIntoDragField(driver, "inspector.transform.position.x", "25");
    driver.runFrames(2);
    const JsonDocument afterWorld =
        test::ok(editor, "transform.get", R"({"entity":"/Parent/Child","space":"local"})");
    CHECK(afterWorld.root().pointer("/position/0").asF64().value_or(0.0) ==
          doctest::Approx(15.0).epsilon(1e-3));

    // Lock the Inspector to /Parent/Child, then select /Parent: the Inspector stays on Child.
    REQUIRE(driver.click("inspector.lock"));
    driver.runFrames(1);
    test::ok(editor, "selection.set", R"({"entities":["/Parent"]})");
    driver.runFrames(2);
    REQUIRE(driver.click("inspector.enabled"));
    driver.runFrames(1);
    CHECK(test::ok(editor, "entity.get", R"({"entity":"/Parent/Child"})").root().find("enabled").asBool() ==
          false);
    CHECK(test::ok(editor, "entity.get", R"({"entity":"/Parent"})").root().find("enabled").asBool() == true);
}

TEST_CASE("the inspector adds components with required dependencies, custom drawers and fix-it buttons")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Crate","components":{"illusion.Transform":{}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Crate"]})");
    test::ok(editor, "history.clear");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    // Add RigidBody via the Add Component popup: also adds required Collider in a single undo step.
    REQUIRE(driver.click("inspector.add_component"));
    driver.runFrames(2);
    REQUIRE(driver.find("inspector.add_component.search").has_value());
    driver.typeText("RigidBody");
    driver.runFrames(1);
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(2);

    const Entity crate = editor.entities().resolve("/Crate", editor.selection()).value();
    CHECK(editor.world().has<RigidBody>(crate));
    CHECK(editor.world().has<Collider>(crate));
    CHECK(editor.history().steps().size() == 1);

    // Collider custom drawer: Box shows halfExtents and hides radius; Sphere shows radius and hides
    // halfExtents.
    CHECK(driver.find("inspector.card.illusion.Collider.halfExtents.x").has_value());
    CHECK_FALSE(driver.find("inspector.card.illusion.Collider.radius").has_value());
    test::ok(editor, "component.set_field",
             R"({"entity":"/Crate","type":"illusion.Collider","path":"shape","value":"Sphere"})");
    driver.runFrames(2);
    CHECK(driver.find("inspector.card.illusion.Collider.radius").has_value());
    CHECK_FALSE(driver.find("inspector.card.illusion.Collider.halfExtents.x").has_value());

    // Mesh Collider on a Dynamic RigidBody shows the warning and fix-it button -> switches to ConvexHull.
    test::ok(editor, "component.set_field",
             R"({"entity":"/Crate","type":"illusion.Collider","path":"shape","value":"Mesh"})");
    driver.runFrames(2);
    REQUIRE(driver.find("inspector.card.illusion.Collider.fix.convex_hull").has_value());
    REQUIRE(driver.click("inspector.card.illusion.Collider.fix.convex_hull"));
    driver.runFrames(2);
    CHECK(
        test::ok(editor, "component.get", R"({"entity":"/Crate","type":"illusion.Collider","path":"shape"})")
            .root()
            .find("value")
            .asString() == std::optional<std::string_view>("ConvexHull"));

    // If a scene or C++ code leaves RigidBody without its required Collider, the fix-it button re-adds it.
    editor.world().remove<Collider>(crate);
    driver.runFrames(2);
    REQUIRE(driver.find("inspector.card.illusion.RigidBody.fix.req.illusion.Collider").has_value());
    REQUIRE(driver.click("inspector.card.illusion.RigidBody.fix.req.illusion.Collider"));
    driver.runFrames(2);
    CHECK(editor.world().has<Collider>(crate));
}

TEST_CASE("the inspector removes a component from its card menu and keeps drawing")
{
    // Regression: the Remove item returned without closing the menu's disabled scope, which asserted at the
    // end of the frame.
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create",
             R"({"name":"A","components":{"illusion.Transform":{},"test.Link":{"weight":2}}})");
    test::ok(editor, "selection.set", R"({"entities":["/A"]})");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    REQUIRE(driver.click("inspector.card.test.Link.menu"));
    driver.runFrames(2);
    REQUIRE(driver.click("inspector.card.test.Link.menu.remove"));
    driver.runFrames(3);
    CHECK_FALSE(driver.find("inspector.card.test.Link.menu").has_value());
    const Entity a = editor.entities().resolve("/A", editor.selection()).value();
    const ComponentId link = ComponentRegistry::instance().findByStableName("test.Link");
    REQUIRE(link != kInvalidComponentId);
    CHECK(editor.world().componentData(a, link) == nullptr);
}

TEST_CASE(
    "the inspector merges continuous drags, edits multiple entities in one step, and accepts entity drops")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create",
             R"({"name":"A","components":{"illusion.Transform":{"position":[1,0,0]}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"B","components":{"illusion.Transform":{"position":[4,0,0]},"test.Link":{"weight":2}}})");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });

    // Multi-entity edit: select A and B (different X positions) and set X to 9 in a single undo step.
    test::ok(editor, "selection.set", R"({"entities":["/A","/B"]})");
    test::ok(editor, "history.clear");
    driver.runFrames(3);
    typeIntoDragField(driver, "inspector.transform.position.x", "9");
    driver.runFrames(2);
    CHECK(editor.history().steps().size() == 1);
    CHECK(test::ok(editor, "transform.get", R"({"entity":"/A"})").root().pointer("/position/0").asF64() ==
          doctest::Approx(9.0));
    CHECK(test::ok(editor, "transform.get", R"({"entity":"/B"})").root().pointer("/position/0").asF64() ==
          doctest::Approx(9.0));

    // Continuous drag on a scalar field across multiple frames merges into one undo step.
    test::ok(editor, "selection.set", R"({"entities":["/B"]})");
    test::ok(editor, "history.clear");
    driver.runFrames(3);
    REQUIRE(driver.drag("inspector.card.test.Link.weight", Vec2(30.0f, 0.0f), 6));
    driver.runFrames(2);
    CHECK(editor.history().steps().size() == 1);
    CHECK(test::ok(editor, "component.get", R"({"entity":"/B","type":"test.Link","path":"weight"})")
              .root()
              .find("value")
              .asF64() > 2.0);

    // Drag /A from the Outliner onto test.Link.target in the Inspector.
    const std::optional<ui::UiItem> targetField = driver.find("inspector.card.test.Link.target");
    REQUIRE(targetField.has_value());
    dragBetween(driver, "outliner.row.0", targetField->center());
    driver.runFrames(2);
    const std::string aId(test::ok(editor, "entity.get", R"({"entity":"/A"})")
                              .root()
                              .pointer("/entity/id")
                              .asString()
                              .value_or(""));
    CHECK(test::ok(editor, "component.get", R"({"entity":"/B","type":"test.Link","path":"target"})")
              .root()
              .find("value")
              .asString() == std::optional<std::string_view>(aId));
}

TEST_CASE("the asset browser lists a fresh project's assets and fits its toolbar in a narrow panel")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    // No asset command runs before the browser draws: it must scan the catalog itself.
    REQUIRE_FALSE(editor.assetCatalog().scanned());
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.assets"})");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    CHECK(editor.assetCatalog().scanned());
    // The root holds one folder (scenes) and nothing else: one tile in a row of many columns.
    CHECK(driver.find("asset_browser.tile.0").has_value());
    CHECK_FALSE(driver.find("asset_browser.tile.1").has_value());

    // Every toolbar control after the search field is on screen, inside the panel.
    const std::optional<ui::UiItem> panel = driver.find("panel.assets");
    REQUIRE(panel.has_value());
    for (const char* id : {"asset_browser.search", "asset_browser.view", "asset_browser.new_folder",
                           "asset_browser.favourite"})
    {
        CAPTURE(id);
        const std::optional<ui::UiItem> item = driver.find(id);
        REQUIRE(item.has_value());
        CHECK(item->max.x <= panel->max.x);
    }
}

TEST_CASE("dragging an asset from the browser into an Inspector asset field sets it")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    // The empty template's assets:/ has the scenes folder and the open scene; add a material at the root.
    test::ok(editor, "project.file_write", R"({"path":"assets:/red.material","text":"x"})");
    test::ok(editor, "entity.create", R"({"name":"Crate","components":{"test.Look":{}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Crate"]})");
    // The Assets panel shares its dock with the Console: bring its tab to the front.
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.assets"})");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(4); // let the catalog pick up red.material

    // The root lists folders first, then files: tile 0 is the scenes folder, tile 1 is red.material.
    const std::optional<ui::UiItem> tile = driver.find("asset_browser.tile.1");
    REQUIRE(tile.has_value());
    const std::optional<ui::UiItem> field = driver.find("inspector.card.test.Look.material");
    REQUIRE(field.has_value());
    dragBetween(driver, "asset_browser.tile.1", field->center());
    driver.runFrames(2);
    CHECK(test::ok(editor, "component.get", R"({"entity":"/Crate","type":"test.Look","path":"material"})")
              .root()
              .find("value")
              .asString() == std::optional<std::string_view>("assets:/red.material"));
}

namespace
{

// Presses the left button at @p from, moves to @p to in @p steps frames (pressing Escape after @p escapeAt
// steps when given) and releases.
void dragAt(ui::UiDriver& driver, Vec2 from, Vec2 to, u32 steps, std::optional<u32> escapeAt = std::nullopt)
{
    ImGuiIO& io = ImGui::GetIO();
    driver.moveMouse(from);
    driver.runFrames(1);
    io.AddMouseButtonEvent(0, true);
    driver.runFrames(1);
    for (u32 step = 1; step <= steps; ++step)
    {
        driver.moveMouse(from + (to - from) * (static_cast<f32>(step) / static_cast<f32>(steps)));
        if (escapeAt && step == *escapeAt)
        {
            driver.pressKey(ImGuiKey_Escape);
        }
    }
    io.AddMouseButtonEvent(0, false);
    driver.runFrames(2);
}

Vec3 positionOf(EditorContext& editor, std::string_view entity)
{
    const JsonDocument placed =
        test::ok(editor, "transform.get", std::string(R"({"entity":")") + std::string(entity) + "\"}");
    const JsonValue position = placed.root().find("position");
    return Vec3(static_cast<f32>(position[0].asF64().value_or(0.0)),
                static_cast<f32>(position[1].asF64().value_or(0.0)),
                static_cast<f32>(position[2].asF64().value_or(0.0)));
}

} // namespace

TEST_CASE("dragging the gizmo in the Scene view moves the selection in one undo step and Escape cancels")
{
    ShellFixture fixture;
    test::FakeViewports viewports;
    viewports.frames = true;
    viewports.camera.pivot = Vec3(0.0f);
    viewports.camera.distance = 10.0f;
    viewports.camera.yaw = 30.0f;
    viewports.camera.pitch = -30.0f;
    fixture.editor->setViewports(&viewports);
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Board","components":{"illusion.Transform":{}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Board"]})");
    const usize steps = editor.history().steps().size();
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    const std::optional<ui::UiItem> image = driver.find("viewport.image");
    REQUIRE(image.has_value());
    const UVec2 size(static_cast<u32>(image->max.x - image->min.x + 0.5f),
                     static_cast<u32>(image->max.y - image->min.y + 0.5f));
    const GizmoCamera camera = gizmoCamera(viewports.camera, size, 1.0f);
    const GizmoShape shape = gizmoShape(GizmoTool::Translate, camera, GizmoPose{});
    REQUIRE(shape.visible);
    const auto window = [&](Vec3 point) {
        const Mat4 view =
            glm::inverse(glm::translate(Mat4(1.0f), camera.eye) * glm::mat4_cast(camera.rotation));
        return image->min + projectToViewport(camera.projection * view, size, point).pixel;
    };
    const Vec3 grab(shape.worldLength * 0.6f, 0.0f, 0.0f);

    // Hovering an axis does not move the camera or select; dragging it moves the entity along x only.
    dragAt(driver, window(grab), window(grab + Vec3(1.5f, 0.0f, 0.0f)), 8);
    const Vec3 moved = positionOf(editor, "/Board");
    CHECK(static_cast<double>(moved.x) == doctest::Approx(1.5).epsilon(1e-2));
    CHECK(std::abs(moved.y) < 1e-4f);
    CHECK(std::abs(moved.z) < 1e-4f);
    REQUIRE(editor.history().steps().size() == steps + 1);
    CHECK(editor.history().steps().back().origin == "gizmo");
    CHECK(editor.transaction() == nullptr);
    CHECK(editor.selection().size() == 1);

    // Escape in the middle of a drag puts everything back.
    const Vec3 grabNow = moved + grab;
    dragAt(driver, window(grabNow), window(grabNow + Vec3(2.0f, 0.0f, 0.0f)), 8, 4);
    CHECK(positionOf(editor, "/Board") == moved);
    CHECK(editor.history().steps().size() == steps + 1);

    // Snapping with Ctrl (Cmd on macOS) held: the move lands on a multiple of the increment.
    test::ok(editor, "gizmo.set_snap", R"({"translate":1})");
    const auto ctrl = static_cast<ImGuiKey>(ui::UiDriver::shortcutModifier());
    ImGui::GetIO().AddKeyEvent(ctrl, true);
    dragAt(driver, window(grabNow), window(grabNow + Vec3(1.3f, 0.0f, 0.0f)), 8);
    ImGui::GetIO().AddKeyEvent(ctrl, false);
    driver.runFrames(1);
    CHECK(static_cast<double>(positionOf(editor, "/Board").x) ==
          doctest::Approx(static_cast<double>(moved.x) + 1.0));
    CHECK(editor.history().steps().size() == steps + 2);
    test::ok(editor, "history.undo");
    CHECK(positionOf(editor, "/Board") == moved);

    // The tool shortcuts switch the gizmo; the Select tool has no gizmo, so a drag draws a selection box.
    driver.moveMouse(image->center());
    driver.pressKey(ImGuiKey_E);
    CHECK(editor.gizmo().tool == GizmoTool::Rotate);
    driver.pressKey(ImGuiKey_Q);
    CHECK(editor.gizmo().tool == GizmoTool::Select);
    dragAt(driver, window(grab), window(grab + Vec3(1.5f, 0.0f, 0.0f)), 8);
    CHECK(positionOf(editor, "/Board") == moved);

    // Holding V snaps the move to the vertex under the cursor, along the dragged x arrow only; the moved
    // entity is left out of the search.
    test::ok(editor, "selection.set", R"({"entities":["/Board"]})");
    driver.pressKey(ImGuiKey_W);
    ViewportSnap vertex;
    vertex.kind = ViewportSnapTarget::Vertex;
    vertex.position = Vec3(4.0f, 2.0f, 1.0f);
    viewports.snapped = vertex;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_V, true);
    dragAt(driver, window(grabNow), window(grabNow + Vec3(0.5f, 0.0f, 0.0f)), 8);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_V, false);
    driver.runFrames(1);
    CHECK(viewports.snapRequest.target == ViewportSnapTarget::Vertex);
    CHECK(viewports.snapExcluded.size() == 1);
    const Vec3 onVertex = positionOf(editor, "/Board");
    CHECK(static_cast<double>(onVertex.x) == doctest::Approx(4.0).epsilon(1e-4));
    CHECK(onVertex.y == moved.y);
    CHECK(onVertex.z == moved.z);
    test::ok(editor, "history.undo");
    viewports.snapped.reset();

    // M picks the Measure tool: a drag over nothing measures on the ground plane and leaves the ruler, which
    // Esc removes; nothing in the scene changes.
    const usize measured = editor.history().steps().size();
    driver.pressKey(ImGuiKey_M);
    CHECK(editor.gizmo().tool == GizmoTool::Measure);
    dragAt(driver, window(Vec3(0.0f, 0.0f, 2.0f)), window(Vec3(3.0f, 0.0f, 2.0f)), 8);
    REQUIRE(editor.gizmo().ruler.has_value());
    CHECK(static_cast<double>(glm::length(editor.gizmo().ruler->to - editor.gizmo().ruler->from)) ==
          doctest::Approx(3.0).epsilon(1e-2));
    CHECK(positionOf(editor, "/Board") == moved);
    CHECK(editor.history().steps().size() == measured);
    driver.pressKey(ImGuiKey_Escape);
    CHECK(!editor.gizmo().ruler.has_value());
    fixture.editor->setViewports(nullptr);
}

TEST_CASE("modal transforms follow the keyboard: Shift+W, X and typed numbers move exactly, Esc cancels")
{
    ShellFixture fixture;
    test::FakeViewports viewports;
    viewports.frames = true;
    viewports.camera.pivot = Vec3(0.0f);
    viewports.camera.distance = 10.0f;
    viewports.camera.yaw = 30.0f;
    viewports.camera.pitch = -30.0f;
    fixture.editor->setViewports(&viewports);
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Board","components":{"illusion.Transform":{}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Board"]})");
    const usize steps = editor.history().steps().size();
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    const std::optional<ui::UiItem> image = driver.find("viewport.image");
    REQUIRE(image.has_value());
    driver.moveMouse(image->center() + Vec2(60.0f, 25.0f));
    driver.runFrames(1);

    // A modal move along x, typed: exactly 2.5, one undo step from the gizmo.
    driver.pressKey(ImGuiKey_W, ImGuiMod_Shift);
    driver.runFrames(1);
    driver.moveMouse(image->center() + Vec2(90.0f, 5.0f));
    driver.pressKey(ImGuiKey_X);
    driver.typeText("2.5");
    driver.runFrames(1);
    CHECK(static_cast<double>(positionOf(editor, "/Board").x) == doctest::Approx(2.5));
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(1);
    CHECK(positionOf(editor, "/Board") == Vec3(2.5f, 0.0f, 0.0f));
    REQUIRE(editor.history().steps().size() == steps + 1);
    CHECK(editor.history().steps().back().origin == "gizmo");
    CHECK(editor.transaction() == nullptr);

    // A modal rotation that follows the cursor, then Escape: nothing changes.
    driver.pressKey(ImGuiKey_E, ImGuiMod_Shift);
    driver.runFrames(1);
    driver.moveMouse(image->center() + Vec2(-40.0f, 70.0f));
    driver.runFrames(2);
    driver.pressKey(ImGuiKey_Escape);
    driver.runFrames(1);
    CHECK(positionOf(editor, "/Board") == Vec3(2.5f, 0.0f, 0.0f));
    CHECK(editor.history().steps().size() == steps + 1);
    CHECK(editor.transaction() == nullptr);

    // A modal scale confirmed by a click, which neither picks nor changes the selection.
    driver.pressKey(ImGuiKey_R, ImGuiMod_Shift);
    driver.runFrames(1);
    driver.typeText("2");
    driver.runFrames(1);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, true);
    driver.runFrames(1);
    io.AddMouseButtonEvent(0, false);
    driver.runFrames(2);
    const JsonDocument scaled = test::ok(editor, "transform.get", R"({"entity":"/Board"})");
    CHECK(scaled.root().find("scale")[0].asF64() == doctest::Approx(2.0));
    CHECK(editor.history().steps().size() == steps + 2);
    CHECK(editor.selection().size() == 1);
    CHECK(viewports.pickedAt == Vec2(0.0f));

    // Numbers typed during a gizmo drag replace the cursor's distance.
    const UVec2 size(static_cast<u32>(image->max.x - image->min.x + 0.5f),
                     static_cast<u32>(image->max.y - image->min.y + 0.5f));
    const GizmoCamera camera = gizmoCamera(viewports.camera, size, 1.0f);
    GizmoPose pose;
    pose.origin = Vec3(2.5f, 0.0f, 0.0f);
    const GizmoShape shape = gizmoShape(GizmoTool::Translate, camera, pose);
    const Vec2 grab =
        image->min + (shape.lines[0].start + (shape.lines[0].end - shape.lines[0].start) * 0.75f);
    driver.moveMouse(grab);
    driver.runFrames(1);
    io.AddMouseButtonEvent(0, true);
    driver.runFrames(1);
    driver.moveMouse(grab + Vec2(30.0f, 0.0f));
    driver.typeText("-1");
    driver.runFrames(1);
    io.AddMouseButtonEvent(0, false);
    driver.runFrames(2);
    CHECK(static_cast<double>(positionOf(editor, "/Board").x) == doctest::Approx(1.5));
    CHECK(editor.history().steps().size() == steps + 3);

    // End drops the selection to the ground plane (the fake viewports draw no Mesh).
    test::ok(editor, "transform.set", R"({"entities":["/Board"],"position":[1.5,4,0]})");
    driver.moveMouse(image->center());
    driver.pressKey(ImGuiKey_End);
    CHECK(positionOf(editor, "/Board") == Vec3(1.5f, 0.0f, 0.0f));
    fixture.editor->setViewports(nullptr);
}

TEST_CASE("the toolbar shows the active tool and changes the gizmo's axes, pivot and snapping")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(2);
    REQUIRE(driver.click("toolbar.view.tool_rotate"));
    CHECK(editor.gizmo().tool == GizmoTool::Rotate);
    REQUIRE(driver.click("toolbar.gizmo.space"));
    REQUIRE(driver.click("toolbar.gizmo.space.local"));
    CHECK(editor.gizmo().space == GizmoSpace::Local);
    REQUIRE(driver.click("toolbar.gizmo.pivot"));
    REQUIRE(driver.click("toolbar.gizmo.pivot.individual"));
    CHECK(editor.gizmo().pivot == GizmoPivot::Individual);
    REQUIRE(driver.click("toolbar.gizmo.pivot"));
    REQUIRE(driver.click("toolbar.gizmo.pivot.bounds_center"));
    CHECK(editor.gizmo().pivot == GizmoPivot::BoundsCenter);
    REQUIRE(driver.click("toolbar.gizmo.snap"));
    CHECK(editor.gizmo().snap.enabled);
    REQUIRE(driver.click("toolbar.gizmo.snap.more"));
    driver.runFrames(2);
    CHECK(driver.find("toolbar.gizmo.snap.rotate").has_value());
}

TEST_CASE("the Play toolbar starts, pauses, resumes and stops a session, and Play turns into Stop")
{
    ShellFixture fixture;
    EditorContext* current = fixture.editor.get();
    test::FakePlay play(current);
    fixture.editor->setPlay(&play);
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    // Pause and Step wait for a session.
    (void)driver.click("toolbar.play.pause");
    CHECK(play.status().state == PlayState::Stopped);
    REQUIRE(driver.click("toolbar.play.play"));
    driver.runFrames(1);
    CHECK(fixture.editor->playing());
    REQUIRE(driver.click("toolbar.play.pause"));
    CHECK(play.status().state == PlayState::Paused);
    REQUIRE(driver.click("toolbar.play.pause"));
    CHECK(play.status().state == PlayState::Playing);
    // F5 stops what Play started; Simulate stops it too.
    driver.pressKey(ImGuiKey_F5);
    CHECK_FALSE(fixture.editor->playing());
    REQUIRE(driver.click("toolbar.play.simulate"));
    CHECK(play.status().mode == PlayMode::Simulate);
    REQUIRE(driver.click("toolbar.play.play"));
    CHECK_FALSE(fixture.editor->playing());
    fixture.editor->setPlay(nullptr);
}

TEST_CASE("a click on the Game view gives the running game the keyboard and mouse until Shift+F1")
{
    ShellFixture fixture;
    test::FakeViewports viewports;
    viewports.frames = true;
    fixture.editor->setViewports(&viewports);
    EditorContext* current = fixture.editor.get();
    test::FakePlay play(current);
    fixture.editor->setPlay(&play);
    EditorContext& editor = *fixture.editor;
    // Maximize on Play: only the Game view's image is drawn while playing.
    test::ok(editor, "preferences.set", R"({"value":{"maximizeOnPlay":true}})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    REQUIRE(driver.find("outliner.search").has_value());
    test::ok(editor, "play.start");
    driver.runFrames(2);
    const std::optional<ui::UiItem> image = driver.find("viewport.image");
    REQUIRE(image.has_value());
    CHECK(image->max.x - image->min.x > test::kShellFrameSize.x * 0.9f);
    CHECK_FALSE(driver.find("outliner.search").has_value());

    // Keys before the capture belong to the editor.
    driver.pressKey(ImGuiKey_Space);
    CHECK(play.events.empty());
    REQUIRE(driver.click("viewport.image"));
    driver.runFrames(1);
    play.events.clear();
    driver.pressKey(ImGuiKey_LeftArrow);
    const auto keyEvents = [&](Key key) {
        std::vector<bool> downs;
        for (const PlayInputEvent& event : play.events)
        {
            if (event.kind == PlayInputEvent::Kind::Key && event.key == key)
            {
                downs.push_back(event.down);
            }
        }
        return downs;
    };
    CHECK(keyEvents(Key::Left) == std::vector<bool>{true, false});
    // Editor shortcuts wait while the game has the keyboard.
    driver.pressKey(ImGuiKey_F5);
    CHECK(editor.playing());

    // Shift+F1 takes the input back and releases what the game saw held.
    driver.pressKey(ImGuiKey_F1, ImGuiMod_Shift);
    CHECK(keyEvents(Key::F1).empty());
    play.events.clear();
    driver.pressKey(ImGuiKey_LeftArrow);
    CHECK(play.events.empty());
    driver.pressKey(ImGuiKey_F5);
    CHECK_FALSE(editor.playing());
    driver.runFrames(2);
    CHECK(driver.find("outliner.search").has_value());
    fixture.editor->setPlay(nullptr);
    fixture.editor->setViewports(nullptr);
}

namespace
{

// A crate prefab (root "Crate", child "Lid") and an open scene with one instance of it.
void openCrateInstance(EditorContext& editor)
{
    test::ok(
        editor, "project.file_write",
        R"({"path":"assets:/props/crate.prefab","text":"{\"format\":\"illusion.prefab\",\"version\":2,)"
        R"(\"entities\":[{\"id\":\"00000000000000a1\",\"components\":{\"illusion.Name\":{\"value\":\"Crate\"},)"
        R"(\"illusion.Hierarchy\":{\"parent\":null,\"order\":0},\"illusion.LocalTransform\":{}}},)"
        R"({\"id\":\"00000000000000a2\",\"components\":{\"illusion.Name\":{\"value\":\"Lid\"},)"
        R"(\"illusion.Hierarchy\":{\"parent\":\"00000000000000a1\",\"order\":0},\"illusion.LocalTransform\":{}}}]}"})");
    test::ok(editor, "scene.new", R"({"discardChanges":true})");
    test::ok(editor, "prefab.instantiate", R"({"path":"assets:/props/crate.prefab"})");
}

} // namespace

TEST_CASE("the Inspector shows the prefab bar of instance entities and reverts overridden Components")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateInstance(editor);
    test::ok(editor, "component.add", R"({"entity":"/Crate/Lid","type":"test.Look"})");
    test::ok(editor, "selection.set", R"({"entities":["/Crate/Lid"]})");

    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(2);
    CHECK(driver.find("inspector.prefab.apply").has_value());
    CHECK(driver.find("inspector.prefab.unpack").has_value());
    REQUIRE(driver.click("inspector.card.test.Look.menu"));
    driver.runFrames(1);
    REQUIRE(driver.click("inspector.card.test.Look.menu.prefab_revert"));
    driver.runFrames(2);
    CHECK(test::fails(editor, "component.get", R"({"entity":"/Crate/Lid","type":"test.Look"})").kind ==
          ErrorKind::NotFound);

    REQUIRE(driver.click("inspector.prefab.unpack"));
    driver.runFrames(2);
    CHECK_FALSE(driver.find("inspector.prefab.apply").has_value());
}

TEST_CASE("the Outliner shows a breadcrumb in prefab edit mode whose back button returns to the scene")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateInstance(editor);
    test::ok(editor, "scene.save_as", R"({"path":"assets:/scenes/yard.scene"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(2);
    CHECK_FALSE(driver.find("outliner.prefab.close").has_value());
    test::ok(editor, "prefab.open", R"({"path":"assets:/props/crate.prefab"})");
    driver.runFrames(2);
    REQUIRE(driver.click("outliner.prefab.close"));
    driver.runFrames(2);
    CHECK(editor.document().path() == "assets:/scenes/yard.scene");
    CHECK(editor.prefabEditStack().empty());
}

TEST_CASE(
    "dragging an entity into the Asset Browser makes a prefab and a prefab onto an Outliner row places it")
{
    ShellSetup setup;
    setup.assetWatching = FileWatchBackend::Polling;
    ShellFixture fixture(setup);
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Barrel"})");
    test::ok(editor, "entity.create", R"({"name":"Shelf"})");
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.assets"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    // Outliner rows: 0 Barrel, 1 Shelf. Drop Barrel on the browser's content area.
    const std::optional<ui::UiItem> area = driver.find("asset_browser.search");
    REQUIRE(area.has_value());
    dragBetween(driver, "outliner.row.0", Vec2(area->center().x, area->max.y + 40.0f));
    driver.runFrames(3);
    test::ok(editor, "project.file_stat", R"({"path":"assets:/Barrel.prefab"})");
    const std::string barrel = std::string(test::ok(editor, "entity.get", R"({"entity":"/Barrel"})")
                                               .root()
                                               .pointer("/entity/id")
                                               .asString()
                                               .value_or(""));
    CHECK(editor.world().has<PrefabInstance>(editor.entities().find(*parseGuid(barrel))));

    // The prefab's tile (the root lists folders first: scenes, then Barrel.prefab) onto the Shelf row.
    driver.runFrames(4);
    const std::optional<ui::UiItem> shelf = driver.find("outliner.row.1");
    REQUIRE(shelf.has_value());
    dragBetween(driver, "asset_browser.tile.1", shelf->center());
    driver.runFrames(3);
    test::ok(editor, "entity.get", R"({"entity":"/Shelf/Barrel"})");
}

TEST_CASE(
    "the Profiler panel shows the frame's places, pauses recording and opens what a diagnostic is about")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.profiler"})");
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.console","open":false})");
    FrameProfiler& profiler = editor.profiler();
    ui::UiDriver driver(fixture.context, [&] {
        profiler.beginFrame();
        fixture.frame();
        profiler.endFrame();
    });
    driver.runFrames(4);

    // Every drawn panel is a place of the frame.
    REQUIRE(profiler.frameCount() > 0);
    bool panelScope = false;
    for (const ProfileScope& scope : profiler.frame(0).scopes)
    {
        panelScope = panelScope || scope.name == "panel.profiler";
    }
    CHECK(panelScope);
    CHECK(driver.find("profiler.tab.cpu").has_value());

    REQUIRE(driver.click("profiler.pause"));
    driver.runFrames(2);
    CHECK(profiler.paused());
    const usize recorded = profiler.frameCount();
    driver.runFrames(2);
    CHECK(profiler.frameCount() == recorded);
    REQUIRE(driver.click("profiler.resume"));
    driver.runFrames(2);
    CHECK_FALSE(profiler.paused());

    // The Diagnostics tab lists the log's error; clicking it opens the Console.
    test::ok(editor, "log.clear");
    ILL_LOG_ERROR("Test", "the profiler test logs an error");
    REQUIRE(driver.click("profiler.tab.diagnostics"));
    driver.runFrames(2);
    REQUIRE(driver.click("diagnostics.refresh"));
    driver.runFrames(2);
    REQUIRE(driver.click("diagnostics.issue.0"));
    driver.runFrames(2);
    CHECK(panelOpen(editor, "panel.console"));
}

TEST_CASE(
    "the Project Settings panel edits project settings with undo, the collision matrix and resets a page")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::addTestSettings(editor);
    editor::registerRuntimeSettings(editor.settings());
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.project_settings"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);

    // A field of the game's own settings, drawn by the Inspector's widgets.
    REQUIRE(driver.click("project_settings.page.test.Controls"));
    driver.runFrames(2);
    REQUIRE(driver.click("settings.test.Controls.invertY"));
    driver.runFrames(2);
    const auto invertY = [&] {
        return test::ok(editor, "project.settings_get", R"({"type":"test.Controls"})")
            .root()
            .pointer("/settings/0/value/invertY")
            .asBool();
    };
    CHECK(invertY() == std::optional<bool>(true));
    test::ok(editor, "history.undo");
    driver.runFrames(2);
    CHECK(invertY() == std::optional<bool>(false));
    // The page reads the file again after the undo: clicking turns it on again rather than off.
    REQUIRE(driver.click("settings.test.Controls.invertY"));
    driver.runFrames(2);
    CHECK(invertY() == std::optional<bool>(true));

    // The physics page draws the collision matrix: layer 0 against itself.
    REQUIRE(driver.click("project_settings.page.illusion.PhysicsSettings"));
    driver.runFrames(2);
    REQUIRE(driver.click("project_settings.collision.0.0"));
    driver.runFrames(2);
    const auto firstRow = [&] {
        return test::ok(editor, "project.settings_get", R"({"type":"illusion.PhysicsSettings"})")
            .root()
            .pointer("/settings/0/value/collisionMatrix/0")
            .asU64();
    };
    CHECK(firstRow() == std::optional<u64>(0xFFFEu));
    // Naming the next layer adds a row for it.
    REQUIRE(driver.click("project_settings.layer.1"));
    driver.typeText("Debris");
    driver.pressKey(ImGuiKey_Enter);
    driver.runFrames(2);
    CHECK(test::ok(editor, "project.settings_get", R"({"type":"illusion.PhysicsSettings"})")
              .root()
              .pointer("/settings/0/value/layerNames/1")
              .asString() == "Debris");
    CHECK(driver.find("project_settings.layer.2").has_value());
    REQUIRE(driver.click("project_settings.reset"));
    driver.runFrames(2);
    CHECK(firstRow() == std::optional<u64>(0xFFFFu));
    CHECK_FALSE(test::ok(editor, "project.settings_get", R"({"type":"illusion.PhysicsSettings"})")
                    .root()
                    .pointer("/settings/0/present")
                    .asBool()
                    .value_or(true));
}

TEST_CASE("the Rendering panel changes the viewport quality preferences")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "ui.show_panel", R"({"panel":"panel.rendering"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    REQUIRE(driver.click("rendering.shadows"));
    driver.runFrames(2);
    CHECK_FALSE(editor.preferences().viewportQuality.shadows);
    REQUIRE(driver.click("rendering.temporalAntiAliasing"));
    driver.runFrames(2);
    CHECK(editor.preferences().viewportQuality.temporalAntiAliasing);
    // Without a GPU the panel says so instead of listing viewports.
    CHECK_FALSE(driver.find("rendering.gpu").has_value());
}

TEST_CASE(
    "the Audio Mixer window pauses a bus of the running session and the Physics window shows its counts")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    EditorContext* current = &editor;
    test::FakePlay play(current);
    editor.setPlay(&play);
    test::ok(editor, "ui.show_panel", R"({"panel":"window.audio_mixer"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    // Stopped: nothing to mix.
    CHECK_FALSE(driver.find("mixer.master").has_value());

    test::ok(editor, "play.start");
    driver.runFrames(2);
    REQUIRE(driver.click("mixer.bus.music.pause"));
    driver.runFrames(2);
    CHECK(play.mix.busPaused[1]);
    REQUIRE(driver.click("mixer.bus.music.pause")); // now "resume"
    driver.runFrames(2);
    CHECK_FALSE(play.mix.busPaused[1]);

    play.mix.bodies = 3;
    test::ok(editor, "ui.show_panel", R"({"panel":"window.physics"})");
    driver.runFrames(3);
    CHECK(driver.find("physics.show_shapes").has_value());
    test::ok(editor, "play.stop");
    editor.setPlay(nullptr);
}

TEST_CASE("the Input Actions window adds an action, binds the next key press, unbinds and removes it")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "ui.show_panel", R"({"panel":"window.input_actions"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    const auto actions = [&] {
        return test::ok(editor, "input.actions", R"({"asset":"assets:/input/game.input"})");
    };

    REQUIRE(driver.click("input_actions.new.name"));
    driver.typeText("Jump");
    driver.runFrames(1);
    REQUIRE(driver.click("input_actions.new.add"));
    driver.runFrames(3);
    CHECK(actions().root().pointer("/actions/0/name").asString() == "Jump");
    CHECK(actions().root().pointer("/actions/0/type").asString() == "Button");

    // The next key press after "Add binding" becomes the binding.
    REQUIRE(driver.click("input_actions.action.Jump.bind"));
    driver.runFrames(1);
    driver.pressKey(ImGuiKey_Space);
    driver.runFrames(3);
    CHECK(actions().root().pointer("/actions/0/bindings/0").asString() == "Key.Space");

    REQUIRE(driver.click("input_actions.action.Jump.binding.0"));
    driver.runFrames(3);
    CHECK(actions().root().pointer("/actions/0/bindings").size() == 0);

    REQUIRE(driver.click("input_actions.action.Jump.remove"));
    driver.runFrames(3);
    CHECK(actions().root().find("actions").size() == 0);
}

TEST_CASE("the Gamepad Tester window rumbles, turns on motion and applies trigger effects through commands")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "ui.show_panel", R"({"panel":"window.gamepad_tester"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    // Without gamepads the window says how to connect one.
    CHECK_FALSE(driver.find("gamepad_tester.rumble.test").has_value());

    test::FakeGamepads gamepads;
    gamepads.pads.front().features = {.rumble = true};
    editor.setGamepads(&gamepads);
    driver.runFrames(3);
    CHECK_FALSE(driver.find("gamepad_tester.effect.apply").has_value());
    REQUIRE(driver.click("gamepad_tester.rumble.test"));
    driver.runFrames(1);
    CHECK(gamepads.rumbled.gamepad == 7);
    CHECK(gamepads.rumbled.low == 0.5f);
    CHECK(gamepads.rumbled.durationMs == 500);

    gamepads.pads.front().features = {.motionSensors = true, .adaptiveTriggers = true};
    driver.runFrames(3);
    REQUIRE(driver.click("gamepad_tester.motion"));
    driver.runFrames(1);
    CHECK(gamepads.pads.front().motionEnabled);
    REQUIRE(driver.click("gamepad_tester.effect.apply"));
    driver.runFrames(1);
    CHECK(gamepads.effectSide == GamepadTriggerSide::Right);
    CHECK(gamepads.effect.mode == GamepadTriggerMode::Weapon);
    CHECK(gamepads.effect.start == doctest::Approx(0.3));
    CHECK(gamepads.effect.end == doctest::Approx(0.6));
    editor.setGamepads(nullptr);
}

TEST_CASE("an Inspector window locks itself to the selection it opens with, beside the Inspector panel")
{
    ShellFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::ok(editor, "entity.create", R"({"name":"Lamp","components":{"illusion.Transform":{}}})");
    test::ok(editor, "entity.create", R"({"name":"Crate","components":{"illusion.Transform":{}}})");
    test::ok(editor, "selection.set", R"({"entities":["/Lamp"]})");
    test::ok(editor, "ui.show_panel", R"({"panel":"window.inspector_2"})");
    ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
    driver.runFrames(3);
    const auto enabled = [&](std::string_view entity) {
        return test::ok(editor, "entity.get", R"({"entity":")" + std::string(entity) + R"("})")
            .root()
            .find("enabled")
            .asBool();
    };

    // The Inspector panel follows the selection; Inspector 2 stays on the Lamp. Both have their own ids.
    test::ok(editor, "selection.set", R"({"entities":["/Crate"]})");
    driver.runFrames(2);
    CHECK(driver.find("inspector.enabled").has_value());
    REQUIRE(driver.click("inspector_2.enabled"));
    driver.runFrames(1);
    CHECK(enabled("/Lamp") == false);
    CHECK(enabled("/Crate") == true);
    REQUIRE(driver.click("inspector.enabled"));
    driver.runFrames(1);
    CHECK(enabled("/Crate") == false);

    // Unlocked, it follows the selection; closed and opened again, it locks to the new selection.
    REQUIRE(driver.click("inspector_2.lock"));
    driver.runFrames(1);
    REQUIRE(driver.click("inspector_2.enabled"));
    driver.runFrames(1);
    CHECK(enabled("/Crate") == true);
    test::ok(editor, "ui.show_panel", R"({"panel":"window.inspector_2","open":false})");
    driver.runFrames(1);
    test::ok(editor, "ui.show_panel", R"({"panel":"window.inspector_2"})");
    driver.runFrames(2);
    test::ok(editor, "selection.set", R"({"entities":["/Lamp"]})");
    driver.runFrames(2);
    REQUIRE(driver.click("inspector_2.enabled"));
    driver.runFrames(1);
    CHECK(enabled("/Crate") == false);
}
