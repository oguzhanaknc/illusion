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

TEST_CASE("asset references are not rewritten in scenes that refer into prefab instances")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    openCrateScene(editor, true);
    ok(editor, "scene.save");
    ok(editor, "scene.new", R"({"discardChanges":true})");
    const CommandError error = fails(
        editor, "asset.move", R"({"from":"assets:/props/crate.prefab","to":"assets:/props/box.prefab"})");
    CHECK(error.message.find("prefab instances") != std::string::npos);
    // Nothing moved: the scene still refers to the prefab where it is.
    CHECK(savedScene(editor).find("assets:/props/crate.prefab") != std::string::npos);
    ok(editor, "project.file_read", R"({"path":"assets:/props/crate.prefab"})");
}
