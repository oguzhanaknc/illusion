// The camera.* commands (ADR 0064) on the edit World: listing virtual cameras and brains, raising a camera
// above the others with undo, the Game view preview state and every invalid param.

#include "EditorCoreTestSupport.hpp"

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

// A brain, two virtual cameras (a follow camera that looks at the Hero, a disabled one) and a hero.
void addRig(EditorContext& editor)
{
    const JsonDocument hero =
        ok(editor, "entity.create",
           R"({"name":"Hero","components":{"illusion.Transform":{"position":[0,0,0]}}})");
    const std::string heroId(hero.root().pointer("/entity/id").asString().value_or(""));
    ok(editor, "entity.create",
       R"({"name":"Brain","components":{"illusion.Transform":{},"illusion.CameraBrain":{}}})");
    ok(editor, "entity.create",
       R"({"name":"Chase","components":{"illusion.Transform":{"position":[0,2,6]},)"
       R"("illusion.VirtualCamera":{"priority":5,"follow":")" +
           heroId + R"(","lookAt":")" + heroId + R"(","fovY":1.0471976},)" +
           R"("illusion.CameraFollow":{},"illusion.CameraLookAt":{},"illusion.CameraNoise":{}}})");
    ok(editor, "entity.create",
       R"({"name":"Wide","components":{"illusion.Transform":{"position":[0,8,12]},)"
       R"("illusion.VirtualCamera":{"priority":9},"illusion.CameraOrbit":{}}})");
    ok(editor, "entity.create",
       R"({"name":"Off","components":{"illusion.Transform":{},"illusion.VirtualCamera":{"priority":50,"enabled":false}}})");
}

i64 priorityOf(EditorContext& editor, std::string_view entity)
{
    return ok(editor, "component.get",
              R"({"entity":")" + std::string(entity) + R"(","type":"illusion.VirtualCamera"})")
        .root()
        .pointer("/value/priority")
        .asI64()
        .value_or(-1);
}

} // namespace

TEST_CASE("camera.list reports the virtual cameras with their stages and the brains with their blends")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    const JsonDocument empty = ok(editor, "camera.list");
    CHECK(empty.root().find("world").asString() == "edit");
    CHECK(empty.root().find("cameras").size() == 0);
    CHECK(empty.root().find("brains").size() == 0);

    addRig(editor);
    const JsonDocument list = ok(editor, "camera.list");
    CHECK(list.root().find("totalCameras").asU64() == 3u);
    CHECK(list.root().find("totalBrains").asU64() == 1u);
    CHECK(list.root().find("impulses").asU64() == 0u);
    REQUIRE(list.root().find("cameras").size() == 3);
    const JsonValue chase = list.root().pointer("/cameras/0");
    CHECK(chase.pointer("/entity/path").asString() == "/Chase");
    CHECK(chase.find("priority").asI64() == 5);
    CHECK(chase.find("body").asString() == "Follow");
    CHECK(chase.find("aim").asString() == "LookAt");
    CHECK(chase.find("noise").asBool() == true);
    CHECK(chase.find("follow").asString() == "/Hero");
    CHECK(chase.find("lookAt").asString() == "/Hero");
    CHECK(chase.find("fieldOfView").asF64().value_or(0.0) == doctest::Approx(60.0));
    CHECK(chase.find("live").asBool() == false);
    const JsonValue wide = list.root().pointer("/cameras/1");
    CHECK(wide.find("body").asString() == "Orbit");
    CHECK(wide.find("aim").asString() == "None");
    CHECK(wide.find("follow").asString() == "");
    const JsonValue off = list.root().pointer("/cameras/2");
    CHECK(off.find("enabled").asBool() == false);
    CHECK(off.find("body").asString() == "None");
    REQUIRE(list.root().find("brains").size() == 1);
    CHECK(list.root().pointer("/brains/0/entity/path").asString() == "/Brain");
    CHECK(list.root().pointer("/brains/0/defaultBlend").asString() == "EaseInOut");
    CHECK(list.root().pointer("/brains/0/defaultDuration").asF64().value_or(0.0) == doctest::Approx(1.0));
    CHECK(list.root().pointer("/brains/0/overrides").asU64() == 0u);

    ok(editor, "entity.create",
       R"({"name":"Hit","components":{"illusion.Transform":{},"illusion.CameraImpulse":{}}})");
    CHECK(ok(editor, "camera.list").root().find("impulses").asU64() == 1u);
}

TEST_CASE("camera.status says that the edit World does not run its cameras")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addRig(editor);
    const JsonDocument status = ok(editor, "camera.status");
    CHECK(status.root().find("world").asString() == "edit");
    CHECK(status.root().find("live").asBool() == false);
    CHECK(status.root().find("brains").size() == 0);
    CHECK(status.root().find("cameras").asU64() == 3u);
    CHECK(status.root().find("evaluated").asU64() == 0u);
    CHECK_FALSE(status.root().find("note").asString().value_or("").empty());
}

TEST_CASE("camera.activate puts a camera one above the others, undoes, and leaves a leading camera alone")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addRig(editor);

    const JsonDocument raised = ok(editor, "camera.activate", R"({"entity":"/Chase"})");
    CHECK(raised.root().find("previous").asI64() == 5);
    CHECK(raised.root().find("priority").asI64() == 10); // /Wide has 9; the disabled /Off does not count
    CHECK(raised.root().find("changed").asBool() == true);
    CHECK(priorityOf(editor, "/Chase") == 10);

    const JsonDocument again = ok(editor, "camera.activate", R"({"entity":"/Chase"})");
    CHECK(again.root().find("changed").asBool() == false);
    CHECK(again.root().find("priority").asI64() == 10);

    ok(editor, "history.undo");
    CHECK(priorityOf(editor, "/Chase") == 5);
    ok(editor, "history.redo");
    CHECK(priorityOf(editor, "/Chase") == 10);

    ok(editor, "camera.activate", R"({"entity":"/Wide"})");
    CHECK(priorityOf(editor, "/Wide") == 11);

    const JsonDocument dry = ok(editor, "camera.activate", R"({"entity":"/Chase","dryRun":true})");
    CHECK(dry.root().find("priority").asI64() == 12);
    CHECK(priorityOf(editor, "/Chase") == 10);
}

TEST_CASE("camera.activate refuses cameras that cannot be shown and a priority that cannot grow")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addRig(editor);
    CHECK(fails(editor, "camera.activate", R"({"entity":"/Off"})").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "camera.activate", R"({"entity":"/Hero"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "camera.activate", R"({"entity":"/Nobody"})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "camera.activate", "{}").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "camera.activate", R"({"entity":5})").kind == ErrorKind::ValidationFailed);

    ok(editor, "component.set_field",
       R"({"entity":"/Wide","type":"illusion.VirtualCamera","path":"priority","value":2147483647})");
    CHECK(fails(editor, "camera.activate", R"({"entity":"/Chase"})").kind == ErrorKind::PreconditionFailed);
    CHECK(priorityOf(editor, "/Chase") == 5);
}

TEST_CASE(
    "camera.preview chooses the virtual camera of the Game views, reports it and keeps it out of dry runs")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addRig(editor);
    CHECK(editor.view().previewCamera().isNull());

    const JsonDocument none = ok(editor, "camera.preview");
    CHECK(none.root().find("entity").isNull());

    const JsonDocument dry = ok(editor, "camera.preview", R"({"entity":"/Chase","dryRun":true})");
    CHECK(dry.root().pointer("/entity/path").asString() == "/Chase");
    CHECK(editor.view().previewCamera().isNull());

    const JsonDocument shown = ok(editor, "camera.preview", R"({"entity":"/Chase"})");
    CHECK(shown.root().pointer("/entity/path").asString() == "/Chase");
    CHECK_FALSE(editor.view().previewCamera().isNull());
    CHECK(ok(editor, "camera.preview").root().pointer("/entity/path").asString() == "/Chase");

    // The preview is editor state, not scene content: it is not in the history and survives a rename.
    ok(editor, "entity.rename", R"({"entity":"/Chase","name":"Follow"})");
    CHECK(ok(editor, "camera.preview").root().pointer("/entity/path").asString() == "/Follow");

    ok(editor, "camera.preview", R"({"clear":true})");
    CHECK(editor.view().previewCamera().isNull());
    CHECK(ok(editor, "camera.preview").root().find("entity").isNull());

    ok(editor, "camera.preview", R"({"entity":"/Wide"})");
    ok(editor, "entity.destroy", R"({"entities":["/Wide"]})");
    CHECK(ok(editor, "camera.preview").root().find("entity").isNull());
    ok(editor, "history.undo");
    CHECK(ok(editor, "camera.preview").root().pointer("/entity/path").asString() == "/Wide");
}

TEST_CASE("camera.preview rejects a missing or wrong entity and both params together")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addRig(editor);
    CHECK(fails(editor, "camera.preview", R"({"entity":"/Hero"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "camera.preview", R"({"entity":"/Nobody"})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "camera.preview", R"({"entity":"/Chase","clear":true})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "camera.preview", R"({"clear":"yes"})").kind == ErrorKind::ValidationFailed);
    CHECK(editor.view().previewCamera().isNull());
}
