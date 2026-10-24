#include "core/EditorCommandCases.hpp"
#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/core/Preferences.hpp>
#include <illusion/editor/core/UiActions.hpp>

#include <fstream>
#include <set>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

TEST_CASE("every UI action runs a registered command, or names the milestone that brings it")
{
    EditorContext editor(EditorContextOptions{.seed = 1});
    std::set<std::string_view> ids;
    for (const UiAction& action : uiActions())
    {
        INFO(action.id);
        CHECK(ids.insert(action.id).second);
        const CommandDescriptor* command = editor.commands().find(action.command);
        if (action.milestone.empty())
        {
            CHECK(command != nullptr);
        }
        else
        {
            // When the command lands, the action loses its milestone.
            CHECK(command == nullptr);
            CHECK(action.title.table != nullptr);
        }
        CHECK((action.title.table != nullptr || command != nullptr));
        const JsonDocument params = test::parseJson(action.params.empty() ? "{}" : action.params);
        CHECK(params.root().isObject());
        CHECK(findUiAction(action.id) == &action);
    }
    CHECK(findUiAction("no.such") == nullptr);
    CHECK(uiMenuName(UiMenu::Window) == "window");
}

TEST_CASE("chords parse and format in the same form, digits and Cmd included")
{
    const std::optional<Chord> chord = parseChord("Ctrl+Shift+P");
    REQUIRE(chord.has_value());
    CHECK(chord->key == Key::P);
    CHECK(chord->modifiers == (kChordCtrl | kChordShift));
    CHECK(formatChord(*chord) == "Ctrl+Shift+P");
    CHECK(formatChord(*parseChord("Cmd+1")) == "Ctrl+1");
    CHECK(formatChord(*parseChord("F2")) == "F2");
    CHECK(formatChord(*parseChord("Alt+Super+Delete")) == "Alt+Super+Delete");
    for (const std::string_view bad :
         {"", "Ctrl+", "Ctrl", "Ctrl+Ctrl+P", "Shift+LeftShift", "Hyper+P", "P+Q", "ctrl+p", "+"})
    {
        INFO(bad);
        CHECK_FALSE(parseChord(bad).has_value());
    }
    CHECK(formatChord(Chord{}).empty());
}

TEST_CASE("every keymap preset is free of conflicts and binds the palette")
{
    EditorContext editor(EditorContextOptions{.seed = 1});
    for (const std::string_view preset : keymapPresets())
    {
        INFO(preset);
        const Keymap keymap = Keymap::build(preset, {}, &editor.commands());
        CHECK(keymap.preset() == preset);
        CHECK(keymap.conflicts().empty());
        const usize palette = static_cast<usize>(findUiAction("edit.palette") - uiActions().data());
        CHECK(keymap.chord(palette).valid());
        CHECK(keymap.find(keymap.chord(palette)) == palette);
    }
    const Keymap illusionKeys = Keymap::build("illusion", {}, &editor.commands());
    const usize undo = static_cast<usize>(findUiAction("edit.undo") - uiActions().data());
    CHECK(illusionKeys.text(undo) == "Ctrl+Z"); // the command's default shortcut
    const Keymap blender = Keymap::build("blender", {}, &editor.commands());
    const usize duplicate = static_cast<usize>(findUiAction("edit.duplicate") - uiActions().data());
    CHECK(blender.text(duplicate) == "Shift+D");
    CHECK(Keymap::build("emacs", {}, &editor.commands()).preset() == "illusion");
}

TEST_CASE("the user's shortcuts override the preset and conflicts between them are reported")
{
    EditorContext editor(EditorContextOptions{.seed = 1});
    const usize undo = static_cast<usize>(findUiAction("edit.undo") - uiActions().data());
    const usize redo = static_cast<usize>(findUiAction("edit.redo") - uiActions().data());
    const std::vector<ShortcutBinding> overrides{
        {"edit.redo", "Ctrl+Z"}, {"edit.paste", ""}, {"no.such", "F9"}, {"edit.copy", "Bad+Key"}};
    const Keymap keymap = Keymap::build("illusion", overrides, &editor.commands());
    REQUIRE(keymap.conflicts().size() == 1);
    CHECK(keymap.conflicts().front().first == undo);
    CHECK(keymap.conflicts().front().second == redo);
    const usize paste = static_cast<usize>(findUiAction("edit.paste") - uiActions().data());
    CHECK_FALSE(keymap.chord(paste).valid());
    const usize copy = static_cast<usize>(findUiAction("edit.copy") - uiActions().data());
    CHECK(keymap.text(copy) == "Ctrl+C"); // an unparsable override is ignored
    CHECK_FALSE(keymap.find(Chord{}).has_value());
}

TEST_CASE("preferences round-trip through their file and reject invalid values")
{
    Preferences preferences;
    preferences.theme = "light";
    preferences.uiScale = 1.5f;
    preferences.shortcuts = {{"edit.duplicate", "Shift+D"}};
    preferences.recentProjects = {"C:/projects/tetris"};
    const Expected<Preferences, std::string> read = readPreferences(writePreferences(preferences));
    REQUIRE_MESSAGE(read.hasValue(), (read ? std::string() : read.error()));
    CHECK(read->theme == "light");
    CHECK(read->uiScale == 1.5f);
    REQUIRE(read->shortcuts.size() == 1);
    CHECK(read->shortcuts.front().chord == "Shift+D");
    CHECK(read->recentProjects == preferences.recentProjects);

    CHECK_FALSE(readPreferences("not json").hasValue());
    CHECK_FALSE(readPreferences(R"({"format":"illusion.settings","version":1,"data":{}})").hasValue());
    CHECK_FALSE(readPreferences(R"({"format":"illusion.preferences","version":1,"data":{"theme":"pink"}})")
                    .hasValue());
    CHECK(readPreferences(R"({"format":"illusion.preferences","version":1,"data":{}})").hasValue());

    const auto invalid = [](auto change) {
        Preferences value;
        change(value);
        return !validatePreferences(value).hasValue();
    };
    CHECK(invalid([](Preferences& p) { p.accent = "red"; }));
    CHECK(invalid([](Preferences& p) { p.uiScale = 3.0f; }));
    CHECK(invalid([](Preferences& p) { p.locale = "not a tag!"; }));
    CHECK(invalid([](Preferences& p) { p.keymap = "emacs"; }));
    CHECK(invalid([](Preferences& p) { p.shortcuts = {{"no.such", "F1"}}; }));
    CHECK(invalid([](Preferences& p) { p.shortcuts = {{"edit.undo", "Bad"}}; }));
    CHECK(invalid([](Preferences& p) { p.shortcuts = {{"edit.undo", "F1"}, {"edit.undo", "F2"}}; }));
    CHECK(invalid([](Preferences& p) { p.agentPermissions = "yes"; }));
    CHECK(invalid([](Preferences& p) { p.autosaveInterval = -1.0; }));
    CHECK(invalid([](Preferences& p) { p.autosaveCopies = 0; }));
    CHECK(invalid([](Preferences& p) { p.layout.clear(); }));
    CHECK(invalid([](Preferences& p) { p.recentProjects.assign(kMaxRecentProjects + 1, "x"); }));
}

TEST_CASE("recent projects keep the newest first without duplicates and at most ten")
{
    std::vector<std::string> recent;
    for (int i = 0; i < 12; ++i)
    {
        addRecentProject(recent, "p" + std::to_string(i));
    }
    CHECK(recent.size() == kMaxRecentProjects);
    CHECK(recent.front() == "p11");
    addRecentProject(recent, "p5");
    CHECK(recent.front() == "p5");
    CHECK(std::count(recent.begin(), recent.end(), "p5") == 1);
}

TEST_CASE("preferences.set saves to the user directory and the next editor loads them")
{
    test::TempDirectory user;
    {
        EditorContext editor(EditorContextOptions{.seed = 1, .userDirectory = user.path});
        const u64 version = editor.preferencesVersion();
        const JsonDocument result =
            ok(editor, "preferences.set", R"({"value":{"theme":"high-contrast","autosaveInterval":30}})");
        CHECK(result.root().find("value").find("theme").asString() ==
              std::optional<std::string_view>("high-contrast"));
        CHECK(editor.preferencesVersion() > version);
        CHECK(editor.options().autosaveInterval == 30.0);
        // A dry run changes nothing.
        ok(editor, "preferences.set", R"({"value":{"theme":"light"},"dryRun":true})");
        CHECK(editor.preferences().theme == "high-contrast");
    }
    EditorContext reopened(EditorContextOptions{.seed = 1, .userDirectory = user.path});
    CHECK(reopened.preferences().theme == "high-contrast");
    CHECK(reopened.options().autosaveInterval == 30.0);
    CHECK(test::parseJson(test::run(reopened, "preferences.get").value())
              .root()
              .find("value")
              .find("theme")
              .asString() == std::optional<std::string_view>("high-contrast"));
}

TEST_CASE("preferences.set reports the invalid field with a pointer and suggestions")
{
    EditorContext editor(EditorContextOptions{.seed = 1});
    const CommandError unknown = fails(editor, "preferences.set", R"({"value":{"theem":"light"}})");
    CHECK(unknown.kind == ErrorKind::ValidationFailed);
    REQUIRE_FALSE(unknown.errors.empty());
    CHECK(unknown.errors.front().pointer == "/value/theem");
    CHECK(std::find(unknown.suggestions.begin(), unknown.suggestions.end(), "theme") !=
          unknown.suggestions.end());
    const CommandError bad = fails(editor, "preferences.set", R"({"value":{"theme":"pink"}})");
    REQUIRE_FALSE(bad.errors.empty());
    CHECK(bad.errors.front().pointer == "/value/theme");
    CHECK(fails(editor, "preferences.set", R"({"value":[1]})").errors.front().pointer == "/value");
    // Replacing resets every field but keeps the recent projects.
    ok(editor, "preferences.set", R"({"value":{"theme":"light","recentProjects":["a"]}})");
    ok(editor, "preferences.set", R"({"value":{"uiScale":1.25},"replace":true})");
    CHECK(editor.preferences().theme == "dark");
    CHECK(editor.preferences().recentProjects == std::vector<std::string>{"a"});
}

TEST_CASE("a corrupt preferences file gives the defaults and a warning")
{
    test::TempDirectory user;
    {
        std::ofstream file(user.path / "preferences.json", std::ios::binary);
        file << "{ broken";
    }
    std::string warning;
    const Preferences preferences = loadPreferences(user.path, warning);
    CHECK(preferences.theme == "dark");
    CHECK(warning.find("preferences.json") != std::string::npos);
    warning.clear();
    CHECK(loadPreferences(user.path / "missing", warning).theme == "dark");
    CHECK(warning.empty());
}

TEST_CASE("opening a project adds it to the recent projects")
{
    test::TempDirectory user;
    EditorFixture fixture(1, EditorContextOptions{.userDirectory = user.path});
    const std::vector<std::string>& recent = fixture.editor->preferences().recentProjects;
    REQUIRE(recent.size() == 1);
    CHECK(recent.front().find("project") != std::string::npos);
    EditorContext reopened(EditorContextOptions{.seed = 1, .userDirectory = user.path});
    CHECK(reopened.preferences().recentProjects == recent);
}

TEST_CASE("the ui commands drive the UI host and fail without one")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const JsonDocument state = ok(editor, "ui.state");
    CHECK(state.root().find("panels")[0].find("id").asString() ==
          std::optional<std::string_view>("panel.console"));
    ok(editor, "ui.show_panel", R"({"panel":"panel.console"})");
    CHECK(fixture.ui.consoleOpen);
    ok(editor, "ui.show_panel", R"({"panel":"panel.console","open":false})");
    CHECK_FALSE(fixture.ui.consoleOpen);
    const CommandError unknown = fails(editor, "ui.show_panel", R"({"panel":"panel.consol"})");
    CHECK(unknown.kind == ErrorKind::NotFound);
    CHECK(unknown.suggestions.front() == "panel.console");

    ok(editor, "ui.layout_save", R"({"name":"wide"})");
    CHECK(fixture.ui.active == "wide");
    CHECK(fails(editor, "ui.layout_save", R"({"name":"default"})").kind == ErrorKind::AlreadyExists);
    CHECK(fails(editor, "ui.layout_save", R"({"name":"no spaces"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "ui.layout_delete", R"({"name":"default"})").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "ui.layout_apply", R"({"name":"nope"})").kind == ErrorKind::NotFound);
    ok(editor, "ui.layout_delete", R"({"name":"wide"})");
    ok(editor, "ui.palette", R"({"mode":"settings","query":"keys"})");
    CHECK(fixture.ui.palette == "settings:keys");
    CHECK(fails(editor, "ui.notify", R"({"message":""})").kind == ErrorKind::ValidationFailed);
    ok(editor, "ui.notify", R"({"message":"Hi"})");
    CHECK(fixture.ui.notices == std::vector<std::string>{"Hi"});
    CHECK(fails(editor, "editor.screenshot", R"({"width":10,"height":10})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "editor.screenshot", R"({"width":128})").kind == ErrorKind::ValidationFailed);
    const JsonDocument shot = ok(editor, "editor.screenshot", R"({"width":128,"height":96})");
    CHECK(shot.root().find("width").asU64() == 128u);
    CHECK(shot.root().find("_images")[0].find("mimeType").asString() ==
          std::optional<std::string_view>("image/png"));
    CHECK(shot.root().find("_images")[0].find("data").asString() ==
          std::optional<std::string_view>("iVBORw0KGgo="));

    editor.setUi(nullptr);
    for (const std::string_view name : {"ui.state", "ui.layout_reset", "ui.quit"})
    {
        CHECK(fails(editor, name, "{}").kind == ErrorKind::Unsupported);
    }
    CHECK(fails(editor, "editor.screenshot", "{}").kind == ErrorKind::Unsupported);
}
