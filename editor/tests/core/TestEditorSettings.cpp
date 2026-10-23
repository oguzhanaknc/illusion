#include "EditorCoreTestSupport.hpp"

#include <illusion/editor/core/SettingsCatalog.hpp>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

std::string read(EditorContext& editor, std::string_view path)
{
    return std::string(ok(editor, "project.file_read", R"({"path":")" + std::string(path) + R"("})")
                           .root()
                           .find("text")
                           .asString()
                           .value_or(""));
}

// Writes @p text to @p path through project.file_write.
void write(EditorContext& editor, std::string_view path, std::string_view text)
{
    JsonMutDocument doc;
    JsonMutValue params = doc.makeObject();
    params.add("path", doc.makeString(path));
    params.add("text", doc.makeString(text));
    doc.setRoot(params);
    std::string json;
    REQUIRE(doc.write(json, JsonWriteOptions{.pretty = false}));
    ok(editor, "project.file_write", json);
}

} // namespace

TEST_CASE("scene settings are listed with defaults, merged, saved with the scene and undone")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;

    const JsonDocument listed = ok(editor, "scene.settings_get");
    REQUIRE(listed.root().find("settings").size() == 1);
    CHECK(listed.root().pointer("/settings/0/type").asString() == "test.Difficulty");
    CHECK(listed.root().pointer("/settings/0/present").asBool() == false);
    CHECK(listed.root().pointer("/settings/0/known").asBool() == true);
    CHECK(listed.root().pointer("/settings/0/value/enemies").asU64() == 3);

    const u64 revision = editor.revision();
    const JsonDocument set =
        ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"enemies":12}})");
    CHECK(set.root().find("changed").asBool() == true);
    CHECK(set.root().pointer("/value/speed").asF64() == doctest::Approx(1.0));
    CHECK(editor.revision() == revision + 1);
    CHECK(editor.dirty());
    ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"speed":2}})");
    const JsonDocument merged = ok(editor, "scene.settings_get", R"({"type":"test.Difficulty"})");
    CHECK(merged.root().pointer("/settings/0/present").asBool() == true);
    CHECK(merged.root().pointer("/settings/0/value/enemies").asU64() == 12);
    CHECK(merged.root().pointer("/settings/0/value/speed").asF64() == doctest::Approx(2.0));
    const usize steps = editor.history().position();
    CHECK(ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"speed":2}})")
              .root()
              .find("changed")
              .asBool() == false);
    CHECK(editor.history().position() == steps); // nothing changed: no undo step
    ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"speed":3},"replace":true})");
    CHECK(ok(editor, "scene.settings_get").root().pointer("/settings/0/value/enemies").asU64() == 3);

    // Entity references in settings are ids of the scene's entities.
    const std::string board(ok(editor, "entity.get", R"({"entity":"/Game/Board"})")
                                .root()
                                .pointer("/entity/id")
                                .asString()
                                .value_or(""));
    ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"boss":")" + board + R"("}})");
    CHECK(ok(editor, "scene.settings_get").root().pointer("/settings/0/value/boss").asString() == board);
    CHECK(fails(editor, "scene.settings_set",
                R"({"type":"test.Difficulty","value":{"boss":"0000000000000999"}})")
              .errors.at(0)
              .pointer == "/value/boss");

    // Saved in the scene file and read back.
    ok(editor, "scene.save");
    CHECK(read(editor, "assets:/scenes/main.scene").find("\"test.Difficulty\"") != std::string::npos);
    ok(editor, "scene.open", R"({"path":"assets:/scenes/main.scene"})");
    CHECK(ok(editor, "scene.settings_get").root().pointer("/settings/0/value/speed").asF64() ==
          doctest::Approx(3.0));

    ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":null})");
    CHECK(ok(editor, "scene.settings_get").root().pointer("/settings/0/present").asBool() == false);
    ok(editor, "history.undo");
    CHECK(ok(editor, "scene.settings_get").root().pointer("/settings/0/present").asBool() == true);

    const CommandError unknown =
        fails(editor, "scene.settings_set", R"({"type":"test.Dificulty","value":{}})");
    CHECK(unknown.kind == ErrorKind::NotFound);
    CHECK(unknown.errors.at(0).pointer == "/type");
    CHECK(unknown.suggestions.at(0) == "test.Difficulty");
    const CommandError field =
        fails(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"enemys":1}})");
    CHECK(field.errors.at(0).pointer == "/value/enemys");
    CHECK(field.suggestions.at(0) == "enemies");
    CHECK(fails(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"enemies":5000}})")
              .errors.at(0)
              .pointer == "/value/enemies");
    CHECK(fails(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":[1]})")
              .errors.at(0)
              .pointer == "/value");
    CHECK(fails(editor, "scene.settings_get", R"({"type":"test.Nope"})").kind == ErrorKind::NotFound);

    ok(editor, "scene.new", R"({"discardChanges":true})");
    CHECK(ok(editor, "scene.settings_get").root().pointer("/settings/0/present").asBool() == false);
}

TEST_CASE("settings of unknown types are kept when the scene is saved")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    write(
        editor, "assets:/scenes/kept.scene",
        R"({"format":"illusion.scene","version":2,"types":{"other.Thing":4},"settings":{"other.Thing":{"x":1}},)"
        R"("entities":[]})");
    ok(editor, "scene.open", R"({"path":"assets:/scenes/kept.scene","discardChanges":true})");
    const JsonDocument listed = ok(editor, "scene.settings_get", R"({"type":"other.Thing"})");
    CHECK(listed.root().pointer("/settings/0/known").asBool() == false);
    CHECK(listed.root().pointer("/settings/0/version").asU64() == 4);
    CHECK(listed.root().pointer("/settings/0/value/x").asU64() == 1);
    CHECK(fails(editor, "scene.settings_set", R"({"type":"other.Thing","value":{"x":2}})").kind ==
          ErrorKind::NotFound);
    ok(editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"enemies":4}})");
    ok(editor, "scene.save");
    const std::string saved = read(editor, "assets:/scenes/kept.scene");
    CHECK(saved.find("\"other.Thing\"") != std::string::npos);
    CHECK(saved.find("\"test.Difficulty\"") != std::string::npos);
}

TEST_CASE("the crash journal recovers changes of scene settings")
{
    EditorFixture fixture(31);
    fixture.buildScene();
    ok(*fixture.editor, "scene.settings_set", R"({"type":"test.Difficulty","value":{"enemies":7}})");
    fixture.editor->endFrame(0.0);
    const std::string crashed = fixture.scene();
    const std::filesystem::path journals = fixture.directory.path / "project" / ".illusion" / "journal";
    std::filesystem::copy_file(journals / (fixture.editor->session() + ".jsonl"), journals / "crashed.jsonl");
    fixture.open(32);
    ok(*fixture.editor, "project.recover");
    CHECK(fixture.scene() == crashed);
}

TEST_CASE("project settings live in their own files under settings:")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;

    const JsonDocument listed = ok(editor, "project.settings_get");
    REQUIRE(listed.root().find("settings").size() == 1);
    CHECK(listed.root().pointer("/settings/0/file").asString() == "settings:/controls.json");
    CHECK(listed.root().pointer("/settings/0/present").asBool() == false);
    CHECK(listed.root().pointer("/settings/0/value/invertY").asBool() == false);

    CHECK(ok(editor, "project.settings_set", R"({"type":"test.Controls","value":{"invertY":true}})")
              .root()
              .find("changed")
              .asBool() == true);
    const std::string file = read(editor, "settings:/controls.json");
    CHECK(file.find("\"format\": \"illusion.settings\"") != std::string::npos);
    CHECK(file.find("\"type\": \"test.Controls\"") != std::string::npos);
    ok(editor, "project.settings_set", R"({"type":"test.Controls","value":{"sensitivity":2}})");
    const JsonDocument merged = ok(editor, "project.settings_get", R"({"type":"test.Controls"})");
    CHECK(merged.root().pointer("/settings/0/present").asBool() == true);
    CHECK(merged.root().pointer("/settings/0/value/invertY").asBool() == true);
    CHECK(merged.root().pointer("/settings/0/value/sensitivity").asF64() == doctest::Approx(2.0));
    CHECK(ok(editor, "project.settings_set", R"({"type":"test.Controls","value":{"sensitivity":2}})")
              .root()
              .find("changed")
              .asBool() == false);

    ok(editor, "project.settings_set", R"({"type":"test.Controls","value":null})");
    CHECK_FALSE(editor.files().exists("settings:/controls.json"));
    ok(editor, "history.undo");
    CHECK(read(editor, "settings:/controls.json").find("\"sensitivity\": 2.0") != std::string::npos);

    // A broken file: reading shows defaults with a warning, merging needs replace.
    write(editor, "settings:/controls.json", "{");
    const JsonDocument broken = ok(editor, "project.settings_get");
    CHECK(broken.root().pointer("/_editor/warnings").size() == 1);
    CHECK(
        fails(editor, "project.settings_set", R"({"type":"test.Controls","value":{"invertY":true}})").kind ==
        ErrorKind::PreconditionFailed);
    ok(editor, "project.settings_set", R"({"type":"test.Controls","value":{"invertY":true},"replace":true})");
    CHECK(ok(editor, "project.settings_get").root().pointer("/settings/0/value/invertY").asBool() == true);

    CHECK(fails(editor, "project.settings_set", R"({"type":"test.Difficulty","value":{}})").kind ==
          ErrorKind::NotFound);
    CHECK(fails(editor, "project.settings_set", R"({"type":"test.Controls","value":{"sensitivity":-1}})")
              .errors.at(0)
              .pointer == "/value/sensitivity");
    EditorContext loose(EditorContextOptions{.seed = 9});
    CHECK(fails(loose, "project.settings_get", "{}").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("settings files keep their type and version and migrate on reading")
{
    test::reflectEditorTestComponents();
    const StructReflection& type = *ComponentRegistry::instance().structs().reflection(
        ComponentRegistry::instance().structs().findByStableName("test.Controls"));
    editortest::Controls controls;
    controls.invertY = true;
    const Expected<std::string, std::string> text = writeSettingsFile(type, &controls);
    REQUIRE(text.hasValue());
    editortest::Controls back;
    REQUIRE(readSettingsFile(type, *text, &back).hasValue());
    CHECK(back.invertY);
    CHECK(readSettingsFile(type, "{", &back).error() == "is not valid JSON");
    CHECK_FALSE(readSettingsFile(type, R"({"format":"illusion.scene"})", &back).hasValue());
    CHECK_FALSE(readSettingsFile(type, R"({"format":"illusion.settings","type":"x"})", &back).hasValue());
    CHECK_FALSE(
        readSettingsFile(type, R"({"format":"illusion.settings","type":"test.Controls"})", &back).hasValue());
    CHECK(readSettingsFile(type,
                           R"({"format":"illusion.settings","type":"test.Controls","version":1,)"
                           R"("value":{"sensitivity":"x"}})",
                           &back)
              .error()
              .starts_with("'/value/sensitivity'"));
}

TEST_CASE("the settings catalogue rejects bad registrations")
{
    test::reflectEditorTestComponents();
    SettingsCatalog catalog;
    catalog.add("test.Difficulty", SettingsScope::Scene);
    catalog.add("test.Controls", SettingsScope::Project, "settings:/controls.json");
    CHECK(catalog.find("test.Difficulty", SettingsScope::Scene) != nullptr);
    CHECK(catalog.find("test.Difficulty", SettingsScope::Project) == nullptr);
    CHECK(catalog.types().size() == 2);
    CHECK(catalog.suggest("test.Contrls", SettingsScope::Project).at(0) == "test.Controls");
    CHECK(settingsScopeName(SettingsScope::Project) == "project");

    test::ScopedAssertCatcher asserts;
    CHECK_THROWS_AS(catalog.add("test.Nope", SettingsScope::Scene), test::AssertionFailure);
    CHECK_THROWS_AS(catalog.add("test.Difficulty", SettingsScope::Scene), test::AssertionFailure);
    CHECK_THROWS_AS(catalog.add("test.Controls", SettingsScope::Scene, "settings:/x.json"),
                    test::AssertionFailure);
    SettingsCatalog other;
    CHECK_THROWS_AS(other.add("test.Controls", SettingsScope::Project), test::AssertionFailure);
    CHECK_THROWS_AS(other.add("test.Controls", SettingsScope::Project, "assets:/c.json"),
                    test::AssertionFailure);
    CHECK_THROWS_AS(other.add("test.Difficulty", SettingsScope::Project, "settings:/d.json"),
                    test::AssertionFailure); // holds an entity reference
}
