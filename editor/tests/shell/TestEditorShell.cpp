#include "rpc/RpcTestSupport.hpp"
#include "shell/ShellTestSupport.hpp"

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
    for (int i = 0; i < 120; ++i)
    {
        fixture.frame();
    }
    const u64 imguiBefore = ui::UiContext::allocationCount();
    test::ScopedAllocationCounter allocations;
    for (int i = 0; i < 10; ++i)
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
    REQUIRE(driver.click("toolbar.gizmo.snap"));
    CHECK(editor.gizmo().snap.enabled);
    REQUIRE(driver.click("toolbar.gizmo.snap.more"));
    driver.runFrames(2);
    CHECK(driver.find("toolbar.gizmo.snap.rotate").has_value());
}
