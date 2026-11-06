#include "EditorCoreTestSupport.hpp"

#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Prefab.hpp>

#include <format>
#include <string>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

constexpr EntityGuid kLid{0xa2};
constexpr EntityGuid kInstance{0x1000};

// A crate: a root and a child "Lid".
constexpr std::string_view kCratePrefab =
    R"({"format": "illusion.prefab", "version": 2, "entities": [
         {"id": "00000000000000a1", "components": {"illusion.Name": {"value": "Crate"},
           "illusion.Hierarchy": {"parent": null, "order": 0}, "illusion.LocalTransform": {}, "illusion.Transform": {}}},
         {"id": "00000000000000a2", "components": {"illusion.Name": {"value": "Lid"},
           "illusion.Hierarchy": {"parent": "00000000000000a1", "order": 0}, "illusion.LocalTransform": {}}}]})";

// Writes the crate prefab and a scene with one instance of it (and, with @p sticker, a plain entity
// parented under the instance's lid), then opens the scene.
void openCrateScene(EditorContext& editor, bool sticker)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("path", doc.makeString("assets:/props/crate.prefab")).add("text", doc.makeString(kCratePrefab));
    std::string params;
    REQUIRE(doc.write(params, JsonWriteOptions{.pretty = false}));
    ok(editor, "project.file_write", params);

    std::string entities = R"([{"prefab": "assets:/props/crate.prefab", "id": "0000000000001000"})";
    if (sticker)
    {
        entities += std::format(R"(, {{"id": "0000000000000055", "components": {{"illusion.Name": {{"value":
            "Sticker"}}, "illusion.Hierarchy": {{"parent": "{}", "order": 0}}, "illusion.LocalTransform": {{}}}}}})",
                                toString(derivePrefabGuid(kInstance, kLid)));
    }
    entities += "]";
    const std::string scene =
        std::format(R"({{"format": "illusion.scene", "version": 2, "entities": {}}})", entities);
    JsonMutDocument write;
    JsonMutValue writeRoot = write.makeObject();
    write.setRoot(writeRoot);
    writeRoot.add("path", write.makeString("assets:/scenes/yard.scene")).add("text", write.makeString(scene));
    params.clear();
    REQUIRE(write.write(params, JsonWriteOptions{.pretty = false}));
    ok(editor, "project.file_write", params);
    ok(editor, "scene.open", R"({"path":"assets:/scenes/yard.scene","discardChanges":true})");
}

std::string savedScene(EditorContext& editor)
{
    return std::string(ok(editor, "project.file_read", R"({"path":"assets:/scenes/yard.scene"})")
                           .root()
                           .find("text")
                           .asString()
                           .value_or(""));
}

} // namespace

TEST_CASE("the editor opens scenes with prefab instances and saves them as records with overrides")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, true);
    ok(editor, "entity.get", R"({"entity":"/Crate/Lid/Sticker"})");
    ok(editor, "entity.rename", R"({"entity":"/Crate/Lid","name":"Top"})");
    ok(editor, "scene.save");

    const Expected<JsonDocument, JsonError> saved = JsonDocument::parse(savedScene(editor));
    REQUIRE(saved.hasValue());
    const JsonValue entities = saved->root().find("entities");
    REQUIRE(entities.size() == 2);
    CHECK(entities[0].find("prefab").asString() == "assets:/props/crate.prefab");
    CHECK(entities[0].pointer("/overrides/00000000000000a2/illusion.Name/value").asString() == "Top");
    CHECK(entities[1].pointer("/components/illusion.Name/value").asString() == "Sticker");

    // The prefab changes on disk: the instance follows it, except for the overridden name.
    const std::string changed =
        std::string(kCratePrefab).replace(std::string(kCratePrefab).find("\"Crate\""), 7, "\"Box\"");
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("path", doc.makeString("assets:/props/crate.prefab")).add("text", doc.makeString(changed));
    std::string params;
    REQUIRE(doc.write(params, JsonWriteOptions{.pretty = false}));
    ok(editor, "project.file_write", params);
    ok(editor, "scene.open", R"({"path":"assets:/scenes/yard.scene","discardChanges":true})");
    ok(editor, "entity.get", R"({"entity":"/Box/Top/Sticker"})");
}

TEST_CASE("undoing the deletion of an instance entity restores its prefab link")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, false);
    ok(editor, "entity.destroy", R"({"entities":["/Crate/Lid"]})");
    ok(editor, "scene.save");
    {
        const Expected<JsonDocument, JsonError> saved = JsonDocument::parse(savedScene(editor));
        REQUIRE(saved.hasValue());
        CHECK(saved->root().pointer("/entities/0/overrides/00000000000000a2").isNull());
    }

    ok(editor, "history.undo");
    const Entity lid = editor.entities().find(derivePrefabGuid(kInstance, kLid));
    REQUIRE_FALSE(lid.isNull());
    REQUIRE(editor.world().has<PrefabMember>(lid));
    CHECK(editor.world().get<PrefabMember>(lid).instance == kInstance);
    CHECK(editor.world().get<PrefabMember>(lid).local == kLid);
    ok(editor, "scene.save");
    CHECK(savedScene(editor).find("overrides") == std::string::npos);
}

TEST_CASE("asset references include the prefabs of instance records")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, false);
    ok(editor, "scene.save");
    const JsonDocument references =
        ok(editor, "asset.references", R"({"path":"assets:/props/crate.prefab"})");
    REQUIRE(references.root().find("references").size() == 1);
    CHECK(references.root().pointer("/references/0/source").asString() == "assets:/scenes/yard.scene");
    CHECK(references.root().pointer("/references/0/owner").asString() == "illusion.PrefabInstance");
    CHECK(references.root().pointer("/references/0/field").asString() == "/prefab");

    SUBCASE("moving the prefab rewrites the record")
    {
        ok(editor, "scene.new", R"({"discardChanges":true})");
        ok(editor, "asset.move", R"({"from":"assets:/props/crate.prefab","to":"assets:/props/box.prefab"})");
        CHECK(savedScene(editor).find("assets:/props/box.prefab") != std::string::npos);
    }
}

TEST_CASE("prefab.create saves a subtree and replaces it with a linked instance in its place")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string lightId = std::string(ok(editor, "entity.get", R"({"entity":"/Light"})")
                                                .root()
                                                .pointer("/entity/id")
                                                .asString()
                                                .value_or(""));
    // Another entity refers to the light: the reference follows it into the instance.
    ok(editor, "entity.create", R"({"name":"Spot"})");
    ok(editor, "component.add",
       std::format(R"({{"entity":"/Spot","type":"test.Link","value":{{"target":"{}"}}}})", lightId));

    const JsonDocument created =
        ok(editor, "prefab.create", R"({"entity":"/Light","path":"assets:/props/light.prefab"})");
    CHECK(created.root().find("created").asBool() == true);
    CHECK(created.root().pointer("/instance/name").asString() == "Light");
    const std::string instanceId =
        std::string(created.root().pointer("/instance/id").asString().value_or(""));
    CHECK(instanceId != lightId);
    const Entity root = editor.entities().find(*parseGuid(instanceId));
    REQUIRE(editor.world().has<PrefabInstance>(root));
    CHECK(ok(editor, "component.get", R"({"entity":"/Spot","type":"test.Link","path":"target"})")
              .root()
              .find("value")
              .asString() == std::optional<std::string_view>(instanceId));
    CHECK(fails(editor, "prefab.create", R"({"entity":"/Spot","path":"assets:/props/light.prefab"})").kind ==
          ErrorKind::AlreadyExists);
    CHECK(fails(editor, "prefab.create", R"({"entity":"/Spot","path":"assets:/props/light.scene"})")
              .errors.at(0)
              .pointer == "/path");

    ok(editor, "history.undo");
    CHECK(editor.entities().find(*parseGuid(lightId)) != Entity{});
    CHECK(ok(editor, "component.get", R"({"entity":"/Spot","type":"test.Link","path":"target"})")
              .root()
              .find("value")
              .asString() == std::optional<std::string_view>(lightId));
    CHECK(fails(editor, "project.file_read", R"({"path":"assets:/props/light.prefab"})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("prefab.instantiate places linked instances and rejects files that are not prefabs")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "prefab.create", R"({"entity":"/Light","path":"assets:/props/light.prefab","replace":false})");
    const JsonDocument placed =
        ok(editor, "prefab.instantiate", R"({"path":"assets:/props/light.prefab","parent":"/Game"})");
    CHECK(placed.root().pointer("/root/path").asString() == "/Game/Light");
    CHECK(placed.root().find("prefab").asString() == "assets:/props/light.prefab");
    CHECK(placed.root().find("entities").asU64() == 1);
    const JsonDocument again = ok(editor, "prefab.instantiate", R"({"path":"assets:/props/light.prefab"})");
    CHECK(again.root().find("instance").asString() != placed.root().find("instance").asString());

    CHECK(fails(editor, "prefab.instantiate", R"({"path":"assets:/props/none.prefab"})").kind ==
          ErrorKind::NotFound);
    ok(editor, "project.file_write", R"({"path":"assets:/props/bad.prefab","text":"{}"})");
    CHECK(
        fails(editor, "prefab.instantiate", R"({"path":"assets:/props/bad.prefab"})").errors.at(0).pointer ==
        "/path");
    // Undo the file write and both instances.
    ok(editor, "history.undo");
    ok(editor, "history.undo");
    ok(editor, "history.undo");
    CHECK(fails(editor, "entity.get", R"({"entity":"/Game/Light"})").kind == ErrorKind::NotFound);
}

TEST_CASE("prefab.overrides, prefab.revert and prefab.unpack work on any entity of an instance")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, false);
    ok(editor, "entity.rename", R"({"entity":"/Crate/Lid","name":"Top"})");
    ok(editor, "component.add", R"({"entity":"/Crate/Top","type":"test.Look"})");

    const JsonDocument overrides = ok(editor, "prefab.overrides", R"({"entity":"/Crate/Top"})");
    CHECK(overrides.root().find("prefab").asString() == "assets:/props/crate.prefab");
    const Expected<JsonDocument, JsonError> patch =
        JsonDocument::parse(overrides.root().find("overrides").dump());
    REQUIRE(patch.hasValue());
    CHECK(patch->root().pointer("/00000000000000a2/illusion.Name/value").asString() == "Top");
    CHECK(patch->root().pointer("/00000000000000a2/test.Look").isObject());

    CHECK(ok(editor, "prefab.revert", R"({"entity":"/Crate/Top","component":"illusion.Name"})")
              .root()
              .find("components")
              .asU64() == 1);
    ok(editor, "entity.get", R"({"entity":"/Crate/Lid"})");
    CHECK(ok(editor, "prefab.revert", R"({"entity":"/Crate/Lid"})").root().find("components").asU64() == 1);
    CHECK(ok(editor, "prefab.overrides", R"({"entity":"/Crate"})").root().find("overrides").dump() == "{}");
    CHECK(fails(editor, "prefab.revert", R"({"entity":"/Crate","component":"illusion.Hierarchy"})")
              .errors.at(0)
              .pointer == "/component");

    const JsonDocument unpacked = ok(editor, "prefab.unpack", R"({"entity":"/Crate/Lid"})");
    CHECK(unpacked.root().find("entities").asU64() == 2);
    CHECK(fails(editor, "prefab.overrides", R"({"entity":"/Crate"})").errors.at(0).pointer == "/entity");
    ok(editor, "scene.save");
    CHECK(savedScene(editor).find("\"prefab\"") == std::string::npos);
    ok(editor, "history.undo");
    ok(editor, "prefab.overrides", R"({"entity":"/Crate"})");
}

TEST_CASE("asset.list knows prefab files")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, false);
    const JsonDocument listed = ok(editor, "asset.list", R"({"recursive":true,"kind":"Prefab"})");
    REQUIRE(listed.root().find("assets").size() == 1);
    CHECK(listed.root().pointer("/assets/0/path").asString() == "assets:/props/crate.prefab");
}

TEST_CASE("prefab.apply writes overrides into the prefab and other instances follow, keeping their own")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, false);
    const JsonDocument second = ok(editor, "prefab.instantiate", R"({"path":"assets:/props/crate.prefab"})");
    // The second instance overrides the lid's name; the first adds a Component to its lid and renames the
    // root.
    const EntityGuid secondInstance = *parseGuid(second.root().find("instance").asString().value_or(""));
    const std::string secondLid = toString(derivePrefabGuid(secondInstance, kLid));
    const std::string firstRoot = toString(derivePrefabGuid(kInstance, EntityGuid{0xa1}));
    const std::string firstLid = toString(derivePrefabGuid(kInstance, kLid));
    ok(editor, "entity.rename", std::format(R"({{"entity":"{}","name":"Hatch"}})", secondLid));
    ok(editor, "component.add", std::format(R"({{"entity":"{}","type":"test.Look"}})", firstLid));
    ok(editor, "entity.rename", std::format(R"({{"entity":"{}","name":"Box"}})", firstRoot));

    CHECK(fails(editor, "prefab.apply",
                std::format(R"({{"entity":"{}","component":"illusion.LocalTransform"}})", firstRoot))
              .errors.at(0)
              .pointer == "/component");
    const JsonDocument applied =
        ok(editor, "prefab.apply", std::format(R"({{"entity":"{}","all":true}})", firstRoot));
    CHECK(applied.root().find("components").asU64() == 2);
    CHECK(applied.root().find("instances").asU64() == 1);
    const std::string prefab =
        std::string(ok(editor, "project.file_read", R"({"path":"assets:/props/crate.prefab"})")
                        .root()
                        .find("text")
                        .asString()
                        .value_or(""));
    CHECK(prefab.find("\"Box\"") != std::string::npos);
    CHECK(prefab.find("test.Look") != std::string::npos);
    // The applied instance has nothing left to override.
    CHECK(ok(editor, "prefab.overrides", std::format(R"({{"entity":"{}"}})", firstRoot))
              .root()
              .find("overrides")
              .dump() == "{}");
    // The other instance follows the prefab but keeps its own lid name.
    CHECK(ok(editor, "entity.get", std::format(R"({{"entity":"{}"}})", secondLid))
              .root()
              .pointer("/entity/path")
              .asString() == "/Box/Hatch");
    ok(editor, "component.get", std::format(R"({{"entity":"{}","type":"test.Look"}})", secondLid));

    ok(editor, "history.undo");
    CHECK(fails(editor, "component.get", std::format(R"({{"entity":"{}","type":"test.Look"}})", secondLid))
              .kind == ErrorKind::NotFound);
    CHECK(ok(editor, "project.file_read", R"({"path":"assets:/props/crate.prefab"})")
              .root()
              .find("text")
              .asString()
              ->find("\"Box\"") == std::string::npos);
}

TEST_CASE("copying a whole instance pastes a linked instance and duplicates are plain copies")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, false);
    ok(editor, "entity.rename", R"({"entity":"/Crate/Lid","name":"Top"})");
    ok(editor, "clipboard.copy", R"({"entities":["/Crate"]})");
    const JsonDocument pasted = ok(editor, "clipboard.paste");
    const std::string root = std::string(pasted.root().pointer("/entities/0/id").asString().value_or(""));
    const Entity copy = editor.entities().find(*parseGuid(root));
    REQUIRE(editor.world().has<PrefabInstance>(copy));
    CHECK(editor.world().get<PrefabInstance>(copy).instance != kInstance);
    CHECK(ok(editor, "prefab.overrides", std::format(R"({{"entity":"{}"}})", root))
              .root()
              .find("overrides")
              .dump()
              .find("Top") != std::string::npos);

    const JsonDocument duplicated =
        ok(editor, "entity.duplicate",
           std::format(R"({{"entities":["{}"]}})", toString(derivePrefabGuid(kInstance, EntityGuid{0xa1}))));
    const std::string twin = std::string(duplicated.root().pointer("/entities/0/id").asString().value_or(""));
    CHECK_FALSE(editor.world().has<PrefabMember>(editor.entities().find(*parseGuid(twin))));
}

TEST_CASE("moving an asset rewrites scenes that refer into prefab instances without adding overrides")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, true);
    ok(editor, "scene.save");
    ok(editor, "scene.new", R"({"discardChanges":true})");
    ok(editor, "asset.move", R"({"from":"assets:/props/crate.prefab","to":"assets:/props/box.prefab"})");
    const std::string scene = savedScene(editor);
    CHECK(scene.find("assets:/props/box.prefab") != std::string::npos);
    CHECK(scene.find("overrides") == std::string::npos);
    ok(editor, "scene.open", R"({"path":"assets:/scenes/yard.scene","discardChanges":true})");
    ok(editor, "entity.get", R"({"entity":"/Crate/Lid/Sticker"})");
}
