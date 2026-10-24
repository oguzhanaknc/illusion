#include "EditorCoreTestSupport.hpp"

using namespace illusion;
using namespace illusion::editor;
using test::fails;
using test::ok;

TEST_CASE("Component paste preserves references and undoes all targets together")
{
    test::EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const auto copied = ok(editor, "component.copy", R"({"entity":"/Game/Player","type":"test.Link"})");
    const std::string before = fixture.scene();
    const auto pasted = ok(editor, "component.paste", R"({"entities":["/Light","/Game/Board"]})");
    CHECK(pasted.root().find("added").asU64() == 2);
    CHECK(editor.history().position() == 1);
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Link"})")
              .root()
              .find("value")
              .dump() == copied.root().find("value").dump());
    const std::string after = fixture.scene();
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);
    ok(editor, "history.redo");
    CHECK(fixture.scene() == after);
}

TEST_CASE("Component clipboard validates missing values and dry runs leave it unchanged")
{
    test::EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    CHECK(fails(editor, "component.paste", R"({"entities":["/Light"]})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "component.copy", R"({"entity":"/Light","type":"test.Link"})").kind ==
          ErrorKind::NotFound);
    ok(editor, "component.copy", R"({"entity":"/Game/Player","type":"test.Link","dryRun":true})");
    CHECK(editor.componentClipboard().type.empty());
    ok(editor, "component.copy", R"({"entity":"/Game/Player","type":"test.Link"})");
    const std::string before = fixture.scene();
    ok(editor, "component.paste", R"({"entities":["/Light"],"dryRun":true})");
    CHECK(fixture.scene() == before);
    CHECK(fails(editor, "component.paste", R"({"entities":["/Light","/Missing"]})").kind ==
          ErrorKind::NotFound);
    CHECK(fixture.scene() == before);
    CHECK(editor.history().position() == 0);
}

TEST_CASE("Component field paths edit vector axes and array entries without losing siblings")
{
    test::EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "component.set_field",
       R"({"entity":"/Light","type":"illusion.Transform","path":"position/x","value":7})");
    ok(editor, "component.set_field",
       R"({"entity":"/Light","type":"illusion.Transform","path":"position/1","value":8})");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"illusion.Transform","path":"position"})")
              .root()
              .find("value")
              .dump() == "[7.0,8.0,0.0]");
    ok(editor, "component.reset", R"({"entity":"/Light","type":"illusion.Transform","path":"position/x"})");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"illusion.Transform","path":"position"})")
              .root()
              .find("value")
              .dump() == "[0.0,8.0,0.0]");
    const std::string before = fixture.scene();
    CHECK(fails(editor, "component.set_field",
                R"({"entity":"/Light","type":"illusion.Transform","path":"position/3","value":9})")
              .kind == ErrorKind::ValidationFailed);
    CHECK(fixture.scene() == before);
    ok(editor, "component.add",
       R"({"entity":"/Light","type":"test.Look","value":{"extras":["builtin:/mesh/cube","builtin:/mesh/sphere"]}})");
    ok(editor, "component.set_field",
       R"({"entity":"/Light","type":"test.Look","path":"extras/1","value":"builtin:/mesh/plane"})");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Look","path":"extras/0"})")
              .root()
              .find("value")
              .asString() == "builtin:/mesh/cube");
}

TEST_CASE("Component field reset changes only the requested field and is undoable")
{
    test::EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "component.set",
       R"({"entity":"/Light","type":"illusion.Transform","value":{"position":[2,3,4],"scale":[2,2,2]}})");
    const std::string before = fixture.scene();
    ok(editor, "component.reset", R"({"entity":"/Light","type":"illusion.Transform","path":"position"})");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"illusion.Transform","path":"position"})")
              .root()
              .find("value")
              .dump() == "[0.0,0.0,0.0]");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"illusion.Transform","path":"scale"})")
              .root()
              .find("value")
              .dump() == "[2.0,2.0,2.0]");
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);
    CHECK(fails(editor, "component.reset",
                R"({"entity":"/Light","type":"illusion.Transform","path":"position/99"})")
              .kind == ErrorKind::ValidationFailed);
    CHECK(fixture.scene() == before);
}

TEST_CASE("View commands keep hidden and locked state out of scenes and undo history")
{
    test::EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    ok(editor, "view.set_entities",
       R"({"entities":["/Game/Board"],"hidden":true,"locked":true,"dryRun":true})");
    CHECK(editor.view().hiddenEntities().empty());
    CHECK(editor.view().lockedEntities().empty());
    const auto result =
        ok(editor, "view.set_entities", R"({"entities":["/Game/Board"],"hidden":true,"locked":true})");
    CHECK(result.root().find("hidden").size() == 3);
    CHECK(result.root().find("locked").size() == 3);
    const u64 version = editor.view().version();
    ok(editor, "view.set_entities", R"({"entities":["/Game/Board"],"hidden":true,"locked":true})");
    CHECK(editor.view().version() == version);
    ok(editor, "view.set_entities", R"({"entities":["/Game/Board"],"hidden":false,"recursive":false})");
    CHECK(editor.view().hiddenEntities().size() == 2);
    CHECK(editor.view().lockedEntities().size() == 3);
    CHECK(fixture.scene() == before);
    CHECK(editor.history().position() == 0);
    CHECK(fails(editor, "view.set_entities", R"({"entities":["/Light"]})").kind ==
          ErrorKind::ValidationFailed);
    ok(editor, "scene.new", R"({"discardChanges":true})");
    CHECK(editor.view().hiddenEntities().empty());
    CHECK(editor.view().lockedEntities().empty());
}
