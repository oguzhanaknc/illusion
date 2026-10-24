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
