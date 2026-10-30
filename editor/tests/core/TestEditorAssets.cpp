#include "EditorCoreTestSupport.hpp"

#include <illusion/editor/core/ReferenceIndex.hpp>

#include <fstream>
#include <map>
#include <sstream>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// Every file of the project except the editor's own state, by relative path.
std::map<std::string, std::string> projectFiles(const EditorFixture& fixture)
{
    std::map<std::string, std::string> files;
    const std::filesystem::path root = fixture.directory.path / "project";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        const std::string name = entry.path().lexically_relative(root).generic_string();
        if (name.starts_with(".illusion"))
        {
            continue;
        }
        std::string contents;
        if (entry.is_regular_file())
        {
            std::ifstream in(entry.path(), std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            contents = text.str();
        }
        files[name] = contents;
    }
    return files;
}

std::string read(EditorContext& editor, std::string_view path)
{
    return std::string(ok(editor, "project.file_read", R"({"path":")" + std::string(path) + R"("})")
                           .root()
                           .find("text")
                           .asString()
                           .value_or(""));
}

std::string lightId(EditorContext& editor)
{
    return std::string(ok(editor, "entity.get", R"({"entity":"/Light"})")
                           .root()
                           .pointer("/entity/id")
                           .asString()
                           .value_or(""));
}

} // namespace

TEST_CASE("references match an asset, its sub-assets and the contents of folders")
{
    CHECK(refersTo("assets:/a/b.glb", "assets:/a/b.glb"));
    CHECK(refersTo("assets:/a/b.glb#mesh/0", "assets:/a/b.glb"));
    CHECK(refersTo("assets:/a/b.glb", "assets:/a"));
    CHECK(refersTo("assets:/a/b.glb", "assets:/a/"));
    CHECK(refersTo("assets:/a/b.glb", "assets:/"));
    CHECK_FALSE(refersTo("assets:/ab.glb", "assets:/a"));
    CHECK_FALSE(refersTo("assets:/a/b.glbx", "assets:/a/b.glb"));
    CHECK_FALSE(refersTo("assets:/a", "assets:/a/b"));
    CHECK_FALSE(refersTo("assets:/a", ""));
    CHECK(movedReference("assets:/a/b.glb#mesh/0", "assets:/a/b.glb", "assets:/c.glb") ==
          "assets:/c.glb#mesh/0");
    CHECK(movedReference("assets:/a/b.glb", "assets:/a/", "assets:/x/y") == "assets:/x/y/b.glb");
}

TEST_CASE("asset.list and asset.info describe assets by kind")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;

    const JsonDocument all = ok(editor, "asset.list", R"({"recursive":true})");
    std::map<std::string, std::string> kinds;
    for (const JsonValue entry : all.root().find("assets").elements())
    {
        kinds[std::string(entry.find("path").asString().value_or(""))] =
            std::string(entry.find("kind").asString().value_or(""));
    }
    CHECK(kinds["assets:/materials"] == "Folder");
    CHECK(kinds["assets:/materials/red.material"] == "Material");
    CHECK(kinds["assets:/textures/red.png"] == "Texture");
    CHECK(kinds["assets:/scenes/main.scene"] == "Scene");
    const JsonDocument top = ok(editor, "asset.list");
    CHECK(top.root().pointer("/assets/0/path").asString() == "assets:/input");
    CHECK(top.root().pointer("/assets/1/path").asString() == "assets:/materials");
    CHECK(ok(editor, "asset.list", R"({"recursive":true,"kind":"Scene"})").root().find("assets").size() == 2);

    const CommandError kind = fails(editor, "asset.list", R"({"kind":"Materal"})");
    CHECK(kind.errors.at(0).pointer == "/kind");
    CHECK(kind.suggestions.at(0) == "Material");
    CHECK(fails(editor, "asset.list", R"({"path":"settings:/"})").errors.at(0).pointer == "/path");
    CHECK(fails(editor, "asset.list", R"({"path":"assets:/../x"})").errors.at(0).pointer == "/path");
    CHECK(fails(editor, "asset.list", R"({"path":"assets:/nothing"})").kind == ErrorKind::NotFound);

    const JsonDocument info = ok(editor, "asset.info", R"({"path":"assets:/materials/red.material"})");
    CHECK(info.root().find("kind").asString() == "Material");
    CHECK(info.root().find("references").asU64() == 2); // the open scene's Light and other.scene's Prop
    CHECK(info.root().find("dependencies").asU64() == 1);
    CHECK(info.root().find("size").asString().value_or("0") != "0"); // u64 values are decimal strings
    CHECK(ok(editor, "asset.info", R"({"path":"assets:/textures"})").root().find("references").asU64() == 1);
    CHECK(fails(editor, "asset.info", R"({"path":"assets:/none.png"})").kind == ErrorKind::NotFound);

    EditorContext loose(EditorContextOptions{.seed = 9});
    CHECK(fails(loose, "asset.list", "{}").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("asset.references finds references in the open scene, closed scenes, materials and settings")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;

    const JsonDocument red = ok(editor, "asset.references", R"({"path":"assets:/materials/red.material"})");
    REQUIRE(red.root().find("references").size() == 2);
    CHECK(red.root().pointer("/references/0/source").asString() == "assets:/scenes/main.scene");
    CHECK(red.root().pointer("/references/0/entity").asString() == lightId(editor));
    CHECK(red.root().pointer("/references/0/owner").asString() == "test.Look");
    CHECK(red.root().pointer("/references/0/field").asString() == "/material");
    CHECK(red.root().pointer("/references/1/source").asString() == "assets:/scenes/other.scene");
    CHECK(red.root().pointer("/references/1/entity").asString() == "00000000000000a1");

    const JsonDocument textures = ok(editor, "asset.references", R"({"path":"assets:/textures"})");
    REQUIRE(textures.root().find("references").size() == 1);
    CHECK(textures.root().pointer("/references/0/source").asString() == "assets:/materials/red.material");
    CHECK(textures.root().pointer("/references/0/field").asString() == "/textures/baseColor");
    CHECK(textures.root().pointer("/references/0/owner").asString() == "");

    const JsonDocument ship = ok(editor, "asset.references", R"({"path":"assets:/models/ship.glb"})");
    REQUIRE(ship.root().find("references").size() == 1);
    CHECK(ship.root().pointer("/references/0/asset").asString() == "assets:/models/ship.glb#mesh/0");
    CHECK(ship.root().pointer("/references/0/field").asString() == "/extras/0");

    // Unsaved changes of the open scene count, its file on disk does not.
    ok(editor, "component.set",
       R"({"entity":"/Light","type":"test.Look","value":{"material":"assets:/materials/blue.material"}})");
    CHECK(ok(editor, "asset.references", R"({"path":"assets:/materials/red.material"})")
              .root()
              .find("references")
              .size() == 1);

    // Scene settings with asset fields.
    ok(editor, "scene.settings_set",
       R"({"type":"test.Difficulty","value":{"music":"assets:/audio/theme.ogg"}})");
    const JsonDocument music = ok(editor, "asset.references", R"({"path":"assets:/audio/theme.ogg"})");
    REQUIRE(music.root().find("references").size() == 1);
    CHECK(music.root().pointer("/references/0/owner").asString() == "test.Difficulty");
    CHECK(music.root().pointer("/references/0/entity").asString() == "");
}

TEST_CASE("asset.move updates every reference to the asset, and undo puts everything back")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    const std::map<std::string, std::string> files = projectFiles(fixture);
    const std::string scene = fixture.scene();

    const JsonDocument moved =
        ok(editor, "asset.move",
           R"({"from":"assets:/materials/red.material","to":"assets:/mats/crimson.material"})");
    CHECK(moved.root().find("references").asU64() == 2);
    REQUIRE(moved.root().find("updated").size() == 1);
    CHECK(moved.root().pointer("/updated/0").asString() == "assets:/scenes/other.scene");
    CHECK(moved.root().pointer("/_editor/warnings").size() == 1); // the open scene must be saved
    CHECK(read(editor, "assets:/scenes/other.scene").find("assets:/mats/crimson.material") !=
          std::string::npos);
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Look","path":"material"})")
              .root()
              .find("value")
              .asString() == "assets:/mats/crimson.material");
    CHECK(ok(editor, "asset.references", R"({"path":"assets:/materials/red.material"})")
              .root()
              .find("references")
              .size() == 0);

    ok(editor, "history.undo");
    CHECK(projectFiles(fixture) == files);
    CHECK(fixture.scene() == scene);

    // A folder: the material inside moves and its texture reference follows the moved texture folder.
    ok(editor, "asset.move", R"({"from":"assets:/textures","to":"assets:/images"})");
    CHECK(read(editor, "assets:/materials/red.material").find("assets:/images/red.png") != std::string::npos);
    ok(editor, "history.undo");
    CHECK(projectFiles(fixture) == files);

    // Sub-assets keep their fragment.
    ok(editor, "project.file_write", R"({"path":"assets:/models/ship.glb","text":"not a model"})");
    ok(editor, "asset.move", R"({"from":"assets:/models/ship.glb","to":"assets:/models/boat.glb"})");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Look","path":"extras/0"})")
              .root()
              .find("value")
              .asString() == "assets:/models/boat.glb#mesh/0");
}

TEST_CASE("asset.move refuses existing targets, moves into the moved folder and the open scene")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    CHECK(fails(editor, "asset.move",
                R"({"from":"assets:/materials/red.material","to":"assets:/textures/red.png"})")
              .kind == ErrorKind::AlreadyExists);
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/materials","to":"assets:/materials/sub"})")
              .errors.at(0)
              .pointer == "/to");
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/scenes/main.scene","to":"assets:/main.scene"})")
              .kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/scenes","to":"assets:/levels"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "asset.move", R"({"from":"settings:/x.json","to":"assets:/x.json"})")
              .errors.at(0)
              .pointer == "/from");
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/","to":"assets:/x"})").errors.at(0).pointer ==
          "/from");
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/none","to":"assets:/x"})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("asset.delete refuses referenced assets unless forced")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    const std::map<std::string, std::string> files = projectFiles(fixture);

    const CommandError refused =
        fails(editor, "asset.delete", R"({"path":"assets:/materials/red.material"})");
    CHECK(refused.kind == ErrorKind::PreconditionFailed);
    CHECK(refused.message.find("2 times") != std::string::npos);
    const JsonDocument forced =
        ok(editor, "asset.delete", R"({"path":"assets:/materials/red.material","force":true})");
    CHECK(forced.root().find("dangling").size() == 2);
    CHECK_FALSE(editor.files().exists("assets:/materials/red.material"));
    ok(editor, "history.undo");
    CHECK(projectFiles(fixture) == files);

    // References from inside a deleted folder do not hold it back.
    ok(editor, "project.file_write", R"({"path":"assets:/kit/t.png","text":"x"})");
    ok(editor, "material.create",
       R"({"path":"assets:/kit/a.material","value":{"textures":{"baseColor":"assets:/kit/t.png"}}})");
    ok(editor, "asset.delete", R"({"path":"assets:/kit"})");
    CHECK(fails(editor, "asset.delete", R"({"path":"assets:/scenes"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "asset.delete", R"({"path":"assets:/kit"})").kind == ErrorKind::NotFound);
}

TEST_CASE("asset.dependencies lists what a scene or material refers to")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    const JsonDocument open = ok(editor, "asset.dependencies", R"({"path":"assets:/scenes/main.scene"})");
    REQUIRE(open.root().find("dependencies").size() == 2);
    CHECK(open.root().pointer("/dependencies/0").asString() == "assets:/materials/red.material");
    CHECK(open.root().pointer("/dependencies/1").asString() == "assets:/models/ship.glb#mesh/0");
    CHECK(ok(editor, "asset.dependencies", R"({"path":"assets:/scenes/other.scene"})")
              .root()
              .pointer("/dependencies/0")
              .asString() == "assets:/materials/red.material");
    CHECK(ok(editor, "asset.dependencies", R"({"path":"assets:/materials/red.material"})")
              .root()
              .pointer("/dependencies/0")
              .asString() == "assets:/textures/red.png");
    CHECK(ok(editor, "asset.dependencies", R"({"path":"assets:/textures/red.png"})")
              .root()
              .find("dependencies")
              .size() == 0);
    CHECK(fails(editor, "asset.dependencies", R"({"path":"assets:/textures"})").errors.at(0).pointer ==
          "/path");
    CHECK(fails(editor, "asset.dependencies", R"({"path":"assets:/none.scene"})").kind ==
          ErrorKind::NotFound);
    ok(editor, "project.file_write", R"({"path":"assets:/scenes/bad.scene","text":"{"})");
    CHECK(
        fails(editor, "asset.dependencies", R"({"path":"assets:/scenes/bad.scene"})").errors.at(0).pointer ==
        "/path");
}

TEST_CASE("the reference index follows file changes and reports files it cannot read")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    CHECK(editor.references().size() == 3); // two scenes and one material
    CHECK(editor.references().problems().empty());

    ok(editor, "project.file_write", R"({"path":"assets:/scenes/bad.scene","text":"{"})");
    REQUIRE(editor.references().problems().size() == 1);
    CHECK(editor.references().problems()[0].starts_with("assets:/scenes/bad.scene: "));
    const JsonDocument found = ok(editor, "asset.references", R"({"path":"assets:/materials/red.material"})");
    CHECK(found.root().pointer("/_editor/warnings").size() == 1);

    // A rewritten file of the same length is read again.
    std::string other = read(editor, "assets:/scenes/other.scene");
    const usize at = other.find("red.material");
    REQUIRE(at != std::string::npos);
    other.replace(at, 3, "tan");
    JsonMutDocument doc;
    JsonMutValue params = doc.makeObject();
    params.add("path", doc.makeString("assets:/scenes/other.scene"));
    params.add("text", doc.makeString(other));
    doc.setRoot(params);
    std::string json;
    REQUIRE(doc.write(json, JsonWriteOptions{.pretty = false}));
    ok(editor, "project.file_write", json);
    const std::vector<AssetReference>* held = editor.references().referencesIn("assets:/scenes/other.scene");
    REQUIRE(held != nullptr);
    REQUIRE(held->size() == 1);
    CHECK(held->front().asset == "assets:/materials/tan.material");
}

TEST_CASE("material commands create, read and change material files with undo")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;

    CHECK(
        ok(editor, "material.create",
           R"({"path":"assets:/m/blue.material","value":{"material":{"baseColorFactor":"#0000ff","metallicFactor":1}}})")
            .root()
            .find("created")
            .asBool() == true);
    const JsonDocument blue = ok(editor, "material.get", R"({"path":"assets:/m/blue.material"})");
    CHECK(blue.root().pointer("/value/material/metallicFactor").asF64() == doctest::Approx(1.0));
    CHECK(blue.root().pointer("/value/material/roughnessFactor").asF64() == doctest::Approx(1.0));
    CHECK(blue.root().pointer("/value/textures/baseColor").asString() == "");
    const std::string first = read(editor, "assets:/m/blue.material");
    CHECK(first.find("illusion.material") != std::string::npos);

    CHECK(
        ok(editor, "material.set",
           R"({"path":"assets:/m/blue.material","value":{"material":{"roughnessFactor":0.25},"textures":{"normal":"assets:/n.png"}}})")
            .root()
            .find("changed")
            .asBool() == true);
    const JsonDocument changed = ok(editor, "material.get", R"({"path":"assets:/m/blue.material"})");
    CHECK(changed.root().pointer("/value/material/roughnessFactor").asF64() == doctest::Approx(0.25));
    CHECK(changed.root().pointer("/value/material/metallicFactor").asF64() == doctest::Approx(1.0));
    CHECK(changed.root().pointer("/value/textures/normal").asString() == "assets:/n.png");
    CHECK(ok(editor, "material.set",
             R"({"path":"assets:/m/blue.material","value":{"material":{"roughnessFactor":0.25}}})")
              .root()
              .find("changed")
              .asBool() == false);
    ok(editor, "history.undo");
    CHECK(read(editor, "assets:/m/blue.material") == first);

    CHECK(fails(editor, "material.create", R"({"path":"assets:/m/blue.material"})").kind ==
          ErrorKind::AlreadyExists);
    ok(editor, "material.create", R"({"path":"assets:/m/blue.material","overwrite":true})");
    CHECK(fails(editor, "material.create", R"({"path":"assets:/m/blue.txt"})").errors.at(0).pointer ==
          "/path");
    CHECK(fails(editor, "material.create", R"({"path":"settings:/blue.material"})").errors.at(0).pointer ==
          "/path");
    CHECK(
        fails(editor, "material.set", R"({"path":"assets:/m/blue.material","value":{"material":{"nope":1}}})")
            .errors.at(0)
            .pointer == "/value/material/nope");
    CHECK(fails(editor, "material.set",
                R"({"path":"assets:/m/blue.material","value":{"material":{"metallicFactor":2}}})")
              .errors.at(0)
              .pointer == "/value/material/metallicFactor");
    CHECK(fails(editor, "material.set", R"({"path":"assets:/m/blue.material","value":[]})")
              .errors.at(0)
              .pointer == "/value");
    CHECK(fails(editor, "material.get", R"({"path":"assets:/m/none.material"})").kind == ErrorKind::NotFound);
    ok(editor, "project.file_write", R"({"path":"assets:/m/bad.material","text":"{}"})");
    CHECK(fails(editor, "material.get", R"({"path":"assets:/m/bad.material"})").errors.at(0).pointer ==
          "/path");
}

TEST_CASE("the startup scene counts as a reference and cannot be moved")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    REQUIRE(editor.project()->manifest().startupScene == "assets:/scenes/main.scene");
    ok(editor, "scene.save_as", R"({"path":"assets:/scenes/work.scene"})");

    const JsonDocument found = ok(editor, "asset.references", R"({"path":"assets:/scenes/main.scene"})");
    REQUIRE(found.root().find("references").size() == 1);
    CHECK(found.root().pointer("/references/0/source").asString() == "project.illusion");
    CHECK(found.root().pointer("/references/0/field").asString() == "/startupScene");
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/scenes/main.scene","to":"assets:/main.scene"})")
              .kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "asset.delete", R"({"path":"assets:/scenes/main.scene"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(ok(editor, "asset.delete", R"({"path":"assets:/scenes/main.scene","force":true})")
              .root()
              .find("dangling")
              .size() == 1);
}

TEST_CASE("references in project settings files are found and moved")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    ok(editor, "project.settings_set",
       R"({"type":"test.Controls","value":{"cursor":"assets:/textures/red.png"}})");
    ok(editor, "project.file_write", R"({"path":"settings:/notes.json","text":"{\"not\":\"settings\"}"})");

    const JsonDocument found = ok(editor, "asset.references", R"({"path":"assets:/textures/red.png"})");
    REQUIRE(found.root().find("references").size() == 2);
    CHECK(found.root().pointer("/references/1/source").asString() == "settings:/controls.json");
    CHECK(found.root().pointer("/references/1/owner").asString() == "test.Controls");
    CHECK(found.root().pointer("/references/1/field").asString() == "/cursor");
    CHECK(found.root().pointer("/_editor/warnings").size() ==
          0); // other JSON under settings: is not an error

    const JsonDocument moved =
        ok(editor, "asset.move", R"({"from":"assets:/textures","to":"assets:/images"})");
    CHECK(moved.root().find("references").asU64() == 2);
    CHECK(ok(editor, "project.settings_get").root().pointer("/settings/0/value/cursor").asString() ==
          "assets:/images/red.png");
    ok(editor, "history.undo");
    CHECK(ok(editor, "project.settings_get").root().pointer("/settings/0/value/cursor").asString() ==
          "assets:/textures/red.png");
}

TEST_CASE("files are indexed whatever the case of their extension")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    ok(editor, "project.file_write",
       R"({"path":"assets:/Level.SCENE","text":"{\"format\":\"illusion.scene\",\"version\":2,\"entities\":[)"
       R"({\"id\":\"00000000000000b1\",\"components\":{\"test.Look\":{\"material\":\"assets:/m/x.material\"}}}]}"})");
    const JsonDocument found = ok(editor, "asset.references", R"({"path":"assets:/m/x.material"})");
    REQUIRE(found.root().find("references").size() == 1);
    CHECK(found.root().pointer("/references/0/source").asString() == "assets:/Level.SCENE");
    CHECK(ok(editor, "asset.list", R"({"kind":"Scene"})").root().find("assets").size() == 1);
}
