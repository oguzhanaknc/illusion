// The gameui.* commands (ADR 0057): elements with defaults, layouts of the edit and the play World, and
// clicks and focus in Play mode (FakePlay lays its World's UI out at 1920x1080).

#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <fstream>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

std::string stringAt(const JsonDocument& doc, std::string_view pointer)
{
    return std::string(doc.root().pointer(pointer).asString().value_or(""));
}

f64 number(const JsonDocument& doc, std::string_view pointer)
{
    return doc.root().pointer(pointer).asF64().value_or(-1.0);
}

// The element of a gameui.layout result with @p path, as a JSON Pointer prefix ("" when absent).
std::string elementPointer(const JsonDocument& layout, std::string_view path)
{
    const JsonValue elements = layout.root().find("elements");
    for (usize i = 0; i < elements.size(); ++i)
    {
        if (elements[i].pointer("/entity/path").asString().value_or("") == path)
        {
            return "/elements/" + std::to_string(i);
        }
    }
    return {};
}

std::string sceneHash(EditorContext& editor)
{
    return stringAt(ok(editor, "scene.info"), "/hash");
}

} // namespace

TEST_CASE("gameui.create makes a button with a label in a new canvas as one undo step")
{
    EditorFixture f;
    f.buildScene();
    EditorContext& editor = *f.editor;
    const std::string before = f.scene();
    const usize steps = editor.history().position();
    const JsonDocument made = ok(editor, "gameui.create", R"({"kind":"button","name":"Start","text":"Go"})");
    CHECK(stringAt(made, "/entity/path") == "/Canvas/Start");
    CHECK(stringAt(made, "/canvas/path") == "/Canvas");
    CHECK(stringAt(made, "/parts/0/role") == "label");
    CHECK(stringAt(made, "/parts/0/entity/path") == "/Canvas/Start/Label");
    CHECK(editor.history().position() == steps + 1);
    const JsonDocument label = ok(editor, "entity.get", R"({"entity":"/Canvas/Start/Label"})");
    CHECK(stringAt(label, "/components/illusion.UiText/text") == "Go");
    CHECK(stringAt(label, "/components/illusion.UiText/align") == "Center");
    const JsonDocument button = ok(editor, "entity.get", R"({"entity":"/Canvas/Start"})");
    CHECK(button.root().pointer("/components/illusion.UiButton").isAbsent() == false);
    CHECK(button.root().pointer("/components/illusion.UiInteractable/enabled").asBool() == true);
    const std::string after = f.scene();

    ok(editor, "history.undo");
    CHECK(f.scene() == before);
    ok(editor, "history.redo");
    CHECK(f.scene() == after);
}

TEST_CASE("every kind of gameui.create makes the children its widget needs")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    struct Expectation
    {
        std::string_view kind;
        std::string_view component;
        std::vector<std::string_view> roles;
    };
    const Expectation expectations[] = {
        {"panel", "illusion.UiImage", {}},
        {"text", "illusion.UiText", {}},
        {"button", "illusion.UiButton", {"label"}},
        {"toggle", "illusion.UiToggle", {"check"}},
        {"slider", "illusion.UiSlider", {"fill", "handle"}},
        {"text_field", "illusion.UiTextField", {}},
        {"scroll_view", "illusion.UiScrollView", {"content"}},
        {"stack", "illusion.UiStack", {}},
        {"grid", "illusion.UiGrid", {}},
    };
    for (const Expectation& expectation : expectations)
    {
        CAPTURE(expectation.kind);
        const JsonDocument made =
            ok(editor, "gameui.create",
               R"({"kind":")" + std::string(expectation.kind) + R"(","parent":"/HUD"})");
        CHECK(made.root().find("canvas").isNull());
        const JsonDocument root =
            ok(editor, "entity.get", R"({"entity":")" + stringAt(made, "/entity/id") + R"("})");
        CHECK_FALSE(root.root().pointer("/components/" + std::string(expectation.component)).isAbsent());
        CHECK_FALSE(root.root().pointer("/components/illusion.UiElement").isAbsent());
        const JsonValue parts = made.root().find("parts");
        REQUIRE(parts.size() == expectation.roles.size());
        for (usize i = 0; i < parts.size(); ++i)
        {
            CHECK(parts[i].find("role").asString() == expectation.roles[i]);
        }
    }
    const JsonDocument check = ok(editor, "entity.get", R"({"entity":"/HUD/Toggle/Check"})");
    CHECK_FALSE(check.root().pointer("/components/illusion.UiImage").isAbsent());
    const JsonDocument content = ok(editor, "entity.get", R"({"entity":"/HUD/Scroll View/Content"})");
    CHECK_FALSE(content.root().pointer("/components/illusion.UiStack").isAbsent());
}

TEST_CASE("gameui.create puts an element under the selected UI entity, else the first canvas")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"A"})");
    // A canvas inside another one comes after it in the hierarchy.
    const JsonDocument b = ok(editor, "gameui.create", R"({"kind":"canvas","name":"B","parent":"/A"})");
    ok(editor, "selection.set", R"({"entities":[")" + stringAt(b, "/entity/id") + R"("]})");
    CHECK(stringAt(ok(editor, "gameui.create", R"({"kind":"text"})"), "/entity/path") == "/A/B/Text");
    ok(editor, "selection.clear");
    CHECK(stringAt(ok(editor, "gameui.create", R"({"kind":"panel"})"), "/entity/path") == "/A/Panel");
    // A non-UI selection does not count.
    ok(editor, "entity.create", R"({"name":"Prop"})");
    ok(editor, "selection.set", R"({"entities":["/Prop"]})");
    CHECK(stringAt(ok(editor, "gameui.create", R"({"kind":"grid"})"), "/entity/path") == "/A/Grid");
}

TEST_CASE("gameui.create rejects a parent that is no UI entity, unknown kinds and invalid names")
{
    EditorFixture f;
    f.buildScene();
    EditorContext& editor = *f.editor;
    const std::string before = f.scene();
    CHECK(fails(editor, "gameui.create", R"({"kind":"button","parent":"/Game"})").errors.at(0).pointer ==
          "/parent");
    CHECK(fails(editor, "gameui.create", R"({"kind":10})").errors.at(0).pointer == "/kind");
    CHECK(fails(editor, "gameui.create", R"({"kind":"wheel"})").errors.at(0).pointer == "/kind");
    CHECK(fails(editor, "gameui.create", R"({"kind":"text","name":"a/b"})").errors.at(0).pointer == "/name");
    CHECK(fails(editor, "gameui.create", R"({"kind":"canvas","parent":"/Nothing"})").kind ==
          ErrorKind::NotFound);
    CHECK(f.scene() == before);
}

TEST_CASE("gameui.layout of the edit World matches hand-computed placements and changes nothing")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Corner","parent":"/HUD"})");
    ok(editor, "component.set_field",
       R"({"entity":"/HUD/Corner","type":"illusion.UiElement","path":"placement",)"
       R"("value":{"anchorMin":[1,1],"anchorMax":[1,1],"offsetMin":[-200,-100],"offsetMax":[-20,-20]}})");
    ok(editor, "gameui.create", R"({"kind":"button","name":"Start","parent":"/HUD"})");
    ok(editor, "scene.save");
    const std::string hash = sceneHash(editor);
    const usize position = editor.history().position();
    const u64 revision = editor.revision();

    // 1280x720 fits the 1920x1080 reference at 2/3 pixel per unit.
    const JsonDocument layout = ok(editor, "gameui.layout", R"({"width":1280,"height":720})");
    CHECK(stringAt(layout, "/world") == "edit");
    CHECK(number(layout, "/width") == 1280.0);
    REQUIRE(layout.root().find("canvases").size() == 1);
    CHECK(number(layout, "/canvases/0/pixelsPerUnit") == doctest::Approx(2.0 / 3.0));
    CHECK(number(layout, "/canvases/0/size/0") == doctest::Approx(1920.0));
    const std::string corner = elementPointer(layout, "/HUD/Corner");
    REQUIRE_FALSE(corner.empty());
    CHECK(number(layout, corner + "/min/0") == doctest::Approx(1720.0));
    CHECK(number(layout, corner + "/min/1") == doctest::Approx(980.0));
    CHECK(number(layout, corner + "/max/0") == doctest::Approx(1900.0));
    CHECK(number(layout, corner + "/max/1") == doctest::Approx(1060.0));
    CHECK(number(layout, corner + "/pixelMin/0") == doctest::Approx(1720.0 * 2.0 / 3.0));
    CHECK(number(layout, corner + "/pixelMax/1") == doctest::Approx(1060.0 * 2.0 / 3.0));
    CHECK(layout.root().pointer(corner + "/interactable").isNull());
    CHECK(layout.root().pointer(corner + "/focused").isNull());
    const std::string start = elementPointer(layout, "/HUD/Start");
    CHECK(number(layout, start + "/min/0") == doctest::Approx(24.0));
    CHECK(number(layout, start + "/max/0") == doctest::Approx(264.0));
    CHECK(number(layout, start + "/max/1") == doctest::Approx(88.0));
    CHECK(layout.root().pointer(start + "/interactable").asBool() == true);
    // Children come after their parent: the button's label follows it.
    CHECK(elementPointer(layout, "/HUD/Start/Label") > start);
    // The scope of one element is it and its subtree.
    const JsonDocument scoped = ok(editor, "gameui.layout", R"({"entity":"/HUD/Start"})");
    CHECK(scoped.root().find("elements").size() == 2);
    CHECK(number(scoped, "/width") == 1920.0);

    CHECK(sceneHash(editor) == hash);
    CHECK_FALSE(editor.dirty());
    CHECK(editor.history().position() == position);
    CHECK(editor.revision() == revision);
}

TEST_CASE("gameui.layout measures a content-sized text with the project's font")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    // The fixture removes the template's font; put it back.
    const std::filesystem::path fonts = f.directory.path / "project" / "assets" / "fonts";
    std::filesystem::create_directories(fonts);
    std::filesystem::copy_file(editorDataDirectory() / "templates" / "empty" / "assets" / "fonts" /
                                   "Roboto-Regular.ttf",
                               fonts / "Roboto-Regular.ttf");
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"stack","name":"List","parent":"/HUD"})");
    const JsonDocument made =
        ok(editor, "gameui.create", R"({"kind":"text","parent":"/HUD/List","text":"Hello"})");
    const JsonDocument shown =
        ok(editor, "entity.get", R"({"entity":")" + stringAt(made, "/entity/id") + R"("})");
    REQUIRE(stringAt(shown, "/components/illusion.UiText/font") == "assets:/fonts/Roboto-Regular.ttf");

    AssetDatabase database(editor.files(), "");
    const std::optional<Font> font = database.loadFont("assets:/fonts/Roboto-Regular.ttf");
    REQUIRE(font.has_value());
    const f64 lineHeight = static_cast<f64>(font->metrics().lineHeight()) * 32.0;
    const JsonDocument layout = ok(editor, "gameui.layout");
    const std::string element = elementPointer(layout, "/HUD/List/Text");
    REQUIRE_FALSE(element.empty());
    CHECK(number(layout, element + "/min/1") == doctest::Approx(24.0));
    CHECK(number(layout, element + "/max/1") - number(layout, element + "/min/1") ==
          doctest::Approx(lineHeight).epsilon(0.001));
    // The stack stretches it across its width (crossAlign Stretch).
    CHECK(number(layout, element + "/max/0") == doctest::Approx(344.0));
}

TEST_CASE("gameui.click clicks a button and flips a toggle of the running game")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"button","name":"Start"})");
    ok(editor, "gameui.create", R"({"kind":"toggle","name":"Music","parent":"/Canvas"})");
    ok(editor, "component.set_field",
       R"({"entity":"/Canvas/Music","type":"illusion.UiElement","path":"placement/offsetMin","value":[24,200]})");
    ok(editor, "component.set_field",
       R"({"entity":"/Canvas/Music","type":"illusion.UiElement","path":"placement/offsetMax","value":[72,248]})");
    CHECK(fails(editor, "gameui.click", R"({"entity":"/Canvas/Start"})").kind == ErrorKind::WrongMode);
    ok(editor, "play.start");

    const JsonDocument clicked = ok(editor, "gameui.click", R"({"entity":"/Canvas/Start"})");
    CHECK(number(clicked, "/pixels/0") == doctest::Approx(144.0));
    CHECK(number(clicked, "/pixels/1") == doctest::Approx(56.0));
    CHECK(number(clicked, "/updates") == 2.0);
    const JsonValue events = clicked.root().find("events");
    bool sawClick = false;
    for (usize i = 0; i < events.size(); ++i)
    {
        if (events[i].find("type").asString() == "clicked")
        {
            sawClick = events[i].pointer("/entity/path").asString() == "/Canvas/Start";
        }
    }
    CHECK(sawClick);

    // The check mark is hidden while the toggle is off.
    CHECK(fails(editor, "gameui.click", R"({"entity":"/Canvas/Music/Check"})").message.find("hidden") !=
          std::string::npos);
    const JsonDocument toggled = ok(editor, "gameui.click", R"({"entity":"/Canvas/Music"})");
    CHECK(toggled.root().pointer("/events").size() >= 1);
    const JsonDocument music = ok(editor, "entity.get", R"({"entity":"/Canvas/Music"})");
    CHECK(music.root().pointer("/components/illusion.UiToggle/on").asBool() == true);
    // Play mode leaves the edit World as it was.
    const JsonDocument edited = ok(editor, "entity.get", R"({"entity":"/Canvas/Music","world":"edit"})");
    CHECK(edited.root().pointer("/components/illusion.UiToggle/on").asBool() == false);
    ok(editor, "play.stop");
}

TEST_CASE("gameui.click says what keeps it from clicking an element")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"button","name":"Under"})");
    ok(editor, "gameui.create", R"({"kind":"button","name":"Over","parent":"/Canvas"})");
    ok(editor, "gameui.create", R"({"kind":"button","name":"Off","parent":"/Canvas"})");
    ok(editor, "component.set_field",
       R"({"entity":"/Canvas/Off","type":"illusion.UiElement","path":"placement/offsetMin","value":[24,300]})");
    ok(editor, "component.set_field",
       R"({"entity":"/Canvas/Off","type":"illusion.UiElement","path":"placement/offsetMax","value":[264,364]})");
    ok(editor, "component.set_field",
       R"({"entity":"/Canvas/Off","type":"illusion.UiInteractable","path":"enabled","value":false})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Faded","parent":"/Canvas"})");
    ok(editor, "component.set_field",
       R"({"entity":"/Canvas/Faded","type":"illusion.UiElement","path":"opacity","value":0})");
    ok(editor, "play.start");

    const CommandError covered = fails(editor, "gameui.click", R"({"entity":"/Canvas/Under"})");
    CHECK(covered.kind == ErrorKind::PreconditionFailed);
    CHECK(covered.message.find("covered") != std::string::npos);
    CHECK(covered.message.find("/Canvas/Over") != std::string::npos);
    CHECK(fails(editor, "gameui.click", R"({"entity":"/Canvas/Off"})").message.find("disabled") !=
          std::string::npos);
    CHECK(fails(editor, "gameui.click", R"({"entity":"/Canvas/Faded"})").message.find("opacity") !=
          std::string::npos);
    CHECK(fails(editor, "gameui.click", R"({"entity":"/Canvas"})").message.find("not a UI element") !=
          std::string::npos);
    CHECK(
        fails(editor, "gameui.click", R"({"entity":"/Canvas/Over/Label"})").message.find("UiInteractable") !=
        std::string::npos);
    // The element on top can be clicked.
    ok(editor, "gameui.click", R"({"entity":"/Canvas/Over"})");
    ok(editor, "play.stop");
}

TEST_CASE("gameui.focus gives an element of the running game the focus and clears it")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"button","name":"Start"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Back","parent":"/Canvas"})");
    CHECK(fails(editor, "gameui.focus", R"({"entity":"/Canvas/Start"})").kind == ErrorKind::WrongMode);
    ok(editor, "play.start");

    CHECK(stringAt(ok(editor, "gameui.focus", R"({"entity":"/Canvas/Start"})"), "/focused/path") ==
          "/Canvas/Start");
    const JsonDocument layout = ok(editor, "gameui.layout");
    CHECK(stringAt(layout, "/world") == "play");
    CHECK(layout.root().pointer(elementPointer(layout, "/Canvas/Start") + "/focused").asBool() == true);
    // A panel cannot take the focus: it stays where it was, with a warning.
    const JsonDocument refused = ok(editor, "gameui.focus", R"({"entity":"/Canvas/Back"})");
    CHECK(stringAt(refused, "/focused/path") == "/Canvas/Start");
    CHECK(refused.root().pointer("/_editor/warnings").size() == 1);
    CHECK(ok(editor, "gameui.focus", R"({"entity":null})").root().find("focused").isNull());
    CHECK(ok(editor, "gameui.focus").root().find("focused").isNull());
    ok(editor, "play.stop");
}

TEST_CASE("gameui.layout measures a keyed text with the formatted message of the project's locale files")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    const std::filesystem::path assets = f.directory.path / "project" / "assets";
    std::filesystem::create_directories(assets / "fonts");
    std::filesystem::copy_file(editorDataDirectory() / "templates" / "empty" / "assets" / "fonts" /
                                   "Roboto-Regular.ttf",
                               assets / "fonts" / "Roboto-Regular.ttf");
    std::filesystem::create_directories(assets / "locales");
    std::ofstream(assets / "locales" / "en.json")
        << R"({"format": "illusion.locale", "version": 1, "locale": "en", "displayName": "English",)"
        << R"( "plural": "en", "strings": {"menu.title": "Hi"}})";
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"stack","name":"Row","parent":"/HUD"})");
    ok(editor, "component.set_field",
       R"({"entity":"/HUD/Row","type":"illusion.UiStack","path":"axis","value":"Horizontal"})");
    ok(editor, "gameui.create", R"({"kind":"text","name":"Title","parent":"/HUD/Row"})");
    ok(editor, "component.set_field",
       R"({"entity":"/HUD/Row/Title","type":"illusion.UiText","path":"key","value":"menu.title"})");

    AssetDatabase database(editor.files(), "");
    const std::optional<Font> font = database.loadFont("assets:/fonts/Roboto-Regular.ttf");
    REQUIRE(font.has_value());
    UiTextStyle style;
    style.size = 32.0f;
    const f64 formatted = static_cast<f64>(measureText(*font, "Hi", style).size.x);
    const f64 key = static_cast<f64>(measureText(*font, "menu.title", style).size.x);
    REQUIRE(formatted < key);

    const JsonDocument layout = ok(editor, "gameui.layout");
    const std::string element = elementPointer(layout, "/HUD/Row/Title");
    REQUIRE_FALSE(element.empty());
    CHECK(number(layout, element + "/max/0") - number(layout, element + "/min/0") ==
          doctest::Approx(formatted).epsilon(0.001));
}
