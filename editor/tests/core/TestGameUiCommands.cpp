// The gameui.* commands (ADR 0057): elements with defaults, layouts of the edit and the play World, and
// clicks and focus in Play mode (FakePlay lays its World's UI out at 1920x1080).

#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/ecs/Hierarchy.hpp>
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

TEST_CASE("gameui.place puts an element at a rectangle that the layout reports at any target size")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Box","parent":"/HUD"})");
    const std::string before = f.scene();
    const usize steps = editor.history().position();

    const JsonDocument placed =
        ok(editor, "gameui.place", R"({"entity":"/HUD/Box","min":[100,50],"max":[340,114]})");
    CHECK(stringAt(placed, "/entity/path") == "/HUD/Box");
    CHECK(number(placed, "/offsetMin/0") == doctest::Approx(100.0));
    CHECK(number(placed, "/offsetMax/1") == doctest::Approx(114.0));
    CHECK(placed.root().find("placedByLayout").asBool() == false);
    CHECK(editor.history().position() == steps + 1);

    // The anchors coincide at the top left, so the rectangle is the same on every target.
    for (const std::string_view target :
         {R"({"width":1280,"height":720})", R"({"width":1000,"height":1000})"})
    {
        CAPTURE(target);
        const JsonDocument layout = ok(editor, "gameui.layout", target);
        const std::string box = elementPointer(layout, "/HUD/Box");
        REQUIRE_FALSE(box.empty());
        CHECK(number(layout, box + "/min/0") == doctest::Approx(100.0));
        CHECK(number(layout, box + "/min/1") == doctest::Approx(50.0));
        CHECK(number(layout, box + "/max/0") == doctest::Approx(340.0));
        CHECK(number(layout, box + "/max/1") == doctest::Approx(114.0));
    }

    // Stretched anchors reach the rectangle at the given target size.
    ok(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"stretch","keepRect":false})");
    ok(editor, "gameui.place",
       R"({"entity":"/HUD/Box","min":[10,20],"max":[30,40],"width":1000,"height":1000})");
    const JsonDocument square = ok(editor, "gameui.layout", R"({"width":1000,"height":1000})");
    const std::string box = elementPointer(square, "/HUD/Box");
    CHECK(number(square, box + "/min/0") == doctest::Approx(10.0));
    CHECK(number(square, box + "/max/1") == doctest::Approx(40.0));

    ok(editor, "history.undo", R"({"steps":3})");
    CHECK(f.scene() == before);
}

TEST_CASE("gameui.set_anchors keeps the element's rectangle, or moves it with the anchors without keepRect")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Box","parent":"/HUD"})");
    const std::string before = f.scene();

    const JsonDocument anchored =
        ok(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"bottom_right"})");
    CHECK(number(anchored, "/anchorMin/0") == 1.0);
    CHECK(number(anchored, "/anchorMax/1") == 1.0);
    CHECK(number(anchored, "/offsetMin/0") == doctest::Approx(24.0 - 1920.0));
    const JsonDocument layout = ok(editor, "gameui.layout");
    const std::string box = elementPointer(layout, "/HUD/Box");
    CHECK(number(layout, box + "/min/0") == doctest::Approx(24.0));
    CHECK(number(layout, box + "/max/1") == doctest::Approx(324.0));

    const JsonDocument stretched = ok(editor, "gameui.set_anchors",
                                      R"({"entity":"/HUD/Box","anchorMin":[0.25,0],"anchorMax":[0.75,1]})");
    CHECK(number(stretched, "/anchorMin/0") == 0.25);
    const JsonDocument again = ok(editor, "gameui.layout");
    CHECK(number(again, box + "/min/0") == doctest::Approx(24.0));
    CHECK(number(again, box + "/max/0") == doctest::Approx(424.0));

    ok(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"center","keepRect":false})");
    const JsonDocument moved = ok(editor, "gameui.layout");
    CHECK(number(moved, box + "/min/0") == doctest::Approx(960.0 + 24.0 - 480.0));

    ok(editor, "history.undo", R"({"steps":3})");
    CHECK(f.scene() == before);
}

TEST_CASE("a child of a stack is sized by gameui.place and refused by gameui.set_anchors")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"stack","name":"List","parent":"/HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Row","parent":"/HUD/List"})");
    const JsonDocument row = ok(editor, "entity.get", R"({"entity":"/HUD/List/Row"})");
    const JsonDocument placed =
        ok(editor, "gameui.place", R"({"entity":"/HUD/List/Row","min":[0,0],"max":[200,60]})");
    CHECK(placed.root().find("placedByLayout").asBool() == true);
    CHECK(number(placed, "/size/0") == doctest::Approx(200.0));
    CHECK(number(placed, "/size/1") == doctest::Approx(60.0));
    const JsonDocument after = ok(editor, "entity.get", R"({"entity":"/HUD/List/Row"})");
    CHECK(number(after, "/components/illusion.UiElement/placement/offsetMin/0") ==
          number(row, "/components/illusion.UiElement/placement/offsetMin/0"));
    CHECK(number(after, "/components/illusion.UiElement/size/1") == doctest::Approx(60.0));
    CHECK(fails(editor, "gameui.set_anchors", R"({"entity":"/HUD/List/Row","preset":"center"})").kind ==
          ErrorKind::PreconditionFailed);
}

TEST_CASE("gameui.place and gameui.set_anchors reject what they cannot place")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Box","parent":"/HUD"})");
    ok(editor, "entity.create", R"({"name":"Prop"})");
    const std::string before = f.scene();
    CHECK(fails(editor, "gameui.place", R"({"entity":"/Prop","min":[0,0],"max":[1,1]})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "gameui.place", R"({"entity":"/HUD","min":[0,0],"max":[1,1]})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "gameui.place", R"({"entity":"/HUD/Box","min":[10,0],"max":[5,1]})")
              .errors.at(0)
              .pointer == "/max");
    CHECK(fails(editor, "gameui.place", R"({"entity":"/HUD/Box","min":[0,0],"max":[1,1],"width":0})")
              .errors.at(0)
              .pointer == "/width");
    CHECK(fails(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box"})").errors.at(0).pointer == "/preset");
    CHECK(fails(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"top","anchorMin":[0,0]})")
              .errors.at(0)
              .pointer == "/preset");
    CHECK(fails(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","anchorMin":[0,0]})")
              .errors.at(0)
              .pointer == "/anchorMax");
    CHECK(fails(editor, "gameui.set_anchors",
                R"({"entity":"/HUD/Box","anchorMin":[0.5,0],"anchorMax":[0.2,1]})")
              .errors.at(0)
              .pointer == "/anchorMax");
    CHECK(fails(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"sideways"})")
              .errors.at(0)
              .pointer == "/preset");
    CHECK(f.scene() == before);

    ok(editor, "entity.set_enabled", R"({"entity":"/HUD","enabled":false})");
    CHECK(fails(editor, "gameui.place", R"({"entity":"/HUD/Box","min":[0,0],"max":[1,1]})")
              .message.find("not laid out") != std::string::npos);
    // Without keepRect no layout is needed.
    ok(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"top","keepRect":false})");
    ok(editor, "history.undo", R"({"steps":2})");
    CHECK(f.scene() == before);
}

TEST_CASE("disabled canvases and elements of the edit World are neither laid out nor picked without a GPU")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Box","parent":"/HUD"})");
    ok(editor, "gameui.place", R"({"entity":"/HUD/Box","min":[500,500],"max":[700,700]})");
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"Menu"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Back","parent":"/Menu"})");
    ok(editor, "gameui.place", R"({"entity":"/Menu/Back","min":[1000,500],"max":[1200,700]})");
    ok(editor, "entity.set_enabled", R"({"entity":"/Menu","enabled":false})");
    ok(editor, "entity.set_enabled", R"({"entity":"/HUD/Box","enabled":false})");
    ok(editor, "scene.save");
    const std::string hash = sceneHash(editor);
    const usize position = editor.history().position();
    const u64 revision = editor.revision();

    const auto laidOut = [&](std::string_view path) {
        return !elementPointer(ok(editor, "gameui.layout"), path).empty();
    };
    const auto picked = [&](std::string_view at) {
        return stringAt(ok(editor, "gameui.pick", at), "/entity/path");
    };
    const JsonDocument disabled = ok(editor, "gameui.layout");
    REQUIRE(disabled.root().find("canvases").size() == 1);
    CHECK(stringAt(disabled, "/canvases/0/entity/path") == "/HUD");
    CHECK_FALSE(laidOut("/HUD/Box"));
    CHECK_FALSE(laidOut("/Menu/Back"));
    CHECK(picked(R"({"x":600,"y":600})").empty());
    CHECK(picked(R"({"x":1100,"y":600})").empty());
    CHECK(sceneHash(editor) == hash);
    CHECK_FALSE(editor.dirty());
    CHECK(editor.history().position() == position);
    CHECK(editor.revision() == revision);

    ok(editor, "entity.set_enabled", R"({"entity":"/Menu","enabled":true})");
    ok(editor, "entity.set_enabled", R"({"entity":"/HUD/Box","enabled":true})");
    CHECK(ok(editor, "gameui.layout").root().find("canvases").size() == 2);
    CHECK(laidOut("/HUD/Box"));
    CHECK(laidOut("/Menu/Back"));
    CHECK(picked(R"({"x":600,"y":600})") == "/HUD/Box");
    CHECK(picked(R"({"x":1100,"y":600})") == "/Menu/Back");

    // Undo disables them again.
    ok(editor, "history.undo", R"({"steps":2})");
    CHECK(ok(editor, "gameui.layout").root().find("canvases").size() == 1);
    CHECK_FALSE(laidOut("/Menu/Back"));
    CHECK(picked(R"({"x":600,"y":600})").empty());
    CHECK(sceneHash(editor) == hash);
}

TEST_CASE("gameui.pick finds the topmost element at a pixel, interactable or not")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
    ok(editor, "gameui.create", R"({"kind":"button","name":"Start","parent":"/HUD"})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Under","parent":"/HUD"})");
    ok(editor, "gameui.place", R"({"entity":"/HUD/Under","min":[500,500],"max":[700,700]})");
    ok(editor, "gameui.create", R"({"kind":"panel","name":"Over","parent":"/HUD"})");
    ok(editor, "gameui.place", R"({"entity":"/HUD/Over","min":[600,600],"max":[800,800]})");
    const usize steps = editor.history().position();

    // The button's label is a text without UiInteractable, drawn over the button. At 1280x720 a unit is
    // 2/3 of a pixel: the button's centre (144, 56) is at pixel (96, 37.3).
    const JsonDocument label = ok(editor, "gameui.pick", R"({"x":96,"y":37,"width":1280,"height":720})");
    CHECK(stringAt(label, "/entity/path") == "/HUD/Start/Label");
    const JsonValue path = label.root().find("path");
    REQUIRE(path.size() == 3);
    CHECK(path[0].find("path").asString() == "/HUD");
    CHECK(path[1].find("path").asString() == "/HUD/Start");
    CHECK(path[2].find("path").asString() == "/HUD/Start/Label");

    CHECK(stringAt(ok(editor, "gameui.pick", R"({"x":650,"y":650})"), "/entity/path") == "/HUD/Over");
    CHECK(stringAt(ok(editor, "gameui.pick", R"({"x":550,"y":550})"), "/entity/path") == "/HUD/Under");
    const JsonDocument nothing = ok(editor, "gameui.pick", R"({"x":1900,"y":1000})");
    CHECK(nothing.root().find("entity").isNull());
    CHECK(nothing.root().find("path").size() == 0);
    CHECK(editor.history().position() == steps);
}
